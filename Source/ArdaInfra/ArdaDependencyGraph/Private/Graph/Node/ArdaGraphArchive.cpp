#include "Graph/Node/ArdaGraphSemanticFields.h"
#include "Graph/Node/ArdaGraphArchive.h"

namespace arda
{
	void FArdaGraphArchiveWriter::WriteInteger(uint64_t Value, size_t Bytes)
	{
		if (Bytes > 8)
		{
			Fail("Archive integer width is invalid.");
			return;
		}
		for (size_t Index = 0; Index < Bytes; ++Index)
		{
			const char Byte = static_cast<char>(Value >> (Index * 8));
			WriteBytes(&Byte, 1);
		}
	}

	void FArdaGraphArchiveWriter::WriteBytes(const char* Data, size_t Count)
	{
		if (!mStatus || !Count)
		{
			return;
		}
		if (Count > mMaximumBytes - mData.size())
		{
			Fail("Graph archive exceeds the size limit.");
			return;
		}
		mData.append(Data, Count);
	}

	void FArdaGraphArchiveWriter::Fail(const char* Message, EArdaRHIResult Code)
	{
		if (mStatus)
		{
			mStatus = FArdaRHIStatus::Error(Code, Message);
		}
	}

	const FArdaRHIStatus& FArdaGraphArchiveWriter::GetStatus() const noexcept
	{
		return mStatus;
	}

	const eastl::string& FArdaGraphArchiveWriter::GetData() const noexcept
	{
		return mData;
	}

	FArdaGraphArchiveReader::FArdaGraphArchiveReader(const eastl::string& Data)
	    : mData(Data)
	{
		if (Data.size() > FArdaGraphArchiveWriter::mMaximumBytes)
		{
			Fail("Graph archive exceeds the size limit.");
		}
	}

	uint64_t FArdaGraphArchiveReader::ReadInteger(size_t Bytes)
	{
		if (!mStatus)
		{
			return 0;
		}
		if (Bytes > 8 || Bytes > mData.size() - mPosition)
		{
			Fail("Graph archive is truncated or has an invalid integer width.");
			return 0;
		}
		uint64_t Value = 0;
		for (size_t Index = 0; Index < Bytes; ++Index)
		{
			Value |= uint64_t(static_cast<uint8_t>(mData[mPosition++])) << (Index * 8);
		}
		return Value;
	}

	void FArdaGraphArchiveReader::ReadBytes(eastl::string& Value, size_t Count)
	{
		if (!mStatus)
		{
			return;
		}
		if (Count > mData.size() - mPosition)
		{
			Fail("Graph archive contains a truncated string.");
			return;
		}
		Value.assign(mData.data() + mPosition, Count);
		mPosition += Count;
	}

	size_t FArdaGraphArchiveReader::ReadCount()
	{
		const auto Count = ReadInteger(8);
		if (Count > 1024 * 1024)
		{
			Fail("Graph archive collection exceeds the element limit.");
			return 0;
		}
		return static_cast<size_t>(Count);
	}

	FArdaDependencyResourceHandle FArdaGraphArchiveReader::ReadResource()
	{
		const auto Index = ReadInteger(4);
		if (Index == UINT32_MAX || !mStatus)
		{
			return {};
		}
		if (!mResources || Index >= mResources->size())
		{
			Fail("Graph archive references an unknown resource.");
			return {};
		}
		const auto Resource = (*mResources)[static_cast<size_t>(Index)];
		if (mResourceReferences)
		{
			mResourceReferences->push_back(Resource);
		}
		return Resource;
	}

	FArdaGraphNodeHandle FArdaGraphArchiveReader::ReadNode()
	{
		const auto Index = ReadInteger(4);
		if (Index == UINT32_MAX || !mStatus)
		{
			return {};
		}
		if (!mNodes || Index >= mNodes->size())
		{
			Fail("Graph archive references an unknown or forward stage node.");
			return {};
		}
		return (*mNodes)[static_cast<size_t>(Index)];
	}

	void FArdaGraphArchiveReader::Fail(const char* Message, EArdaRHIResult Code)
	{
		if (mStatus)
		{
			mStatus = FArdaRHIStatus::Error(Code, Message);
		}
	}

	const FArdaRHIStatus& FArdaGraphArchiveReader::GetStatus() const noexcept
	{
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphArchiveReader::Finish() const
	{
		return !mStatus || mPosition == mData.size()
		    ? mStatus
		    : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Graph archive contains trailing bytes.");
	}

	void TArdaGraphCodec<FArdaRHIBufferDesc>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIBufferDesc& Value)
	{
		BufferDescriptorFields(Archive, Value);
	}

	void TArdaGraphCodec<FArdaRHIBufferDesc>::Read(FArdaGraphArchiveReader& Archive, FArdaRHIBufferDesc& Value)
	{
		BufferDescriptorFields(Archive, Value);
	}

	void TArdaGraphCodec<FArdaRHITextureDesc>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITextureDesc& Value)
	{
		TextureDescriptorFields(Archive, Value);
	}

	void TArdaGraphCodec<FArdaRHITextureDesc>::Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureDesc& Value)
	{
		TextureDescriptorFields(Archive, Value);
	}

	void TArdaGraphCodec<FArdaRHIBufferRange>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIBufferRange& Value)
	{
		Archive.Fields(Value.mByteOffset, Value.mByteSize);
	}

	void TArdaGraphCodec<FArdaRHIBufferRange>::Read(FArdaGraphArchiveReader& Archive, FArdaRHIBufferRange& Value)
	{
		Archive.Fields(Value.mByteOffset, Value.mByteSize);
	}

	void TArdaGraphCodec<FArdaRHITextureSubresourceRange>::Write(FArdaGraphArchiveWriter& Archive,
	    const FArdaRHITextureSubresourceRange& Value)
	{
		Archive.Fields(Value.mBaseMipLevel,
		    Value.mMipLevelCount,
		    Value.mBaseArraySlice,
		    Value.mArraySliceCount,
		    Value.mBasePlane,
		    Value.mPlaneCount);
	}

	void TArdaGraphCodec<FArdaRHITextureSubresourceRange>::Read(FArdaGraphArchiveReader& Archive,
	    FArdaRHITextureSubresourceRange& Value)
	{
		Archive.Fields(Value.mBaseMipLevel,
		    Value.mMipLevelCount,
		    Value.mBaseArraySlice,
		    Value.mArraySliceCount,
		    Value.mBasePlane,
		    Value.mPlaneCount);
	}

	void TArdaGraphCodec<FArdaRHIViewDesc>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIViewDesc& Value)
	{
		Archive.Fields(Value.mFormat, Value.mDimension, Value.mBufferRange, Value.mTextureRange);
	}

	void TArdaGraphCodec<FArdaRHIViewDesc>::Read(FArdaGraphArchiveReader& Archive, FArdaRHIViewDesc& Value)
	{
		Archive.Fields(Value.mFormat, Value.mDimension, Value.mBufferRange, Value.mTextureRange);
	}

	void TArdaGraphCodec<FArdaRHIColor>::Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIColor& Value)
	{
		Archive.Fields(Value.mR, Value.mG, Value.mB, Value.mA);
	}

	void TArdaGraphCodec<FArdaRHIColor>::Read(FArdaGraphArchiveReader& Archive, FArdaRHIColor& Value)
	{
		Archive.Fields(Value.mR, Value.mG, Value.mB, Value.mA);
	}

	void TArdaGraphCodec<FArdaRHITextureSlice>::Write(FArdaGraphArchiveWriter& Archive,
	    const FArdaRHITextureSlice& Value)
	{
		Archive.Fields(Value.mX,
		    Value.mY,
		    Value.mZ,
		    Value.mWidth,
		    Value.mHeight,
		    Value.mDepth,
		    Value.mMipLevel,
		    Value.mArraySlice,
		    Value.mPlane);
	}

	void TArdaGraphCodec<FArdaRHITextureSlice>::Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureSlice& Value)
	{
		Archive.Fields(Value.mX,
		    Value.mY,
		    Value.mZ,
		    Value.mWidth,
		    Value.mHeight,
		    Value.mDepth,
		    Value.mMipLevel,
		    Value.mArraySlice,
		    Value.mPlane);
	}

	void TArdaGraphCodec<FArdaRHITextureBufferLayout>::Write(FArdaGraphArchiveWriter& Archive,
	    const FArdaRHITextureBufferLayout& Value)
	{
		Archive.Fields(Value.mByteOffset, Value.mRowPitch);
	}

	void TArdaGraphCodec<FArdaRHITextureBufferLayout>::Read(FArdaGraphArchiveReader& Archive,
	    FArdaRHITextureBufferLayout& Value)
	{
		Archive.Fields(Value.mByteOffset, Value.mRowPitch);
	}

	void TArdaGraphCodec<FArdaRHIAccelStructDesc>::Write(FArdaGraphArchiveWriter& Archive,
	    const FArdaRHIAccelStructDesc& Value)
	{
		Archive.Fields(uint64_t(Value.mTopLevelMaxInstances),
		    Value.mBuildFlags,
		    Value.mbTrackLiveness,
		    Value.mbAllowUnsafeLivenessOptOut,
		    Value.mbTopLevel,
		    Value.mbVirtual,
		    Value.mResultSizeOverride,
		    Value.mDebugName);
		Archive.WriteInteger(Value.mBottomLevelGeometries.size(), 8);
		for (const auto& Geometry : Value.mBottomLevelGeometries)
		{
			if (Geometry.mIndexBuffer || Geometry.mVertexOrAABBBuffer || Geometry.mOpacityMicromap ||
			    Geometry.mOpacityMicromapIndexBuffer)
			{
				Archive.Fail("Acceleration-structure shape cannot contain native geometry bindings.",
				    EArdaRHIResult::Unsupported);
				return;
			}
			Archive.Fields(Geometry.mType,
			    Geometry.mFlags,
			    Geometry.mIndexFormat,
			    Geometry.mVertexFormat,
			    Geometry.mIndexOffset,
			    Geometry.mVertexOrAABBOffset,
			    Geometry.mIndexCount,
			    Geometry.mVertexOrAABBCount,
			    Geometry.mStride,
			    Geometry.mOpacityMicromapIndexOffset,
			    Geometry.mOpacityMicromapIndexFormat);
			Archive.WriteInteger(Geometry.mOpacityMicromapUsageCounts.size(), 8);
			for (const auto& Usage : Geometry.mOpacityMicromapUsageCounts)
			{
				Archive.Fields(Usage.mCount, Usage.mSubdivisionLevel, Usage.mFormat);
			}
		}
	}

	void TArdaGraphCodec<FArdaRHIAccelStructDesc>::Read(FArdaGraphArchiveReader& Archive,
	    FArdaRHIAccelStructDesc& Value)
	{
		uint64_t MaxInstances = 0;
		Archive.Fields(MaxInstances,
		    Value.mBuildFlags,
		    Value.mbTrackLiveness,
		    Value.mbAllowUnsafeLivenessOptOut,
		    Value.mbTopLevel,
		    Value.mbVirtual,
		    Value.mResultSizeOverride,
		    Value.mDebugName);
		if (MaxInstances > SIZE_MAX)
		{
			Archive.Fail("Acceleration-structure capacity exceeds this platform's size limit.");
			return;
		}
		Value.mTopLevelMaxInstances = static_cast<size_t>(MaxInstances);
		Value.mBottomLevelGeometries.clear();
		const auto Count = Archive.ReadCount();
		for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
		{
			FArdaRHIRayTracingGeometryDesc Geometry;
			Archive.Fields(Geometry.mType,
			    Geometry.mFlags,
			    Geometry.mIndexFormat,
			    Geometry.mVertexFormat,
			    Geometry.mIndexOffset,
			    Geometry.mVertexOrAABBOffset,
			    Geometry.mIndexCount,
			    Geometry.mVertexOrAABBCount,
			    Geometry.mStride,
			    Geometry.mOpacityMicromapIndexOffset,
			    Geometry.mOpacityMicromapIndexFormat);
			const auto UsageCount = Archive.ReadCount();
			for (size_t UsageIndex = 0; UsageIndex < UsageCount && Archive.GetStatus(); ++UsageIndex)
			{
				FArdaRHIOpacityMicromapUsageCount Usage;
				Archive.Fields(Usage.mCount, Usage.mSubdivisionLevel, Usage.mFormat);
				Geometry.mOpacityMicromapUsageCounts.push_back(Usage);
			}
			Value.mBottomLevelGeometries.push_back(eastl::move(Geometry));
		}
	}
}
