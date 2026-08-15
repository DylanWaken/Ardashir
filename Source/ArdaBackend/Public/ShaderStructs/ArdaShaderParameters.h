/** @file ArdaShaderParameters.h
 *  @brief Declares shader parameter metadata, binding helpers, and authoring macros.
 */
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
    /** Identifies the semantic kind of a shader parameter member. */
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

    /** Identifies shader parameter validation or binding failures. */
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

    /** Reports the outcome and diagnostic text of a shader-struct operation. */
    struct FArdaShaderStructStatus
    {
        /** Shader-struct result code. */
        EArdaShaderStructError mCode = EArdaShaderStructError::None;
        /** Human-readable diagnostic message. */
        eastl::string mMessage;

        /** @return True when the operation succeeded. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mCode == EArdaShaderStructError::None;
        }
    };

    /** Describes the layout and members of a shader parameter struct. */
    class FArdaShaderParameterMetadata;

    /** Describes one direct member of a shader parameter struct. */
    struct FArdaShaderParameterMember
    {
        /** Declared C++ member name. */
        const char* mName = nullptr;
        /** Semantic shader parameter kind. */
        EArdaShaderParameterKind mKind = EArdaShaderParameterKind::Value;
        /** RHI binding type used for resource members. */
        rhi::EArdaRHIBindingType mBindingType = rhi::EArdaRHIBindingType::TextureSRV;
        /** Shader register slot. */
        uint32_t mSlot = 0;
        /** Shader register space. */
        uint32_t mRegisterSpace = 0;
        /** Number of elements in the declared member. */
        uint32_t mArrayCount = 1;
        /** Byte offset from the containing struct. */
        size_t mOffset = 0;
        /** Total byte size of the declared member. */
        size_t mSize = 0;
        /** Byte stride between array elements. */
        size_t mElementStride = 0;
        /** Shader stages that can access the member. */
        rhi::EArdaRHIShaderStage mVisibility = rhi::EArdaRHIShaderStage::None;
        /** Metadata for a nested parameter struct, or null. */
        const FArdaShaderParameterMetadata* mNestedMetadata = nullptr;
    };

    /** One recursively flattened member and its offset from the root struct. */
    struct FArdaFlattenedShaderParameterMember
    {
        /** Direct member metadata represented by this flattened entry. */
        const FArdaShaderParameterMember* mMember = nullptr;
        /** Byte offset from the root parameter struct. */
        size_t mAbsoluteOffset = 0;
        /** Dotted member path from the root parameter struct. */
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
        /**
         * Creates and validates metadata for a parameter struct.
         * @param Name Stable parameter-struct name.
         * @param Size C++ struct size in bytes.
         * @param Alignment C++ struct alignment in bytes.
         * @param Members Direct members in declaration order.
         */
        FArdaShaderParameterMetadata(
            const char* Name,
            size_t Size,
            size_t Alignment,
            eastl::vector<FArdaShaderParameterMember> Members);

        /** @return The stable parameter-struct name. */
        [[nodiscard]] const char* GetName() const noexcept { return mName; }
        /** @return The C++ parameter-struct size in bytes. */
        [[nodiscard]] size_t GetSize() const noexcept { return mSize; }
        /** @return The C++ parameter-struct alignment in bytes. */
        [[nodiscard]] size_t GetAlignment() const noexcept { return mAlignment; }
        /** @return Stable hash of the validated parameter layout. */
        [[nodiscard]] uint64_t GetLayoutHash() const noexcept { return mLayoutHash; }
        /** @return Validation status for this metadata. */
        [[nodiscard]] const FArdaShaderStructStatus& GetStatus() const noexcept { return mStatus; }
        /** @return Direct members in declaration order. */
        [[nodiscard]] const eastl::vector<FArdaShaderParameterMember>& GetMembers() const noexcept
        {
            return mMembers;
        }

        /**
         * Finds a direct member by name.
         * @param Name Direct member name.
         * @return The matching member, or null.
         */
        [[nodiscard]] const FArdaShaderParameterMember* FindMember(
            const eastl::string& Name) const noexcept;

        /**
         * Finds a direct or dotted nested member path.
         * @param Path Direct or dotted member path.
         * @param AbsoluteOffset Optionally receives the offset from the root struct.
         * @return The matching leaf member, or null.
         */
        [[nodiscard]] const FArdaShaderParameterMember* FindFlattenedMember(
            const eastl::string& Path,
            size_t* AbsoluteOffset = nullptr) const noexcept;

        /**
         * Enumerates leaf members recursively in deterministic declaration order.
         * @param OutMembers Receives flattened member entries.
         */
        void GetFlattenedMembers(
            eastl::vector<FArdaFlattenedShaderParameterMember>& OutMembers) const;

        /**
         * Builds one RHI layout per register-space and visibility group.
         * @param OutDescs Receives generated layout descriptions.
         * @return Layout generation status.
         */
        [[nodiscard]] FArdaShaderStructStatus BuildBindingLayoutDescs(
            eastl::vector<rhi::FArdaRHIBindingLayoutDesc>& OutDescs) const;

        /**
         * Builds direct RHI binding descriptors from an instance containing RHI refs.
         * @param Parameters Parameter-struct instance.
         * @param Layout Layout selecting the bindings to build.
         * @param OutDesc Receives the binding-set description.
         * @return Binding description status.
         */
        [[nodiscard]] FArdaShaderStructStatus BuildBindingSetDesc(
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutRef& Layout,
            rhi::FArdaRHIBindingSetDesc& OutDesc) const;

        /**
         * Builds and creates a direct binding set from concrete RHI-ref members.
         * @param Device Device used to create the binding set.
         * @param Parameters Parameter-struct instance.
         * @param Layout Binding layout for the set.
         * @param OutBindingSet Receives the created binding set.
         * @return Binding-set creation status.
         */
        [[nodiscard]] FArdaShaderStructStatus CreateBindingSet(
            rhi::IArdaRHIDevice& Device,
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutRef& Layout,
            rhi::FArdaRHIBindingSetRef& OutBindingSet) const;

        /**
         * Applies this layout's concrete push-constant block from a parameter instance.
         * @param CommandList Command list receiving push constants.
         * @param Parameters Parameter-struct instance.
         * @param Layout Layout selecting the push-constant block.
         * @return Push-constant application status.
         */
        [[nodiscard]] FArdaShaderStructStatus ApplyPushConstants(
            rhi::IArdaRHICommandList& CommandList,
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutDesc& Layout) const;

        /**
         * Resolves the concrete byte range consumed by ApplyPushConstants.
         * @param Parameters Parameter-struct instance.
         * @param Layout Layout selecting the push-constant block.
         * @param OutData Receives a pointer to the push-constant bytes.
         * @param OutSize Receives the push-constant byte count.
         * @return Push-constant resolution status.
         */
        [[nodiscard]] FArdaShaderStructStatus GetPushConstantData(
            const void* Parameters,
            const rhi::FArdaRHIBindingLayoutDesc& Layout,
            const void*& OutData,
            size_t& OutSize) const;

    private:
        /** Validates members and computes the stable layout hash. */
        void ValidateAndHash();

        /** Stable parameter-struct name. */
        const char* mName = nullptr;
        /** C++ parameter-struct size in bytes. */
        size_t mSize = 0;
        /** C++ parameter-struct alignment in bytes. */
        size_t mAlignment = 0;
        /** Direct members in declaration order. */
        eastl::vector<FArdaShaderParameterMember> mMembers;
        /** Metadata validation status. */
        FArdaShaderStructStatus mStatus;
        /** Stable hash of the validated parameter layout. */
        uint64_t mLayoutHash = 0;
    };

    /**
     * Applies push constants using a parameter type's static metadata.
     * @tparam ParameterType Macro-authored shader parameter struct type.
     * @param CommandList Command list receiving push constants.
     * @param Parameters Parameter-struct instance.
     * @param Layout Layout selecting the push-constant block.
     * @return Push-constant application status.
     */
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

/** Internal implementation for declaring an array shader parameter member. */
#define ARDA_INTERNAL_SHADER_PARAMETER(Kind, BindingType, CppType, MemberName, Slot, Space, Count, Visibility) \
        FArdaShaderMemberId##MemberName;                                                                       \
    public:                                                                                                     \
        static_assert((Count) > 0, "Shader parameter arrays cannot be empty.");                                \
        /** Shader parameter array declared by the invoking wrapper macro. */                                   \
        eastl::array<CppType, Count> MemberName{};                                                              \
    private:                                                                                                    \
        struct FArdaShaderNextMemberId##MemberName {};                                                          \
        /** Appends metadata for the generated shader parameter array. */                                       \
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

/** Internal implementation for declaring a scalar shader parameter member. */
#define ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(Kind, BindingType, CppType, MemberName, Slot, Space, Visibility) \
        FArdaShaderMemberId##MemberName;                                                                        \
    public:                                                                                                      \
        /** Shader parameter member declared by the invoking wrapper macro. */                                  \
        CppType MemberName{};                                                                                    \
    private:                                                                                                     \
        struct FArdaShaderNextMemberId##MemberName {};                                                           \
        /** Appends metadata for the generated shader parameter member. */                                      \
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

/**
 * Begins a macro-authored shader parameter struct declaration.
 * @param StructType Name of the struct to declare.
 */
#define ARDA_BEGIN_SHADER_PARAMETER_STRUCT(StructType)                                                          \
    struct StructType                                                                                            \
    {                                                                                                            \
    public:                                                                                                      \
        /** Creates a value-initialized shader parameter struct. */                                              \
        StructType() = default;                                                                                  \
    private:                                                                                                     \
        using FArdaShaderThisStruct = StructType;                                                                \
        /** Stable name of the generated shader parameter struct. */                                             \
        static constexpr const char* FArdaShaderStructName = #StructType;                                       \
        struct FArdaShaderFirstMemberId {};                                                                      \
        /** Initializes recursive metadata collection for the generated struct. */                              \
        static void FArdaAppendShaderMembers(                                                                    \
            FArdaShaderFirstMemberId,                                                                            \
            eastl::vector<::arda::backend::FArdaShaderParameterMember>&) {}                                     \
        typedef FArdaShaderFirstMemberId

/** Ends a shader parameter struct and defines its static metadata accessor. */
#define ARDA_END_SHADER_PARAMETER_STRUCT()                                                                       \
        FArdaShaderLastMemberId;                                                                                 \
    public:                                                                                                      \
        /** @return Immutable metadata for this shader parameter struct. */                                     \
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

/**
 * Declares an unbound value member in a shader parameter struct.
 * @param CppType C++ value type.
 * @param MemberName C++ member name.
 */
#define ARDA_SHADER_PARAMETER_VALUE(CppType, MemberName)                                                        \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::Value,                                                       \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, CppType, MemberName, 0, 0,                               \
        ::arda::rhi::EArdaRHIShaderStage::None)

/** Declares a texture SRV member with the specified name, slot, space, and visibility. */
#define ARDA_SHADER_TEXTURE_SRV(MemberName, Slot, Space, Visibility)                                            \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::TextureSRV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Visibility)
/** Declares a texture UAV member with the specified name, slot, space, and visibility. */
#define ARDA_SHADER_TEXTURE_UAV(MemberName, Slot, Space, Visibility)                                            \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::TextureUAV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureUAV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Visibility)
/** Declares a structured-buffer SRV member with the specified binding. */
#define ARDA_SHADER_BUFFER_SRV(MemberName, Slot, Space, Visibility)                                             \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::BufferSRV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferSRV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Visibility)
/** Declares a structured-buffer UAV member with the specified binding. */
#define ARDA_SHADER_BUFFER_UAV(MemberName, Slot, Space, Visibility)                                             \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::BufferUAV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferUAV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Visibility)
/** Declares a constant-buffer member with the specified binding. */
#define ARDA_SHADER_CONSTANT_BUFFER(MemberName, Slot, Space, Visibility)                                        \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::ConstantBuffer,                                             \
        ::arda::rhi::EArdaRHIBindingType::ConstantBuffer, ::arda::rhi::FArdaRHIBufferRef,                      \
        MemberName, Slot, Space, Visibility)
/** Declares a typed uniform-buffer member with the specified binding. */
#define ARDA_SHADER_UNIFORM_BUFFER(MemberName, Slot, Space, Visibility)                                         \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::UniformBuffer,                                              \
        ::arda::rhi::EArdaRHIBindingType::ConstantBuffer, ::arda::rhi::FArdaRHIUniformBufferRef,               \
        MemberName, Slot, Space, Visibility)
/** Declares a sampler member with the specified binding. */
#define ARDA_SHADER_SAMPLER(MemberName, Slot, Space, Visibility)                                                \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::Sampler,                                                    \
        ::arda::rhi::EArdaRHIBindingType::Sampler, ::arda::rhi::FArdaRHISamplerRef,                            \
        MemberName, Slot, Space, Visibility)
/** Declares a ray-tracing acceleration-structure member with the specified binding. */
#define ARDA_SHADER_ACCELERATION_STRUCTURE(MemberName, Slot, Space, Visibility)                                 \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::AccelerationStructure,                                      \
        ::arda::rhi::EArdaRHIBindingType::RayTracingAccelStruct, ::arda::rhi::FArdaRHIAccelStructRef,          \
        MemberName, Slot, Space, Visibility)
/** Declares a fixed-size texture SRV array with the specified binding and count. */
#define ARDA_SHADER_TEXTURE_SRV_ARRAY(MemberName, Slot, Space, Count, Visibility)                               \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::TextureSRV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureSRV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a fixed-size texture UAV array with the specified binding and count. */
#define ARDA_SHADER_TEXTURE_UAV_ARRAY(MemberName, Slot, Space, Count, Visibility)                               \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::TextureUAV,                                                 \
        ::arda::rhi::EArdaRHIBindingType::TextureUAV, ::arda::rhi::FArdaRHITextureRef,                         \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a fixed-size structured-buffer SRV array with the specified binding and count. */
#define ARDA_SHADER_BUFFER_SRV_ARRAY(MemberName, Slot, Space, Count, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::BufferSRV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferSRV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a fixed-size structured-buffer UAV array with the specified binding and count. */
#define ARDA_SHADER_BUFFER_UAV_ARRAY(MemberName, Slot, Space, Count, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::BufferUAV,                                                  \
        ::arda::rhi::EArdaRHIBindingType::StructuredBufferUAV, ::arda::rhi::FArdaRHIBufferRef,                 \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a fixed-size sampler array with the specified binding and count. */
#define ARDA_SHADER_SAMPLER_ARRAY(MemberName, Slot, Space, Count, Visibility)                                   \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::Sampler,                                                    \
        ::arda::rhi::EArdaRHIBindingType::Sampler, ::arda::rhi::FArdaRHISamplerRef,                            \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a fixed-size acceleration-structure array with the specified binding and count. */
#define ARDA_SHADER_ACCELERATION_STRUCTURE_ARRAY(MemberName, Slot, Space, Count, Visibility)                    \
    ARDA_INTERNAL_SHADER_PARAMETER(                                                                              \
        ::arda::backend::EArdaShaderParameterKind::AccelerationStructure,                                      \
        ::arda::rhi::EArdaRHIBindingType::RayTracingAccelStruct, ::arda::rhi::FArdaRHIAccelStructRef,          \
        MemberName, Slot, Space, Count, Visibility)
/** Declares a typed push-constant block with the specified binding and visibility. */
#define ARDA_SHADER_PUSH_CONSTANTS(CppType, MemberName, Slot, Space, Visibility)                                \
    ARDA_INTERNAL_SHADER_PARAMETER_SCALAR(                                                                       \
        ::arda::backend::EArdaShaderParameterKind::PushConstants,                                              \
        ::arda::rhi::EArdaRHIBindingType::PushConstants, CppType, MemberName, Slot, Space, Visibility)

/**
 * Declares a nested shader parameter struct member.
 * @param StructType Macro-authored nested parameter struct type.
 * @param MemberName C++ member name.
 */
#define ARDA_SHADER_PARAMETER_STRUCT(StructType, MemberName)                                                    \
        FArdaShaderMemberId##MemberName;                                                                         \
    public:                                                                                                      \
        /** Nested shader parameter struct member. */                                                            \
        StructType MemberName{};                                                                                 \
    private:                                                                                                     \
        struct FArdaShaderNextMemberId##MemberName {};                                                           \
        /** Appends metadata for the generated nested shader parameter member. */                               \
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
