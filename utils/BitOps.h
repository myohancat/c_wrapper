/**
 * Simple utilities for embedded systems.
 *
 * Note: This code was written while learning modern C++
 *       and may contain design or implementation issues.
 *
 * author: Kyungin Kim <myohancat@naver.com>
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace BitOps
{

    /**
     * Usage:
     *    BitOps::mask<4, uint32_t>() -> 0xF
     */
    template <uint32_t width, typename T>
    constexpr T mask()
    {
        static_assert(std::is_unsigned_v<T>, "T must be an unsigned type");

        return width >= sizeof(T) * 8
            ? static_cast<T>(~static_cast<T>(0))
            : (static_cast<T>(1) << width) - 1;
    }

    /**
     * Usage:
     *    BitOps::mask_dynamic<uint32_t>(4) -> 0xF
     */
    template <typename T>
    constexpr T mask_dynamic(uint32_t width)
    {
        static_assert(std::is_unsigned_v<T>, "T must be an unsigned type");

        return width >= sizeof(T) * 8
            ? static_cast<T>(~static_cast<T>(0))
            : (static_cast<T>(1) << width) - 1;
    }

    /**
     * Offset starts from the least significant bit (LSB).
     *
     * Usage:
     *    uint32_t value = 0xABCDu;
     *    BitOps::extract<0, 4>(value)  ->  0xD
     *    BitOps::extract<4, 4>(value)  ->  0xC
     *    BitOps::extract<8, 4>(value)  ->  0xB
     *    BitOps::extract<12, 4>(value) ->  0xA
     */
    template <uint32_t offset, uint32_t width, typename T>
    constexpr T extract(T data)
    {
        static_assert(std::is_unsigned_v<T>, "T must be an unsigned type");
        static_assert(offset < sizeof(T) * 8, "offset is out of range");

        return (data >> offset) & mask<width, T>();
    }

    /**
     * Offset starts from the least significant bit (LSB).
     *
     * Usage:
     *    uint32_t value = 0xABCDu;
     *    BitOps::extract_dynamic(value, 0, 4)  ->  0xD
     *    BitOps::extract_dynamic(value, 4, 4)  ->  0xC
     *    BitOps::extract_dynamic(value, 8, 4)  ->  0xB
     *    BitOps::extract_dynamic(value, 12, 4) ->  0xA
     */
    template <typename T>
    constexpr T extract_dynamic(T data, uint32_t offset, uint32_t width)
    {
        static_assert(std::is_unsigned_v<T>, "T must be an unsigned type");

        return offset >= sizeof(T) * 8
            ? static_cast<T>(0)
            : (data >> offset) & mask_dynamic<T>(width);
    }

} // namespace BitOps
