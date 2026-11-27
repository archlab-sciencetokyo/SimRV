#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef SIMRV_SOURCE_DIR
#define SIMRV_SOURCE_DIR "."
#endif
#ifndef SIMRV_BUILDROOT_POST_BUILD_PATH
#define SIMRV_BUILDROOT_POST_BUILD_PATH "simrv-linux-builder-post-build"
#endif
#ifndef SIMRV_INSTALL_DATADIR
#define SIMRV_INSTALL_DATADIR "share"
#endif

namespace {
namespace fs = std::filesystem;

std::string buildrootPostBuildPath{SIMRV_BUILDROOT_POST_BUILD_PATH};
fs::path linuxDataRoot;
fs::path linuxVersionConfig;
fs::path linuxBuilderExecutable;
bool usingInstalledLinuxData{};

struct Config {
    std::string arch{"rv64"};
    std::string libc{"auto"};
    std::string rootfs{"alpine"};
    std::string profile{"gui"};
    std::string linuxVersion{"7.3.5"};
    std::string opensbi{"1.9"};
    std::string busybox{"1.38.0"};
    std::string alpine{"3.24.4"};
    std::string buildroot{"2026.08.1"};
    std::string buildrootSha{"34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b"};
    std::string jobs{"4"};
    std::string crossCompile;
    std::string dramSizeMb{"512"};
    std::string alpineMirror;
    std::string buildrootOutput;
    std::string customRootfsOverlay;
    struct Source {
        std::string repository;
        std::string url;
        std::string file;
        std::string sha256;
    };
    Source linuxSource{"https://www.kernel.org/releases.json", {}, {}, {}};
    Source opensbiSource{"https://github.com/riscv-software-src/opensbi/tags", {}, {}, {}};
    Source busyboxSource{"https://busybox.net/downloads/", {}, {}, {}};
    Source alpineSource{
        "https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/riscv64/", {}, {}, {}};
    Source buildrootSource{"https://buildroot.org/downloads/", {}, {}, {}};
    bool verbose{};
    bool rebuildKernel{};
    bool requireKernelReuse{};
    bool reuseBuildroot{};
    bool checkUpdates{};
    bool updateVersions{};
    bool pruneOldSources{};
    bool assumeYes{};
    std::string cleanAction;
};

std::string envOr(std::string_view name, std::string fallback) {
    const std::string key{name};
    if (const auto* value = std::getenv(key.c_str()); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct BuildPaths {
    fs::path source;
    fs::path build;
    fs::path sources;
    fs::path imagesRoot;
    fs::path images;
    fs::path linuxSource;
    fs::path opensbiSource;
    fs::path busyboxSource;
    fs::path buildrootSource;
};

BuildPaths buildPaths(const Config& config) {
    BuildPaths paths;
    paths.source = linuxDataRoot;
    const auto home = envOr("HOME", ".");
    const auto defaultBuildRoot =
        usingInstalledLinuxData
            ? fs::path{envOr("XDG_CACHE_HOME", home + "/.cache")} / "SimRV/linux-build"
            : paths.source / "linux-build";
    const auto defaultImagesRoot =
        usingInstalledLinuxData
            ? fs::path{envOr("XDG_DATA_HOME", home + "/.local/share")} / "SimRV/images"
            : paths.source / "images";
    paths.build = fs::absolute(
        envOr("SIMRV_GUEST_BUILD_DIR",
              envOr("SIMRV_GUEST_BUILDS_DIR",
                    envOr("SIMRV_LINUX_BUILD_DIR",
                          envOr("SIMRV_LINUX_BUILDS_DIR", defaultBuildRoot.string())))));
    paths.sources = paths.build / "sources";
    paths.imagesRoot =
        fs::absolute(envOr("SIMRV_GUEST_IMAGES_DIR",
                           envOr("SIMRV_LINUX_IMAGES_DIR",
                                 envOr("SIMRV_LINUX_IMAGES_ROOT", defaultImagesRoot.string()))));
    paths.images =
        paths.imagesRoot / config.arch /
        (config.rootfs == config.profile ? config.rootfs : config.rootfs + "-" + config.profile);
    paths.linuxSource = paths.build / ("linux-" + config.linuxVersion);
    paths.opensbiSource = paths.build / ("opensbi-" + config.opensbi);
    paths.busyboxSource = paths.build / ("busybox-" + config.busybox);
    paths.buildrootSource = paths.build / ("buildroot-" + config.buildroot);
    return paths;
}

struct ProcessResult {
    int status{127};
    std::string output;
};

ProcessResult runProcess(const std::vector<std::string>& arguments,
                         const fs::path& workingDirectory = {},
                         const std::unordered_map<std::string, std::string>& environment = {},
                         bool captureOutput = false) {
    if (arguments.empty()) throw std::runtime_error("Cannot run an empty command");
    int pipeFds[2]{-1, -1};
    if (captureOutput && pipe(pipeFds) != 0) throw std::runtime_error("pipe() failed");
    const pid_t child = fork();
    if (child < 0) {
        if (captureOutput) {
            close(pipeFds[0]);
            close(pipeFds[1]);
        }
        throw std::runtime_error("fork() failed");
    }
    if (child == 0) {
        if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) _exit(126);
        for (const auto& [key, value] : environment) setenv(key.c_str(), value.c_str(), 1);
        if (captureOutput) {
            dup2(pipeFds[1], STDOUT_FILENO);
            dup2(pipeFds[1], STDERR_FILENO);
            close(pipeFds[0]);
            close(pipeFds[1]);
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& item : arguments) argv.push_back(const_cast<char*>(item.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    if (captureOutput) close(pipeFds[1]);
    ProcessResult result;
    if (captureOutput) {
        std::array<char, 8192> buffer{};
        ssize_t count{};
        while ((count = read(pipeFds[0], buffer.data(), buffer.size())) > 0) {
            result.output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        close(pipeFds[0]);
    }
    int waitStatus{};
    while (waitpid(child, &waitStatus, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("waitpid() failed");
    }
    if (WIFEXITED(waitStatus))
        result.status = WEXITSTATUS(waitStatus);
    else if (WIFSIGNALED(waitStatus))
        result.status = 128 + WTERMSIG(waitStatus);
    return result;
}

void runChecked(const std::vector<std::string>& arguments, const fs::path& workingDirectory = {},
                const std::unordered_map<std::string, std::string>& environment = {},
                std::optional<fs::path> logFile = {}, bool verbose = false) {
    ProcessResult result;
    if (!verbose && logFile) {
        if (!logFile->parent_path().empty()) fs::create_directories(logFile->parent_path());
        const int output = open(logFile->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output < 0) throw std::runtime_error("Cannot create build log: " + logFile->string());
        const pid_t child = fork();
        if (child < 0) {
            close(output);
            throw std::runtime_error("fork() failed");
        }
        if (child == 0) {
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) _exit(126);
            for (const auto& [key, value] : environment) setenv(key.c_str(), value.c_str(), 1);
            dup2(output, STDOUT_FILENO);
            dup2(output, STDERR_FILENO);
            close(output);
            std::vector<char*> argv;
            argv.reserve(arguments.size() + 1);
            for (const auto& item : arguments) argv.push_back(const_cast<char*>(item.c_str()));
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(output);
        int status{};
        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("waitpid() failed");
        }
        if (WIFEXITED(status))
            result.status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            result.status = 128 + WTERMSIG(status);
        if (result.status != 0) {
            std::ifstream log{*logFile};
            std::cerr << log.rdbuf();
        }
    } else {
        result = runProcess(arguments, workingDirectory, environment, false);
    }
    if (result.status != 0) {
        std::ostringstream message;
        message << "Command failed (" << result.status << "):";
        for (const auto& argument : arguments) message << ' ' << argument;
        throw std::runtime_error(message.str());
    }
}

void runMake(const Config& config, const fs::path& cwd, std::vector<std::string> arguments,
             const fs::path& logFile,
             const std::unordered_map<std::string, std::string>& environment = {}) {
    std::vector<std::string> command{"make"};
    if (!config.verbose) {
        command.emplace_back("-s");
        command.emplace_back("--no-print-directory");
    }
    command.insert(command.end(), arguments.begin(), arguments.end());
    runChecked(command, cwd, environment, logFile, config.verbose);
}

void runToFile(const std::vector<std::string>& arguments, const fs::path& cwd,
               const fs::path& outputFile) {
    fs::create_directories(outputFile.parent_path());
    const int output = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) throw std::runtime_error("Cannot create output: " + outputFile.string());
    const pid_t child = fork();
    if (child < 0) {
        close(output);
        throw std::runtime_error("fork() failed");
    }
    if (child == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
        dup2(output, STDOUT_FILENO);
        close(output);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& item : arguments) argv.push_back(const_cast<char*>(item.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(output);
    int status{};
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("waitpid() failed");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fs::remove(outputFile);
        throw std::runtime_error("Command failed while generating " + outputFile.string());
    }
}

fs::path versionsPath();

std::string readFile(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Cannot read file: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void writeFile(const fs::path& path, std::string_view content,
               fs::perms permissions = fs::perms::owner_read | fs::perms::owner_write |
                                       fs::perms::group_read | fs::perms::others_read) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    if (fs::is_regular_file(path) && readFile(path) == content) {
        fs::permissions(path, permissions, fs::perm_options::replace);
        return;
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) throw std::runtime_error("Cannot write file: " + path.string());
    fs::permissions(path, permissions, fs::perm_options::replace);
}

void applyRootfsOverlay(const Config& config, const fs::path& root) {
    if (config.customRootfsOverlay.empty()) return;
    auto overlay = fs::path{config.customRootfsOverlay};
    if (overlay.is_relative()) overlay = versionsPath().parent_path() / overlay;
    overlay = fs::absolute(overlay);
    if (!fs::is_directory(overlay))
        throw std::runtime_error("Custom rootfs overlay is not a directory: " + overlay.string());
    fs::copy(overlay, root,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing |
                 fs::copy_options::copy_symlinks);
}

std::string runOutput(const std::vector<std::string>& arguments, const fs::path& cwd = {}) {
    const auto result = runProcess(arguments, cwd, {}, true);
    if (result.status != 0) {
        std::ostringstream message;
        message << "Command failed (" << result.status << "):";
        for (const auto& argument : arguments) message << ' ' << argument;
        if (!result.output.empty()) message << '\n' << result.output;
        throw std::runtime_error(message.str());
    }
    return result.output;
}

std::optional<fs::path> findProgram(std::string_view program) {
    const std::string name{program};
    if (name.find('/') != std::string::npos) {
        const fs::path candidate{name};
        if (fs::is_regular_file(candidate) && access(candidate.c_str(), X_OK) == 0)
            return candidate;
        return std::nullopt;
    }
    const auto path = envOr("PATH", "");
    std::stringstream entries{path};
    std::string entry;
    while (std::getline(entries, entry, ':')) {
        const fs::path candidate = (entry.empty() ? fs::current_path() : fs::path{entry}) / name;
        if (fs::is_regular_file(candidate) && access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    return std::nullopt;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    std::array<char, 9> formatted{};
    std::strftime(formatted.data(), formatted.size(), "%H:%M:%S", &local);
    return formatted.data();
}

void step(std::string_view text) { std::cout << timestamp() << " ==> " << text << '\n'; }
void info(std::string_view text) { std::cout << timestamp() << "   | " << text << '\n'; }

void download(const BuildPaths& paths, std::string filename, std::string url,
              unsigned retries = 1) {
    const auto destination = paths.sources / filename;
    if (fs::is_regular_file(destination)) return;
    step("Downloading " + filename + "...");
    std::vector<std::string> args{"wget", "-q", "--show-progress"};
    if (retries > 1) {
        args.emplace_back("--tries");
        args.push_back(std::to_string(retries));
    }
    args.emplace_back("-O");
    args.push_back(destination.string());
    args.push_back(std::move(url));
    runChecked(args, paths.sources, {}, paths.build / (filename + ".download.log"));
}

std::string sha256(const fs::path& file);

void ensureArchive(const BuildPaths& paths, const Config::Source& source,
                   const std::string& filename, const std::string& fallbackUrl,
                   std::string expectedSha256 = {}) {
    const auto destination = paths.sources / filename;
    const auto sourceStamp = destination.string() + ".source";
    std::string sourceIdentity;
    if (!source.file.empty()) {
        auto configuredFile = fs::path{source.file};
        if (configuredFile.is_relative())
            configuredFile = versionsPath().parent_path() / configuredFile;
        const auto localFile = fs::absolute(configuredFile);
        if (!fs::is_regular_file(localFile))
            throw std::runtime_error("Configured archive file does not exist: " +
                                     localFile.string());
        sourceIdentity = "file:" + localFile.string() + ":" + sha256(localFile);
    } else if (!source.url.empty()) {
        sourceIdentity = "url:" + source.url;
    }
    if (!sourceIdentity.empty() &&
        (!fs::is_regular_file(sourceStamp) || readFile(sourceStamp) != sourceIdentity + "\n")) {
        const auto base = filename.substr(0, filename.find('-'));
        if (base != "alpine") {
            auto version = filename.substr(base.size() + 1);
            const auto extension = version.find(".tar.");
            if (extension != std::string::npos) version.resize(extension);
            fs::remove_all(paths.build / (base + "-" + version));
        }
        fs::remove(destination);
    } else if (sourceIdentity.empty() && fs::is_regular_file(sourceStamp)) {
        fs::remove(destination);
        fs::remove(sourceStamp);
    }
    if (!source.file.empty()) {
        auto configuredFile = fs::path{source.file};
        if (configuredFile.is_relative())
            configuredFile = versionsPath().parent_path() / configuredFile;
        const auto localFile = fs::absolute(configuredFile);
        if (localFile != destination)
            fs::copy_file(localFile, destination, fs::copy_options::overwrite_existing);
        if (!sourceIdentity.empty()) writeFile(sourceStamp, sourceIdentity + "\n");
    } else {
        const auto& url = source.url.empty() ? fallbackUrl : source.url;
        bool shouldDownload = !fs::is_regular_file(destination);
        if (shouldDownload) {
            download(paths, filename, url, 3);
            if (!sourceIdentity.empty()) writeFile(sourceStamp, sourceIdentity + "\n");
        }
    }
    if (!source.sha256.empty()) expectedSha256 = source.sha256;
    if (!expectedSha256.empty()) {
        const auto actual = sha256(destination);
        if (actual != expectedSha256)
            throw std::runtime_error(filename + " SHA-256 mismatch: expected " + expectedSha256 +
                                     ", got " + actual);
    }
}

void extract(const fs::path& archive, const fs::path& destination) {
    if (fs::exists(destination)) return;
    step("Extracting " + archive.filename().string() + "...");
    runChecked({"tar", "-xf", archive.string()}, destination.parent_path());
}

std::string sha256(const fs::path& file) {
    const auto result = runOutput({"sha256sum", file.string()});
    const auto end = result.find_first_of(" \t");
    if (end == std::string::npos)
        throw std::runtime_error("Invalid sha256sum output for " + file.string());
    return result.substr(0, end);
}

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    std::size_t position{};
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

void validateConfig(const Config& config) {
    if (config.arch != "rv32" && config.arch != "rv64")
        throw std::runtime_error("--arch must be rv32 or rv64");
    if (config.libc != "auto" && config.libc != "musl" && config.libc != "glibc")
        throw std::runtime_error("--libc must be auto, musl, or glibc");
    if (config.rootfs != "alpine" && config.rootfs != "buildroot")
        throw std::runtime_error("--rootfs must be alpine or buildroot");
    if (config.profile != "gui" && config.profile != "buildroot" && config.profile != "debug" &&
        config.profile != "uefi" && config.profile != "virtio-gpu")
        throw std::runtime_error("Unsupported Linux profile: " + config.profile);
    if (config.rootfs == "buildroot" && config.profile != "buildroot")
        throw std::runtime_error("Buildroot currently provides only the generic Buildroot profile");
    if ((config.profile == "debug" || config.profile == "uefi") && config.rootfs != "alpine")
        throw std::runtime_error(config.profile + " requires the Alpine root filesystem");
    if (config.profile == "uefi" && config.arch != "rv64")
        throw std::runtime_error("The UEFI profile currently targets rv64");
    if (config.profile == "virtio-gpu" && (config.arch != "rv64" || config.rootfs != "alpine"))
        throw std::runtime_error("virtio-gpu requires RV64 Alpine");
    const std::array versions{config.linuxVersion, config.opensbi, config.busybox, config.alpine,
                              config.buildroot};
    for (const auto& version : versions) {
        if (version.empty() || !std::isalnum(static_cast<unsigned char>(version.front())) ||
            !std::all_of(version.begin(), version.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
            })) {
            throw std::runtime_error("Invalid component version: " + version);
        }
    }
    if (config.jobs.empty() ||
        !std::all_of(config.jobs.begin(), config.jobs.end(),
                     [](unsigned char ch) { return std::isdigit(ch); }) ||
        std::stoul(config.jobs) == 0)
        throw std::runtime_error("Build job count must be a positive integer");
    if (config.dramSizeMb.empty() ||
        !std::all_of(config.dramSizeMb.begin(), config.dramSizeMb.end(),
                     [](unsigned char ch) { return std::isdigit(ch); }) ||
        std::stoull(config.dramSizeMb) < 128)
        throw std::runtime_error("--dram-size-mb must be an integer of at least 128");
    if (config.rootfs == "buildroot" || config.profile == "uefi") {
        const auto& expectedSha = config.buildrootSource.sha256.empty()
                                      ? config.buildrootSha
                                      : config.buildrootSource.sha256;
        if (expectedSha.size() != 64 ||
            !std::all_of(expectedSha.begin(), expectedSha.end(),
                         [](unsigned char ch) { return std::isxdigit(ch); }))
            throw std::runtime_error(
                "Buildroot archive SHA-256 must contain 64 hexadecimal digits");
        if (config.buildroot != "2026.08.1" &&
            expectedSha == "34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b")
            throw std::runtime_error("A matching Buildroot SHA-256 is required for this version");
    }
}

std::string detectCrossCompiler(const Config& config) {
    if (!config.crossCompile.empty()) {
        if (!findProgram(config.crossCompile + "gcc"))
            throw std::runtime_error("Cross compiler not found: " + config.crossCompile + "gcc");
        return config.crossCompile;
    }
    std::vector<std::string> candidates;
    if (config.arch == "rv32" && config.libc == "auto") {
        candidates = {"riscv32-unknown-linux-musl-", "riscv32-linux-musl-",
                      "riscv32-unknown-linux-gnu-", "riscv64-unknown-linux-musl-",
                      "riscv64-unknown-linux-gnu-"};
    } else if (config.arch == "rv32" && config.libc == "musl") {
        candidates = {"riscv32-unknown-linux-musl-", "riscv32-linux-musl-",
                      "riscv64-unknown-linux-musl-"};
    } else if (config.arch == "rv32") {
        candidates = {"riscv32-unknown-linux-gnu-", "riscv64-unknown-linux-gnu-"};
    } else if (config.libc == "musl") {
        candidates = {"riscv64-unknown-linux-musl-", "riscv64-linux-musl-",
                      "riscv64-alpine-linux-musl-"};
    } else if (config.libc == "glibc") {
        candidates = {"riscv64-unknown-linux-gnu-", "riscv64-linux-gnu-"};
    } else {
        candidates = {"riscv64-unknown-linux-musl-", "riscv64-linux-musl-",
                      "riscv64-unknown-linux-gnu-", "riscv64-linux-gnu-"};
    }
    for (const auto& candidate : candidates) {
        if (findProgram(candidate + "gcc")) return candidate;
    }
    throw std::runtime_error("No suitable RISC-V Linux cross compiler found on PATH");
}

void prepareSources(const Config& config, const BuildPaths& paths) {
    fs::create_directories(paths.sources);
    const auto alpineMirror = config.alpineMirror.empty()
                                  ? "https://dl-cdn.alpinelinux.org/alpine/v" +
                                        config.alpine.substr(0, config.alpine.rfind('.'))
                                  : config.alpineMirror;
    ensureArchive(paths, config.opensbiSource, "opensbi-" + config.opensbi + ".tar.gz",
                  "https://github.com/riscv-software-src/opensbi/archive/refs/tags/v" +
                      config.opensbi + ".tar.gz");
    const auto kernelMajor = config.linuxVersion.substr(0, config.linuxVersion.find('.'));
    ensureArchive(paths, config.linuxSource, "linux-" + config.linuxVersion + ".tar.xz",
                  "https://cdn.kernel.org/pub/linux/kernel/v" + kernelMajor + ".x/linux-" +
                      config.linuxVersion + ".tar.xz");
    if (config.arch == "rv32" || config.rootfs == "buildroot") {
        ensureArchive(paths, config.busyboxSource, "busybox-" + config.busybox + ".tar.bz2",
                      "https://busybox.net/downloads/busybox-" + config.busybox + ".tar.bz2");
    } else {
        const auto archive = "alpine-minirootfs-" + config.alpine + "-riscv64.tar.gz";
        ensureArchive(paths, config.alpineSource, archive,
                      alpineMirror + "/releases/riscv64/" + archive);
    }
    if (config.rootfs == "buildroot" || config.profile == "uefi") {
        const auto archive = "buildroot-" + config.buildroot + ".tar.xz";
        ensureArchive(paths, config.buildrootSource, archive,
                      "https://buildroot.org/downloads/" + archive, config.buildrootSha);
    }

    extract(paths.sources / ("opensbi-" + config.opensbi + ".tar.gz"), paths.opensbiSource);
    extract(paths.sources / ("linux-" + config.linuxVersion + ".tar.xz"), paths.linuxSource);
    if (config.arch == "rv32" || config.rootfs == "buildroot")
        extract(paths.sources / ("busybox-" + config.busybox + ".tar.bz2"), paths.busyboxSource);
    if (config.rootfs == "buildroot" || config.profile == "uefi")
        extract(paths.sources / ("buildroot-" + config.buildroot + ".tar.xz"),
                paths.buildrootSource);

    const auto defconfig = paths.opensbiSource / "platform/generic/configs/defconfig";
    if (fs::is_regular_file(defconfig)) {
        auto text = readFile(defconfig);
        replaceAll(text, "CONFIG_SERIAL_SEMIHOSTING=y", "# CONFIG_SERIAL_SEMIHOSTING is not set");
        if (text.find("CONFIG_SBI_ECALL_LEGACY=y") != std::string::npos)
            replaceAll(text, "CONFIG_SBI_ECALL_LEGACY=y", "# CONFIG_SBI_ECALL_LEGACY is not set");
        else if (text.find("CONFIG_SBI_ECALL_LEGACY") == std::string::npos)
            text += "\n# CONFIG_SBI_ECALL_LEGACY is not set\n";
        writeFile(defconfig, text);
    }
}

void buildBuildrootRootfs(const Config& config, const BuildPaths& paths) {
    const fs::path output = fs::absolute(config.buildrootOutput.empty()
                                             ? paths.build / ("buildroot-output-" + config.arch)
                                             : fs::path{config.buildrootOutput});
    const auto external = paths.source / "configs/buildroot/simrv-buildroot";
    const auto defconfig = external / (config.arch + "_defconfig");
    if (!fs::is_regular_file(defconfig))
        throw std::runtime_error("Missing Buildroot profile defconfig: " + defconfig.string());
    if (!config.reuseBuildroot) fs::remove_all(output);
    fs::create_directories(output);
    fs::copy_file(defconfig, output / ".config", fs::copy_options::overwrite_existing);

    const auto buildrootConfig = paths.buildrootSource / "utils/config";
    runChecked({buildrootConfig.string(), "--file", (output / ".config").string(), "--set-str",
                "BR2_ROOTFS_POST_BUILD_SCRIPT", buildrootPostBuildPath},
               paths.buildrootSource);
    runMake(config, paths.buildrootSource,
            {"O=" + output.string(), "BR2_EXTERNAL=" + external.string(), "olddefconfig"},
            paths.build / "buildroot-olddefconfig.log");

    const auto overlay = external / "rootfs-overlay";
    const auto target = output / "target";
    fs::create_directories(target);
    if (fs::is_directory(overlay))
        fs::copy(overlay, target,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    std::unordered_map<std::string, std::string> buildEnvironment;
    if (!config.customRootfsOverlay.empty()) {
        auto customOverlay = fs::path{config.customRootfsOverlay};
        if (customOverlay.is_relative())
            customOverlay = versionsPath().parent_path() / customOverlay;
        customOverlay = fs::absolute(customOverlay);
        if (!fs::is_directory(customOverlay))
            throw std::runtime_error("Custom rootfs overlay is not a directory: " +
                                     customOverlay.string());
        buildEnvironment.emplace("SIMRV_LINUX_CUSTOM_OVERLAY", customOverlay.string());
    }
    step("Building Buildroot " + config.buildroot + " Buildroot root filesystem...");
    runMake(config, paths.buildrootSource,
            {"O=" + output.string(), "BR2_EXTERNAL=" + external.string(), "-j" + config.jobs},
            paths.build / "buildroot-build.log", buildEnvironment);
    const auto rootfs = output / "images/rootfs.ext4";
    if (!fs::is_regular_file(rootfs))
        throw std::runtime_error("Buildroot did not produce " + rootfs.string());
    fs::create_directories(paths.images);
    fs::copy_file(rootfs, paths.images / "rootfs.img", fs::copy_options::overwrite_existing);
    runChecked({"e2fsck", "-fy", (paths.images / "rootfs.img").string()}, {}, {},
               paths.build / "rootfs-e2fsck.log", config.verbose);
}

struct TargetArch {
    std::string xlen;
    std::string march;
    std::string mabi;
};

TargetArch targetArch(const Config& config) {
    if (config.arch == "rv64") return {"64", "rv64gc", "lp64d"};
    if (config.rootfs == "buildroot" && config.profile == "buildroot")
        return {"32", "rv32ima_zicsr_zicntr", "ilp32"};
    return {"32", "rv32gc", "ilp32d"};
}

void buildInitramfs(const Config& config, const BuildPaths& paths,
                    const std::string& crossCompile) {
    const auto architecture = targetArch(config);
    const auto initramfs = paths.build / ("initramfs-" + config.arch);
    fs::remove_all(initramfs);
    fs::create_directories(initramfs);

    if (config.rootfs == "buildroot") {
        const fs::path output = fs::absolute(config.buildrootOutput.empty()
                                                 ? paths.build / ("buildroot-output-" + config.arch)
                                                 : fs::path{config.buildrootOutput});
        const auto target = output / "target";
        if (!fs::is_regular_file(target / "bin/busybox"))
            throw std::runtime_error("Buildroot did not install BusyBox in " + target.string());
        for (const auto* directory : {"bin", "sbin", "lib", "usr/lib"}) {
            const auto source = target / directory;
            if (fs::is_directory(source)) {
                fs::create_directories((initramfs / directory).parent_path());
                fs::copy(source, initramfs / directory,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing |
                             fs::copy_options::copy_symlinks);
            }
        }
        for (const auto* command : {"bin/sh", "bin/mount", "bin/mkdir", "bin/rm", "bin/sleep",
                                    "sbin/init", "sbin/switch_root"}) {
            if (!fs::exists(initramfs / command))
                throw std::runtime_error(
                    "Buildroot BusyBox is missing required early-userspace "
                    "command: " +
                    std::string{command});
        }
    } else if (architecture.xlen == "32") {
        const auto install = paths.busyboxSource / "_install";
        if (!fs::is_regular_file(install / "bin/busybox")) {
            step("Configuring BusyBox...");
            (void)runProcess({"make", "-s", "--no-print-directory", "clean"}, paths.busyboxSource);
            const auto compiler =
                crossCompile + "gcc -march=" + architecture.march + " -mabi=" + architecture.mabi;
            // BusyBox's LD is a compiler-driver invocation (CC -nostdlib),
            // not the raw binutils linker. The driver supplies the target
            // runtime and handles BusyBox's archive ordering correctly.
            auto linker = compiler + " -nostdlib";
            std::string linkerFlags;
            if (architecture.xlen == "32") {
                linkerFlags = "-Wl,-m,elf32lriscv";
            }
            runMake(config, paths.busyboxSource,
                    {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "CC=" + compiler,
                     "LD=" + linker, "defconfig"},
                    paths.build / "busybox-defconfig.log");
            auto busyboxConfig = readFile(paths.busyboxSource / ".config");
            replaceAll(busyboxConfig, "# CONFIG_STATIC is not set", "CONFIG_STATIC=y");
            if (architecture.xlen == "32")
                replaceAll(busyboxConfig, "CONFIG_HWCLOCK=y", "# CONFIG_HWCLOCK is not set");
            replaceAll(busyboxConfig, "CONFIG_TC=y", "# CONFIG_TC is not set");
            replaceAll(busyboxConfig, "CONFIG_FEATURE_TC_INGRESS=y",
                       "# CONFIG_FEATURE_TC_INGRESS is not set");
            writeFile(paths.busyboxSource / ".config", busyboxConfig);
            step("Building BusyBox " + config.arch + "...");
            runMake(config, paths.busyboxSource,
                    {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "CC=" + compiler,
                     "LD=" + linker, "EXTRA_LDFLAGS=" + linkerFlags, "-j" + config.jobs, "install"},
                    paths.build / "busybox-build.log");
        }
        if (!fs::is_directory(install))
            throw std::runtime_error("BusyBox install tree is missing: " + install.string());
        fs::copy(install, initramfs,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing |
                     fs::copy_options::copy_symlinks);
    } else {
        const auto archive =
            paths.sources / ("alpine-minirootfs-" + config.alpine + "-riscv64.tar.gz");
        step("Extracting Alpine minirootfs into the initramfs...");
        runChecked({"tar", "-xf", archive.string(), "-C", initramfs.string()});
    }

    for (const auto& directory : {"proc", "sys", "dev", "etc", "tmp"})
        fs::create_directories(initramfs / directory);
    writeFile(initramfs / "etc/inittab", "ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100\n");
    writeFile(initramfs / "etc/hostname", "SimRV\n");
    std::string initScript = R"SIMRV(#!/bin/sh
mkdir -p /dev /proc /sys /etc /tmp /run
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /tmp/simrv-jwm.lock/pid
rmdir /tmp/simrv-jwm.lock 2>/dev/null || true
/bin/mount -t proc proc /proc 2>/dev/null || true
/bin/mount -t sysfs sysfs /sys 2>/dev/null || true
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts
/bin/mount -t devpts devpts /dev/pts 2>/dev/null || true
if [ ! -e /dev/ptmx ] && [ ! -L /dev/ptmx ]; then ln -s pts/ptmx /dev/ptmx 2>/dev/null || true; fi
[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1 2>/dev/null || true
[ -c /dev/tty ] || mknod -m 666 /dev/tty c 5 0 2>/dev/null || true
[ -c /dev/ttyS0 ] || mknod -m 666 /dev/ttyS0 c 4 64 2>/dev/null || true
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3 2>/dev/null || true
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5 2>/dev/null || true
[ -c /dev/mem ] || mknod -m 600 /dev/mem c 1 1 2>/dev/null || true
exec 3>/dev/ttyS0 2>/dev/null || exec 3>/dev/console
boot_log() { echo "[INIT] $*" >&3; }
if [ -b /dev/vda ]; then
    mkdir -p /newroot
    mounted=0
    attempt=0
    for _ in 1 2 3 4 5; do
        attempt=$((attempt + 1))
        if mount -t ext4 -L ROOTFS_LABEL_PLACEHOLDER /newroot 2>/dev/null || mount -t ext4 /dev/vda /newroot 2>/dev/null; then
            mounted=1
            break
        fi
        boot_log "Waiting for root filesystem on /dev/vda (attempt $attempt/5)"
        sleep 1
    done
    if [ "$mounted" -eq 1 ]; then
        boot_log "Mounted root filesystem on /dev/vda"
        handoff_ok=1
        if ! mount --move /dev /newroot/dev; then
            boot_log "ERROR: could not move /dev into the root filesystem"
            handoff_ok=0
        elif ! mount --move /proc /newroot/proc; then
            boot_log "ERROR: could not move /proc into the root filesystem"
            mount --move /newroot/dev /dev 2>/dev/null || true
            handoff_ok=0
        elif ! mount --move /sys /newroot/sys; then
            boot_log "ERROR: could not move /sys into the root filesystem"
            mount --move /newroot/proc /proc 2>/dev/null || true
            mount --move /newroot/dev /dev 2>/dev/null || true
            handoff_ok=0
        fi
        if [ "$handoff_ok" -eq 1 ]; then
            boot_log "Switching root to /sbin/init"
            if ! exec /sbin/switch_root -c /dev/console /newroot /sbin/init; then
                boot_log "ERROR: switch_root failed; returning to initramfs recovery"
                mount --move /newroot/sys /sys 2>/dev/null || true
                mount --move /newroot/proc /proc 2>/dev/null || true
                mount --move /newroot/dev /dev 2>/dev/null || true
            fi
        fi
    else
        boot_log "ERROR: could not mount the ext4 root filesystem on /dev/vda after 5 attempts"
    fi
else
    boot_log "ERROR: root disk /dev/vda is missing; starting initramfs recovery"
fi
[ -f /etc/hostname ] && hostname -F /etc/hostname 2>/dev/null || true
echo ""
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
if [ -f /etc/alpine-release ]; then
    echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release 2>/dev/null || echo 3.24)"
else
    echo "Minimal BusyBox Linux (riscv32)"
fi
echo "=================================================="
echo ""
exec /sbin/init
)SIMRV";
    replaceAll(
        initScript, "ROOTFS_LABEL_PLACEHOLDER",
        config.rootfs == "buildroot" || config.profile == "uefi" ? "simrv-root" : "simrv-alpine");
    if (config.profile == "debug") {
        const auto newline = initScript.find('\n');
        if (newline != std::string::npos) initScript.insert(newline + 1, "set -x\n");
    }
    writeFile(initramfs / "init", initScript,
              fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                  fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                  fs::perms::others_exec);
    applyRootfsOverlay(config, initramfs);

    const auto genInitCpio = paths.build / "gen_init_cpio";
    const auto genInitCpioSource = paths.linuxSource / "usr/gen_init_cpio.c";
    if (!fs::is_regular_file(genInitCpio) ||
        fs::last_write_time(genInitCpioSource) > fs::last_write_time(genInitCpio)) {
        const auto hostCompiler =
            findProgram("gcc").value_or(findProgram("cc").value_or(fs::path{}));
        if (hostCompiler.empty())
            throw std::runtime_error("A host C compiler is required for gen_init_cpio");
        step("Building gen_init_cpio...");
        runChecked(
            {hostCompiler.string(), "-O2", "-o", genInitCpio.string(), genInitCpioSource.string()});
    }

    const auto manifest = paths.build / ("initramfs_list_" + config.arch + ".txt");
    std::ostringstream entries;
    entries << "# Required device nodes\n"
            << "dir /dev 0755 0 0\n"
            << "nod /dev/console 0600 0 0 c 5 1\n"
            << "nod /dev/ttyS0 0666 0 0 c 4 64\n"
            << "nod /dev/null 0666 0 0 c 1 3\n"
            << "nod /dev/tty 0666 0 0 c 5 0\n"
            << "nod /dev/zero 0666 0 0 c 1 5\n"
            << "nod /dev/urandom 0666 0 0 c 1 9\n"
            << "nod /dev/random 0666 0 0 c 1 8\n"
            << "nod /dev/mem 0600 0 0 c 1 1\n";
    std::vector<fs::path> contentPaths;
    for (const auto& entry : fs::recursive_directory_iterator(initramfs))
        contentPaths.push_back(entry.path());
    std::sort(contentPaths.begin(), contentPaths.end());
    for (const auto& path : contentPaths) {
        const auto relative = path.lexically_relative(initramfs).generic_string();
        if (relative == "dev" || relative.rfind("dev/", 0) == 0) continue;
        struct stat status{};
        if (lstat(path.c_str(), &status) != 0)
            throw std::runtime_error("lstat failed: " + path.string());
        std::ostringstream mode;
        mode << '0' << std::oct << (status.st_mode & 07777);
        if (S_ISDIR(status.st_mode)) {
            entries << "dir /" << relative << ' ' << mode.str() << " 0 0\n";
        } else if (S_ISLNK(status.st_mode)) {
            entries << "slink /" << relative << ' ' << fs::read_symlink(path).string() << ' '
                    << mode.str() << " 0 0\n";
        } else if (S_ISREG(status.st_mode)) {
            entries << "file /" << relative << ' ' << path.string() << ' ' << mode.str()
                    << " 0 0\n";
        }
    }
    writeFile(manifest, entries.str());
    const auto cpioPath = paths.build / ("initramfs_" + config.arch + ".cpio");
    step("Generating initramfs cpio...");
    runToFile({genInitCpio.string(), "-t", "0", manifest.string()}, paths.build, cpioPath);
}

std::string jsonString(std::string_view json, std::string_view key) {
    const std::regex property{"\"" + std::string{key} + "\"\\s*:\\s*\"([^\"]*)\""};
    std::smatch match;
    const std::string input{json};
    return std::regex_search(input, match, property) ? match[1].str() : std::string{};
}

bool kernelConfigEnabled(std::string_view text, std::string_view symbol) {
    return text.find(std::string{symbol} + "=y") != std::string_view::npos;
}

void configureKernel(const Config& config, const BuildPaths& paths,
                     const std::string& crossCompile) {
    const auto source = paths.linuxSource;
    const auto configTool = source / "scripts/config";
    const auto defconfig = config.arch == "rv64" ? "defconfig" : "rv32_defconfig";
    runMake(config, source, {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, defconfig},
            paths.build / "linux-defconfig.log");

    std::vector<std::string> changes{configTool.string()};
    const auto enable = [&changes](std::string_view name) {
        changes.emplace_back("--enable");
        changes.emplace_back(name);
    };
    const auto disable = [&changes](std::string_view name) {
        changes.emplace_back("--disable");
        changes.emplace_back(name);
    };
    const auto setString = [&changes](std::string_view name, std::string value) {
        changes.emplace_back("--set-str");
        changes.emplace_back(name);
        changes.push_back(std::move(value));
    };

    for (const auto symbol : {"CONFIG_BLK_DEV_INITRD",
                              "CONFIG_SERIAL_8250",
                              "CONFIG_SERIAL_8250_CONSOLE",
                              "CONFIG_SERIAL_8250_MMIO",
                              "CONFIG_SERIAL_OF_PLATFORM",
                              "CONFIG_VIRTIO",
                              "CONFIG_VIRTIO_MENU",
                              "CONFIG_VIRTIO_MMIO",
                              "CONFIG_VIRTIO_BLK",
                              "CONFIG_VIRTIO_NET",
                              "CONFIG_VIRTIO_CONSOLE",
                              "CONFIG_FUSE_FS",
                              "CONFIG_VIRTIO_FS",
                              "CONFIG_NET",
                              "CONFIG_INET",
                              "CONFIG_NETDEVICES",
                              "CONFIG_ETHERNET",
                              "CONFIG_EXT4_FS",
                              "CONFIG_JBD2",
                              "CONFIG_EXT4_FS_POSIX_ACL",
                              "CONFIG_TMPFS",
                              "CONFIG_PROC_FS",
                              "CONFIG_SYSFS",
                              "CONFIG_INPUT",
                              "CONFIG_INPUT_EVDEV",
                              "CONFIG_INPUT_KEYBOARD",
                              "CONFIG_INPUT_MOUSE",
                              "CONFIG_VIRTIO_INPUT",
                              "CONFIG_POWER_RESET",
                              "CONFIG_POWER_RESET_SYSCON",
                              "CONFIG_POWER_RESET_SYSCON_POWEROFF",
                              "CONFIG_DEVTMPFS",
                              "CONFIG_DEVTMPFS_MOUNT",
                              "CONFIG_DEVMEM",
                              "CONFIG_TTY",
                              "CONFIG_VT",
                              "CONFIG_FB",
                              "CONFIG_FRAMEBUFFER_CONSOLE",
                              "CONFIG_PERF_EVENTS",
                              "CONFIG_RISCV_PMU",
                              "CONFIG_RISCV_PMU_SBI",
                              "CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS",
                              "CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE"})
        enable(symbol);
    setString("CONFIG_INITRAMFS_SOURCE", "initramfs.cpio");
    disable("CONFIG_MODULES");

    const bool buildrootProfile = config.rootfs == "buildroot" && config.profile == "buildroot";
    if (buildrootProfile) {
        disable("CONFIG_PCI");
        disable("CONFIG_VIRTIO_PCI");
    } else {
        enable("CONFIG_VIRTIO_PCI");
    }
    if (!buildrootProfile) enable("CONFIG_FB_SIMPLE");
    if (config.profile == "virtio-gpu") {
        for (const auto symbol :
             {"CONFIG_DRM", "CONFIG_DRM_VIRTIO_GPU", "CONFIG_DRM_FBDEV_EMULATION",
              "CONFIG_VIRTIO_PCI", "CONFIG_FB_SIMPLE"})
            enable(symbol);
    }
    if (config.profile == "uefi") {
        for (const auto symbol : {"CONFIG_EFI", "CONFIG_EFI_STUB", "CONFIG_EFI_PARTITION",
                                  "CONFIG_CMDLINE_BOOL", "CONFIG_CMDLINE_FORCE"})
            enable(symbol);
        setString("CONFIG_CMDLINE",
                  "console=ttyS0,115200 earlycon=uart8250,mmio,0x10000000,115200n8 "
                  "root=LABEL=simrv-root rootfstype=ext4 rootwait rw loglevel=7");
    }

    for (const auto symbol :
         {"CONFIG_SLUB_DEBUG", "CONFIG_DEBUG_KERNEL", "CONFIG_PROFILING", "CONFIG_SOUND",
          "CONFIG_WLAN", "CONFIG_RAID6_PQ_BENCHMARK", "CONFIG_MD_RAID456", "CONFIG_MD",
          "CONFIG_BLK_DEV_MD", "CONFIG_RISCV_EMULATED_UNALIGNED_ACCESS"})
        disable(symbol);
    if (config.profile != "virtio-gpu") disable("CONFIG_DRM");
    if (config.profile == "virtio-gpu") {
        for (const auto symbol : {"CONFIG_SOUND", "CONFIG_SND", "CONFIG_SND_VIRTIO"})
            enable(symbol);
    }
    if (config.profile == "debug") {
        for (const auto symbol :
             {"CONFIG_PRINTK_TIME", "CONFIG_PRINTK_CALLER", "CONFIG_DEBUG_KERNEL",
              "CONFIG_DEBUG_FS", "CONFIG_KALLSYMS", "CONFIG_KALLSYMS_ALL", "CONFIG_STACKTRACE",
              "CONFIG_MAGIC_SYSRQ", "CONFIG_FTRACE", "CONFIG_FUNCTION_TRACER",
              "CONFIG_FUNCTION_GRAPH_TRACER", "CONFIG_BLK_DEV_IO_TRACE", "CONFIG_DEBUG_INFO",
              "CONFIG_DEBUG_INFO_DWARF5"})
            enable(symbol);
    } else {
        for (const auto symbol :
             {"CONFIG_PRINTK_TIME", "CONFIG_PRINTK_CALLER", "CONFIG_DEBUG_FS",
              "CONFIG_KALLSYMS_ALL", "CONFIG_FUNCTION_GRAPH_TRACER", "CONFIG_BLK_DEV_IO_TRACE"})
            disable(symbol);
    }
    runChecked(changes, source);
    runMake(config, source, {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "olddefconfig"},
            paths.build / "linux-olddefconfig.log");

    const auto finalConfig = readFile(source / ".config");
    const auto requireEnabled = [&finalConfig](std::string_view symbol, std::string_view reason) {
        if (!kernelConfigEnabled(finalConfig, symbol))
            throw std::runtime_error("Linux config did not enable " + std::string{symbol} + "; " +
                                     std::string{reason});
    };
    for (const auto symbol : {"CONFIG_PERF_EVENTS", "CONFIG_RISCV_PMU", "CONFIG_RISCV_PMU_SBI"})
        requireEnabled(symbol, "SBI HPM/perf support would be unavailable");
    if (config.profile == "virtio-gpu") {
        for (const auto symbol : {"CONFIG_DRM", "CONFIG_DRM_VIRTIO_GPU",
                                  "CONFIG_DRM_FBDEV_EMULATION", "CONFIG_FB_SIMPLE"})
            requireEnabled(symbol, "both framebuffer devices are required");
    }
    if (config.profile == "virtio-gpu") {
        for (const auto symbol : {"CONFIG_SOUND", "CONFIG_SND", "CONFIG_SND_VIRTIO"})
            requireEnabled(symbol, "VirtIO sound is required");
    }
    if (config.profile == "uefi") {
        for (const auto symbol : {"CONFIG_EFI", "CONFIG_EFI_STUB", "CONFIG_CMDLINE_FORCE"})
            requireEnabled(symbol, "UEFI boot image cannot be produced");
        if (finalConfig.find("CONFIG_CMDLINE=\"console=ttyS0,115200 "
                             "earlycon=uart8250,mmio,0x10000000,115200n8 root=LABEL=simrv-root") ==
            std::string::npos)
            throw std::runtime_error("Linux config did not retain the UEFI rootfs command line");
    }
    if (config.profile != "debug" && kernelConfigEnabled(finalConfig, "CONFIG_PRINTK_CALLER"))
        throw std::runtime_error("Normal image profile unexpectedly enables CONFIG_PRINTK_CALLER");
}

void buildKernel(const Config& config, const BuildPaths& paths, const std::string& crossCompile) {
    const auto source = paths.linuxSource;
    fs::create_directories(paths.images);
    const bool reuseArtifacts =
        envOr("SIMRV_LINUX_REUSE_KERNEL", "1") != "0" && !config.rebuildKernel;
    step("Preparing Linux kernel " + config.linuxVersion + " for " + config.arch + "...");
    if (!reuseArtifacts)
        runMake(config, source, {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "mrproper"},
                paths.build / "linux-mrproper.log");
    fs::copy_file(paths.build / ("initramfs_" + config.arch + ".cpio"), source / "initramfs.cpio",
                  fs::copy_options::overwrite_existing);
    configureKernel(config, paths, crossCompile);

    const auto configHash = sha256(source / ".config");
    const auto initramfsHash = sha256(paths.build / ("initramfs_" + config.arch + ".cpio"));
    const auto stampConfig = config.arch + "|" + config.linuxVersion + "|" + configHash;
    const auto stampContents = stampConfig + "|" + initramfsHash;
    bool reuseKernel = false;
    const auto publishedKernel = paths.images / "vmlinux";
    const auto publishedManifest = paths.images / "manifest.json";
    if (reuseArtifacts && fs::is_regular_file(publishedManifest) &&
        fs::is_regular_file(publishedKernel)) {
        const auto manifest = readFile(publishedManifest);
        const bool archCompatible = true;
        if (archCompatible && jsonString(manifest, "architecture") == config.arch &&
            jsonString(manifest, "linux_version") == config.linuxVersion &&
            jsonString(manifest, "profile") == config.profile &&
            jsonString(manifest, "rootfs") == config.rootfs &&
            jsonString(manifest, "initramfs_sha256") == initramfsHash &&
            jsonString(manifest, "kernel_config_sha256") == configHash &&
            jsonString(manifest, "vmlinux") == sha256(publishedKernel)) {
            fs::copy_file(publishedKernel, source / "vmlinux",
                          fs::copy_options::overwrite_existing);
            reuseKernel = true;
            info("Skipping kernel rebuild; the published image matches the selected inputs.");
        }
    }
    const auto stamp = source / ".simrv-kernel-stamp";
    const auto previousStamp = fs::is_regular_file(stamp) ? readFile(stamp) : std::string{};
    if (!reuseKernel && reuseArtifacts && fs::is_regular_file(source / "vmlinux") &&
        previousStamp == stampContents + "\n") {
        reuseKernel = true;
        info("Skipping kernel rebuild; the build-tree image matches the selected inputs.");
    }
    if (!reuseKernel && config.requireKernelReuse)
        throw std::runtime_error(
            "The selected kernel does not match a reusable image or build stamp; refusing to "
            "rebuild");
    if (!reuseKernel) {
        const bool kernelConfigMatches =
            reuseArtifacts && fs::is_regular_file(source / "vmlinux") &&
            (previousStamp == stampConfig + "\n" || previousStamp.starts_with(stampConfig + "|"));
        if (!kernelConfigMatches)
            runMake(config, source, {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "clean"},
                    paths.build / "linux-clean.log");
        runMake(config, source, {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "olddefconfig"},
                paths.build / "linux-post-clean-olddefconfig.log");
        step("Building Linux vmlinux...");
        runMake(config, source,
                {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "-j" + config.jobs, "vmlinux"},
                paths.build / "linux-vmlinux.log");
    }
    if (!fs::is_regular_file(source / "vmlinux"))
        throw std::runtime_error("Linux build did not produce vmlinux");
    writeFile(stamp, stampContents + "\n");
    if (config.profile == "uefi") {
        runMake(config, source,
                {"ARCH=riscv", "CROSS_COMPILE=" + crossCompile, "-j" + config.jobs, "Image"},
                paths.build / "linux-image.log");
        fs::copy_file(source / "arch/riscv/boot/Image", paths.images / "uefi-kernel.efi",
                      fs::copy_options::overwrite_existing);
    }
    const auto objcopy = crossCompile + "objcopy";
    runChecked({objcopy, "-O", "binary", "vmlinux",
                (paths.build / ("vmlinux_" + config.arch + ".bin")).string()},
               source, {}, paths.build / "linux-objcopy.log", config.verbose);
    fs::copy_file(paths.build / ("vmlinux_" + config.arch + ".bin"),
                  paths.build / ("Image_" + config.arch), fs::copy_options::overwrite_existing);
}

std::string hexNumber(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

void removePciNode(std::string& dts) {
    const auto start = dts.find("        pci@30000000 {");
    if (start == std::string::npos) throw std::runtime_error("PCI node not found in virt DTS");
    const auto open = dts.find('{', start);
    if (open == std::string::npos) throw std::runtime_error("Malformed PCI node in virt DTS");
    std::size_t depth{};
    std::size_t end = open;
    for (; end < dts.size(); ++end) {
        if (dts[end] == '{')
            ++depth;
        else if (dts[end] == '}' && --depth == 0) {
            ++end;
            break;
        }
    }
    if (depth != 0) throw std::runtime_error("Unclosed PCI node in virt DTS");
    if (end < dts.size() && dts[end] == ';') ++end;
    if (end < dts.size() && dts[end] == '\n') ++end;
    dts.erase(start, end - start);
}

fs::path compileDeviceTree(const Config& config, const BuildPaths& paths) {
    const auto architecture = targetArch(config);
    const auto templatePath =
        paths.source / "scripts/templates" / ("virt-rv" + architecture.xlen + ".dts");
    auto dts = readFile(templatePath);
    if (config.rootfs == "buildroot" && config.profile == "buildroot") {
        removePciNode(dts);
        const auto rtc = dts.find("        rtc: rtc@70000000 {");
        if (rtc == std::string::npos) throw std::runtime_error("RTC node not found in virt DTS");
        const std::string mmio = R"DTS(        virtio@10001000 {
            compatible = "virtio,mmio";
            reg = <0x10001000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <2>;
        };
        virtio@10002000 {
            compatible = "virtio,mmio";
            reg = <0x10002000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <1>;
        };
        virtio@10003000 {
            compatible = "virtio,mmio";
            reg = <0x10003000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <4>;
        };
        virtio@10005000 {
            compatible = "virtio,mmio";
            reg = <0x10005000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <6>;
        };
        virtio@10007000 {
            compatible = "virtio,mmio";
            reg = <0x10007000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <8>;
        };
        virtio@1000a000 {
            compatible = "virtio,mmio";
            reg = <0x1000a000 0x1000>;
            interrupt-parent = <&plic>;
            interrupts = <9>;
        };
)DTS";
        dts.insert(rtc, mmio);
    }

    const auto memoryBytes =
        static_cast<std::uint64_t>(std::stoull(config.dramSizeMb)) * 1024 * 1024;
    const auto regPattern = config.arch == "rv64"
                                ? std::regex{R"rx(reg = <0x0 0x80000000 0x0 0x[0-9a-fA-F]+>;)rx"}
                                : std::regex{R"rx(reg = <0x80000000 0x[0-9a-fA-F]+>;)rx"};
    const auto memoryReg = config.arch == "rv64"
                               ? "reg = <0x0 0x80000000 0x0 " + hexNumber(memoryBytes) + ">;"
                               : "reg = <0x80000000 " + hexNumber(memoryBytes) + ">;";
    dts = std::regex_replace(dts, regPattern, memoryReg, std::regex_constants::format_first_only);
    const std::string rootLabel =
        config.rootfs == "buildroot" || config.profile == "uefi" ? "simrv-root" : "simrv-alpine";
    dts = std::regex_replace(dts, std::regex{R"rx(root=[^ ]+)rx"}, "root=LABEL=" + rootLabel);
    replaceAll(dts, " rdinit=/init", "");
    if (config.profile == "debug") replaceAll(dts, " rw loglevel=7", " rw rdinit=/init loglevel=7");
    if (config.profile == "virtio-gpu")
        replaceAll(dts, " rw loglevel=7", " rw fbcon=map:01 loglevel=7");

    const auto generated = paths.build / ("virt-rv" + architecture.xlen + "-" + config.rootfs +
                                          "-" + config.profile + ".dts");
    writeFile(generated, dts);
    auto dtc = findProgram("dtc");
    if (!dtc) {
        const auto inTree = paths.linuxSource / "scripts/dtc/dtc";
        if (fs::is_regular_file(inTree) && access(inTree.c_str(), X_OK) == 0) dtc = inTree;
    }
    if (!dtc) throw std::runtime_error("Device-tree compiler dtc was not found");
    runChecked({dtc->string(), "-I", "dts", "-O", "dtb", "-o",
                (paths.images / "devicetree.dtb").string(), generated.string()},
               {}, {}, paths.build / "dtc.log", config.verbose);
    return generated;
}

void writeSetupScript(const Config& config, const BuildPaths& paths) {
    std::string setup = R"SH(#!/bin/bash
if [ -n "${BASH_SOURCE:-}" ] && [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "Source this file to export image paths: source $0" >&2
    exit 1
fi
if [ -n "${BASH_SOURCE:-}" ]; then
    _simrv_setup_source="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
    _simrv_setup_source="${(%):-%N}"
else
    _simrv_setup_source="$0"
fi
IMAGES_DIR="$(cd "$(dirname "$_simrv_setup_source")" && pwd)"
unset _simrv_setup_source
export SIMRV_LINUX_MEM_IMG="$IMAGES_DIR/opensbi-linux-payload.elf"
export SIMRV_LINUX_DISK_IMG="$IMAGES_DIR/rootfs.img"
export SIMRV_LINUX_DTB="$IMAGES_DIR/devicetree.dtb"
export SIMRV_LINUX_IMAGE="$IMAGES_DIR/Image"
if [ -f "$IMAGES_DIR/uefi-disk.img" ]; then
    export SIMRV_UEFI_DISK_IMG="$IMAGES_DIR/uefi-disk.img"
    export SIMRV_UEFI_KERNEL="$IMAGES_DIR/uefi-kernel.efi"
    export SIMRV_UEFI_CODE="$IMAGES_DIR/RISCV_VIRT_CODE.fd"
    export SIMRV_UEFI_VARS_TEMPLATE="$IMAGES_DIR/RISCV_VIRT_VARS.fd"
fi
export SIMRV_LINUX_OPENSBI="$IMAGES_DIR/opensbi-dynamic.bin"
export SIMRV_LINUX_PROFILE="PROFILE_PLACEHOLDER"
export SIMRV_LINUX_ROOTFS="ROOTFS_PLACEHOLDER"
export SIMRV_LINUX_TIMEOUT=60
export SIMRV_LINUX_END=1200000
for _simrv_setup_file in "$SIMRV_LINUX_MEM_IMG" "$SIMRV_LINUX_DISK_IMG" \
                         "$SIMRV_LINUX_DTB" "$SIMRV_LINUX_IMAGE" "$SIMRV_LINUX_OPENSBI"; do
    if [ ! -f "$_simrv_setup_file" ]; then
        echo "SimRV Linux image is missing: $_simrv_setup_file" >&2
        unset _simrv_setup_file
        return 1 2>/dev/null || exit 1
    fi
done
unset _simrv_setup_file
echo "SimRV OpenSBI + Linux ROOTFS_PLACEHOLDER/PROFILE_PLACEHOLDER setup complete"
echo "├─ Payload image: $SIMRV_LINUX_MEM_IMG"
echo "├─ OpenSBI dynamic: $SIMRV_LINUX_OPENSBI"
echo "├─ Linux Image: $SIMRV_LINUX_IMAGE"
echo "├─ Disk image: $SIMRV_LINUX_DISK_IMG"
echo "└─ Device tree: $SIMRV_LINUX_DTB"
echo "Guest networking: DHCP through SimRV user-mode NAT; no host network setup required"
)SH";
    replaceAll(setup, "PROFILE_PLACEHOLDER", config.profile);
    replaceAll(setup, "ROOTFS_PLACEHOLDER", config.rootfs);
    writeFile(paths.images / "setup.sh", setup,
              fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                  fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                  fs::perms::others_exec);
}

void buildOpenSbi(const Config& config, const BuildPaths& paths, const std::string& crossCompile) {
    const auto architecture = targetArch(config);
    (void)compileDeviceTree(config, paths);
    const auto output = paths.opensbiSource / "build";
    auto opensbiArch = architecture.march;
    const auto opensbiBuildStamp = paths.opensbiSource / ".simrv-build-stamp";
    const auto expectedBuildStamp =
        config.arch + "|" + opensbiArch + "|" + architecture.mabi + "|" + crossCompile;
    if (fs::is_directory(output) && (!fs::is_regular_file(opensbiBuildStamp) ||
                                     readFile(opensbiBuildStamp) != expectedBuildStamp + "\n")) {
        info("OpenSBI build settings changed; clearing stale objects...");
        runMake(config, paths.opensbiSource, {"clean"}, paths.build / "opensbi-clean.log");
    }
    step("Building OpenSBI v" + config.opensbi + " payload...");
    runMake(config, paths.opensbiSource,
            {"PLATFORM=generic", "CROSS_COMPILE=" + crossCompile,
             "CC=" + crossCompile + "gcc -march=" + opensbiArch + " -mabi=" + architecture.mabi,
             "PLATFORM_RISCV_XLEN=" + architecture.xlen, "PLATFORM_RISCV_ISA=" + opensbiArch,
             "PLATFORM_RISCV_ABI=" + architecture.mabi,
             "FW_PAYLOAD_PATH=" + (paths.build / ("vmlinux_" + config.arch + ".bin")).string(),
             "FW_PAYLOAD_FDT_PATH=" + (paths.images / "devicetree.dtb").string(),
             "FW_TEXT_START=0x80000000", "FW_PAYLOAD_FDT_ADDR=0x8FF00000", "-j" + config.jobs},
            paths.build / "opensbi-fw-payload.log");
    const auto firmware = output / "platform/generic/firmware";
    const auto payload = firmware / "fw_payload.elf";
    if (!fs::is_regular_file(payload))
        throw std::runtime_error("OpenSBI did not produce fw_payload.elf");
    writeFile(opensbiBuildStamp, expectedBuildStamp + "\n");
    fs::copy_file(payload, paths.images / "opensbi-linux-payload.elf",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(paths.linuxSource / "vmlinux", paths.images / "vmlinux",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(paths.build / ("Image_" + config.arch), paths.images / "Image",
                  fs::copy_options::overwrite_existing);

    const auto dynamic = firmware / "fw_dynamic.bin";
    if (!fs::is_regular_file(dynamic)) {
        runMake(config, paths.opensbiSource,
                {"PLATFORM=generic", "CROSS_COMPILE=" + crossCompile,
                 "CC=" + crossCompile + "gcc -march=" + architecture.march +
                     " -mabi=" + architecture.mabi,
                 "PLATFORM_RISCV_XLEN=" + architecture.xlen, "FW_DYNAMIC=y",
                 "FW_TEXT_START=0x80000000", "-j" + config.jobs},
                paths.build / "opensbi-fw-dynamic.log");
    }
    if (!fs::is_regular_file(dynamic))
        throw std::runtime_error("OpenSBI did not produce fw_dynamic.bin");
    fs::copy_file(dynamic, paths.images / "opensbi-dynamic.bin",
                  fs::copy_options::overwrite_existing);
    writeSetupScript(config, paths);
}

std::string rootfsLabel(const Config& config) {
    return config.rootfs == "buildroot" || config.profile == "uefi" ? "simrv-root" : "simrv-alpine";
}

void buildRootDisk(const Config& config, const BuildPaths& paths) {
    if (config.rootfs == "buildroot") {
        return;
    }
    if (config.arch != "rv64") {
        const auto image = paths.images / "rootfs.img";
        runChecked({"truncate", "-s", "1M", image.string()});
        runChecked({"mkfs.ext4", "-L", rootfsLabel(config), "-F", image.string()});
        return;
    }

    const auto root = paths.imagesRoot / (".rootfs-work-" + config.arch + "-" + config.profile);
    fs::remove_all(root);
    fs::create_directories(root);
    const auto archive = paths.sources / ("alpine-minirootfs-" + config.alpine + "-riscv64.tar.gz");
    step("Preparing Alpine root disk...");
    runChecked({"tar", "-xf", archive.string(), "-C", root.string()});
    const auto templates = paths.source / "scripts/linux/templates/alpine";
    fs::copy(templates, root, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    for (const auto& directory : {"proc", "sys", "dev", "tmp", "run", "root"})
        fs::create_directories(root / directory);
    const auto shadow = root / "etc/shadow";
    if (fs::is_regular_file(shadow)) {
        auto contents = readFile(shadow);
        contents = std::regex_replace(contents, std::regex{R"rx((^root:)[^:]*:)rx"}, "$1:");
        writeFile(shadow, contents);
    }
    writeFile(root / "etc/hostname", "SimRV\n");
    const auto mirror = config.alpineMirror.empty()
                            ? "https://dl-cdn.alpinelinux.org/alpine/v" +
                                  config.alpine.substr(0, config.alpine.rfind('.'))
                            : config.alpineMirror;
    writeFile(root / "etc/repositories", mirror + "/main\n" + mirror + "/community\n");
    auto inittab = readFile(root / "etc/inittab");
    if (config.profile == "virtio-gpu" && inittab.find("tty2::respawn") == std::string::npos)
        inittab += "tty2::respawn:/sbin/getty -L 38400 tty2 linux\n";
    writeFile(root / "etc/inittab", inittab);
    auto qemu = envOr("SIMRV_QEMU_RISCV64", "");
    if (qemu.empty()) qemu = "/opt/riscv/linux-glibc-rv64/bin/qemu-riscv64";
    if (access(qemu.c_str(), X_OK) == 0 && access((root / "sbin/apk").c_str(), X_OK) == 0) {
        step("Bundling Alpine desktop packages with RISC-V QEMU...");
        const auto apkLog = paths.build / "alpine-apk-bundle.log";
        const auto result = runProcess({qemu,
                                        "-L",
                                        root.string(),
                                        (root / "sbin/apk").string(),
                                        "--root",
                                        root.string(),
                                        "--no-cache",
                                        "--no-scripts",
                                        "add",
                                        "xorg-server",
                                        "xinit",
                                        "jwm",
                                        "xterm",
                                        "xf86-video-fbdev",
                                        "xf86-input-evdev",
                                        "font-misc-misc",
                                        "font-dejavu",
                                        "links-graphics",
                                        "bsd-games",
                                        "gnuchess",
                                        "htop",
                                        "alpine-conf"},
                                       {}, {}, true);
        writeFile(apkLog, result.output);
        if (result.status == 0 && fs::is_regular_file(root / "usr/bin/xinit") &&
            fs::is_regular_file(root / "usr/bin/htop"))
            info("Alpine X11/JWM package files bundled.");
        else
            info(
                "Alpine desktop bundle unavailable; guest first-boot installation remains "
                "enabled. " +
                apkLog.string());
    } else {
        info(
            "RISC-V QEMU unavailable; guest first-boot Alpine package installation remains "
            "enabled.");
    }
    applyRootfsOverlay(config, root);
    const auto size = std::max<std::uintmax_t>(4ULL * 1024 * 1024 * 1024,
                                               fs::file_size(archive) * 2 + 512ULL * 1024 * 1024);
    const auto image = paths.images / "rootfs.img";
    runChecked({"truncate", "-s", std::to_string(size), image.string()});
    runChecked({"mkfs.ext4", "-L", rootfsLabel(config), "-d", root.string(), "-F", image.string()},
               {}, {}, paths.build / "rootfs-mkfs.log", config.verbose);
    runChecked({"e2fsck", "-fy", image.string()}, {}, {}, paths.build / "rootfs-e2fsck.log",
               config.verbose);
}

void packageImages(const Config& config, const BuildPaths& paths, const std::string& crossCompile) {
    const auto generatedDts = paths.build / ("virt-rv" + targetArch(config).xlen + "-" +
                                             config.rootfs + "-" + config.profile + ".dts");
    const auto dts = readFile(generatedDts);
    std::smatch bootargsMatch;
    const std::regex bootargsPattern{R"rx(bootargs\s*=\s*"([^"]*)")rx"};
    if (!std::regex_search(dts, bootargsMatch, bootargsPattern))
        throw std::runtime_error("Generated device tree has no bootargs property");
    const auto bootargs = bootargsMatch[1].str();
    const auto configHash = sha256(paths.linuxSource / ".config");
    const auto initramfsHash = sha256(paths.build / ("initramfs_" + config.arch + ".cpio"));
    std::ostringstream manifest;
    manifest << "{\n  \"architecture\": \"" << config.arch << "\",\n"
             << "  \"xlen\": " << targetArch(config).xlen << ",\n"
             << "  \"profile\": \"" << config.profile << "\",\n"
             << "  \"rootfs\": \"" << config.rootfs << "\",\n"
             << "  \"opensbi_version\": \"" << config.opensbi << "\",\n"
             << "  \"linux_version\": \"" << config.linuxVersion << "\",\n"
             << "  \"buildroot_version\": \"" << config.buildroot << "\",\n"
             << "  \"buildroot_source_sha256\": \"" << config.buildrootSha << "\",\n"
             << "  \"dram_size_mb\": " << config.dramSizeMb << ",\n"
             << "  \"rootfs_label\": \"" << rootfsLabel(config) << "\",\n"
             << "  \"bootargs\": \"" << bootargs << "\",\n"
             << "  \"kernel_config_sha256\": \"" << configHash << "\",\n"
             << "  \"initramfs_sha256\": \"" << initramfsHash << "\",\n"
             << "  \"debug_features\": "
             << (config.profile == "debug" ? "[\"debugfs\",\"kallsyms-all\",\"magic-sysrq\","
                                             "\"function-tracing\",\"function-graph-tracer\","
                                             "\"block-i/o-tracing\",\"stacktrace\",\"dwarf5\"]"
                                           : "[]")
             << ",\n  \"artifacts\": {\n";
    bool first = true;
    for (const auto* name : {"opensbi-linux-payload.elf", "opensbi-dynamic.bin", "Image",
                             "uefi-kernel.efi", "uefi-disk.img", "RISCV_VIRT_CODE.fd",
                             "RISCV_VIRT_VARS.fd", "vmlinux", "devicetree.dtb", "rootfs.img"}) {
        const auto artifact = paths.images / name;
        if (!fs::is_regular_file(artifact)) continue;
        manifest << (first ? "" : ",\n") << "    \"" << name << "\": \"" << sha256(artifact)
                 << "\"";
        first = false;
    }
    manifest << "\n  }\n}\n";
    writeFile(paths.images / "manifest.json", manifest.str());
    if (config.rootfs == "buildroot") {
        const auto output = fs::absolute(config.buildrootOutput.empty()
                                             ? paths.build / ("buildroot-output-" + config.arch)
                                             : fs::path{config.buildrootOutput});
        fs::create_directories(output / "images");
        for (const auto* name : {"opensbi-linux-payload.elf", "devicetree.dtb"})
            fs::copy_file(paths.images / name, output / "images" / name,
                          fs::copy_options::overwrite_existing);
        fs::copy_file(paths.images / "manifest.json", output / "images/simrv-manifest.json",
                      fs::copy_options::overwrite_existing);
    }
    (void)crossCompile;
}

void buildUefi(const Config& config, const BuildPaths& paths) {
    if (config.profile != "uefi") return;
    const auto output = paths.build / ("buildroot-output-uefi-rv64-" + config.buildroot);
    const auto external = paths.source / "configs/buildroot/simrv-buildroot";
    fs::create_directories(output);
    fs::copy_file(external / "rv64_defconfig", output / ".config",
                  fs::copy_options::overwrite_existing);
    const auto makeArguments = [&] {
        return std::vector<std::string>{"O=" + output.string(),
                                        "BR2_EXTERNAL=" + external.string()};
    };
    auto args = makeArguments();
    args.emplace_back("olddefconfig");
    runMake(config, paths.buildrootSource, args, paths.build / "edk2-olddefconfig.log");
    runChecked(
        {(paths.buildrootSource / "utils/config").string(), "--file", (output / ".config").string(),
         "--enable", "BR2_TARGET_EDK2", "--enable", "BR2_TARGET_EDK2_PLATFORM_OVMF_RISCV"},
        paths.buildrootSource);
    args = makeArguments();
    args.emplace_back("olddefconfig");
    runMake(config, paths.buildrootSource, args, paths.build / "edk2-enable-olddefconfig.log");
    args = makeArguments();
    args.emplace_back("edk2-extract");
    runMake(config, paths.buildrootSource, args, paths.build / "edk2-extract.log");
    fs::path edk2;
    const auto buildDirectory = output / "build";
    for (const auto& item : fs::directory_iterator(buildDirectory)) {
        if (item.is_directory() && item.path().filename().string().starts_with("edk2-edk2-")) {
            edk2 = item.path();
            break;
        }
    }
    if (edk2.empty()) throw std::runtime_error("Buildroot did not extract EDK2 sources");
    const auto makefile = paths.buildrootSource / "boot/edk2/edk2.mk";
    if (fs::is_regular_file(makefile)) {
        auto content = readFile(makefile);
        replaceAll(content, "EXTRA_OPTFLAGS=\"$(HOST_CPPFLAGS)\"",
                   "EXTRA_OPTFLAGS=\"$(HOST_CPPFLAGS) -std=gnu17 -Wno-unused-but-set-variable\"");
        writeFile(makefile, content);
    }
    const auto pccts = edk2 / "BaseTools/Source/C/VfrCompile/Pccts";
    if (fs::is_directory(pccts)) {
        for (const auto& item : fs::recursive_directory_iterator(pccts)) {
            if (!item.is_regular_file() || item.path().filename() != "makefile") continue;
            auto content = readFile(item.path());
            if (content.find("-std=gnu17") == std::string::npos) {
                const std::regex cflags{R"rx((^CFLAGS\s*=.*)$)rx", std::regex_constants::multiline};
                content = std::regex_replace(content, cflags, "$1 -std=gnu17");
                writeFile(item.path(), content);
            }
        }
    }
    args = makeArguments();
    args.emplace_back("-j" + config.jobs);
    args.emplace_back("edk2");
    runMake(config, paths.buildrootSource, args, paths.build / "edk2-build.log");
    for (const auto* name : {"RISCV_VIRT_CODE.fd", "RISCV_VIRT_VARS.fd"}) {
        const auto firmware = output / "images" / name;
        if (!fs::is_regular_file(firmware) || fs::file_size(firmware) == 0)
            throw std::runtime_error(std::string{"Buildroot did not produce "} + name);
        if (fs::file_size(firmware) > 32ULL * 1024 * 1024)
            throw std::runtime_error(std::string{name} + " exceeds the 32 MiB pflash device");
        fs::copy_file(firmware, paths.images / name, fs::copy_options::overwrite_existing);
    }
    auto toolsBin = envOr("SIMRV_TOOLS_BIN", "");
    if (toolsBin.empty()) {
        const auto installedTools = linuxBuilderExecutable.parent_path() / "simrv-tools";
        if (access(installedTools.c_str(), X_OK) == 0) toolsBin = installedTools.string();
    }
    if (toolsBin.empty()) {
        for (const auto& candidate : {paths.source / "build/rv64-release/simrv-tools",
                                      paths.source / "build/rv64-dev/simrv-tools",
                                      paths.source / "build/rv32-release/simrv-tools",
                                      paths.source / "build/rv32-dev/simrv-tools"}) {
            if (access(candidate.c_str(), X_OK) == 0) {
                toolsBin = candidate.string();
                break;
            }
        }
    }
    if (toolsBin.empty()) {
        if (const auto found = findProgram("simrv-tools")) {
            toolsBin = found->string();
        }
    }
    if (toolsBin.empty() || access(toolsBin.c_str(), X_OK) != 0)
        throw std::runtime_error(
            "simrv-tools executable is required to create the UEFI disk; set SIMRV_TOOLS_BIN");
    runChecked({toolsBin, "disk", "create", (paths.images / "uefi-disk.img").string(), "--kernel",
                (paths.images / "uefi-kernel.efi").string(), "--rootfs",
                (paths.images / "rootfs.img").string()});
}

bool hasLinuxData(const fs::path& root) {
    return fs::is_regular_file(root / "scripts/linux-versions.conf") &&
           fs::is_regular_file(root / "scripts/templates/virt-rv64.dts") &&
           fs::is_directory(root / "scripts/linux/templates/alpine") &&
           fs::is_regular_file(root / "configs/buildroot/simrv-buildroot/rv64_defconfig");
}

fs::path resolveExecutable(std::string_view argument) {
    fs::path executable{argument};
    if (executable.has_parent_path()) return fs::absolute(executable);
    if (const char* pathValue = std::getenv("PATH"); pathValue != nullptr) {
        std::stringstream paths{pathValue};
        std::string directory;
        while (std::getline(paths, directory, ':')) {
            const fs::path candidate = fs::path{directory.empty() ? "." : directory} / executable;
            std::error_code error;
            if (fs::is_regular_file(candidate, error) && !error) return fs::absolute(candidate);
        }
    }
    return fs::absolute(executable);
}

void initializeLinuxDataPaths(std::string_view argv0) {
    linuxBuilderExecutable = resolveExecutable(argv0);
    const fs::path companion =
        linuxBuilderExecutable.parent_path() / "simrv-linux-builder-post-build";
    std::error_code error;
    if (fs::is_regular_file(companion, error) && !error)
        buildrootPostBuildPath = companion.string();

    const fs::path sourceRoot = SIMRV_SOURCE_DIR;
    const fs::path installDataRoot = linuxBuilderExecutable.parent_path().parent_path() /
                                     SIMRV_INSTALL_DATADIR / "SimRV/LinuxBuilder";
    if (const char* overrideRoot = std::getenv("SIMRV_LINUX_SOURCE_DIR")) {
        linuxDataRoot = fs::absolute(overrideRoot);
        if (!hasLinuxData(linuxDataRoot))
            throw std::runtime_error("Linux builder data is incomplete at " +
                                     linuxDataRoot.string());
    } else if (const char* overrideData = std::getenv("SIMRV_LINUX_DATA_DIR")) {
        linuxDataRoot = fs::absolute(overrideData);
        if (!hasLinuxData(linuxDataRoot))
            throw std::runtime_error("Installed Linux builder data is incomplete at " +
                                     linuxDataRoot.string());
        usingInstalledLinuxData = true;
    } else if (hasLinuxData(installDataRoot)) {
        linuxDataRoot = installDataRoot;
        usingInstalledLinuxData = true;
    } else if (hasLinuxData(sourceRoot)) {
        linuxDataRoot = sourceRoot;
    } else {
        throw std::runtime_error(
            "Linux builder source data not found; reinstall SimRV tools or "
            "set SIMRV_LINUX_SOURCE_DIR");
    }

    if (const char* overrideConfig = std::getenv("SIMRV_LINUX_VERSION_FILE")) {
        linuxVersionConfig = overrideConfig;
    } else if (usingInstalledLinuxData) {
        const auto home = envOr("HOME", ".");
        const fs::path configRoot = envOr("XDG_CONFIG_HOME", home + "/.config");
        linuxVersionConfig = configRoot / "SimRV/linux-versions.conf";
        fs::create_directories(linuxVersionConfig.parent_path());
        if (!fs::exists(linuxVersionConfig))
            fs::copy_file(linuxDataRoot / "scripts/linux-versions.conf", linuxVersionConfig);
    } else {
        linuxVersionConfig = linuxDataRoot / "scripts/linux-versions.conf";
    }
}

std::filesystem::path versionsPath() { return linuxVersionConfig; }

void loadVersions(Config& config) {
    std::ifstream input{versionsPath()};
    std::string line;
    while (std::getline(input, line)) {
        const auto delimiter = line.find('=');
        if (delimiter == std::string::npos) continue;
        const auto key = line.substr(0, delimiter);
        const auto value = line.substr(delimiter + 1);
        if (key == "linux")
            config.linuxVersion = value;
        else if (key == "opensbi")
            config.opensbi = value;
        else if (key == "busybox")
            config.busybox = value;
        else if (key == "alpine")
            config.alpine = value;
        else if (key == "buildroot")
            config.buildroot = value;
        else if (key == "custom_rootfs_overlay")
            config.customRootfsOverlay = value;
        else {
            const auto separator = key.find('_');
            if (separator == std::string::npos) continue;
            const auto component = key.substr(0, separator);
            const auto setting = key.substr(separator + 1);
            Config::Source* source = component == "linux"       ? &config.linuxSource
                                     : component == "opensbi"   ? &config.opensbiSource
                                     : component == "busybox"   ? &config.busyboxSource
                                     : component == "alpine"    ? &config.alpineSource
                                     : component == "buildroot" ? &config.buildrootSource
                                                                : nullptr;
            if (source == nullptr) continue;
            if (setting == "repository")
                source->repository = value;
            else if (setting == "url")
                source->url = value;
            else if (setting == "file")
                source->file = value;
            else if (setting == "sha256")
                source->sha256 = value;
        }
    }
    if (!config.buildrootSource.sha256.empty()) config.buildrootSha = config.buildrootSource.sha256;
}

bool saveVersions(const Config& config) {
    const auto path = versionsPath();
    auto temporary = path;
    temporary += ".tmp." + std::to_string(getpid());
    {
        std::ofstream output{temporary, std::ios::trunc};
        output << "linux=" << config.linuxVersion << '\n'
               << "opensbi=" << config.opensbi << '\n'
               << "busybox=" << config.busybox << '\n'
               << "alpine=" << config.alpine << '\n'
               << "buildroot=" << config.buildroot << '\n'
               << "custom_rootfs_overlay=" << config.customRootfsOverlay << '\n';
        const auto saveSource = [&output](std::string_view name, const Config::Source& source) {
            output << name << "_repository=" << source.repository << '\n'
                   << name << "_url=" << source.url << '\n'
                   << name << "_file=" << source.file << '\n'
                   << name << "_sha256=" << source.sha256 << '\n';
        };
        saveSource("linux", config.linuxSource);
        saveSource("opensbi", config.opensbiSource);
        saveSource("busybox", config.busyboxSource);
        saveSource("alpine", config.alpineSource);
        saveSource("buildroot", config.buildrootSource);
        output.flush();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            std::cerr << "Could not write version pins to " << temporary << '\n';
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        std::cerr << "Could not replace version pins at " << path << ": " << error.message()
                  << '\n';
        return false;
    }
    return true;
}

bool versionGreater(std::string_view left, std::string_view right) {
    std::size_t li = 0;
    std::size_t ri = 0;
    while (li < left.size() || ri < right.size()) {
        while (li < left.size() && !std::isdigit(static_cast<unsigned char>(left[li]))) ++li;
        while (ri < right.size() && !std::isdigit(static_cast<unsigned char>(right[ri]))) ++ri;
        if (li == left.size() || ri == right.size()) break;
        std::uint64_t lv{};
        std::uint64_t rv{};
        while (li < left.size() && std::isdigit(static_cast<unsigned char>(left[li]))) {
            lv = lv * 10 + static_cast<unsigned>(left[li++] - '0');
        }
        while (ri < right.size() && std::isdigit(static_cast<unsigned char>(right[ri]))) {
            rv = rv * 10 + static_cast<unsigned>(right[ri++] - '0');
        }
        if (lv != rv) return lv > rv;
    }
    return left > right;
}

std::vector<std::string> fetchVersions(std::string_view component, const Config::Source& source) {
    if (source.repository.empty()) return {};
    ProcessResult page;
    if (source.repository.rfind("git+", 0) == 0 || source.repository.ends_with(".git")) {
        auto repository = source.repository;
        if (repository.rfind("git+", 0) == 0) repository.erase(0, 4);
        page = runProcess({"timeout", "20s", "git", "ls-remote", "--tags", "--refs", repository},
                          {}, {{"GIT_TERMINAL_PROMPT", "0"}}, true);
        if (page.status == 0) {
            std::string tags;
            std::istringstream lines{page.output};
            std::string line;
            while (std::getline(lines, line)) {
                const auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                auto tag = line.substr(tab + 1);
                constexpr std::string_view prefix{"refs/tags/"};
                if (tag.rfind(prefix, 0) == 0) tag.erase(0, prefix.size());
                tags += tag;
                tags.push_back('\n');
            }
            page.output = std::move(tags);
        }
    } else {
        page = runProcess({"wget", "--timeout=12", "--tries=1", "-qO-", source.repository}, {}, {},
                          true);
    }
    if (page.status != 0) return {};

    std::vector<std::string> versions;
    const auto collect = [&](const std::regex& expression, std::size_t group) {
        for (std::sregex_iterator it(page.output.begin(), page.output.end(), expression), end;
             it != end; ++it) {
            versions.push_back((*it)[group].str());
        }
    };
    const std::string content = page.output;
    if (component == "linux") {
        const std::regex release{
            R"json("moniker"\s*:\s*"(?:stable|longterm)"\s*,\s*"version"\s*:\s*"([0-9]+\.[0-9]+(?:\.[0-9]+)?)")json"};
        collect(release, 1);
    } else if (component == "opensbi") {
        const std::regex tag{R"((?:refs/tags/|/(?:tree|tag)/)v?([0-9]+\.[0-9]+(?:\.[0-9]+)?)\b)"};
        collect(tag, 1);
    } else if (component == "busybox") {
        const std::regex archive{R"(busybox-([0-9]+\.[0-9]+\.[0-9]+)\.tar\.bz2)"};
        collect(archive, 1);
        const std::regex tag{R"((?:refs/tags/|/)([0-9]+)_([0-9]+)_([0-9]+)(?:\n|\s|$))"};
        for (std::sregex_iterator it(content.begin(), content.end(), tag), end; it != end; ++it)
            versions.push_back((*it)[1].str() + "." + (*it)[2].str() + "." + (*it)[3].str());
    } else if (component == "alpine") {
        const std::regex archive{R"(alpine-minirootfs-([0-9]+\.[0-9]+\.[0-9]+)-riscv64\.tar\.gz)"};
        collect(archive, 1);
    } else if (component == "buildroot") {
        const std::regex archive{R"(buildroot-([0-9]{4}\.[0-9]{2}(?:\.[0-9]+)?)\.tar\.xz)"};
        collect(archive, 1);
    }
    std::sort(versions.begin(), versions.end(), versionGreater);
    versions.erase(std::unique(versions.begin(), versions.end()), versions.end());
    if (versions.size() > 12) versions.resize(12);
    return versions;
}

std::string ask(std::string_view title, std::string current) {
    std::cout << title << " [" << current << "]: " << std::flush;
    std::string answer;
    if (std::getline(std::cin, answer) && !answer.empty()) current = std::move(answer);
    return current;
}

bool choose(std::string_view title, const std::vector<std::string>& choices, std::string& value) {
    std::cout << "\n" << title << "\n";
    for (std::size_t i = 0; i < choices.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << choices[i];
        if (choices[i] == value) std::cout << "  [selected]";
        std::cout << '\n';
    }
    const auto answer = ask("Select number (Enter keeps current)", "");
    if (answer.empty()) return true;
    try {
        const auto selection = std::stoul(answer);
        if (selection == 0 || selection > choices.size()) {
            std::cerr << "Selection is out of range.\n";
            return false;
        }
        value = choices[selection - 1];
    } catch (...) {
        std::cerr << "Enter a number from the list.\n";
        return false;
    }
    return true;
}

void pickVersion(std::string_view component, std::string_view label, std::string& current,
                 const Config::Source& source) {
    std::cout << "\n" << label << " version from " << source.repository << "\n";
    auto choices = fetchVersions(component, source);
    if (choices.empty()) {
        std::cout << "  Repository unavailable or no releases matched; keeping the pin " << current
                  << ".\n";
        current = ask("Custom version (Enter keeps the pin)", current);
    } else {
        if (std::find(choices.begin(), choices.end(), current) == choices.end())
            choices.push_back(current);
        std::sort(choices.begin(), choices.end(), versionGreater);
        for (std::size_t i = 0; i < choices.size(); ++i) {
            std::cout << "  " << (i + 1) << ") " << choices[i];
            if (choices[i] == current) std::cout << "  [pinned]";
            std::cout << '\n';
        }
        std::cout << "  c) enter a custom version\n";
        const auto answer = ask("Choose version (Enter keeps current)", "");
        if (answer == "c" || answer == "C")
            current = ask("Custom version", current);
        else if (!answer.empty()) {
            try {
                const auto selection = std::stoul(answer);
                if (selection == 0 || selection > choices.size())
                    throw std::out_of_range("version choice");
                current = choices[selection - 1];
            } catch (...) {
                std::cout << "Invalid version selection; keeping " << current << ".\n";
            }
        }
    }
}

void editSource(std::string_view label, Config::Source& source) {
    std::cout << "\nCustom source for " << label << " (blank leaves a value unchanged):\n";
    source.repository = ask("  Version catalog URL", source.repository);
    const auto editOptional = [](std::string_view prompt, std::string& value) {
        auto answer = ask(prompt, value);
        if (answer == "-") answer.clear();
        value = std::move(answer);
    };
    editOptional("  Archive URL (type - to clear)", source.url);
    editOptional("  Local archive file (type - to clear)", source.file);
    editOptional("  Archive SHA-256 (type - to clear)", source.sha256);
}

bool configure(Config& config) {
    std::cout << "SimRV Linux image builder\n"
              << "Configure source versions and image profile. Leave a field blank to keep its "
                 "current value.\n";
    if (!choose("Target architecture", {"rv32", "rv64"}, config.arch)) return false;
    if (!choose("C library", {"auto", "musl", "glibc"}, config.libc)) return false;
    if (!choose("Root filesystem", {"alpine", "buildroot"}, config.rootfs)) return false;
    if (config.rootfs == "buildroot") config.profile = "buildroot";
    const std::vector<std::string> profiles =
        config.rootfs == "buildroot"
            ? std::vector<std::string>{"buildroot"}
            : std::vector<std::string>{"gui", "debug", "uefi", "virtio-gpu"};
    if (!choose("Linux profile", profiles, config.profile)) return false;

    pickVersion("linux", "Linux", config.linuxVersion, config.linuxSource);
    pickVersion("opensbi", "OpenSBI", config.opensbi, config.opensbiSource);
    if (config.arch == "rv32" || config.rootfs == "buildroot")
        pickVersion("busybox", "BusyBox", config.busybox, config.busyboxSource);
    if (config.rootfs == "alpine" && config.arch == "rv64")
        pickVersion("alpine", "Alpine minirootfs", config.alpine, config.alpineSource);
    if (config.rootfs == "buildroot" || config.profile == "uefi") {
        const auto previous = config.buildroot;
        pickVersion("buildroot", "Buildroot", config.buildroot, config.buildrootSource);
        if (previous != config.buildroot) {
            config.buildrootSha.clear();
            config.buildrootSource.sha256.clear();
        }
        if (!config.buildrootSource.sha256.empty())
            config.buildrootSha = config.buildrootSource.sha256;
        config.buildrootSha = ask("  Buildroot archive SHA-256", config.buildrootSha);
    }
    config.jobs = ask("Parallel build jobs", config.jobs);
    const auto customizeSources =
        ask("Edit catalog URLs, custom archive links or local files? [y/N]", "n");
    if (customizeSources == "y" || customizeSources == "Y") {
        editSource("Linux", config.linuxSource);
        editSource("OpenSBI", config.opensbiSource);
        if (config.arch == "rv32" || config.rootfs == "buildroot")
            editSource("BusyBox", config.busyboxSource);
        if (config.rootfs == "alpine" && config.arch == "rv64")
            editSource("Alpine", config.alpineSource);
        if (config.rootfs == "buildroot" || config.profile == "uefi")
            editSource("Buildroot", config.buildrootSource);
    }
    if (!config.buildrootSource.sha256.empty()) config.buildrootSha = config.buildrootSource.sha256;
    config.customRootfsOverlay =
        ask("Custom rootfs overlay directory (type - to clear)", config.customRootfsOverlay);
    if (config.customRootfsOverlay == "-") config.customRootfsOverlay.clear();
    std::cout << "\nPlan: " << config.arch << ", " << config.rootfs << '/' << config.profile
              << ", Linux " << config.linuxVersion << ", OpenSBI " << config.opensbi << '\n';
    const auto confirmation = ask("Start build? [y/N]", "n");
    if (confirmation != "y" && confirmation != "Y") return false;
    const auto save = ask("Save these versions as the new pins? [y/N]", "n");
    if (save == "y" || save == "Y") {
        if (config.buildrootSource.sha256.empty())
            config.buildrootSource.sha256 = config.buildrootSha;
        if (!saveVersions(config)) return false;
    }
    return true;
}

void printHelp(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "  --tui                         Configure interactively and pick upstream versions\n"
        << "  --arch rv32|rv64              Target architecture\n"
        << "  --libc auto|musl|glibc        Linux cross compiler preference\n"
        << "  --rootfs alpine|buildroot     Root filesystem backend\n"
        << "  --profile gui|buildroot|debug|uefi|virtio-gpu\n"
        << "  --linux-version VERSION       Linux kernel release\n"
        << "  --opensbi-version VERSION     OpenSBI release\n"
        << "  --busybox-version VERSION     BusyBox release\n"
        << "  --alpine-version VERSION      Alpine minirootfs release\n"
        << "  --buildroot-version VERSION   Buildroot release\n"
        << "  --buildroot-sha256 HASH       Required archive digest when using Buildroot\n"
        << "  --cross-compile PREFIX        RISC-V Linux toolchain prefix\n"
        << "  --dram-size-mb SIZE           Guest DRAM size for the generated device tree\n"
        << "  --alpine-mirror URL           Alpine repository mirror\n"
        << "  --rootfs-overlay DIR          Add guest files and symlinks from a directory\n"
        << "  --check-updates               List current and newest catalog versions\n"
        << "  --update-versions             Save newest catalog versions, then build\n"
        << "  --prune-old-sources           With --update-versions, remove stale source caches\n"
        << "  --yes                         Accept source-cache cleanup without prompting\n"
        << "  --clean-build | --clean       Remove intermediates, or intermediates and images\n"
        << "  --clean-old-kernels           Remove stale extracted Linux kernel trees\n"
        << "  --reuse-buildroot             Keep the existing Buildroot output directory\n"
        << "  --rebuild-kernel              Force a kernel rebuild\n"
        << "  --reuse-kernel                Fail if the selected kernel cannot be reused\n"
        << "  --verbose                     Show full build output\n"
        << "  --help                        Show this help\n\n"
        << "Edit scripts/linux-versions.conf for version pins, repository catalogs, archive URLs, "
           "local files, and checksums.\n";
}

void clean(const Config& config, const BuildPaths& paths) {
    if (config.cleanAction == "all") {
        fs::remove_all(paths.build);
        fs::remove_all(paths.imagesRoot);
        info("Removed Linux build cache and generated images.");
    } else if (config.cleanAction == "build") {
        fs::remove_all(paths.build);
        info("Removed Linux build cache; generated images were kept.");
    } else if (config.cleanAction == "old-kernels") {
        if (!fs::is_directory(paths.build)) return;
        for (const auto& item : fs::directory_iterator(paths.build)) {
            if (item.is_directory() && item.path().filename().string().starts_with("linux-") &&
                item.path() != paths.linuxSource)
                fs::remove_all(item.path());
        }
        info("Removed extracted Linux source trees other than the selected version.");
    }
}

void pruneOldSources(const Config& config, const BuildPaths& paths) {
    if (!config.pruneOldSources || !fs::is_directory(paths.sources)) return;
    if (!config.assumeYes) {
        const auto answer = ask("Remove cached source archives for other versions? [y/N]", "n");
        if (answer != "y" && answer != "Y") return;
    }
    const std::array keep{"linux-" + config.linuxVersion, "opensbi-" + config.opensbi,
                          "busybox-" + config.busybox, "alpine-minirootfs-" + config.alpine,
                          "buildroot-" + config.buildroot};
    for (const auto& item : fs::directory_iterator(paths.sources)) {
        const auto name = item.path().filename().string();
        if (name.ends_with(".source")) continue;
        const auto prefix = std::find_if(
            keep.begin(), keep.end(), [&](const auto& value) { return name.rfind(value, 0) == 0; });
        if (prefix == keep.end()) fs::remove(item.path());
    }
    if (!fs::is_directory(paths.build)) return;
    for (const auto& item : fs::directory_iterator(paths.build)) {
        if (!item.is_directory()) continue;
        const auto name = item.path().filename().string();
        if (name.starts_with("linux-") && item.path() != paths.linuxSource)
            fs::remove_all(item.path());
        else if (name.starts_with("opensbi-") && item.path() != paths.opensbiSource)
            fs::remove_all(item.path());
        else if (name.starts_with("busybox-") && item.path() != paths.busyboxSource)
            fs::remove_all(item.path());
        else if (name.starts_with("buildroot-") && item.path() != paths.buildrootSource &&
                 name.find("output") == std::string::npos)
            fs::remove_all(item.path());
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        initializeLinuxDataPaths(argc > 0 ? argv[0] : "simrv-linux-builder");
    } catch (const std::exception& error) {
        std::cerr << "Linux builder: " << error.what() << '\n';
        return 1;
    }
    Config config;
    loadVersions(config);
    config.linuxVersion = envOr("LINUX_VER", config.linuxVersion);
    config.jobs = envOr("SIMRV_LINUX_JOBS", config.jobs);
    config.dramSizeMb = envOr("SIMRV_LINUX_DRAM_SIZE_MB", config.dramSizeMb);
    config.alpineMirror = envOr("SIMRV_ALPINE_MIRROR", "");
    config.buildrootOutput = envOr("SIMRV_LINUX_BUILDROOT_OUTPUT", "");
    config.reuseBuildroot = envOr("SIMRV_LINUX_REUSE_BUILDROOT", "0") == "1";
    bool tui = false;
    bool help = false;
    bool profileSet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--tui") {
            tui = true;
            continue;
        }
        if ((arg == "--arch" || arg == "--libc" || arg == "--rootfs" || arg == "--profile" ||
             arg == "--linux-version" || arg == "--opensbi-version" || arg == "--busybox-version" ||
             arg == "--alpine-version" || arg == "--buildroot-version" ||
             arg == "--buildroot-sha256" || arg == "--cross-compile" || arg == "--dram-size-mb" ||
             arg == "--alpine-mirror" || arg == "--rootfs-overlay") &&
            i + 1 < argc) {
            const std::string value{argv[++i]};
            if (arg == "--arch")
                config.arch = value;
            else if (arg == "--libc")
                config.libc = value;
            else if (arg == "--rootfs")
                config.rootfs = value;
            else if (arg == "--profile") {
                config.profile = value;
                profileSet = true;
            } else if (arg == "--linux-version")
                config.linuxVersion = value;
            else if (arg == "--opensbi-version")
                config.opensbi = value;
            else if (arg == "--busybox-version")
                config.busybox = value;
            else if (arg == "--alpine-version")
                config.alpine = value;
            else if (arg == "--buildroot-version")
                config.buildroot = value;
            else if (arg == "--buildroot-sha256")
                config.buildrootSha = value;
            else if (arg == "--cross-compile")
                config.crossCompile = value;
            else if (arg == "--dram-size-mb")
                config.dramSizeMb = value;
            else if (arg == "--alpine-mirror")
                config.alpineMirror = value;
            else if (arg == "--rootfs-overlay")
                config.customRootfsOverlay = value;
        } else {
            if (arg == "--help" || arg == "-h") help = true;
            if (arg == "--verbose") config.verbose = true;
            if (arg == "--rebuild-kernel") config.rebuildKernel = true;
            if (arg == "--reuse-kernel") config.requireKernelReuse = true;
            if (arg == "--check-updates") config.checkUpdates = true;
            if (arg == "--update-versions") config.updateVersions = true;
            if (arg == "--prune-old-sources") config.pruneOldSources = true;
            if (arg == "--yes") config.assumeYes = true;
            if (arg == "--clean") config.cleanAction = "all";
            if (arg == "--clean-build") config.cleanAction = "build";
            if (arg == "--clean-old-kernels") config.cleanAction = "old-kernels";
            if (arg == "--reuse-buildroot") config.reuseBuildroot = true;
            if (arg == "--help" || arg == "-h") continue;
            if (arg == "--verbose" || arg == "--rebuild-kernel" || arg == "--reuse-kernel" ||
                arg == "--check-updates" || arg == "--update-versions" ||
                arg == "--prune-old-sources" || arg == "--yes" || arg == "--clean" ||
                arg == "--clean-build" || arg == "--clean-old-kernels" ||
                arg == "--reuse-buildroot")
                continue;
            std::cerr << "Unknown option: " << arg << '\n';
            return 2;
        }
    }
    if (help) {
        printHelp(argv[0]);
        return 0;
    }
    if (!profileSet && config.rootfs == "buildroot") {
        config.profile = "buildroot";
    }
    if (tui) {
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            std::cerr << "--tui requires an interactive terminal.\n";
            return 2;
        }
        if (!configure(config)) return 0;
        if (!config.jobs.empty()) setenv("SIMRV_LINUX_JOBS", config.jobs.c_str(), 1);
    }
    try {
        const auto paths = buildPaths(config);
        if (!config.cleanAction.empty()) {
            clean(config, paths);
            return 0;
        }
        if (config.checkUpdates || config.updateVersions) {
            bool changed = false;
            const auto update = [&](std::string_view component, std::string_view label,
                                    std::string& current, const Config::Source& source) {
                auto choices = fetchVersions(component, source);
                if (choices.empty()) {
                    std::cerr << label << ": repository catalog is unavailable\n";
                    return;
                }
                std::sort(choices.begin(), choices.end(), versionGreater);
                std::cout << label << ": pinned " << current << ", newest " << choices.front()
                          << '\n';
                if (config.updateVersions && current != choices.front()) {
                    current = choices.front();
                    changed = true;
                }
            };
            update("linux", "Linux", config.linuxVersion, config.linuxSource);
            update("opensbi", "OpenSBI", config.opensbi, config.opensbiSource);
            if (config.arch == "rv32" || config.rootfs == "buildroot")
                update("busybox", "BusyBox", config.busybox, config.busyboxSource);
            if (config.rootfs == "alpine" && config.arch == "rv64")
                update("alpine", "Alpine", config.alpine, config.alpineSource);
            if (config.rootfs == "buildroot" || config.profile == "uefi")
                update("buildroot", "Buildroot", config.buildroot, config.buildrootSource);
            if (!config.updateVersions) return 0;
            if (changed && config.buildroot != "2026.08.1" &&
                config.buildrootSource.sha256 ==
                    "34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b")
                throw std::runtime_error(
                    "Update Buildroot SHA-256 in the editable version file before building");
            if (!saveVersions(config)) throw std::runtime_error("Could not save updated versions");
            pruneOldSources(config, paths);
        }
        if (config.pruneOldSources && !config.updateVersions)
            throw std::runtime_error("--prune-old-sources requires --update-versions");
        if (!help) {
            validateConfig(config);
            if (config.requireKernelReuse && config.rebuildKernel)
                throw std::runtime_error("--reuse-kernel cannot be combined with --rebuild-kernel");
            if ((config.rootfs == "buildroot" || config.profile == "uefi") &&
                runProcess({"perl", "-MExtUtils::MakeMaker", "-e", "1"}, {}, {}, true).status != 0)
                throw std::runtime_error("Buildroot requires Perl ExtUtils::MakeMaker");
            config.crossCompile = detectCrossCompiler(config);
            info("Selected " + config.arch + " cross compiler " + config.crossCompile);
            prepareSources(config, paths);
            if (config.rootfs == "buildroot") buildBuildrootRootfs(config, paths);
            buildInitramfs(config, paths, config.crossCompile);
            buildKernel(config, paths, config.crossCompile);
            buildOpenSbi(config, paths, config.crossCompile);
            buildRootDisk(config, paths);
            buildUefi(config, paths);
            packageImages(config, paths, config.crossCompile);
            step("Linux image build complete");
            info("Images: " + paths.images.string());
            info("Manifest: " + (paths.images / "manifest.json").string());
        }
    } catch (const std::exception& error) {
        std::cerr << "Linux builder: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
