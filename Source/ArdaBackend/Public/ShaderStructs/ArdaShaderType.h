#pragma once

#include "ShaderStructs/ArdaShaderParameters.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace arda::backend
{
    class FArdaGlobalShader
    {
    public:
        virtual ~FArdaGlobalShader() = default;
    };

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

    struct FArdaShaderRegistrationStatus
    {
        EArdaShaderRegistrationError mCode = EArdaShaderRegistrationError::None;
        eastl::string mMessage;
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderRegistrationError::None;
        }
    };

    class FArdaShaderType final
    {
    public:
        using FParameterMetadataFunction =
            const FArdaShaderParameterMetadata* (*)();

        [[nodiscard]] const char* GetName() const noexcept { return mName; }
        /** Logical source stem used for source identity and diagnostics only. */
        [[nodiscard]] const char* GetSourceStem() const noexcept { return mSourceStem; }
        /** Artifact filename stem used by the global shader map bytecode loader. */
        [[nodiscard]] const char* GetOutputStem() const noexcept { return mOutputStem; }
        [[nodiscard]] const char* GetEntryPoint() const noexcept { return mEntryPoint; }
        [[nodiscard]] rhi::EArdaRHIShaderStage GetStage() const noexcept { return mStage; }
        [[nodiscard]] const FArdaShaderParameterMetadata* GetParameterMetadata() const;
        [[nodiscard]] uint64_t GetIdentityHash() const noexcept { return mIdentityHash; }

    private:
        friend class FArdaShaderTypeRegistration;
        const char* mName = nullptr;
        const char* mSourceStem = nullptr;
        const char* mOutputStem = nullptr;
        const char* mEntryPoint = nullptr;
        rhi::EArdaRHIShaderStage mStage = rhi::EArdaRHIShaderStage::None;
        FParameterMetadataFunction mParameterMetadataFunction = nullptr;
        uint64_t mIdentityHash = 0;
    };

    /** Deferred registration node; construction only appends to a pending list. */
    class FArdaShaderTypeRegistration final
    {
    public:
        FArdaShaderTypeRegistration(
            const char* Name,
            const char* SourceStem,
            const char* OutputStem,
            const char* EntryPoint,
            rhi::EArdaRHIShaderStage Stage,
            FArdaShaderType::FParameterMetadataFunction ParameterMetadataFunction);
        ~FArdaShaderTypeRegistration();

        FArdaShaderTypeRegistration(const FArdaShaderTypeRegistration&) = delete;
        FArdaShaderTypeRegistration& operator=(const FArdaShaderTypeRegistration&) = delete;

        [[nodiscard]] const FArdaShaderType& GetType() const noexcept { return mType; }

        /** Deterministically validates and publishes every pending shader type. */
        [[nodiscard]] static FArdaShaderRegistrationStatus CommitAll();
        [[nodiscard]] static const FArdaShaderType* Find(const eastl::string& Name);
        [[nodiscard]] static eastl::vector<const FArdaShaderType*> Enumerate();

        /** Test-only registry reset; static nodes become pending again. */
        static void ResetForTests();

    private:
        FArdaShaderType mType;
    };
}

#define ARDA_DECLARE_GLOBAL_SHADER(ShaderClass)                                                              \
    public:                                                                                                  \
        static const ::arda::backend::FArdaShaderType& GetStaticType();                                     \
    private:                                                                                                 \
        static ::arda::backend::FArdaShaderTypeRegistration sArdaShaderRegistration

#define ARDA_IMPLEMENT_GLOBAL_SHADER(ShaderClass, SourceStem, OutputStem, EntryPoint, Stage)                  \
    ::arda::backend::FArdaShaderTypeRegistration ShaderClass::sArdaShaderRegistration(                      \
        #ShaderClass, SourceStem, OutputStem, EntryPoint, Stage,                                             \
        []() -> const ::arda::backend::FArdaShaderParameterMetadata*                                        \
        { return &ShaderClass::FParameters::GetStaticMetadata(); });                                         \
    const ::arda::backend::FArdaShaderType& ShaderClass::GetStaticType()                                    \
    { return sArdaShaderRegistration.GetType(); }

#define ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(                                                     \
    ShaderClass, SourceStem, OutputStem, EntryPoint, Stage)                                                  \
    ::arda::backend::FArdaShaderTypeRegistration ShaderClass::sArdaShaderRegistration(                      \
        #ShaderClass, SourceStem, OutputStem, EntryPoint, Stage, nullptr);                                   \
    const ::arda::backend::FArdaShaderType& ShaderClass::GetStaticType()                                    \
    { return sArdaShaderRegistration.GetType(); }
