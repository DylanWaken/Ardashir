/** @file ArdaCudaParameters.h
 * One resource schema generates retained host parameters and a plain CUDA argument.
 * Eligibility, signatures, formats and resource availability are checked at runtime.
 */
#pragma once
#include "ArdaComputeParameters.h"
#include <cstring>
#include <typeinfo>

namespace arda
{
	/** Checks scalar channel interpretation, or explicit aggregate pixel storage size, at runtime.
     * Aggregate pixel layouts are author-defined; no color, normalization or channel packing is performed.
     */
	template <class T>
	bool IsArdaCudaSurfaceElementSupported(EArdaRHIFormat Format)
	{
		const auto Info = GetArdaCudaFormatInfo(Format);
		if (!std::is_trivially_copyable_v<T> || !std::is_standard_layout_v<T> || !Info.mChannels ||
		    sizeof(T) != size_t(Info.mChannels) * Info.mBits / 8)
		{
			return false;
		}
		if constexpr (std::is_arithmetic_v<T>)
		{
			const auto Scalar = std::is_floating_point_v<T> ? EArdaCudaScalarType::Float
			    : std::is_signed_v<T>                       ? EArdaCudaScalarType::SInt
			                                                : EArdaCudaScalarType::UInt;
			return !std::is_same_v<T, bool> && Info.mChannels == 1 && Info.mScalarType == Scalar;
		}
		return !std::is_pointer_v<T>;
	}

	/** Host representation of the shared parameter schema. */
	struct FArdaCudaHostDomain
	{
		template <class T>
		using Value = T;
		template <class T, EArdaComputeAccess Access>
		using Buffer = FArdaComputeBufferParameter;
		template <class T, EArdaComputeAccess Access>
		using Surface = FArdaComputeTextureParameter;
	};

	/** Device representation; unsupported values receive inert storage and fail runtime validation. */
	struct FArdaCudaDeviceDomain
	{
		template <class T>
		using Value = std::conditional_t<std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>, T, uint64_t>;
		template <class T, EArdaComputeAccess Access>
		using Buffer = std::conditional_t<Access == EArdaComputeAccess::Read, const T*, T*>;
		template <class T, EArdaComputeAccess Access>
		using Surface = uint64_t;
	};

	/** One host member and its corresponding CUDA argument location. */
	struct FArdaCudaParameterMember
	{
		const char* mName = nullptr;
		EArdaComputeParameterKind mKind = EArdaComputeParameterKind::Value;
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		size_t mHostOffset = 0;
		size_t mCudaOffset = 0;
		size_t mHostSize = 0;
		size_t mCudaSize = 0;
		size_t mHostAlignment = 1;
		size_t mElementSize = 0;
		size_t mElementAlignment = 1;
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		bool mbSupported = false;
	};

	/** Immutable paired layouts and resource declarations derived from one schema. */
	class FArdaCudaParameterMetadata
	{
	public:
		/** Validates the generated layouts without accessing CUDA or a GPU. */
		FArdaCudaParameterMetadata(const char* Name,
		    size_t HostSize,
		    size_t HostAlignment,
		    FArdaCudaKernelSignature Signature,
		    bool HostLayoutSupported,
		    eastl::vector<FArdaCudaParameterMember> Members);

		/** Reports unsupported host/device field types and malformed layouts at runtime. */
		const FArdaRHIStatus& GetStatus() const noexcept
		{
			return mStatus;
		}

		/** Host resource enumeration for ownership and scheduling. */
		const FArdaComputeParameterMetadata& GetHostMetadata() const noexcept
		{
			return mHost;
		}

		/** Exact single-argument kernel contract. */
		FArdaCudaKernelSignature GetSignature() const noexcept
		{
			return mSignature;
		}

		/** Paired member locations, in declaration order. */
		const eastl::vector<FArdaCudaParameterMember>& GetMembers() const noexcept
		{
			return mMembers;
		}

		/** Freezes plain values and resource patches, retaining resources independently of Parameters.
         * Checks sharing, ranges, element alignment and surface format; the RHI checks device ownership.
         * No native addresses are resolved and no GPU work is issued. Output changes only on success.
         */
		FArdaRHIStatus Prepare(const void* Parameters, FArdaCudaDispatch& Output) const;

	private:
		FArdaCudaKernelSignature mSignature;
		eastl::vector<FArdaCudaParameterMember> mMembers;
		FArdaComputeParameterMetadata mHost;
		FArdaRHIStatus mStatus;
	};
}

/** Internal schema expansion: values remain host values; resources depend on Domain. */
#define ARDA_INTERNAL_CUDA_VALUE(T, Name) typename Domain::template Value<T> Name{};
#define ARDA_INTERNAL_CUDA_BUFFER(T, Name, Access) typename Domain::template Buffer<T, Access> Name{};
#define ARDA_INTERNAL_CUDA_SURFACE(T, Name, Access, Format) typename Domain::template Surface<T, Access> Name{};
#define ARDA_INTERNAL_CUDA_VALUE_INFO(T, Name)                                                                         \
	{#Name,                                                                                                            \
	    ::arda::EArdaComputeParameterKind::Value,                                                                      \
	    ::arda::EArdaComputeAccess::Read,                                                                              \
	    offsetof(FParameters, Name),                                                                                   \
	    offsetof(FCuda, Name),                                                                                         \
	    sizeof(T),                                                                                                     \
	    sizeof(typename FCuda::FDomain::template Value<T>),                                                            \
	    alignof(T),                                                                                                    \
	    sizeof(T),                                                                                                     \
	    alignof(T),                                                                                                    \
	    ::arda::EArdaRHIFormat::Unknown,                                                                               \
	    std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> && !std::is_pointer_v<T>},
#define ARDA_INTERNAL_CUDA_BUFFER_INFO(T, Name, Access)                                                                \
	{#Name,                                                                                                            \
	    ::arda::EArdaComputeParameterKind::Buffer,                                                                     \
	    Access,                                                                                                        \
	    offsetof(FParameters, Name),                                                                                   \
	    offsetof(FCuda, Name),                                                                                         \
	    sizeof(::arda::FArdaComputeBufferParameter),                                                                   \
	    sizeof(uint64_t),                                                                                              \
	    alignof(::arda::FArdaComputeBufferParameter),                                                                  \
	    sizeof(T),                                                                                                     \
	    alignof(T),                                                                                                    \
	    ::arda::EArdaRHIFormat::Unknown,                                                                               \
	    std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>},
#define ARDA_INTERNAL_CUDA_SURFACE_INFO(T, Name, Access, Format)                                                       \
	{#Name,                                                                                                            \
	    ::arda::EArdaComputeParameterKind::Texture,                                                                    \
	    Access,                                                                                                        \
	    offsetof(FParameters, Name),                                                                                   \
	    offsetof(FCuda, Name),                                                                                         \
	    sizeof(::arda::FArdaComputeTextureParameter),                                                                  \
	    sizeof(uint64_t),                                                                                              \
	    alignof(::arda::FArdaComputeTextureParameter),                                                                 \
	    sizeof(T),                                                                                                     \
	    alignof(T),                                                                                                    \
	    Format,                                                                                                        \
	    ::arda::IsArdaCudaSurfaceElementSupported<T>(Format)},
#define ARDA_INTERNAL_CUDA_VISIT_VALUE(T, Name) Visitor.template Value<T>(#Name, offsetof(FSelf, Name));
#define ARDA_INTERNAL_CUDA_VISIT_BUFFER(T, Name, Access)                                                               \
	Visitor.template Buffer<T, Access>(#Name, offsetof(FSelf, Name));
#define ARDA_INTERNAL_CUDA_VISIT_SURFACE(T, Name, Access, Format)                                                      \
	Visitor.template Surface<T, Access>(#Name, offsetof(FSelf, Name), Format);

/** Declares a paired parameter schema. Fields is a macro accepting VALUE, BUFFER and SURFACE.
 * BUFFER declares an element type/name/access; SURFACE also declares its exact storage format.
 * Rebind permits a scheduler to provide logical-resource storage without changing the CUDA ABI.
 */
#define ARDA_CUDA_PARAMETER_STRUCT(Name, Fields)                                                                       \
	template <class Domain>                                                                                            \
	struct Name##Storage                                                                                               \
	{                                                                                                                  \
		using FSelf = Name##Storage<Domain>;                                                                           \
		using FDomain = Domain;                                                                                        \
		using FParameters = Name##Storage<::arda::FArdaCudaHostDomain>;                                                \
		using FCuda = Name##Storage<::arda::FArdaCudaDeviceDomain>;                                                    \
		template <class OtherDomain>                                                                                   \
		using Rebind = Name##Storage<OtherDomain>;                                                                     \
		Fields(ARDA_INTERNAL_CUDA_VALUE,                                                                               \
		    ARDA_INTERNAL_CUDA_BUFFER,                                                                                 \
		    ARDA_INTERNAL_CUDA_SURFACE) static const ::arda::FArdaCudaParameterMetadata& GetCudaMetadata()             \
		{                                                                                                              \
			static const ::arda::FArdaCudaParameterMetadata Metadata(#Name,                                            \
			    sizeof(FParameters),                                                                                   \
			    alignof(FParameters),                                                                                  \
			    {&typeid(FCuda),                                                                                       \
			        sizeof(FCuda),                                                                                     \
			        alignof(FCuda),                                                                                    \
			        std::is_trivially_copyable_v<FCuda> && std::is_standard_layout_v<FCuda>},                          \
			    std::is_standard_layout_v<FParameters>,                                                                \
			    {Fields(ARDA_INTERNAL_CUDA_VALUE_INFO,                                                                 \
			        ARDA_INTERNAL_CUDA_BUFFER_INFO,                                                                    \
			        ARDA_INTERNAL_CUDA_SURFACE_INFO)});                                                                \
			return Metadata;                                                                                           \
		}                                                                                                              \
		static const ::arda::FArdaComputeParameterMetadata& GetStaticMetadata()                                        \
		{                                                                                                              \
			return GetCudaMetadata().GetHostMetadata();                                                                \
		}                                                                                                              \
		template <class V>                                                                                             \
		static void VisitMembers(V& Visitor)                                                                           \
		{                                                                                                              \
			Fields(ARDA_INTERNAL_CUDA_VISIT_VALUE, ARDA_INTERNAL_CUDA_VISIT_BUFFER, ARDA_INTERNAL_CUDA_VISIT_SURFACE)  \
		}                                                                                                              \
	};                                                                                                                 \
	using Name = Name##Storage<::arda::FArdaCudaHostDomain>;
