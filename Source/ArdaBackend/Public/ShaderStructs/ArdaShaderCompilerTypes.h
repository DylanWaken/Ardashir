/** @file ArdaShaderCompilerTypes.h
 *  @brief Declares backend-neutral shader permutation and compilation inputs.
 */
#pragma once

#include "ArdaBackendProvider.h"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace arda::backend
{
    class FArdaShaderType;

    /** Stores one preprocessor definition supplied to shader compilation. */
    struct FArdaShaderDefine
    {
        /** Definition name. */
        eastl::string mName;
        /** Definition value. */
        eastl::string mValue;
    };

    /** Collects deterministic, backend-neutral inputs for shader compilation. */
    class FArdaShaderCompileEnvironment final
    {
    public:
        /**
         * Adds or replaces a definition while preserving name-sorted order.
         * @param Name Definition name; an empty name is rejected.
         * @param Value Definition value.
         * @return True when the definition name was valid.
         */
        bool SetDefine(const eastl::string& Name, const eastl::string& Value)
        {
            if (Name.empty())
                return false;
            auto Position = eastl::lower_bound(
                mDefines.begin(),
                mDefines.end(),
                Name,
                [](const FArdaShaderDefine& Define, const eastl::string& Key)
                {
                    return Define.mName < Key;
                });
            if (Position != mDefines.end() && Position->mName == Name)
                Position->mValue = Value;
            else
                mDefines.insert(Position, { Name, Value });
            return true;
        }

        /**
         * Adds or replaces an integral definition.
         * @tparam Integer Non-Boolean integral value type.
         * @param Name Definition name; an empty name is rejected.
         * @param Value Integral definition value.
         * @return True when the definition name was valid.
         */
        template <
            typename Integer,
            std::enable_if_t<
                std::is_integral_v<Integer> &&
                !std::is_same_v<std::remove_cv_t<Integer>, bool>,
                int> = 0>
        bool SetDefine(const eastl::string& Name, Integer Value)
        {
            const std::string Text = std::to_string(Value);
            return SetDefine(Name, eastl::string(Text.data(), Text.size()));
        }

        /**
         * Adds or replaces a Boolean definition as zero or one.
         * @param Name Definition name; an empty name is rejected.
         * @param Value Boolean definition value.
         * @return True when the definition name was valid.
         */
        bool SetDefine(const eastl::string& Name, bool Value)
        {
            return SetDefine(Name, eastl::string(Value ? "1" : "0"));
        }

        /** @return Definitions in deterministic ascending name order. */
        [[nodiscard]] const eastl::vector<FArdaShaderDefine>& GetDefines() const noexcept
        {
            return mDefines;
        }

    private:
        /** Name-sorted preprocessor definitions. */
        eastl::vector<FArdaShaderDefine> mDefines;
    };

    /** Identifies one shader type, backend module, and permutation for compilation policy. */
    struct FArdaShaderPermutationParameters
    {
        /** Registered shader type being considered, or null before publication. */
        const FArdaShaderType* mType = nullptr;
        /** Encoded permutation identifier. */
        uint32_t mPermutationId = 0;
        /** Stable backend module targeted by this permutation. */
        eastl::string mBackendName;
        /** Shader binary format requested by the module. */
        EArdaShaderBinaryFormat mBinaryFormat =
            EArdaShaderBinaryFormat::BackendDefined;
    };

    /**
     * Encodes the Cartesian product of shader permutation dimensions.
     * The first listed dimension occupies the least-significant mixed-radix digit.
     * @tparam Dimensions Dimension types declared with an ARDA_SHADER_PERMUTATION macro.
     */
    template <typename... Dimensions>
    class TArdaShaderPermutationDomain final
    {
        static_assert(sizeof...(Dimensions) > 0, "A permutation domain needs a dimension.");

        static constexpr uint64_t CalculateCount()
        {
            uint64_t Count = 1;
            ((Count *= Dimensions::PermutationCount), ...);
            return Count;
        }

        template <typename Dimension>
        static constexpr uint32_t GetDivisor()
        {
            static_assert(
                (std::is_same_v<Dimension, Dimensions> || ...),
                "The requested dimension is not part of this permutation domain.");
            constexpr uint32_t Counts[] = { Dimensions::PermutationCount... };
            constexpr bool Matches[] = {
                std::is_same_v<Dimension, Dimensions>...
            };
            uint32_t Divisor = 1;
            for (size_t Index = 0; Index < sizeof...(Dimensions); ++Index)
            {
                if (Matches[Index])
                    break;
                Divisor *= Counts[Index];
            }
            return Divisor;
        }

        template <typename Dimension>
        [[nodiscard]] constexpr uint32_t GetRaw() const noexcept
        {
            return (mPermutationId / GetDivisor<Dimension>()) %
                Dimension::PermutationCount;
        }

    public:
        /** Number of identifiers in the Cartesian product. */
        static constexpr uint32_t PermutationCount =
            static_cast<uint32_t>(CalculateCount());

        static_assert(
            CalculateCount() <= std::numeric_limits<uint32_t>::max(),
            "Permutation domain does not fit in a uint32 identifier.");

        /** Constructs the first permutation. */
        constexpr TArdaShaderPermutationDomain() noexcept = default;

        /**
         * Decomposes an encoded permutation identifier.
         * @param PermutationId Identifier to store; inspect IsValid before use.
         */
        explicit constexpr TArdaShaderPermutationDomain(uint32_t PermutationId) noexcept
            : mPermutationId(PermutationId)
        {
        }

        /**
         * Checks an encoded identifier against this domain.
         * @param PermutationId Identifier to validate.
         * @return True when the identifier can be decomposed by this domain.
         */
        [[nodiscard]] static constexpr bool IsValidPermutationId(
            uint32_t PermutationId) noexcept
        {
            return PermutationId < PermutationCount;
        }

        /** @return True when the stored identifier belongs to this domain. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return IsValidPermutationId(mPermutationId);
        }

        /** @return The encoded Cartesian-product identifier. */
        [[nodiscard]] constexpr uint32_t ToId() const noexcept
        {
            return mPermutationId;
        }

        /**
         * Reads one dimension from the stored identifier.
         * @tparam Dimension Dimension to read.
         * @return Typed value of the selected dimension.
         */
        template <typename Dimension>
        [[nodiscard]] constexpr typename Dimension::ValueType Get() const noexcept
        {
            return Dimension::Decode(GetRaw<Dimension>());
        }

        /**
         * Replaces one dimension and re-encodes the Cartesian-product identifier.
         * @tparam Dimension Dimension to replace.
         * @param Value New typed dimension value.
         * @return True when Value belongs to the dimension.
         */
        template <typename Dimension>
        constexpr bool Set(typename Dimension::ValueType Value) noexcept
        {
            const uint32_t Encoded = Dimension::Encode(Value);
            if (Encoded >= Dimension::PermutationCount)
                return false;
            const uint32_t Divisor = GetDivisor<Dimension>();
            mPermutationId =
                mPermutationId - GetRaw<Dimension>() * Divisor + Encoded * Divisor;
            return IsValid();
        }

        /**
         * Adds all dimension definitions for the stored identifier.
         * @param Environment Compilation environment receiving definitions.
         * @return True when the stored identifier is valid.
         */
        bool ModifyCompilationEnvironment(
            FArdaShaderCompileEnvironment& Environment) const
        {
            if (!IsValid())
                return false;
            (Dimensions::AddDefine(Environment, Get<Dimensions>()), ...);
            return true;
        }

    private:
        /** Encoded mixed-radix permutation identifier. */
        uint32_t mPermutationId = 0;
    };
}

/**
 * Declares a Boolean shader permutation dimension.
 * @param Name C++ dimension type name.
 * @param DefineName Shader preprocessor definition name.
 */
#define ARDA_SHADER_PERMUTATION_BOOL(Name, DefineName)                                      \
    struct Name final                                                                        \
    {                                                                                        \
        /** Typed value represented by this dimension. */                                   \
        using ValueType = bool;                                                              \
        /** Number of values represented by this dimension. */                              \
        static constexpr uint32_t PermutationCount = 2;                                     \
        /** @return Value decoded from a mixed-radix digit. */                              \
        static constexpr ValueType Decode(uint32_t Value) noexcept { return Value != 0; }   \
        /** @return Mixed-radix digit encoded from Value. */                                \
        static constexpr uint32_t Encode(ValueType Value) noexcept                          \
        { return Value ? 1u : 0u; }                                                         \
        /** Adds this dimension's definition to Environment. */                             \
        static void AddDefine(                                                               \
            ::arda::backend::FArdaShaderCompileEnvironment& Environment,                    \
            ValueType Value)                                                                 \
        { Environment.SetDefine(DefineName, Value); }                                       \
    }

/**
 * Declares a bounded integer shader permutation dimension.
 * @param Name C++ dimension type name.
 * @param DefineName Shader preprocessor definition name.
 * @param Count Number of integer values, beginning at zero.
 */
#define ARDA_SHADER_PERMUTATION_INT(Name, DefineName, Count)                                 \
    struct Name final                                                                        \
    {                                                                                        \
        /** Typed value represented by this dimension. */                                   \
        using ValueType = uint32_t;                                                          \
        /** Number of values represented by this dimension. */                              \
        static constexpr uint32_t PermutationCount = static_cast<uint32_t>(Count);           \
        static_assert(PermutationCount > 0, "Integer permutation count must be nonzero.");  \
        /** @return Value decoded from a mixed-radix digit. */                              \
        static constexpr ValueType Decode(uint32_t Value) noexcept { return Value; }        \
        /** @return Mixed-radix digit encoded from Value. */                                \
        static constexpr uint32_t Encode(ValueType Value) noexcept { return Value; }        \
        /** Adds this dimension's definition to Environment. */                             \
        static void AddDefine(                                                               \
            ::arda::backend::FArdaShaderCompileEnvironment& Environment,                    \
            ValueType Value)                                                                 \
        { Environment.SetDefine(DefineName, static_cast<uint64_t>(Value)); }                \
    }
