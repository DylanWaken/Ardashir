/** @file ArdaRHIBuffer.h
 * Declares Buffer definitions for the RHI resources module.
 */

#pragma once
#include <EASTL/numeric_limits.h>

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include <EASTL/string.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Forward declaration of buffer desc. */
	struct FArdaRHIBufferDesc;

	/**
     * Performs the max operation.
     * @return The requested numeric value.
     */
	inline constexpr uint64_t ArdaRHIWholeBuffer = eastl::numeric_limits<uint64_t>::max();

	/** Enumerates buffer usage values. */
	enum class EArdaRHIBufferUsage : uint16_t
	{
		None = 0,
		ShaderResource = 1u << 0,
		UnorderedAccess = 1u << 1,
		Vertex = 1u << 2,
		Index = 1u << 3,
		Constant = 1u << 4,
		Indirect = 1u << 5,
		Raw = 1u << 6,
		Structured = 1u << 7,
		Volatile = 1u << 8,
		AccelStructBuildInput = 1u << 9,
		AccelStructStorage = 1u << 10,
		ShaderBindingTable = 1u << 11,
		OpacityMicromapBuildInput = 1u << 12
	};

	constexpr EArdaRHIBufferUsage operator|(EArdaRHIBufferUsage A, EArdaRHIBufferUsage B) noexcept
	{
		return static_cast<EArdaRHIBufferUsage>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIBufferUsage operator&(EArdaRHIBufferUsage A, EArdaRHIBufferUsage B) noexcept
	{
		return static_cast<EArdaRHIBufferUsage>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
	}
	constexpr EArdaRHIBufferUsage& operator|=(EArdaRHIBufferUsage& A, EArdaRHIBufferUsage B) noexcept
	{
		A = A | B;
		return A;
	}
	constexpr bool HasAnyFlags(EArdaRHIBufferUsage Value, EArdaRHIBufferUsage Flags) noexcept
	{
		return static_cast<uint32_t>(Value & Flags) != 0;
	}

	/** Describes buffer range. */
	struct FArdaRHIBufferRange
	{
		/** Stores the byte offset. */
		uint64_t mByteOffset = 0;
		/** Stores the byte size. */
		uint64_t mByteSize = ArdaRHIWholeBuffer;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBufferRange& O) const noexcept
		{
			return mByteOffset == O.mByteOffset && mByteSize == O.mByteSize;
		}

		/**
         * Performs the resolve operation.
         * @param Desc The desc.
         * @return The requested value.
         */
		[[nodiscard]] FArdaRHIBufferRange Resolve(const FArdaRHIBufferDesc& Desc) const noexcept;

		/**
         * Tests whether the whole buffer.
         * @param Desc The desc.
         * @return True when the condition is satisfied; otherwise false.
         */
		[[nodiscard]] bool IsWholeBuffer(const FArdaRHIBufferDesc& Desc) const noexcept;
	};

	/** Describes buffer desc. */
	struct FArdaRHIBufferDesc
	{
		/** Stores the byte size. */
		uint64_t mByteSize = 0;
		/** Structured element stride in bytes; DWORD-aligned and no larger than the allocation when Structured. */
		uint32_t mStructureStride = 0;
		/** Maximum number of backing-buffer versions. */
		uint32_t mMaxVersions = 0;
		/** Stores the format. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		/** Stores the usage. */
		EArdaRHIBufferUsage mUsage = EArdaRHIBufferUsage::None;
		/** Portable heap access: Write permits GPU reads; Read permits copy destinations and CPU reads. */
		EArdaRHICpuAccess mCpuAccess = EArdaRHICpuAccess::None;
		/** Stores the initial state. */
		EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Common;
		/** Stores the keep initial state. */
		bool mbKeepInitialState = false;
		/** Stores the virtual. */
		bool mbVirtual = false;
		/** Creates a sparse/reserved buffer committed in physical tiles. */
		bool mbTiled = false;
		/** Stores the debug name. */
		eastl::string mDebugName;
		/** Request CUDA-compatible native allocation; not valid for transient placed storage. */
		bool mbCudaInterop = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBufferDesc& O) const noexcept;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIBufferRange& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIBufferDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBufferDesc& Value) noexcept;

	/** Interface for buffer. */
	class IArdaRHIBuffer : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIBufferDesc& GetDesc() const noexcept = 0;

		/** Returns the physical identity. */
		[[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
	};

	/** Describes uniform buffer desc. */
	struct FArdaRHIUniformBufferDesc
	{
		/** Stores the byte size. */
		size_t mByteSize = 0;
		/** Stores the max versions. */
		uint32_t mMaxVersions = 1;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Constant-buffer resource with a stable logical identity. */
	class IArdaRHIUniformBuffer : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIUniformBufferDesc& GetDesc() const noexcept = 0;

		/**
         * Returns the buffer.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIBufferRef& GetBuffer() const noexcept = 0;
	};
}
