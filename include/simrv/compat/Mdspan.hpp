/**
 * @file Mdspan.hpp
 * @brief Small two-dimensional mdspan compatibility view for older standard libraries.
 *
 * GCC versions that predate C++23's <mdspan> header can still build the cache model. The
 * cache only needs a contiguous two-dimensional view, so keep this fallback deliberately
 * narrow and switch to the standard implementation whenever it is available.
 */
#pragma once

#include <cstddef>

namespace simrv::compat {

template <typename Element>
class Mdspan2D {
   public:
    using element_type = Element;
    using value_type = Element;
    using index_type = std::size_t;
    using size_type = std::size_t;

    constexpr Mdspan2D() noexcept = default;

    constexpr Mdspan2D(Element* data, index_type rows, index_type columns) noexcept
        : data_(data), rows_(rows), columns_(columns) {}

    [[nodiscard]] constexpr auto data_handle() const noexcept -> Element* { return data_; }
    [[nodiscard]] constexpr auto extent(index_type rank) const noexcept -> index_type {
        return rank == 0 ? rows_ : columns_;
    }
    [[nodiscard]] constexpr auto size() const noexcept -> index_type { return rows_ * columns_; }

    [[nodiscard]] constexpr auto operator()(index_type row, index_type column) noexcept
        -> Element& {
        return data_[row * columns_ + column];
    }
    [[nodiscard]] constexpr auto operator()(index_type row, index_type column) const noexcept
        -> const Element& {
        return data_[row * columns_ + column];
    }

   private:
    Element* data_ = nullptr;
    index_type rows_ = 0;
    index_type columns_ = 0;
};

}  // namespace simrv::compat
