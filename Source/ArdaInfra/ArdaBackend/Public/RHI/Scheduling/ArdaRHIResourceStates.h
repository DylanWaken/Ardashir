/** @file ArdaRHIResourceStates.h
 * Declares ResourceStates definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Interop/ArdaRHINativeResourceTypes.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"

#include <cstdint>

namespace arda
{
	/** Pipeline domains participating in a resource transition. */
	enum class EArdaRHIPipeline : uint8_t
	{
		None = 0,
		Graphics = 1u << 0,
		AsyncCompute = 1u << 1,
		Copy = 1u << 2,
		All = 0x07
	};

	constexpr EArdaRHIPipeline operator|(EArdaRHIPipeline A, EArdaRHIPipeline B) noexcept
	{
		return static_cast<EArdaRHIPipeline>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIPipeline operator&(EArdaRHIPipeline A, EArdaRHIPipeline B) noexcept
	{
		return static_cast<EArdaRHIPipeline>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIPipeline& operator|=(EArdaRHIPipeline& A, EArdaRHIPipeline B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIPipeline Value, EArdaRHIPipeline Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Optional transition scheduling and lifetime semantics. */
	enum class EArdaRHITransitionFlags : uint8_t
	{
		None = 0,
		BeginOnly = 1u << 0,
		EndOnly = 1u << 1,
		Discard = 1u << 2
	};

	constexpr EArdaRHITransitionFlags operator|(EArdaRHITransitionFlags A, EArdaRHITransitionFlags B) noexcept
	{
		return static_cast<EArdaRHITransitionFlags>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHITransitionFlags operator&(EArdaRHITransitionFlags A, EArdaRHITransitionFlags B) noexcept
	{
		return static_cast<EArdaRHITransitionFlags>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHITransitionFlags& operator|=(EArdaRHITransitionFlags& A, EArdaRHITransitionFlags B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHITransitionFlags Value, EArdaRHITransitionFlags Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Enumerates resource state values. */
	enum class EArdaRHIResourceState : uint32_t
	{
		Unknown = 0,
		Common = 1u << 0,
		ConstantBuffer = 1u << 1,
		VertexBuffer = 1u << 2,
		IndexBuffer = 1u << 3,
		IndirectArgument = 1u << 4,
		PixelShaderResource = 1u << 5,
		NonPixelShaderResource = 1u << 6,
		ShaderResource = (1u << 5) | (1u << 6),
		UnorderedAccess = 1u << 7,
		RenderTarget = 1u << 8,
		DepthWrite = 1u << 9,
		DepthRead = 1u << 10,
		CopyDest = 1u << 11,
		CopySource = 1u << 12,
		ResolveDest = 1u << 13,
		ResolveSource = 1u << 14,
		Present = 1u << 15,
		AccelStructRead = 1u << 16,
		AccelStructWrite = 1u << 17,
		AccelStructBuildInput = 1u << 18,
		AccelStructBuildBlas = 1u << 19,
		CpuRead = 1u << 20,
		OpacityMicromapWrite = 1u << 21,
		OpacityMicromapBuildInput = 1u << 22,
		Discard = 1u << 23,
		ShadingRateSource = 1u << 24
	};

	constexpr EArdaRHIResourceState operator|(EArdaRHIResourceState A, EArdaRHIResourceState B) noexcept
	{
		return static_cast<EArdaRHIResourceState>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIResourceState operator&(EArdaRHIResourceState A, EArdaRHIResourceState B) noexcept
	{
		return static_cast<EArdaRHIResourceState>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIResourceState& operator|=(EArdaRHIResourceState& A, EArdaRHIResourceState B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIResourceState Value, EArdaRHIResourceState Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Describes the state independently tracked by a native backend. */
	struct FArdaRHINativeResourceState
	{
		/** Abstract Arda state represented by the backend tracker. */
		EArdaRHIResourceState mState = EArdaRHIResourceState::Unknown;
		/** Native resource representation that owns the encoded state. */
		EArdaRHINativeResourceType mNativeType = EArdaRHINativeResourceType::BackendDefined;
		/** D3D12 state bits or Vulkan image layout, depending on native type. */
		uint64_t mPrimaryState = 0;
		/** Native synchronization pipeline-stage mask, when applicable. */
		uint64_t mPipelineStageMask = 0;
		/** Native synchronization access mask, when applicable. */
		uint64_t mAccessMask = 0;
		/** Owning Vulkan queue family, or 0xffffffff for APIs without families. */
		uint32_t mQueueFamily = 0xffffffffu;
		/** Whether the backend has an authoritative state for the range. */
		bool mbKnown = false;
		/** Whether the encoded native values are valid for mState. */
		bool mbNativeCompatible = false;
	};

	/** Describes independently observed facade and native resource state. */
	struct FArdaRHIResourceStateSnapshot
	{
		/** State maintained by the common ArdaRHI facade tracker. */
		EArdaRHIResourceState mFacadeState = EArdaRHIResourceState::Unknown;
		/** Queue whose command list produced this observation. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		/** Queue that owns the resource after the recorded operation. */
		EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
		/** Whether facade queue ownership is authoritative. */
		bool mbFacadeQueueOwnerKnown = false;
		/** Native backend tracker and exact barrier encoding. */
		FArdaRHINativeResourceState mNative;
		/** Whether the facade has an authoritative state for the range. */
		bool mbFacadeKnown = false;

		/**
         * Tests whether every independently tracked layer agrees.
         * @return True when facade, backend, and native encoding are consistent.
         */
		[[nodiscard]] bool IsConsistent() const noexcept
		{
			return mbFacadeKnown && mNative.mbKnown && mNative.mbNativeCompatible && mFacadeState == mNative.mState;
		}
	};
}
