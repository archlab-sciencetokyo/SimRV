/**
 * @file GuestCapabilityCatalog.hpp
 * @brief Shared guest-visible optional ISA and cache block capabilities.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "simrv/cache/DCache.hpp"

namespace simrv::core {
inline constexpr uint32_t kGuestCacheBlockBytes = simrv::cache::DCache::kLineBytes;

inline constexpr std::array<std::string_view, 13> kBaseGuestExtensions{
    "i", "m", "a", "f", "d", "c", "b", "v", "zicntr", "zihpm", "zicsr", "zifencei", "zihintpause"};

inline constexpr std::array<std::string_view, 32> kPatchableGuestExtensions{
    "zihintntl", "zkt",     "zvbb",    "zvkt",    "zimop",   "zic64b",       "zicclsm", "za64rs",
    "ziccamoa",  "zfhmin",  "zvfhmin", "zfa",     "zicond",  "zicboz",       "zicbom",  "zicbop",
    "zawrs",     "svinval", "svpbmt",  "svnapot", "sstc",    "sscofpmf",     "ziccrse", "ziccif",
    "ssu64xl",   "ssnpm",   "supm",    "svade",   "ssccptr", "sscounterenw", "sstvala", "sstvecd"};
}  // namespace simrv::core
