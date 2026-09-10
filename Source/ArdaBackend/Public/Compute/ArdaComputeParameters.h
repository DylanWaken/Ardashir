/** @file ArdaComputeParameters.h
 * Host parameter metadata independent of shader registers and kernel argument ABIs.
 */
#pragma once
#include "RHI/ArdaRHICuda.h"
#include <EASTL/array.h>
#include <EASTL/functional.h>
#include <cstddef>
#include <type_traits>

namespace arda
{
    /** Value members are host data; Buffer/Texture members declare external dependencies. */
    enum class EArdaComputeParameterKind : uint8_t
    {
        /** Arbitrary host data, never marshaled automatically. */
        Value,
        /** Retained buffer view and declared access. */
        Buffer,
        /** Retained texture subresources and declared access. */
        Texture,
        /** Recursively described parameter struct. */
        NestedStruct
    };

    /** Retained buffer view; shapes and dtype/layout are application-defined value parameters. */
    struct FArdaComputeBufferParameter
    {
        /** Referenced buffer; null can denote an unused optional input. */
        FArdaRHIBufferRef mBuffer;
        /** Byte range used by the operation. */
        FArdaRHIBufferRange mRange;
    };
    /** Retained texture view without assuming CUDA surface or shader binding semantics. */
    struct FArdaComputeTextureParameter
    {
        /** Referenced texture; null can denote an unused optional input. */
        FArdaRHITextureRef mTexture;
        /** Mips and array slices used by the operation. */
        FArdaRHITextureSubresourceRange mRange;
    };
    class FArdaComputeParameterMetadata;
    /** Static description of one direct member, including fixed arrays and nested structs. */
    struct FArdaComputeParameterMember
    {
        /** Declared member name with static lifetime. */
        const char* mName = nullptr;
        /** Diagnostic C++ type spelling, not a serialization or kernel ABI. */
        const char* mCppType = nullptr;
        /** Host value, resource or nested struct. */
        EArdaComputeParameterKind mKind = EArdaComputeParameterKind::Value;
        /** Author-declared resource access; ignored for ordinary values. */
        EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
        /** Offset from the containing struct. */
        size_t mOffset = 0;
        /** Total member size including array elements. */
        size_t mSize = 0;
        /** Required element alignment. */
        size_t mAlignment = 1;
        /** Fixed element count; scalar members have count one. */
        size_t mElementCount = 1;
        /** Byte stride of one element. */
        size_t mElementStride = 0;
        /** Static nested metadata, or null for a leaf. */
        const FArdaComputeParameterMetadata* mNestedMetadata = nullptr;
    };
    /** Resolved leaf element, borrowed from a live C++ parameter instance. */
    struct FArdaComputeParameter
    {
        /** Static leaf metadata. */
        const FArdaComputeParameterMember* mMember = nullptr;
        /** Leaf value address, valid only while the parameter instance lives. */
        const void* mValue = nullptr;
        /** Dotted path with fixed-array indices. */
        eastl::string mPath;
        /** Offset from the root parameter instance. */
        size_t mAbsoluteOffset = 0;
    };
    /** Receives each borrowed leaf; do not retain its value address. */
    using FArdaComputeParameterVisitor = eastl::function<void(const FArdaComputeParameter&)>;
    /** Retained dependency declaration for a future graph import/resolve adapter. */
    struct FArdaComputeResourceAccess
    {
        /** Full parameter path. */
        eastl::string mPath;
        /** Buffer or Texture. */
        EArdaComputeParameterKind mKind = EArdaComputeParameterKind::Buffer;
        /** Read, Write or ReadWrite as declared by the author. */
        EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
        /** Retains the resource, or null for an optional/unbound field. */
        FArdaRHIResourceRef mResource;
        /** Original buffer range; whole-buffer sentinel is preserved. */
        FArdaRHIBufferRange mBufferRange;
        /** Original texture subresource range. */
        FArdaRHITextureSubresourceRange mTextureRange;
    };
    /** Immutable C++ layout metadata, with no compilation or GPU execution. */
    class FArdaComputeParameterMetadata final
    {
    public:
        /** Builds/validates metadata; names and nested metadata must outlive this object. */
        FArdaComputeParameterMetadata(const char* Name, size_t Size, size_t Alignment,
            eastl::vector<FArdaComputeParameterMember> Members);
        /** Returns the struct name. */
        [[nodiscard]] const char* GetName() const noexcept { return mName; }
        /** Returns C++ object size, not serialized size. */
        [[nodiscard]] size_t GetSize() const noexcept { return mSize; }
        /** Returns C++ alignment. */
        [[nodiscard]] size_t GetAlignment() const noexcept { return mAlignment; }
        /** Invalid metadata is never traversed. */
        [[nodiscard]] const FArdaRHIStatus& GetStatus() const noexcept { return mStatus; }
        /** Direct members in declaration order. */
        [[nodiscard]] const eastl::vector<FArdaComputeParameterMember>& GetMembers() const noexcept { return mMembers; }
        /** Finds a direct member by name, or returns null. */
        [[nodiscard]] const FArdaComputeParameterMember* FindMember(const char* Name) const noexcept;
        /** Recursively visits leaf elements. Parameters must be a live instance of this exact
         * type. Null/misaligned input or an empty visitor returns InvalidArgument. Value members
         * are not searched for hidden pointers/resources; declare dependencies explicitly.
         * @ownership Borrows the parameter instance and its leaf addresses during visitation.
         * @threading Metadata is immutable; callers keep the visited instance stable.
         */
        [[nodiscard]] FArdaRHIStatus Enumerate(const void* Parameters, const FArdaComputeParameterVisitor& Visitor) const;
        /** Replaces OutAccesses with retained resource declarations, including null optional
         * fields; preserves ranges and aliases without merging. Failure leaves output unchanged.
         * This does not validate required inputs, ranges, device or CUDA sharing: dispatch owns
         * those checks. It does not register an RDG pass or automatically marshal kernel arguments.
         * @ownership Output retains resource references independently of the parameter object.
         * @threading Metadata is immutable; synchronize parameter mutations and output access.
         */
        [[nodiscard]] FArdaRHIStatus GetResourceAccesses(const void* Parameters,
            eastl::vector<FArdaComputeResourceAccess>& OutAccesses) const;
    private:
        void EnumerateInternal(const uint8_t* Root, size_t Offset, const eastl::string& Prefix,
            const FArdaComputeParameterVisitor& Visitor) const;
        const char* mName;
        size_t mSize;
        size_t mAlignment;
        eastl::vector<FArdaComputeParameterMember> mMembers;
        FArdaRHIStatus mStatus;
    };
}

/** Begins a user-defined parameter struct. */
#define ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(StructType) \
    struct StructType { \
    private: \
        using FArdaComputeThisStruct = StructType; \
        static constexpr const char* FArdaComputeStructName = #StructType; \
        struct FArdaComputeFirstMemberId {}; \
        static void FArdaAppendComputeMembers(FArdaComputeFirstMemberId, eastl::vector<::arda::FArdaComputeParameterMember>&) {} \
        typedef FArdaComputeFirstMemberId

/** Internal member declaration and static metadata collection. */
#define ARDA_INTERNAL_COMPUTE_PARAMETER(Kind, Access, ElementType, DeclaredType, MemberName, Count, Nested) \
        FArdaComputeMemberId##MemberName; \
    public: \
        DeclaredType MemberName{}; \
    private: \
        static_assert((Count) > 0, "Compute parameter arrays cannot be empty."); \
        struct FArdaComputeNextMemberId##MemberName {}; \
        static void FArdaAppendComputeMembers(FArdaComputeNextMemberId##MemberName, eastl::vector<::arda::FArdaComputeParameterMember>& Members) { \
            FArdaAppendComputeMembers(FArdaComputeMemberId##MemberName{}, Members); \
            Members.push_back({#MemberName, #ElementType, Kind, Access, offsetof(FArdaComputeThisStruct, MemberName), \
                sizeof(DeclaredType), alignof(ElementType), Count, sizeof(ElementType), Nested}); \
        } \
        typedef FArdaComputeNextMemberId##MemberName

/** Ends a parameter struct and exposes its static metadata. */
#define ARDA_END_COMPUTE_PARAMETER_STRUCT() \
        FArdaComputeLastMemberId; \
    public: \
        static const ::arda::FArdaComputeParameterMetadata& GetStaticMetadata() { \
            static_assert(std::is_standard_layout_v<FArdaComputeThisStruct>, "Compute parameter structs must have standard layout."); \
            static const ::arda::FArdaComputeParameterMetadata Metadata = [] { \
                eastl::vector<::arda::FArdaComputeParameterMember> Members; \
                FArdaAppendComputeMembers(FArdaComputeLastMemberId{}, Members); \
                return ::arda::FArdaComputeParameterMetadata(FArdaComputeStructName, sizeof(FArdaComputeThisStruct), alignof(FArdaComputeThisStruct), eastl::move(Members)); \
            }(); \
            return Metadata; \
        } \
    };

/** Declares arbitrary host data; it is not packed into kernel arguments automatically. */
#define ARDA_COMPUTE_PARAMETER(CppType, MemberName) \
    ARDA_INTERNAL_COMPUTE_PARAMETER(::arda::EArdaComputeParameterKind::Value, ::arda::EArdaComputeAccess::Read, CppType, CppType, MemberName, 1, nullptr)
/** Declares a buffer view with author-defined access. */
#define ARDA_COMPUTE_BUFFER(MemberName, Access) \
    ARDA_INTERNAL_COMPUTE_PARAMETER(::arda::EArdaComputeParameterKind::Buffer, Access, ::arda::FArdaComputeBufferParameter, ::arda::FArdaComputeBufferParameter, MemberName, 1, nullptr)
/** Declares a texture view with author-defined access. */
#define ARDA_COMPUTE_TEXTURE(MemberName, Access) \
    ARDA_INTERNAL_COMPUTE_PARAMETER(::arda::EArdaComputeParameterKind::Texture, Access, ::arda::FArdaComputeTextureParameter, ::arda::FArdaComputeTextureParameter, MemberName, 1, nullptr)
/** Declares recursively inspected nested parameters. */
#define ARDA_COMPUTE_PARAMETER_STRUCT(StructType, MemberName) \
    ARDA_INTERNAL_COMPUTE_PARAMETER(::arda::EArdaComputeParameterKind::NestedStruct, ::arda::EArdaComputeAccess::Read, StructType, StructType, MemberName, 1, &StructType::GetStaticMetadata())
/** Internal array alias keeps comma-containing array types out of macro argument lists. */
#define ARDA_INTERNAL_COMPUTE_ARRAY(Kind, Access, CppType, MemberName, Count, Nested) \
        FArdaComputeArrayAnchor##MemberName; \
        using FArdaComputeArray##MemberName = eastl::array<CppType, Count>; \
        typedef FArdaComputeArrayAnchor##MemberName \
    ARDA_INTERNAL_COMPUTE_PARAMETER(Kind, Access, CppType, FArdaComputeArray##MemberName, MemberName, Count, Nested)
/** Declares a fixed host-value array. */
#define ARDA_COMPUTE_PARAMETER_ARRAY(CppType, MemberName, Count) \
    ARDA_INTERNAL_COMPUTE_ARRAY(::arda::EArdaComputeParameterKind::Value, ::arda::EArdaComputeAccess::Read, CppType, MemberName, Count, nullptr)
/** Declares a fixed buffer-view array. */
#define ARDA_COMPUTE_BUFFER_ARRAY(MemberName, Count, Access) \
    ARDA_INTERNAL_COMPUTE_ARRAY(::arda::EArdaComputeParameterKind::Buffer, Access, ::arda::FArdaComputeBufferParameter, MemberName, Count, nullptr)
/** Declares a fixed texture-view array. */
#define ARDA_COMPUTE_TEXTURE_ARRAY(MemberName, Count, Access) \
    ARDA_INTERNAL_COMPUTE_ARRAY(::arda::EArdaComputeParameterKind::Texture, Access, ::arda::FArdaComputeTextureParameter, MemberName, Count, nullptr)
/** Declares a fixed array of recursively inspected parameter structs. */
#define ARDA_COMPUTE_PARAMETER_STRUCT_ARRAY(StructType, MemberName, Count) \
    ARDA_INTERNAL_COMPUTE_ARRAY(::arda::EArdaComputeParameterKind::NestedStruct, ::arda::EArdaComputeAccess::Read, StructType, MemberName, Count, &StructType::GetStaticMetadata())
