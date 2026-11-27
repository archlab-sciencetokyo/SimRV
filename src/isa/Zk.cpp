/** @file Zk.cpp */
#include "simrv/isa/Zk.hpp"

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

auto is_zk_extension(std::string_view name) -> bool {
    const auto value = normalized(name);
    return value == "zk" || value == "zkn" || value == "zkr" || value == "zks" || value == "zkt" ||
           value == "zknd" || value == "zkne" || value == "zknh" || value == "zksed" ||
           value == "zksh";
}

void add_zk_extension(std::vector<std::string>& extensions, std::string_view name) {
    const auto value = normalized(name);
    if (value == "zk") {
        add_zk_extension(extensions, "zkn");
        add_zk_extension(extensions, "zkr");
        add_zk_extension(extensions, "zkt");
        return;
    }
    if (is_zk_extension(value)) add_unique(extensions, value);
}

auto is_zkn_operation(OperationId op_id) noexcept -> bool { return is_zb_crypto_operation(op_id); }

}  // namespace simrv::isa
