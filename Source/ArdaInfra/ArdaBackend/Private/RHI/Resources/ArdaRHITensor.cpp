#include "RHI/Resources/ArdaRHITensor.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		FArdaRHIStatus InvalidTensor(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		bool TryMultiply(uint64_t A, uint64_t B, uint64_t& OutValue) noexcept
		{
			if (B && A > UINT64_MAX / B)
			{
				return false;
			}
			OutValue = A * B;
			return true;
		}

		bool TryAdd(uint64_t A, uint64_t B, uint64_t& OutValue) noexcept
		{
			if (A > UINT64_MAX - B)
			{
				return false;
			}
			OutValue = A + B;
			return true;
		}

		uint64_t GetStrideMagnitude(int64_t Stride) noexcept
		{
			return Stride < 0 ? uint64_t(-(Stride + 1)) + 1 : uint64_t(Stride);
		}
	}

	FArdaRHIDataTypeInfo GetArdaRHIDataTypeInfo(EArdaRHIDataType DataType) noexcept
	{
		switch (DataType)
		{
		case EArdaRHIDataType::Bool8:
			return {"bool8", EArdaRHIDataTypeKind::Boolean, 1, 1};
		case EArdaRHIDataType::Int8:
			return {"int8", EArdaRHIDataTypeKind::SignedInteger, 1, 1};
		case EArdaRHIDataType::UInt8:
			return {"uint8", EArdaRHIDataTypeKind::UnsignedInteger, 1, 1};
		case EArdaRHIDataType::Int16:
			return {"int16", EArdaRHIDataTypeKind::SignedInteger, 2, 2};
		case EArdaRHIDataType::UInt16:
			return {"uint16", EArdaRHIDataTypeKind::UnsignedInteger, 2, 2};
		case EArdaRHIDataType::Int32:
			return {"int32", EArdaRHIDataTypeKind::SignedInteger, 4, 4};
		case EArdaRHIDataType::UInt32:
			return {"uint32", EArdaRHIDataTypeKind::UnsignedInteger, 4, 4};
		case EArdaRHIDataType::Int64:
			return {"int64", EArdaRHIDataTypeKind::SignedInteger, 8, 8};
		case EArdaRHIDataType::UInt64:
			return {"uint64", EArdaRHIDataTypeKind::UnsignedInteger, 8, 8};
		case EArdaRHIDataType::Float16:
			return {"float16", EArdaRHIDataTypeKind::FloatingPoint, 2, 2};
		case EArdaRHIDataType::BFloat16:
			return {"bfloat16", EArdaRHIDataTypeKind::FloatingPoint, 2, 2};
		case EArdaRHIDataType::Float32:
			return {"float32", EArdaRHIDataTypeKind::FloatingPoint, 4, 4};
		case EArdaRHIDataType::Float64:
			return {"float64", EArdaRHIDataTypeKind::FloatingPoint, 8, 8};
		case EArdaRHIDataType::Float8E4M3FN:
			return {"float8_e4m3fn", EArdaRHIDataTypeKind::FloatingPoint, 1, 1};
		case EArdaRHIDataType::Float8E5M2:
			return {"float8_e5m2", EArdaRHIDataTypeKind::FloatingPoint, 1, 1};
		case EArdaRHIDataType::Complex64:
			return {"complex64", EArdaRHIDataTypeKind::Complex, 8, 4};
		case EArdaRHIDataType::Complex128:
			return {"complex128", EArdaRHIDataTypeKind::Complex, 16, 8};
		default:
			return {};
		}
	}

	bool FArdaRHITensorDesc::operator==(const FArdaRHITensorDesc& Other) const noexcept
	{
		return mDataType == Other.mDataType && mDimensions == Other.mDimensions && mStrides == Other.mStrides &&
		    mByteOffset == Other.mByteOffset;
	}

	bool FArdaRHITensorDesc::operator!=(const FArdaRHITensorDesc& Other) const noexcept
	{
		return !(*this == Other);
	}

	TArdaRHIResult<uint64_t> GetArdaRHITensorElementCount(const FArdaRHITensorDesc& Desc)
	{
		const auto Info = GetArdaRHIDataTypeInfo(Desc.mDataType);
		if (!Info.mByteSize || Desc.mDimensions.size() != Desc.mStrides.size() || Desc.mByteOffset % Info.mAlignment)
		{
			return {{}, InvalidTensor("Tensor dtype, rank, or origin alignment is invalid.")};
		}
		if (eastl::find(Desc.mDimensions.begin(), Desc.mDimensions.end(), uint64_t(0)) != Desc.mDimensions.end())
		{
			return {0, {}};
		}
		uint64_t Count = 1;
		for (const uint64_t Dimension : Desc.mDimensions)
		{
			if (!TryMultiply(Count, Dimension, Count))
			{
				return {{}, InvalidTensor("Tensor logical element count overflows.")};
			}
		}
		return {Count, {}};
	}

	TArdaRHIResult<FArdaRHIBufferRange> GetArdaRHITensorByteRange(const FArdaRHITensorDesc& Desc)
	{
		const auto Count = GetArdaRHITensorElementCount(Desc);
		if (!Count)
		{
			return {{}, Count.mStatus};
		}
		if (!Count.mValue)
		{
			return {{Desc.mByteOffset, 0}, {}};
		}
		const uint64_t ElementBytes = GetArdaRHIDataTypeInfo(Desc.mDataType).mByteSize;
		uint64_t NegativeSpan = 0;
		uint64_t PositiveSpan = 0;
		for (size_t Axis = 0; Axis < Desc.mDimensions.size(); ++Axis)
		{
			uint64_t Span = 0;
			uint64_t& DirectionSpan = Desc.mStrides[Axis] < 0 ? NegativeSpan : PositiveSpan;
			if (!TryMultiply(Desc.mDimensions[Axis] - 1, GetStrideMagnitude(Desc.mStrides[Axis]), Span) ||
			    !TryMultiply(Span, ElementBytes, Span) || !TryAdd(DirectionSpan, Span, DirectionSpan))
			{
				return {{}, InvalidTensor("Tensor stride span overflows.")};
			}
		}
		uint64_t End = 0;
		if (NegativeSpan > Desc.mByteOffset || !TryAdd(Desc.mByteOffset, PositiveSpan, End) ||
		    !TryAdd(End, ElementBytes, End))
		{
			return {{}, InvalidTensor("Tensor addressing reaches before the buffer or overflows.")};
		}
		const uint64_t Start = Desc.mByteOffset - NegativeSpan;
		const uint64_t Size = End - Start;
		if (Size == ArdaRHIWholeBuffer)
		{
			return {{}, InvalidTensor("Tensor byte span cannot use the whole-buffer sentinel.")};
		}
		return {{Start, Size}, {}};
	}

	bool IsArdaRHITensorNonOverlapping(const FArdaRHITensorDesc& Desc)
	{
		const auto Range = GetArdaRHITensorByteRange(Desc);
		if (!Range)
		{
			return false;
		}
		if (!Range.mValue.mByteSize)
		{
			return true;
		}
		eastl::vector<size_t> Axes;
		for (size_t Axis = 0; Axis < Desc.mDimensions.size(); ++Axis)
		{
			if (Desc.mDimensions[Axis] > 1)
			{
				Axes.push_back(Axis);
			}
		}
		eastl::sort(Axes.begin(),
		    Axes.end(),
		    [&](size_t A, size_t B)
		    {
			    return GetStrideMagnitude(Desc.mStrides[A]) < GetStrideMagnitude(Desc.mStrides[B]);
		    });
		uint64_t CoveredSpan = 1;
		for (const size_t Axis : Axes)
		{
			const uint64_t Stride = GetStrideMagnitude(Desc.mStrides[Axis]);
			uint64_t AdditionalSpan = 0;
			if (Stride < CoveredSpan || !TryMultiply(Desc.mDimensions[Axis] - 1, Stride, AdditionalSpan) ||
			    !TryAdd(CoveredSpan, AdditionalSpan, CoveredSpan))
			{
				return false;
			}
		}
		return true;
	}

	FArdaRHIStatus ValidateArdaRHITensorDesc(const FArdaRHITensorDesc& Desc,
	    uint64_t StorageByteSize,
	    bool bRequireNonOverlapping)
	{
		const auto Range = GetArdaRHITensorByteRange(Desc);
		if (!Range)
		{
			return Range.mStatus;
		}
		if (Range.mValue.mByteOffset > StorageByteSize ||
		    Range.mValue.mByteSize > StorageByteSize - Range.mValue.mByteOffset)
		{
			return InvalidTensor("Tensor view exceeds its buffer storage.");
		}
		if (bRequireNonOverlapping && !IsArdaRHITensorNonOverlapping(Desc))
		{
			return InvalidTensor("Tensor writes require a proven non-overlapping layout.");
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaRHITensorView(const FArdaRHITensorView& View, bool bRequireNonOverlapping)
	{
		if (!View.mBuffer)
		{
			return InvalidTensor("Tensor view requires a retained buffer.");
		}
		return ValidateArdaRHITensorDesc(View.mDesc, View.mBuffer->GetDesc().mByteSize, bRequireNonOverlapping);
	}

	TArdaRHIResult<FArdaRHITensorDesc> MakeArdaContiguousTensorDesc(EArdaRHIDataType DataType,
	    const eastl::vector<uint64_t>& Dimensions,
	    uint64_t ByteOffset)
	{
		FArdaRHITensorDesc Desc{DataType, Dimensions, {}, ByteOffset};
		Desc.mStrides.resize(Dimensions.size(), 0);
		const auto Count = GetArdaRHITensorElementCount(Desc);
		if (!Count)
		{
			return {{}, Count.mStatus};
		}
		if (Count.mValue)
		{
			uint64_t Stride = 1;
			for (size_t Axis = Dimensions.size(); Axis > 0; --Axis)
			{
				if (Stride > uint64_t(INT64_MAX))
				{
					return {{}, InvalidTensor("Tensor contiguous stride is not representable.")};
				}
				Desc.mStrides[Axis - 1] = int64_t(Stride);
				if (!TryMultiply(Stride, Dimensions[Axis - 1], Stride))
				{
					return {{}, InvalidTensor("Tensor contiguous stride overflows.")};
				}
			}
		}
		const auto Range = GetArdaRHITensorByteRange(Desc);
		if (!Range)
		{
			return {{}, Range.mStatus};
		}
		return {eastl::move(Desc), {}};
	}
}
