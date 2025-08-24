// include/utils/narrow.hpp
#pragma once
#include <type_traits>
#include <limits>

namespace util {

    template <class To, class From>
    constexpr To narrow_clamp(From v) noexcept {
        static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<To>,
            "narrow_clamp requires arithmetic types");
        long double x = static_cast<long double>(v);
        const long double lo = static_cast<long double>(std::numeric_limits<To>::lowest());
        const long double hi = static_cast<long double>(std::numeric_limits<To>::max());
        if (x < lo) return std::numeric_limits<To>::lowest();
        if (x > hi) return std::numeric_limits<To>::max();
        return static_cast<To>(v);
    }

} // namespace util
