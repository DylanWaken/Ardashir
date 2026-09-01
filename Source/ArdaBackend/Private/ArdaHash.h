/** @file ArdaHash.h
 * Private hashing primitives shared by backend subsystems.
 */

#pragma once

#include <EASTL/string.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>

namespace arda::private_api
{
    /** Canonical 64-bit FNV-1a offset basis. */
    inline constexpr uint64_t ArdaFnv1a64OffsetBasis =
        14695981039346656037ull;
    /** Canonical 64-bit FNV-1a prime. */
    inline constexpr uint64_t ArdaFnv1a64Prime = 1099511628211ull;

    /** Appends an exact byte sequence to an FNV-1a hash. */
    inline void AppendFnv1a64(
        uint64_t& Hash,
        const void* Data,
        size_t Size) noexcept
    {
        const auto* Bytes = static_cast<const uint8_t*>(Data);
        for (size_t Index = 0; Index < Size; ++Index)
        {
            Hash ^= Bytes[Index];
            Hash *= ArdaFnv1a64Prime;
        }
    }

    /** Appends the requested low bytes of an integer in stable little-endian order. */
    inline void AppendFnv1a64LittleEndian(
        uint64_t& Hash,
        uint64_t Value,
        uint32_t ByteCount = sizeof(uint64_t)) noexcept
    {
        for (uint32_t Index = 0; Index < ByteCount; ++Index)
        {
            const uint8_t Byte = static_cast<uint8_t>(Value >> (Index * 8));
            AppendFnv1a64(Hash, &Byte, sizeof(Byte));
        }
    }

    /** Prevents zero from being confused with an absent persistent hash. */
    [[nodiscard]] inline uint64_t FinishPersistentHash(uint64_t Hash) noexcept
    {
        return Hash == 0 ? 1 : Hash;
    }

    /** Canonical in-process hash-combine operation for RHI value types. */
    template <typename T>
    inline void HashCombine(size_t& Seed, const T& Value) noexcept
    {
        Seed ^= std::hash<T>{}(Value) + size_t(0x9e3779b9) +
            (Seed << 6) + (Seed >> 2);
    }

    /** Hashes an EASTL string using the canonical in-process combiner. */
    inline void HashString(
        size_t& Seed,
        const eastl::string& Value) noexcept
    {
        for (const char Character : Value)
            HashCombine(Seed, static_cast<uint8_t>(Character));
    }

    /** Returns a float's exact object representation for semantic hashing. */
    [[nodiscard]] inline uint32_t FloatBits(float Value) noexcept
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }
}
