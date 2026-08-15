#pragma once

#include "RHIWrappers/ArdaRHIDevice.h"

#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda::backend
{
    enum class EArdaShaderParameterKind : uint8_t
    {
        Value,
        TextureSRV,
        TextureUAV,
        BufferSRV,
        BufferUAV,
        ConstantBuffer,
        UniformBuffer,
        Sampler,
        PushConstants,
        AccelerationStructure,
        NestedStruct
    };

    enum class EArdaShaderStructError : uint8_t
    {
        None,
        InvalidStruct,
        InvalidMember,
        DuplicateRegister,
        IncompatibleVisibility,
        MalformedArray,
        MalformedPushConstants,
        LayoutCreationFailed,
        BindingCreationFailed
    };

    struct FArdaShaderStructStatus
    {
        EArdaShaderStructError mCode = EArdaShaderStructError::None;
        eastl::string mMessage;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderStructError::None;
        }
    };

    class FArdaShaderParameterMetadata;

    struct FArdaShaderParameterMember
    {
        const char* mName = nullptr;
        EArdaShaderParameterKind mKind = EArdaShaderParameterKind::Value;
        rhi::EArdaRHIBindingType mBindingType = rhi::EArdaRHIBindingType::TextureSRV;
        uint32_t mSlot = 0;
        uint32_t mRegisterSpace = 0;
        uint32_t mArrayCount = 1;
        size_t mOffset = 0;
        size_t mSize = 0;
        size_t mElementStride = 0;
        rhi::EArdaRHIShaderStage mVisibility = rhi::EArdaRHIShaderStage::None;
        const FArdaShaderParameterMetadata* mNestedMetadata = nullptr;
    };

    /** One recursively flattened member and its offset from the root struct. */
    struct FArdaFlattenedShaderParameterMember
    {
        const FArdaShaderParameterMember* mMember = nullptr;
        size_t mAbsoluteOffset = 0;
        eastl::string mPath;
    };

    /**
     * C++ parameter-struct metadata authored by macros.
     *
     * This data describes declared C++ values and resource bindings. It is not
     * bytecode reflection and makes no claim about DXIL or SPIR-V contents.
     */
    class FArdaShaderParameterMetadata final
    {
    public:
        FArdaShaderParameterMetadata(
            const char* Name,
            size_t Size,
            size_t Alignment,
            eastl::vector<FArdaShaderParameterMember> Members);

        [[nodiscard]] const char* GetName() const noexcept { return mName; }
        [[nodiscard]] size_t GetSize() const noexcept { return mSize; }
        [[nodiscard]] size_t GetAlignment() const noexcept { return mAlignment; }
        [[nodiscard]] uint64_t GetLayoutHash() const noexcept { return mLayoutHash; }
        [[nodiscard]] const FArdaShaderStructStatus& GetStatus() const noexcept { return mStatus; }
        [[nodiscard]] const eastl::vector<FArdaShaderParameterMember>& GetMembers() const noexcept
        {
            return mMembers;
        }

        [[nodiscard]] const FArdaShaderParameterMember* FindMember(
            const eastl::string& Name) const noexcept;

        /** Finds a direct or dotted nested member path and optionally returns its root offset. */
        [[nodiscard]] const FArdaShaderParameterMember* FindFlattenedMember(
            const eastl::string& Path,
            size_t* AbsoluteOffset = nullptr) const noexcept;

        /** Enumerates leaf members recursively in deterministic declaration order. */
        void GetFlattenedMembers(
            eastl::vector<FArdaFlattenedShaderParameterMember>& OutMembers) const;

        /** Builds one RHI layout per register-space/visibility group. */
        [[nodiscard]] FArdaShaderStructStatus BuildBindingLayoutDescs(
            eastl::vector<rhi::FArdaRHIBindingLayoutDesc>& OutDescs) const;

        /** Builds direct RHI binding descriptors from an instance containing RHI refs. */
        [[nodiscard]] FArdaShaderStructStatus BuildBindingSetDesc(
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutRef& Layout,
            rhi::FArdaRHIBindingSetDesc& OutDesc) const;

        /** Builds and creates a direct binding set from concrete RHI-ref members. */
        [[nodiscard]] FArdaShaderStructStatus CreateBindingSet(
            rhi::IArdaRHIDevice& Device,
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutRef& Layout,
            rhi::FArdaRHIBindingSetRef& OutBindingSet) const;

        /** Applies this layout's concrete push-constant block from a parameter instance. */
        [[nodiscard]] FArdaShaderStructStatus ApplyPushConstants(
            rhi::IArdaRHICommandList& CommandList,
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutDesc& Layout) const;

        /** Resolves the concrete byte range consumed by ApplyPushConstants. */
        [[nodiscard]] FArdaShaderStructStatus GetPushConstantData(
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutDesc& Layout,
            const void*& OutData,
            size_t& OutSize) const;

    private:
        void ValidateAndHash();

        const char* mName = nullptr;
        size_t mSize = 0;
        size_t mAlignment = 0;
        eastl::vector<FArdaShaderParameterMember> mMembers;
        FArdaShaderStructStatus mStatus;
        uint64_t mLayoutHash = 0;
    };

    template <typename ParameterType>
    [[nodiscard]] FArdaShaderStructStatus ApplyShaderPushConstants(
        rhi::IArdaRHICommandList& CommandList,
        const ParameterType& Parameters,
        const rhi::FArdaRHIBindingLayoutDesc& Layout)
    {
        return ParameterType::GetStaticMetadata().ApplyPushConstants(
            CommandList,
            &Parameters,
            Layout);
    }
}

#define ARDA_INTERNAL_SHADER_PARAMETER(Kind, BindingType, CppType, MemberName, Slot, Space, Count, Visibility) \
        FArdaShaderMemberId##MemberName;                                                                       \
    public:                                                                                                     \
        static_assert((Count) > 0, "Shader parameter arrays cannot be empty.");                                \
        eastl::array<CppType, Count> MemberName{};                                                              \
    private:                                                                                                    \
        struct FArdaShaderNextMemberId##MemberName {};                                                          \
        static void FArdaAppendShaderMembers(                                                                   \
            FArdaShaderNextMemberId##MemberName,                                                                \
            eastl::vector<::arda::backend::FArdaShaderParameterMember>& Members)                               \
        {                                                                                                       \
            FArdaAppendShaderMembers(FArdaShaderMemberId##MemberName{}, Members);                              \
            Members.push_back({                                                                                 \
                #MemberName, Kind, BindingType, Slot, Space, Count,                                             \
                offsetof(FArdaShaderThisStruct, MemberName),                                                    \
                sizeof(eastl::array<CppType, Count>), sizeof(CppType), Visibility, nullptr});                   \
        }                                                                                                       \
        typedef FArdaShaderNextMemberId##MemberName

#define ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(Kind, BindingType, CppType, MemberName, Slot, Space, Visibility) \
        FArdaShaderMemberId##MemberName;                                                                        \
    public:                                                                                                      \
        CppType MemberName{};                                                                                    \
    private:                                                                                                     \
        struct FArdaShaderNextMemberId##MemberName {};                                                           \
        static void FArdaAppendShaderMembers(                                                                    \
            FArdaShaderNextMemberId##MemberName,                                                                 \
            eastl::vector<::arda::backend::FArdaShaderParameterMember>& Members)                                \
        {                                                                                                        \
            FArdaAppendShaderMembers(FArdaShaderMemberId##MemberName{}, Members);                               \
            Members.push_back({                                                                                  \
                #MemberName, Kind, BindingType, Slot, Space, 1u,                                                 \
                offsetof(FArdaShaderThisStruct, MemberName), sizeof(CppType), sizeof(CppType),                  \
                Visibility, nullptr});                                                                           \
        }                                                                                                        \
        typedef FArdaShaderNextMemberId##MemberName

#define ARDA_BEGIN_SHADER_PARAMETER_STRUCT(StructType)                                                          \
    struct StructType                                                                                            \
    {                                                                                                            \
    public:                                                                                                      \
        StructType() = default;                                                                                  \
    private:                                                                                                     \
        using FArdaShaderThisStruct = StructType;                                                                \
        static constexpr const char* FArdaShaderStructName = #StructType;                                       \
        struct FArdaShaderFirstMemberId {};                                                                      \
        static void FArdaAppendShaderMembers(                                                                    \
            FArdaShaderFirstMemberId,                                                                            \
            eastl::vector<::arda::backend::FArdaShaderParameterMember>&) {}                                     \
        typedef FArdaShaderFirstMemberId

#define ARDA_END_SHADER_PARAMETER_STRUCT()                                                                       \
        FArdaShaderLastMemberId;                                                                                 \
    public:                                                                                                      \
        static const ::arda::backend::FArdaShaderParameterMetadata& GetStaticMetadata()                         \
        {                                                                                                        \
            static_assert(eastl::is_standard_layout_v<FArdaShaderThisStruct>,                                   \
                "Shader parameter structs must use standard layout.");                                          \
            static const ::arda::backend::FArdaShaderParameterMetadata Metadata = []                            \
            {                                                                                                    \
                eastl::vector<::arda::backend::FArdaShaderParameterMember> Members;                             \
                FArdaAppendShaderMembers(FArdaShaderLastMemberId{}, Members);                                    \
                return ::arda::backend::FArdaShaderParameterMetadata(                                           \
                    FArdaShaderStructName, sizeof(FArdaShaderThisStruct), alignof(FArdaShaderThisStruct),       \
                    eastl::move(Members));                                                                        \
            }();                                                                                                 \
            return Metadata;                                                                                     \
        }                                                                                                        \
    };

#define ARDA_SHADER_PARAMETER_VALUE(CppType, MemberName)                                                        \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::Value,                                                       \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, CppType, MemberName, 0, 0,                               \
        ::arda::rhi::EArdaRHIShaderStage::None)

#define ARDA_SHADER_TEXTURE_SRV(MemberName, Slot, Space, Visibility)                                            \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::TextureSRV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_TEXTURE_UAV(MemberName, Slot, Space, Visibility)                                            \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::TextureUAV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureUAV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_BUFFER_SRV(MemberName, Slot, Space, Visibility)                                             \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::BufferSRV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferSRV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_BUFFER_UAV(MemberName, Slot, Space, Visibility)                                             \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::BufferUAV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferUAV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_CONSTANT_BUFFER(MemberName, Slot, Space, Visibility)                                        \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::ConstantBuffer,                                             \
        ::arda::rhi::EArdaRHIBindingType::ConstantBuffer, ::arda::rhi::FArdaRHIBufferRef,                      \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_UNIFORM_BUFFER(MemberName, Slot, Space, Visibility)                                         \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::UniformBuffer,                                              \
        ::arda::rhi::EArdaRHIBindingType::ConstantBuffer, ::arda::rhi::FArdaRHIUniformBufferRef,               \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_SAMPLER(MemberName, Slot, Space, Visibility)                                                \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::Sampler,                                                    \
        ::arda::rhi::EArdaRHIBindingType::Sampler, ::arda::rhi::FArdaRHISamplerRef,                            \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_ACCELERATION_STRUCTURE(MemberName, Slot, Space, Visibility)                                 \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::AccelerationStructure,                                      \
        ::arda::rhi::EArdaRHIBindingType::RayTracingAccelStruct, ::arda::rhi::FArdaRHIAccelStructRef,          \
        MemberName, Slot, Space, Visibility)
#define ARDA_SHADER_TEXTURE_SRV_ARRAY(MemberName, Slot, Space, Count, Visibility)                               \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::TextureSRV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_TEXTURE_UAV_ARRAY(MemberName, Slot, Space, Count, Visibility)                               \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::TextureUAV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureUAV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_BUFFER_SRV_ARRAY(MemberName, Slot, Space, Count, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::BufferSRV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferSRV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_BUFFER_UAV_ARRAY(MemberName, Slot, Space, Count, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::BufferUAV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferUAV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_SAMPLER_ARRAY(MemberName, Slot, Space, Count, Visibility)                                   \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::Sampler,                                                    \
        ::arda::rhi::EArdaRHIBindingType::Sampler, ::arda::rhi::FArdaRHISamplerRef,                            \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_ACCELERATION_STRUCTURE_ARRAY(MemberName, Slot, Space, Count, Visibility)                    \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::AccelerationStructure,                                      \
        ::arda::rhi::EArdaRHIBindingType::RayTracingAccelStruct, ::arda::rhi::FArdaRHIAccelStructRef,          \
        MemberName, Slot, Space, Count, Visibility)
#define ARDA_SHADER_PUSH_CONSTANTS(CppType, MemberName, Slot, Space, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::PushConstants,                                              \
        ::arda::rhi::EArdaRHIBindingType::PushConstants, CppType, MemberName, Slot, Space, Visibility)

#define ARDA_SHADER_PARAMETER_STRUCT(StructType, MemberName)                                                    \
        FArdaShaderMemberId##MemberName;                                                                         \
    public:                                                                                                      \
        StructType MemberName{};                                                                                 \
    private:                                                                                                     \
        struct FArdaShaderNextMemberId##MemberName {};                                                           \
        static void FArdaAppendShaderMembers(                                                                    \
            FArdaShaderNextMemberId##MemberName,                                                                 \
            eastl::vector<::arda::backend::FArdaShaderParameterMember>& Members)                                \
        {                                                                                                        \
            FArdaAppendShaderMembers(FArdaShaderMemberId##MemberName{}, Members);                               \
            Members.push_back({#MemberName, ::arda::backend::EArdaShaderParameterKind::NestedStruct,           \
                ::arda::rhi::EArdaRHIBindingType::TextureSRV, 0, 0, 1,                                         \
                offsetof(FArdaShaderThisStruct, MemberName), sizeof(StructType), sizeof(StructType),            \
                ::arda::rhi::EArdaRHIShaderStage::None, &StructType::GetStaticMetadata()});                     \
        }                                                                                                        \
        typedef FArdaShaderNextMemberId##MemberName
