/** @file ArdaShaderType.h
 *  @brief Declares global shader types and their registration macros.
 */
#pragma once

#include "ShaderStructs/ArdaShaderCompilerTypes.h"
#include "ShaderStructs/ArdaShaderParameters.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace arda::backend
{
    /** Base class for globally registered shader declarations. */
    class FArdaGlobalShader
    {
    public:
        /** Destroys a global shader declaration. */
        virtual ~FArdaGlobalShader() = default;
    };

    /** Identifies the result of validating and registering shader types. */
    enum class EArdaShaderRegistrationError : uint8_t
    {
        /** Registration succeeded. */
        None,
        /** A shader identity field is missing or invalid. */
        InvalidType,
        /** Two shader types use the same registered name. */
        DuplicateTypeName,
        /** Two shader types have the same complete source identity. */
        DuplicateIdentity,
        /** A permutation count or callback is invalid. */
        InvalidPermutation,
        /** Generated artifact stems overlap across permutation registrations. */
        ArtifactStemCollision,
        /** Shader parameter metadata is invalid. */
        InvalidParameterMetadata,
        /** A registered virtual shader source does not exist. */
        MissingVirtualSource,
        /** Virtual shader directories have not been frozen. */
        DirectoryRegistryNotFrozen,
        /** Virtual shader directory validation failed. */
        DirectoryRegistryFailure,
        /** The registry changed repeatedly while validation was in progress. */
        RegistryMutation
    };

    /** Reports the outcome and diagnostic text of shader registration. */
    struct FArdaShaderRegistrationStatus
    {
        /** Registration result code. */
        EArdaShaderRegistrationError mCode = EArdaShaderRegistrationError::None;
        /** Human-readable registration diagnostic. */
        eastl::string mMessage;
        /** @return True when registration succeeded. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderRegistrationError::None;
        }
    };

    /** Stores immutable identity and parameter metadata for a shader type. */
    class FArdaShaderType final
    {
    public:
        /** Function that returns a shader class's static parameter metadata. */
        using FParameterMetadataFunction =
            const FArdaShaderParameterMetadata* (*)();
        /** Function that decides whether one permutation should be compiled. */
        using FShouldCompilePermutationFunction =
            bool (*)(const FArdaShaderPermutationParameters&);
        /** Function that adds permutation and shader-class compilation definitions. */
        using FModifyCompilationEnvironmentFunction = void (*)(
            const FArdaShaderPermutationParameters&,
            FArdaShaderCompileEnvironment&);

        /** @return The registered shader class name. */
        [[nodiscard]] const char* GetName() const noexcept { return mName.c_str(); }
        /** @return The logical source stem used for identity and diagnostics. */
        [[nodiscard]] const char* GetSourceStem() const noexcept { return mSourceStem.c_str(); }
        /** @return The artifact filename stem used by the bytecode loader. */
        [[nodiscard]] const char* GetOutputStem() const noexcept { return mOutputStem.c_str(); }
        /** @return The shader entry-point name. */
        [[nodiscard]] const char* GetEntryPoint() const noexcept { return mEntryPoint.c_str(); }
        /** @return The RHI stage implemented by the shader. */
        [[nodiscard]] rhi::EArdaRHIShaderStage GetStage() const noexcept { return mStage; }
        /** @return Parameter metadata, or null for a parameterless shader. */
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const;
        /** @return Stable hash of the shader type's source identity. */
        [[nodiscard]] uint64_t GetIdentityHash() const noexcept { return mIdentityHash; }
        /** @return Number of encoded permutations declared by the shader class. */
        [[nodiscard]] uint32_t GetPermutationCount() const noexcept { return mPermutationCount; }
        /**
         * Evaluates the shader class's optional compile policy.
         * @param Backend Target graphics backend.
         * @param PermutationId Encoded permutation identifier.
         * @return True when the valid permutation should be compiled.
         */
        [[nodiscard]] bool ShouldCompilePermutation(
            EArdaBackendType Backend,
            uint32_t PermutationId) const;
        /**
         * Builds deterministic definitions without invoking an external compiler.
         * This is the compiler-integration hook point for a future process layer.
         * @param Backend Target graphics backend.
         * @param PermutationId Encoded permutation identifier.
         * @return Compilation environment containing dimension and class definitions.
         */
        [[nodiscard]] FArdaShaderCompileEnvironment BuildCompilationEnvironment(
            EArdaBackendType Backend,
            uint32_t PermutationId) const;
        /**
         * Produces the artifact stem for one permutation.
         * @param PermutationId Encoded permutation identifier.
         * @return Base stem for a single permutation, otherwise base stem plus _P and the ID.
         */
        [[nodiscard]] eastl::string GetPermutationArtifactStem(
            uint32_t PermutationId) const;

    private:
        friend class FArdaShaderTypeRegistration;
        /** Registered shader class name. */
        eastl::string mName;
        /** Logical source stem used for identity and diagnostics. */
        eastl::string mSourceStem;
        /** Artifact filename stem used by the bytecode loader. */
        eastl::string mOutputStem;
        /** Shader entry-point name. */
        eastl::string mEntryPoint;
        /** RHI shader stage. */
        rhi::EArdaRHIShaderStage mStage = rhi::EArdaRHIShaderStage::None;
        /** Optional static parameter-metadata accessor. */
        FParameterMetadataFunction mParameterMetadataFunction = nullptr;
        /** Number of encoded shader permutations. */
        uint32_t mPermutationCount = 1;
        /** Callback implementing optional compile filtering. */
        FShouldCompilePermutationFunction mShouldCompilePermutationFunction = nullptr;
        /** Callback implementing dimension and class compilation definitions. */
        FModifyCompilationEnvironmentFunction mModifyCompilationEnvironmentFunction = nullptr;
        /** Whether a multi-permutation registration supplied both callbacks. */
        bool mbPermutationCallbacksValid = true;
        /** Stable hash of the shader type's source identity. */
        uint64_t mIdentityHash = 0;
    };

    /** Deferred registration node; construction only appends to a pending list. */
    class FArdaShaderTypeRegistration final
    {
    public:
        /**
         * Appends a shader type to the pending registration list.
         * @param Name Registered shader class name.
         * @param SourceStem Logical source stem.
         * @param OutputStem Compiled artifact filename stem.
         * @param EntryPoint Shader entry-point name.
         * @param Stage RHI shader stage.
         * @param ParameterMetadataFunction Optional parameter-metadata accessor.
         * @param PermutationCount Number of encoded permutations.
         * @param ShouldCompilePermutationFunction Optional compile-policy callback.
         * @param ModifyCompilationEnvironmentFunction Optional definition callback.
         */
        FArdaShaderTypeRegistration(
            const char* Name,
            const char* SourceStem,
            const char* OutputStem,
            const char* EntryPoint,
            rhi::EArdaRHIShaderStage Stage,
            FArdaShaderType::FParameterMetadataFunction ParameterMetadataFunction,
            uint32_t PermutationCount = 1,
            FArdaShaderType::FShouldCompilePermutationFunction
                ShouldCompilePermutationFunction = nullptr,
            FArdaShaderType::FModifyCompilationEnvironmentFunction
                ModifyCompilationEnvironmentFunction = nullptr);
        /** Removes this node from the registration lists. */
        ~FArdaShaderTypeRegistration();

        /** Shader registration nodes cannot be copied. */
        FArdaShaderTypeRegistration(const FArdaShaderTypeRegistration&) = delete;
        /** Shader registration nodes cannot be copy-assigned. */
        FArdaShaderTypeRegistration& operator=(const FArdaShaderTypeRegistration&) = delete;

        /** @return The shader type owned by this registration node. */
        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return *mType; }

        /** @return Status from validating and publishing every pending shader type. */
        [[nodiscard]] static FArdaShaderRegistrationStatus CommitAll();
        /**
         * Finds a committed shader type by name.
         * @param Name Registered shader class name.
         * @return The matching shader type, or null.
         */
        [[nodiscard]] static const FArdaShaderType* Find(const eastl::string& Name);
        /** @return All committed shader types in deterministic order. */
        [[nodiscard]] static eastl::vector<const FArdaShaderType*> Enumerate();
        /** @return Owned snapshots of all committed shader descriptors. */
        [[nodiscard]] static eastl::vector<FArdaShaderType> EnumerateSnapshots();

        /** Test-only registry reset; static nodes become pending again. */
        static void ResetForTests();

    private:
        /** Shader type populated and published by this registration node. */
        std::shared_ptr<FArdaShaderType> mType;
    };

    namespace detail
    {
        template <typename ShaderClass, typename = void>
        struct TShaderPermutationDomain
        {
            static constexpr uint32_t PermutationCount = 1;
            static void AddDefines(
                uint32_t,
                FArdaShaderCompileEnvironment&)
            {
            }
        };

        template <typename ShaderClass>
        struct TShaderPermutationDomain<
            ShaderClass,
            std::void_t<typename ShaderClass::FPermutationDomain>>
        {
            using Domain = typename ShaderClass::FPermutationDomain;
            static constexpr uint32_t PermutationCount = Domain::PermutationCount;
            static void AddDefines(
                uint32_t PermutationId,
                FArdaShaderCompileEnvironment& Environment)
            {
                Domain(PermutationId).ModifyCompilationEnvironment(Environment);
            }
        };

        template <typename ShaderClass, typename = void>
        struct THasShouldCompilePermutation : std::false_type {};

        template <typename ShaderClass>
        struct THasShouldCompilePermutation<
            ShaderClass,
            std::void_t<decltype(&ShaderClass::ShouldCompilePermutation)>>
            : std::bool_constant<std::is_same_v<
                decltype(&ShaderClass::ShouldCompilePermutation),
                bool (*)(const FArdaShaderPermutationParameters&)>> {};

        template <typename ShaderClass, typename = void>
        struct THasNamedShouldCompilePermutation : std::false_type {};

        template <typename ShaderClass>
        struct THasNamedShouldCompilePermutation<
            ShaderClass,
            std::void_t<decltype(&ShaderClass::ShouldCompilePermutation)>>
            : std::true_type {};

        template <typename ShaderClass, typename = void>
        struct THasModifyCompilationEnvironment : std::false_type {};

        template <typename ShaderClass>
        struct THasModifyCompilationEnvironment<
            ShaderClass,
            std::void_t<decltype(&ShaderClass::ModifyCompilationEnvironment)>>
            : std::bool_constant<std::is_same_v<
                decltype(&ShaderClass::ModifyCompilationEnvironment),
                void (*)(
                    const FArdaShaderPermutationParameters&,
                    FArdaShaderCompileEnvironment&)>> {};

        template <typename ShaderClass, typename = void>
        struct THasNamedModifyCompilationEnvironment : std::false_type {};

        template <typename ShaderClass>
        struct THasNamedModifyCompilationEnvironment<
            ShaderClass,
            std::void_t<decltype(&ShaderClass::ModifyCompilationEnvironment)>>
            : std::true_type {};

        template <typename ShaderClass>
        [[nodiscard]] bool ShouldCompileShaderPermutation(
            const FArdaShaderPermutationParameters& Parameters)
        {
            static_assert(
                !THasNamedShouldCompilePermutation<ShaderClass>::value ||
                    THasShouldCompilePermutation<ShaderClass>::value,
                "ShouldCompilePermutation must be static bool(const FArdaShaderPermutationParameters&).");
            if (Parameters.mPermutationId >=
                TShaderPermutationDomain<ShaderClass>::PermutationCount)
            {
                return false;
            }
            if constexpr (THasShouldCompilePermutation<ShaderClass>::value)
                return ShaderClass::ShouldCompilePermutation(Parameters);
            return true;
        }

        template <typename ShaderClass>
        void BuildShaderCompilationEnvironment(
            const FArdaShaderPermutationParameters& Parameters,
            FArdaShaderCompileEnvironment& Environment)
        {
            static_assert(
                !THasNamedModifyCompilationEnvironment<ShaderClass>::value ||
                    THasModifyCompilationEnvironment<ShaderClass>::value,
                "ModifyCompilationEnvironment must be static void(const FArdaShaderPermutationParameters&, FArdaShaderCompileEnvironment&).");
            TShaderPermutationDomain<ShaderClass>::AddDefines(
                Parameters.mPermutationId,
                Environment);
            if constexpr (THasModifyCompilationEnvironment<ShaderClass>::value)
                ShaderClass::ModifyCompilationEnvironment(Parameters, Environment);
        }
    }
}

/**
 * Declares the static type API and registration node for a global shader class.
 * @param ShaderClass Global shader class being declared.
 */
#define ARDA_DECLARE_GLOBAL_SHADER(ShaderClass)                                                              \
    public:                                                                                                  \
        /** @return The registered static shader type. */                                                   \
        static const ::arda::backend::FArdaShaderType& GetStaticType();                                     \
    private:                                                                                                 \
        /** Registration node that owns the class's static shader type. */                                  \
        static ::arda::backend::FArdaShaderTypeRegistration sArdaShaderRegistration;                         \
    public:

/**
 * Defines registration for a global shader class with parameter metadata.
 * @param ShaderClass Global shader class being registered.
 * @param SourceStem Logical source stem.
 * @param OutputStem Compiled artifact filename stem.
 * @param EntryPoint Shader entry-point name.
 * @param Stage RHI shader stage.
 */
#define ARDA_IMPLEMENT_GLOBAL_SHADER(ShaderClass, SourceStem, OutputStem, EntryPoint, Stage)                  \
    /** Defines the shader class's static registration node. */                                             \
    ::arda::backend::FArdaShaderTypeRegistration ShaderClass::sArdaShaderRegistration(                      \
        #ShaderClass, SourceStem, OutputStem, EntryPoint, Stage,                                             \
        []() -> const ::arda::backend::FArdaShaderParameterMetadata*                                        \
        { return &ShaderClass::FParameters::GetStaticMetadata(); },                                          \
        ::arda::backend::detail::TShaderPermutationDomain<ShaderClass>::PermutationCount,                    \
        &::arda::backend::detail::ShouldCompileShaderPermutation<ShaderClass>,                               \
        &::arda::backend::detail::BuildShaderCompilationEnvironment<ShaderClass>);                           \
    /** @return The registered static shader type. */                                                       \
    const ::arda::backend::FArdaShaderType& ShaderClass::GetStaticType()                                    \
    { return sArdaShaderRegistration.GetType(); }

/**
 * Defines registration for a parameterless global shader class.
 * @param ShaderClass Global shader class being registered.
 * @param SourceStem Logical source stem.
 * @param OutputStem Compiled artifact filename stem.
 * @param EntryPoint Shader entry-point name.
 * @param Stage RHI shader stage.
 */
#define ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(                                                     \
    ShaderClass, SourceStem, OutputStem, EntryPoint, Stage)                                                  \
    /** Defines the shader class's static registration node. */                                             \
    ::arda::backend::FArdaShaderTypeRegistration ShaderClass::sArdaShaderRegistration(                      \
        #ShaderClass, SourceStem, OutputStem, EntryPoint, Stage, nullptr,                                    \
        ::arda::backend::detail::TShaderPermutationDomain<ShaderClass>::PermutationCount,                    \
        &::arda::backend::detail::ShouldCompileShaderPermutation<ShaderClass>,                               \
        &::arda::backend::detail::BuildShaderCompilationEnvironment<ShaderClass>);                           \
    /** @return The registered static shader type. */                                                       \
    const ::arda::backend::FArdaShaderType& ShaderClass::GetStaticType()                                    \
    { return sArdaShaderRegistration.GetType(); }
