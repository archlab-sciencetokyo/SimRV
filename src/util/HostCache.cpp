/**
 * @file HostCache.cpp
 * @brief Host microarchitecture and hardware cache topology discovery implementation.
 */
#include "simrv/util/HostCache.hpp"

#include <unistd.h>

#include <fstream>
#include <string>

namespace simrv::util {

namespace {

auto parse_sysfs_size(const std::string& path) noexcept -> size_t {
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    std::string line;
    if (!(file >> line) || line.empty()) return 0;
    size_t multiplier = 1;
    char unit = line.back();
    if (unit == 'K' || unit == 'k') {
        multiplier = 1024;
        line.pop_back();
    } else if (unit == 'M' || unit == 'm') {
        multiplier = 1024 * 1024;
        line.pop_back();
    } else if (unit == 'G' || unit == 'g') {
        multiplier = 1024 * 1024 * 1024;
        line.pop_back();
    }
    try {
        return std::stoull(line) * multiplier;
    } catch (...) {
        return 0;
    }
}

}  // namespace

auto detect_host_cache_topology() noexcept -> HostCacheTopology {
    HostCacheTopology topo{};
    bool sysconf_ok = false;

#if defined(_SC_LEVEL1_DCACHE_SIZE)
    const long l1d = sysconf(_SC_LEVEL1_DCACHE_SIZE);
    if (l1d > 0) {
        topo.l1d_bytes = static_cast<size_t>(l1d);
        sysconf_ok = true;
    }
#endif

#if defined(_SC_LEVEL1_ICACHE_SIZE)
    const long l1i = sysconf(_SC_LEVEL1_ICACHE_SIZE);
    if (l1i > 0) {
        topo.l1i_bytes = static_cast<size_t>(l1i);
        sysconf_ok = true;
    }
#endif

#if defined(_SC_LEVEL1_DCACHE_LINESIZE)
    const long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (line > 0) {
        topo.line_bytes = static_cast<size_t>(line);
        sysconf_ok = true;
    }
#endif

#if defined(_SC_LEVEL2_CACHE_SIZE)
    const long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
    if (l2 > 0) {
        topo.l2_bytes = static_cast<size_t>(l2);
        sysconf_ok = true;
    }
#endif

#if defined(_SC_LEVEL3_CACHE_SIZE)
    const long l3 = sysconf(_SC_LEVEL3_CACHE_SIZE);
    if (l3 > 0) {
        topo.l3_bytes = static_cast<size_t>(l3);
        sysconf_ok = true;
    }
#endif

    // Fallback or supplementary inspection via Linux sysfs
    if (!sysconf_ok) {
        const size_t sys_l1d = parse_sysfs_size("/sys/devices/system/cpu/cpu0/cache/index0/size");
        if (sys_l1d > 0) topo.l1d_bytes = sys_l1d;
        const size_t sys_l1i = parse_sysfs_size("/sys/devices/system/cpu/cpu0/cache/index1/size");
        if (sys_l1i > 0) topo.l1i_bytes = sys_l1i;
        const size_t sys_l2 = parse_sysfs_size("/sys/devices/system/cpu/cpu0/cache/index2/size");
        if (sys_l2 > 0) topo.l2_bytes = sys_l2;
        const size_t sys_l3 = parse_sysfs_size("/sys/devices/system/cpu/cpu0/cache/index3/size");
        if (sys_l3 > 0) topo.l3_bytes = sys_l3;
    }

    topo.detected = (topo.l1d_bytes > 0 && topo.line_bytes > 0);
    return topo;
}

}  // namespace simrv::util
