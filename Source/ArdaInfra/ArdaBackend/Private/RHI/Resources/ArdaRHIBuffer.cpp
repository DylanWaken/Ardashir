#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include "ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		template <typename Descriptor, typename Usage>
		bool AllowsInitialState(const Descriptor& Desc, EArdaRHIResourceState State, Usage RequiredUsage)
		{
			return !HasAnyFlags(Desc.mInitialState, State) || HasAnyFlags(Desc.mUsage, RequiredUsage);
		}
	}

	FArdaRHIBufferRange FArdaRHIBufferRange::Resolve(const FArdaRHIBufferDesc& Desc) const noexcept
	{
		FArdaRHIBufferRange Result = *this;
		Result.mByteOffset = eastl::min(Result.mByteOffset, Desc.mByteSize);
		Result.mByteSize = eastl::min(Result.mByteSize, Desc.mByteSize - Result.mByteOffset);
		return Result;
	}

	bool FArdaRHIBufferRange::IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept
	{
		const auto Result = Resolve(Desc);
		return Result.mByteOffset == 0 && Result.mByteSize == Desc.mByteSize;
	}

	bool FArdaRHIBufferDesc::operator==(const FArdaRHIBufferDesc& O) const noexcept
	{
		return mbCudaInterop == O.mbCudaInterop && mByteSize == O.mByteSize && mStructureStride == O.mStructureStride &&
		    mMaxVersions == O.mMaxVersions && mFormat == O.mFormat && mUsage == O.mUsage &&
		    mCpuAccess == O.mCpuAccess && mInitialState == O.mInitialState &&
		    mbKeepInitialState == O.mbKeepInitialState && mbVirtual == O.mbVirtual && mbTiled == O.mbTiled &&
		    mDebugName == O.mDebugName;
	}

	size_t HashValue(const FArdaRHIBufferRange& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mByteOffset);
		ArdaHashCombine(H, V.mByteSize);
		return H;
	}

	size_t HashValue(const FArdaRHIBufferDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mByteSize);
		ArdaHashCombine(H, V.mStructureStride);
		ArdaHashCombine(H, V.mMaxVersions);
		ArdaHashCombine(H, static_cast<uint8_t>(V.mFormat));
		ArdaHashCombine(H, static_cast<uint16_t>(V.mUsage));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mCpuAccess));
		ArdaHashCombine(H, static_cast<uint32_t>(V.mInitialState));
		ArdaHashCombine(H, V.mbKeepInitialState);
		ArdaHashCombine(H, V.mbVirtual);
		ArdaHashCombine(H, V.mbTiled);
		ArdaHashString(H, V.mDebugName);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIBufferDesc& D) noexcept
	{
		if (!D.mByteSize)
		{
			return Invalid("Buffer size must be non-zero.");
		}
		constexpr auto KnownUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess |
		    EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::Index | EArdaRHIBufferUsage::Constant |
		    EArdaRHIBufferUsage::Indirect | EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured |
		    EArdaRHIBufferUsage::Volatile | EArdaRHIBufferUsage::AccelStructBuildInput |
		    EArdaRHIBufferUsage::AccelStructStorage | EArdaRHIBufferUsage::ShaderBindingTable |
		    EArdaRHIBufferUsage::OpacityMicromapBuildInput;
		if ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(KnownUsage)) != 0 ||
		    D.mCpuAccess > EArdaRHICpuAccess::Write || D.mFormat >= EArdaRHIFormat::Count)
		{
			return Invalid("Buffer usage, CPU access, or format is invalid.");
		}
		if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Structured) &&
		    (!D.mStructureStride || D.mStructureStride % 4 != 0 || D.mStructureStride > D.mByteSize))
		{
			return Invalid("Structured buffers require a DWORD-aligned stride and storage for at least one element.");
		}
		if (D.mFormat != EArdaRHIFormat::Unknown &&
		    (GetArdaRHIFormatInfo(D.mFormat).mbDepth || !GetArdaRHIFormatElementSize(D.mFormat)))
		{
			return Invalid("Typed buffer formats must be uncompressed color formats.");
		}
		if (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Volatile) && !D.mMaxVersions)
		{
			return Invalid("Volatile buffers require a non-zero max version count.");
		}
		constexpr auto BufferStates = EArdaRHIResourceState::Common | EArdaRHIResourceState::ConstantBuffer |
		    EArdaRHIResourceState::VertexBuffer | EArdaRHIResourceState::IndexBuffer |
		    EArdaRHIResourceState::IndirectArgument | EArdaRHIResourceState::ShaderResource |
		    EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::CopySource |
		    EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::AccelStructRead |
		    EArdaRHIResourceState::AccelStructWrite | EArdaRHIResourceState::AccelStructBuildInput |
		    EArdaRHIResourceState::AccelStructBuildBlas | EArdaRHIResourceState::CpuRead |
		    EArdaRHIResourceState::OpacityMicromapWrite | EArdaRHIResourceState::OpacityMicromapBuildInput |
		    EArdaRHIResourceState::Discard;
		if ((static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(BufferStates)) != 0)
		{
			return Invalid("Buffer initial state contains an unknown or texture-only state.");
		}
		if (!AllowsInitialState(D, EArdaRHIResourceState::ShaderResource, EArdaRHIBufferUsage::ShaderResource) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::UnorderedAccess, EArdaRHIBufferUsage::UnorderedAccess) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::VertexBuffer, EArdaRHIBufferUsage::Vertex) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::IndexBuffer, EArdaRHIBufferUsage::Index) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::ConstantBuffer, EArdaRHIBufferUsage::Constant) ||
		    !AllowsInitialState(D, EArdaRHIResourceState::IndirectArgument, EArdaRHIBufferUsage::Indirect))
		{
			return Invalid("Buffer initial state requires its matching declared usage.");
		}
		if ((D.mbVirtual && D.mbTiled) || (D.mCpuAccess != EArdaRHICpuAccess::None && (D.mbTiled || D.mbCudaInterop)))
		{
			return Invalid("Buffer storage flags conflict with virtual, tiled, or CPU-visible storage.");
		}
		if (D.mCpuAccess == EArdaRHICpuAccess::Write &&
		    (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::AccelStructStorage) ||
		        HasAnyFlags(D.mInitialState,
		            EArdaRHIResourceState::UnorderedAccess | EArdaRHIResourceState::CopyDest |
		                EArdaRHIResourceState::AccelStructWrite | EArdaRHIResourceState::OpacityMicromapWrite)))
		{
			return Invalid("CPU-upload buffers cannot be GPU-write destinations.");
		}
		constexpr auto ReadbackStates =
		    EArdaRHIResourceState::Common | EArdaRHIResourceState::CopyDest | EArdaRHIResourceState::CpuRead;
		constexpr auto ReadbackUsage = EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured;
		if (D.mCpuAccess == EArdaRHICpuAccess::Read &&
		    ((static_cast<uint16_t>(D.mUsage) & ~static_cast<uint16_t>(ReadbackUsage)) != 0 ||
		        (static_cast<uint32_t>(D.mInitialState) & ~static_cast<uint32_t>(ReadbackStates)) != 0))
		{
			return Invalid("CPU-readback buffers only support copy destinations and CPU reads.");
		}
		return {};
	}

	FArdaRHIStatus ValidateResourceCapabilities(const FArdaRHIBufferDesc& D,
	    const FArdaRHICapabilities& C,
	    const FArdaRHIFormatSupport& F) noexcept
	{
		if (const auto Status = Validate(D); !Status)
		{
			return Status;
		}
		if ((C.mLimits.mMaxBufferSize && D.mByteSize > C.mLimits.mMaxBufferSize) ||
		    (D.mbVirtual && !C.mbVirtualResources) || (D.mbTiled && !C.mResidency.mbReservedBuffers))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Buffer capacity or requested storage mode is unsupported by the device.");
		}
		// Raw and structured views have no native typed format, even when the descriptor supplies a typed fallback.
		if (C.mbFormatSupportReported && D.mFormat != EArdaRHIFormat::Unknown &&
		    !HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured) &&
		    (!F.mNativeFormat ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::ShaderResource) && !F.mbBufferShaderResource) ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::UnorderedAccess) && !F.mbBufferStorage) ||
		        (HasAnyFlags(D.mUsage, EArdaRHIBufferUsage::Vertex) && !F.mbVertexBuffer)))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Typed buffer format does not support a requested usage on this device.");
		}
		return {};
	}
}
