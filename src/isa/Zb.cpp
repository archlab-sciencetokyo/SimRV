/** @file Zb.cpp */
#include "simrv/isa/Zb.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace simrv::isa {

namespace {

auto normalized(std::string_view name) -> std::string {
    std::string result;
    result.reserve(name.size());
    for (const char ch : name) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

auto add_unique(std::vector<std::string>& extensions, std::string name) -> void {
    if (name.empty()) return;
    if (std::ranges::find(extensions, name) == extensions.end()) {
        extensions.push_back(std::move(name));
    }
}

}  // namespace

auto is_zb_extension(std::string_view name) -> bool {
    const auto value = normalized(name);
    return value == "zb" || value == "zba" || value == "zbb" || value == "zbc" || value == "zbs" ||
           value == "zbkb" || value == "zbkc" || value == "zbkx";
}

void add_zb_extension(std::vector<std::string>& extensions, std::string_view name) {
    const auto value = normalized(name);
    if (value == "zb") {
        add_zb_extension(extensions, "zba");
        add_zb_extension(extensions, "zbb");
        add_zb_extension(extensions, "zbc");
        add_zb_extension(extensions, "zbs");
        return;
    }
    if (is_zb_extension(value)) add_unique(extensions, value);
}

}  // namespace simrv::isa
