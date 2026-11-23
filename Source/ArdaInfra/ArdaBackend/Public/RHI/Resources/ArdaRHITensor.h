/** @file ArdaRHITensor.h
 * Describes typed tensor views over ordinary RHI buffer storage.
 */
#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"

#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	// ================================
	// Tensor scalar types
	// ================================

	/** Stable scalar storage identities, independent of texture formats and native SDK enums.
	 * Values are append-only. Declaring a dtype does not imply provider arithmetic support.
	 */
	enum class EArdaRHIDataType : uint16_t
	{
		Unknown = 0,
		Bool8 = 1,
		Int8 = 2,
		UInt8 = 3,
		Int16 = 4,
		UInt16 = 5,
		Int32 = 6,
		UInt32 = 7,
		Int64 = 8,
		UInt64 = 9,
		Float16 = 10,
		BFloat16 = 11,
		Float32 = 12,
		Float64 = 13,
		Float8E4M3FN = 14,
		Float8E5M2 = 15,
		Complex64 = 16,
		Complex128 = 17
	};

	/** Numeric interpretation of one scalar storage type. */
	enum class EArdaRHIDataTypeKind : uint8_t
	{
		Unknown,
		Boolean,
		SignedInteger,
		UnsignedInteger,
		FloatingPoint,
		Complex
	};

	/** Scalar storage facts; packed sub-byte and quantized encodings require separate contracts. */
	struct FArdaRHIDataTypeInfo
	{
		/** Stable diagnostic name with static lifetime. */
		const char* mName = "unknown";
		/** Numeric category. */
		EArdaRHIDataTypeKind mKind = EArdaRHIDataTypeKind::Unknown;
		/** Bytes per element, or zero for an unsupported identity. */
		uint8_t mByteSize = 0;
		/** Required byte alignment of the element origin, or zero for an unsupported identity. */
		uint8_t mAlignment = 0;
	};

	/** Returns scalar storage facts; unknown enum values return the unknown descriptor. */
	[[nodiscard]] FArdaRHIDataTypeInfo GetArdaRHIDataTypeInfo(EArdaRHIDataType DataType) noexcept;

	// ================================
	// Tensor layouts and buffer views
	// ================================

	/** Concrete, owning tensor layout values without resource handles or native addresses.
	 * Serialize the individual fields, never the C++ object representation. Empty dimensions
	 * mean a scalar; any zero extent means no elements. Strides are explicit and measured in
	 * elements. Negative strides and read-only broadcast views are supported.
	 */
	struct FArdaRHITensorDesc
	{
		/** Scalar storage type, independent of any buffer's typed graphics format. */
		EArdaRHIDataType mDataType = EArdaRHIDataType::Unknown;
		/** Concrete unsigned extents in logical dimension order. */
		eastl::vector<uint64_t> mDimensions;
		/** Signed element strides; must have exactly one entry per dimension. */
		eastl::vector<int64_t> mStrides;
		/** Byte offset of logical element [0,...,0] from the buffer origin. */
		uint64_t mByteOffset = 0;

		/** Compares all layout values without considering a storage allocation. */
		[[nodiscard]] bool operator==(const FArdaRHITensorDesc& Other) const noexcept;
		/** Tests whether any layout value differs. */
		[[nodiscard]] bool operator!=(const FArdaRHITensorDesc& Other) const noexcept;
	};

	/** Retained tensor view; aliases share the existing buffer's identity and allocation. */
	struct FArdaRHITensorView
	{
		/** Retains the ordinary RHI buffer containing this view. */
		FArdaRHIBufferRef mBuffer;
		/** Typed layout within the buffer. */
		FArdaRHITensorDesc mDesc;
	};

	// ================================
	// Tensor layout queries and validation
	// ================================

	/** Returns the checked logical element count; scalar is one, empty tensor is zero.
	 * Rejects unknown dtype, mismatched rank, misaligned origin, and count overflow.
	 */
	[[nodiscard]] TArdaRHIResult<uint64_t> GetArdaRHITensorElementCount(const FArdaRHITensorDesc& Desc);

	/** Returns the smallest covering byte interval, including any holes between elements.
	 * Empty tensors return {origin,0}. Rejects arithmetic overflow or a negative-stride view
	 * reaching before the buffer origin. The returned size is concrete, never a whole-buffer sentinel.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaRHIBufferRange> GetArdaRHITensorByteRange(const FArdaRHITensorDesc& Desc);

	/** Conservatively proves that different elements do not overlap; false also means unproven.
	 * Empty, contiguous, permuted, sliced and reversed layouts can be proven. Broadcast axes
	 * with extent greater than one fail. Invalid descriptors also return false.
	 */
	[[nodiscard]] bool IsArdaRHITensorNonOverlapping(const FArdaRHITensorDesc& Desc);

	/** Validates the layout's covering interval against a concrete storage size.
	 * Writable independent-element uses should set bRequireNonOverlapping. Empty views may
	 * start one byte past the last stored byte. Native address alignment and provider support
	 * remain dispatch-time checks.
	 */
	[[nodiscard]] FArdaRHIStatus ValidateArdaRHITensorDesc(const FArdaRHITensorDesc& Desc,
	    uint64_t StorageByteSize,
	    bool bRequireNonOverlapping = false);

	/** Validates a retained, non-null buffer view against its buffer's byte size. */
	[[nodiscard]] FArdaRHIStatus ValidateArdaRHITensorView(const FArdaRHITensorView& View,
	    bool bRequireNonOverlapping = false);

	/** Builds checked row-major strides with the last dimension contiguous.
	 * Empty tensors use zero strides, since no element address is observable. Overflow and
	 * invalid dtype/alignment return an error without a usable descriptor.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaRHITensorDesc> MakeArdaContiguousTensorDesc(EArdaRHIDataType DataType,
	    const eastl::vector<uint64_t>& Dimensions,
	    uint64_t ByteOffset = 0);
}
