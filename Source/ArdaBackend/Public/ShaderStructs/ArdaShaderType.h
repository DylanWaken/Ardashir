/** @file ArdaShaderType.h
 *  @brief Declares global shader types and their registration macros.
 */
#pragma once

#include "ShaderStructs/ArdaShaderParameters.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

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
        None,
        InvalidType,
        DuplicateTypeName,
        DuplicateIdentity,
        InvalidParameterMetadata,
        MissingVirtualSource,
        DirectoryRegistryNotFrozen,
        DirectoryRegistryFailure
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

        /** @return The registered shader class name. */
        [[nodiscard]] const char* GetName() const noexcept { return mName; }
        /** @return The logical source stem used for identity and diagnostics. */
        [[nodiscard]] const char* GetSourceStem() const noexcept { return mSourceStem; }
        /** @return The artifact filename stem used by the bytecode loader. */
        [[nodiscard]] const char* GetOutputStem() const noexcept { return mOutputStem; }
        /** @return The shader entry-point name. */
        [[nodiscard]] const char* GetEntryPoint() const noexcept { return mEntryPoint; }
        /** @return The RHI stage implemented by the shader. */
        [[nodiscard]] rhi::EArdaRHIShaderStage GetStage() const noexcept { return mStage; }
        /** @return Parameter metadata, or null for a parameterless shader. */
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const;
        /** @return Stable hash of the shader type's source identity. */
        [[nodiscard]] uint64_t GetIdentityHash() const noexcept { return mIdentityHash; }

    private:
        friend class FArdaShaderTypeRegistration;
        /** Registered shader class name. */
        const char* mName = nullptr;
        /** Logical source stem used for identity and diagnostics. */
        const char* mSourceStem = nullptr;
        /** Artifact filename stem used by the bytecode loader. */
        const char* mOutputStem = nullptr;
        /** Shader entry-point name. */
        const char* mEntryPoint = nullptr;
        /** RHI shader stage. */
        rhi::EArdaRHIShaderStage mStage = rhi::EArdaRHIShaderStage::None;
        /** Optional static parameter-metadata accessor. */
        FParameterMetadataFunction mParameterMetadataFunction = nullptr;
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
         */
        FArdaShaderTypeRegistration(
            const char* Name,
            const char* SourceStem,
            const char* OutputStem,
            const char* EntryPoint,
            rhi::EArdaRHIShaderStage Stage,
            FArdaShaderType::FParameterMetadataFunction ParameterMetadataFunction);
        /** Removes this node from the registration lists. */
        ~FArdaShaderTypeRegistration();

        /** Shader registration nodes cannot be copied. */
        FArdaShaderTypeRegistration(const FArdaShaderTypeRegistration&) = delete;
        /** Shader registration nodes cannot be copy-assigned. */
        FArdaShaderTypeRegistration& operator=(const FArdaShaderTypeRegistration&) = delete;

        /** @return The shader type owned by this registration node. */
        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return mType; }

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

        /** Test-only registry reset; static nodes become pending again. */
        static void ResetForTests();

    private:
        /** Shader type populated and published by this registration node. */
        FArdaShaderType mType;
    };
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
        static ::arda::backend::FArdaShaderTypeRegistration sArdaShaderRegistration

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
        { return &ShaderClass::FParameters::GetStaticMetadata(); });                                         \
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
        #ShaderClass, SourceStem, OutputStem, EntryPoint, Stage, nullptr);                                   \
    /** @return The registered static shader type. */                                                       \
    const ::arda::backend::FArdaShaderType& ShaderClass::GetStaticType()                                    \
    { return sArdaShaderRegistration.GetType(); }
