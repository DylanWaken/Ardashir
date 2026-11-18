#include "Graph/Resources/ArdaGraphTensor.h"

#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"

namespace arda
{
	TArdaRHIResult<FArdaRHITensorView> ResolveArdaGraphTensor(const FArdaDependencyExecutionContext& Context,
	    const FArdaGraphTensor& Tensor,
	    bool bRequireNonOverlapping)
	{
		FArdaRHITensorView View{Context.GetBuffer(Tensor.mBuffer), Tensor.mDesc};
		if (auto Status = ValidateArdaRHITensorView(View, bRequireNonOverlapping); !Status)
		{
			return {{}, Status};
		}
		return {eastl::move(View), {}};
	}

	void TArdaGraphCodec<FArdaRHITensorDesc>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITensorDesc& Value)
	{
		if (auto Range = GetArdaRHITensorByteRange(Value); !Range)
		{
			Archive.Fail(Range.mStatus.mMessage.c_str());
			return;
		}
		Archive.Write(Value.mDataType);
		Archive.WriteInteger(Value.mDimensions.size(), 8);
		for (size_t Axis = 0; Axis < Value.mDimensions.size(); ++Axis)
		{
			Archive.Fields(Value.mDimensions[Axis], Value.mStrides[Axis]);
		}
		Archive.Write(Value.mByteOffset);
	}

	void TArdaGraphCodec<FArdaRHITensorDesc>::Read(FArdaGraphArchiveReader& Archive, FArdaRHITensorDesc& Value)
	{
		FArdaRHITensorDesc Decoded;
		Archive.Read(Decoded.mDataType);
		const auto Rank = Archive.ReadCount();
		// Grow only as fields are read, so a truncated count cannot force a large allocation.
		for (size_t Axis = 0; Axis < Rank && Archive.GetStatus(); ++Axis)
		{
			uint64_t Dimension = 0;
			int64_t Stride = 0;
			Archive.Fields(Dimension, Stride);
			Decoded.mDimensions.push_back(Dimension);
			Decoded.mStrides.push_back(Stride);
		}
		Archive.Read(Decoded.mByteOffset);
		if (!Archive.GetStatus())
		{
			return;
		}
		if (auto Range = GetArdaRHITensorByteRange(Decoded); !Range)
		{
			Archive.Fail(Range.mStatus.mMessage.c_str());
			return;
		}
		Value = eastl::move(Decoded);
	}

	void TArdaGraphCodec<FArdaGraphTensor>::Write(FArdaGraphArchiveWriter& Archive, const FArdaGraphTensor& Value)
	{
		Archive.Fields(Value.mBuffer, Value.mDesc);
	}

	void TArdaGraphCodec<FArdaGraphTensor>::Read(FArdaGraphArchiveReader& Archive, FArdaGraphTensor& Value)
	{
		FArdaGraphTensor Decoded;
		Archive.Fields(Decoded.mBuffer, Decoded.mDesc);
		if (Archive.GetStatus())
		{
			Value = eastl::move(Decoded);
		}
	}
}
