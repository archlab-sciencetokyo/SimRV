/**
 * @file SmallFlatMap.hpp
 * @brief Fixed-capacity, zero-allocation flat map and set containers for microarchitectural state.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace simrv::util {

template <typename Key, typename Value, size_t Capacity = 32>
class SmallFlatMap {
   public:
    struct Entry {
        Key first{};
        Value second{};
    };
    using value_type = Entry;
    using iterator = Entry*;
    using const_iterator = const Entry*;

    constexpr SmallFlatMap() noexcept = default;

    [[nodiscard]] constexpr auto begin() noexcept -> iterator { return data_.data(); }
    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return data_.data(); }
    [[nodiscard]] constexpr auto end() noexcept -> iterator { return data_.data() + size_; }
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
        return data_.data() + size_;
    }

    [[nodiscard]] constexpr auto size() const noexcept -> size_t { return size_; }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] static constexpr auto capacity() noexcept -> size_t { return Capacity; }

    constexpr void clear() noexcept { size_ = 0; }

    [[nodiscard]] constexpr auto find(const Key& key) noexcept -> iterator {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i].first == key) return &data_[i];
        }
        return end();
    }

    [[nodiscard]] constexpr auto find(const Key& key) const noexcept -> const_iterator {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i].first == key) return &data_[i];
        }
        return end();
    }

    [[nodiscard]] constexpr auto contains(const Key& key) const noexcept -> bool {
        return find(key) != end();
    }

    constexpr auto emplace(const Key& key, const Value& val) -> std::pair<iterator, bool> {
        auto it = find(key);
        if (it != end()) {
            return {it, false};
        }
        if (size_ < Capacity) {
            auto* slot = &data_[size_++];
            slot->first = key;
            slot->second = val;
            return {slot, true};
        }
        return {end(), false};
    }

    constexpr auto try_emplace(const Key& key) -> std::pair<iterator, bool> {
        auto it = find(key);
        if (it != end()) {
            return {it, false};
        }
        if (size_ < Capacity) {
            auto* slot = &data_[size_++];
            slot->first = key;
            slot->second = Value{};
            return {slot, true};
        }
        return {end(), false};
    }

    constexpr auto erase(const_iterator it) noexcept -> iterator {
        if (it < begin() || it >= end()) return end();
        const auto idx = static_cast<size_t>(it - begin());
        std::move(begin() + idx + 1, end(), begin() + idx);
        --size_;
        return begin() + idx;
    }

    constexpr auto erase(const Key& key) noexcept -> size_t {
        auto it = find(key);
        if (it != end()) {
            erase(it);
            return 1;
        }
        return 0;
    }

   private:
    std::array<value_type, Capacity> data_{};
    size_t size_ = 0;
};

template <typename Key, size_t Capacity = 32>
class SmallFlatSet {
   public:
    using iterator = Key*;
    using const_iterator = const Key*;

    constexpr SmallFlatSet() noexcept = default;

    [[nodiscard]] constexpr auto begin() noexcept -> iterator { return data_.data(); }
    [[nodiscard]] constexpr auto begin() const noexcept -> const_iterator { return data_.data(); }
    [[nodiscard]] constexpr auto end() noexcept -> iterator { return data_.data() + size_; }
    [[nodiscard]] constexpr auto end() const noexcept -> const_iterator {
        return data_.data() + size_;
    }

    [[nodiscard]] constexpr auto size() const noexcept -> size_t { return size_; }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] static constexpr auto capacity() noexcept -> size_t { return Capacity; }

    constexpr void clear() noexcept { size_ = 0; }

    [[nodiscard]] constexpr auto find(const Key& key) noexcept -> iterator {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == key) return &data_[i];
        }
        return end();
    }

    [[nodiscard]] constexpr auto find(const Key& key) const noexcept -> const_iterator {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == key) return &data_[i];
        }
        return end();
    }

    [[nodiscard]] constexpr auto contains(const Key& key) const noexcept -> bool {
        return find(key) != end();
    }

    constexpr auto insert(const Key& key) -> std::pair<iterator, bool> {
        auto it = find(key);
        if (it != end()) {
            return {it, false};
        }
        if (size_ < Capacity) {
            auto* slot = &data_[size_++];
            *slot = key;
            return {slot, true};
        }
        return {end(), false};
    }

    constexpr auto erase(iterator it) noexcept -> iterator {
        if (it < begin() || it >= end()) return end();
        const auto idx = static_cast<size_t>(it - begin());
        std::move(begin() + idx + 1, end(), begin() + idx);
        --size_;
        return begin() + idx;
    }

    constexpr auto erase(const Key& key) noexcept -> size_t {
        auto it = find(key);
        if (it != end()) {
            erase(it);
            return 1;
        }
        return 0;
    }

   private:
    std::array<Key, Capacity> data_{};
    size_t size_ = 0;
};

}  // namespace simrv::util
