/** @file ArdaRHIAccelerationStructures.h
 * Declares AccelerationStructures definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Enumerates ray tracing geometry type values. */
	enum class EArdaRHIRayTracingGeometryType : uint8_t
	{
		Triangles,
		AABBs
	};

	/** Enumerates opacity micromap format values. */
	enum class EArdaRHIOpacityMicromapFormat : uint8_t
	{
		TwoState = 1,
		FourState = 2
	};

	/** Enumerates ray tracing geometry flags values. */
	enum class EArdaRHIRayTracingGeometryFlags : uint8_t
	{
		None = 0,
		Opaque = 1u << 0,
		NoDuplicateAnyHitInvocation = 1u << 1
	};

	constexpr EArdaRHIRayTracingGeometryFlags operator|(EArdaRHIRayTracingGeometryFlags A, EArdaRHIRayTracingGeometryFlags B) noexcept
	{
		return static_cast<EArdaRHIRayTracingGeometryFlags>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIRayTracingGeometryFlags operator&(EArdaRHIRayTracingGeometryFlags A, EArdaRHIRayTracingGeometryFlags B) noexcept
	{
		return static_cast<EArdaRHIRayTracingGeometryFlags>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIRayTracingGeometryFlags& operator|=(EArdaRHIRayTracingGeometryFlags& A, EArdaRHIRayTracingGeometryFlags B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIRayTracingGeometryFlags Value, EArdaRHIRayTracingGeometryFlags Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Enumerates accel struct build flags values. */
	enum class EArdaRHIAccelStructBuildFlags : uint8_t
	{
		None = 0,
		AllowUpdate = 1u << 0,
		AllowCompaction = 1u << 1,
		PreferFastTrace = 1u << 2,
		PreferFastBuild = 1u << 3,
		MinimizeMemory = 1u << 4,
		PerformUpdate = 1u << 5,
		AllowEmptyInstances = 1u << 7
	};

	constexpr EArdaRHIAccelStructBuildFlags operator|(EArdaRHIAccelStructBuildFlags A, EArdaRHIAccelStructBuildFlags B) noexcept
	{
		return static_cast<EArdaRHIAccelStructBuildFlags>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIAccelStructBuildFlags operator&(EArdaRHIAccelStructBuildFlags A, EArdaRHIAccelStructBuildFlags B) noexcept
	{
		return static_cast<EArdaRHIAccelStructBuildFlags>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIAccelStructBuildFlags& operator|=(EArdaRHIAccelStructBuildFlags& A, EArdaRHIAccelStructBuildFlags B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIAccelStructBuildFlags Value, EArdaRHIAccelStructBuildFlags Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Enumerates opacity micromap build flags values. */
	enum class EArdaRHIOpacityMicromapBuildFlags : uint8_t
	{
		None = 0,
		FastTrace = 1u << 0,
		FastBuild = 1u << 1,
		AllowCompaction = 1u << 2
	};

	constexpr EArdaRHIOpacityMicromapBuildFlags operator|(EArdaRHIOpacityMicromapBuildFlags A, EArdaRHIOpacityMicromapBuildFlags B) noexcept
	{
		return static_cast<EArdaRHIOpacityMicromapBuildFlags>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIOpacityMicromapBuildFlags operator&(EArdaRHIOpacityMicromapBuildFlags A, EArdaRHIOpacityMicromapBuildFlags B) noexcept
	{
		return static_cast<EArdaRHIOpacityMicromapBuildFlags>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIOpacityMicromapBuildFlags& operator|=(EArdaRHIOpacityMicromapBuildFlags& A, EArdaRHIOpacityMicromapBuildFlags B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIOpacityMicromapBuildFlags Value, EArdaRHIOpacityMicromapBuildFlags Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Lifecycle state independently tracked for acceleration structures and micromaps. */
	enum class EArdaRHIAccelStructBuildState : uint8_t
	{
		Unbuilt,
		Built,
		Updated,
		Compacted
	};

	/** Describes opacity micromap usage count. */
	struct FArdaRHIOpacityMicromapUsageCount
	{
		/** Number of micromaps with this usage. */
		uint32_t mCount = 0;
		/** Subdivision level for this usage. */
		uint32_t mSubdivisionLevel = 0;
		/** Stores the format. */
		EArdaRHIOpacityMicromapFormat mFormat = EArdaRHIOpacityMicromapFormat::TwoState;
	};

	/** Describes opacity micromap desc. */
	struct FArdaRHIOpacityMicromapDesc
	{
		/** Stores the flags. */
		EArdaRHIOpacityMicromapBuildFlags mFlags = EArdaRHIOpacityMicromapBuildFlags::None;
		/** Stores the counts. */
		eastl::vector<FArdaRHIOpacityMicromapUsageCount> mCounts;
		/** Stores the input buffer. */
		FArdaRHIBufferRef mInputBuffer;
		/** Stores the input buffer offset. */
		uint64_t mInputBufferOffset = 0;
		/** Stores the per micromap desc buffer. */
		FArdaRHIBufferRef mPerMicromapDescBuffer;
		/** Stores the per micromap desc buffer offset. */
		uint64_t mPerMicromapDescBufferOffset = 0;
		/** Optional storage size for a destination created from a compacted-size query. */
		uint64_t mResultSizeOverride = 0;
		/** Stores the track liveness. */
		bool mbTrackLiveness = true;
		/** Must be true when disabling backend liveness tracking. */
		bool mbAllowUnsafeLivenessOptOut = false;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for opacity micromap. */
	class IArdaRHIOpacityMicromap : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIOpacityMicromapDesc& GetDesc() const noexcept = 0;

		/**
         * Tests whether the compacted.
         * @return True when the condition is satisfied; otherwise false.
         */
		[[nodiscard]] virtual bool IsCompacted() const noexcept = 0;

		/**
         * Returns the device address.
         * @return The requested numeric value.
         */
		[[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;

		/** Native micromap handle identity used by conformance diagnostics. */
		[[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;

		/** Last successfully submitted build lifecycle state. */
		[[nodiscard]] virtual EArdaRHIAccelStructBuildState GetBuildState() const noexcept
		{
			return EArdaRHIAccelStructBuildState::Unbuilt;
		}
	};

	/** Describes ray tracing geometry desc. */
	struct FArdaRHIRayTracingGeometryDesc
	{
		/** Stores the type. */
		EArdaRHIRayTracingGeometryType mType = EArdaRHIRayTracingGeometryType::Triangles;
		/** Stores the flags. */
		EArdaRHIRayTracingGeometryFlags mFlags = EArdaRHIRayTracingGeometryFlags::None;
		/** Stores the index buffer. */
		FArdaRHIBufferRef mIndexBuffer;
		/** Stores the vertex or aabbbuffer. */
		FArdaRHIBufferRef mVertexOrAABBBuffer;
		/** Stores the index format. */
		EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
		/** Stores the vertex format. */
		EArdaRHIFormat mVertexFormat = EArdaRHIFormat::Unknown;
		/** Byte offset into the index buffer. */
		uint64_t mIndexOffset = 0;
		/** Byte offset into the vertex or AABB buffer. */
		uint64_t mVertexOrAABBOffset = 0;
		/** Number of indices. */
		uint32_t mIndexCount = 0;
		/** Number of vertices or AABBs. */
		uint32_t mVertexOrAABBCount = 0;
		/** Vertex or AABB element stride in bytes. */
		uint32_t mStride = 0;
		/** Stores the opacity micromap. */
		FArdaRHIOpacityMicromapRef mOpacityMicromap;
		/** Stores the opacity micromap index buffer. */
		FArdaRHIBufferRef mOpacityMicromapIndexBuffer;
		/** Stores the opacity micromap index offset. */
		uint64_t mOpacityMicromapIndexOffset = 0;
		/** Stores the opacity micromap index format. */
		EArdaRHIFormat mOpacityMicromapIndexFormat = EArdaRHIFormat::Unknown;
		/** Stores the opacity micromap usage counts. */
		eastl::vector<FArdaRHIOpacityMicromapUsageCount> mOpacityMicromapUsageCounts;
	};

	/** Describes accel struct desc. */
	struct FArdaRHIAccelStructDesc
	{
		/** Stores the top level max instances. */
		size_t mTopLevelMaxInstances = 0;
		/** Stores the bottom level geometries. */
		eastl::vector<FArdaRHIRayTracingGeometryDesc> mBottomLevelGeometries;
		/** Stores the build flags. */
		EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::None;
		/** Stores the track liveness. */
		bool mbTrackLiveness = true;
		/** Must be true when disabling backend liveness tracking. */
		bool mbAllowUnsafeLivenessOptOut = false;
		/** Stores the top level. */
		bool mbTopLevel = false;
		/** Stores the virtual. */
		bool mbVirtual = false;
		/** Optional exact result size, used for a compacted destination. */
		uint64_t mResultSizeOverride = 0;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Native sizes required to build or update an acceleration structure. */
	struct FArdaRHIAccelStructMemoryRequirements
	{
		uint64_t mResultSize = 0;
		uint64_t mBuildScratchSize = 0;
		uint64_t mUpdateScratchSize = 0;
		uint64_t mResultAlignment = 0;
		uint64_t mScratchAlignment = 0;
	};

	/** Interface for accel struct. */
	class IArdaRHIAccelStruct : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIAccelStructDesc& GetDesc() const noexcept = 0;

		/**
         * Tests whether the compacted.
         * @return True when the condition is satisfied; otherwise false.
         */
		[[nodiscard]] virtual bool IsCompacted() const noexcept = 0;

		/**
         * Returns the device address.
         * @return The requested numeric value.
         */
		[[nodiscard]] virtual uint64_t GetDeviceAddress() const noexcept = 0;

		/** Returns the physical identity. */
		[[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;

		/** Last successfully submitted build/compact lifecycle state. */
		[[nodiscard]] virtual EArdaRHIAccelStructBuildState GetBuildState() const noexcept
		{
			return EArdaRHIAccelStructBuildState::Unbuilt;
		}
	};

	/** Describes ray tracing instance desc. */
	struct FArdaRHIRayTracingInstanceDesc
	{
		/** Stores the transform. */
		float mTransform[12] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f};
		/** Application-defined instance identifier. */
		uint32_t mInstanceId = 0;
		/** Visibility mask for the instance. */
		uint32_t mInstanceMask = 0xff;
		/** Hit-group table contribution for the instance. */
		uint32_t mHitGroupContribution = 0;
		/** Backend ray-tracing instance flags. */
		uint32_t mFlags = 0;
		/** Stores the bottom level accel struct. */
		FArdaRHIAccelStructRef mBottomLevelAccelStruct;
	};
}
