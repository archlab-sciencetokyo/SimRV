#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace simrv::pipeline {

/// Dense compile-time decoder table with bounds and collision checking.
template <typename Value, std::size_t Rows, std::size_t Columns, Value Empty>
class DecodeTable {
   public:
    consteval DecodeTable() {
        for (auto& row : cells_) {
            row.fill(Empty);
        }
    }

    consteval void assign(std::size_t row, std::size_t column, Value value) {
        if (row >= Rows || column >= Columns) {
            throw "decode table index is out of bounds";
        }
        if (value == Empty) {
            throw "decode table cannot assign its empty sentinel";
        }
        if (cells_[row][column] != Empty) {
            throw "duplicate decode table encoding";
        }
        cells_[row][column] = value;
    }

    consteval void assign_row(std::size_t row, Value value) {
        for (std::size_t column = 0; column < Columns; ++column) {
            assign(row, column, value);
        }
    }

    consteval void assign_row(std::size_t row,
                              std::initializer_list<std::pair<std::size_t, Value>> assignments) {
        for (const auto& [column, value] : assignments) {
            assign(row, column, value);
        }
    }

    [[nodiscard]] constexpr auto lookup(std::size_t row, std::size_t column) const noexcept
        -> Value {
        return row < Rows && column < Columns ? cells_[row][column] : Empty;
    }

   private:
    std::array<std::array<Value, Columns>, Rows> cells_{};
};

}  // namespace simrv::pipeline
