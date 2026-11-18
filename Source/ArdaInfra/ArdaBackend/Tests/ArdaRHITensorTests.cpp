#include "RHI/Resources/ArdaRHITensor.h"
#include "RHI/Shaders/ArdaComputeParameters.h"

#include <gtest/gtest.h>
#include <set>

namespace arda
{
	namespace
	{
		class FArdaTensorTestBuffer final : public IArdaRHIBuffer
		{
		public:
			int mReferences = 0;
			FArdaRHIBufferDesc mDesc;

			void AddRef() noexcept override
			{
				++mReferences;
			}

			void Release() noexcept override
			{
				--mReferences;
			}

			EArdaRHIResourceType GetResourceType() const noexcept override
			{
				return EArdaRHIResourceType::Buffer;
			}

			const char* GetDebugName() const noexcept override
			{
				return "tensor-test-buffer";
			}

			const FArdaRHIBufferDesc& GetDesc() const noexcept override
			{
				return mDesc;
			}

			const void* GetPhysicalIdentity() const noexcept override
			{
				return this;
			}
		};

		ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FArdaTensorComputeParameters)
			ARDA_COMPUTE_TENSOR(mOutput, EArdaComputeAccess::Write)
			ARDA_COMPUTE_TENSOR_ARRAY(mInputs, 2, EArdaComputeAccess::Read)
		ARDA_END_COMPUTE_PARAMETER_STRUCT()
	}

	TEST(ArdaRHITensor, ScalarTypesHaveStableStorageIdentities)
	{
		EXPECT_EQ(uint16_t(EArdaRHIDataType::Float32), 12);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(EArdaRHIDataType::Float16).mByteSize, 2);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(EArdaRHIDataType::BFloat16).mByteSize, 2);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(EArdaRHIDataType::Complex128).mByteSize, 16);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(EArdaRHIDataType::Complex128).mAlignment, 8);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(EArdaRHIDataType::Bool8).mKind, EArdaRHIDataTypeKind::Boolean);
		EXPECT_EQ(GetArdaRHIDataTypeInfo(static_cast<EArdaRHIDataType>(65535)).mByteSize, 0);
		for (uint16_t Index = 1; Index <= uint16_t(EArdaRHIDataType::Complex128); ++Index)
		{
			const auto Info = GetArdaRHIDataTypeInfo(static_cast<EArdaRHIDataType>(Index));
			EXPECT_GT(Info.mByteSize, 0);
			EXPECT_GT(Info.mAlignment, 0);
			EXPECT_EQ(Info.mByteSize % Info.mAlignment, 0);
		}
	}

	TEST(ArdaRHITensor, ScalarsAndEmptyTensorsHaveExplicitRanges)
	{
		const auto Scalar = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {}, 8);
		ASSERT_TRUE(Scalar);
		EXPECT_EQ(GetArdaRHITensorElementCount(Scalar.mValue).mValue, 1u);
		EXPECT_EQ(GetArdaRHITensorByteRange(Scalar.mValue).mValue, (FArdaRHIBufferRange{8, 4}));
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Scalar.mValue, 12, true));
		EXPECT_FALSE(ValidateArdaRHITensorDesc(Scalar.mValue, 11));

		const auto Empty = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float64, {UINT64_MAX, UINT64_MAX, 0}, 24);
		ASSERT_TRUE(Empty);
		EXPECT_EQ(GetArdaRHITensorElementCount(Empty.mValue).mValue, 0u);
		EXPECT_EQ(GetArdaRHITensorByteRange(Empty.mValue).mValue, (FArdaRHIBufferRange{24, 0}));
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Empty.mValue, 24, true));
		EXPECT_FALSE(ValidateArdaRHITensorDesc(Empty.mValue, 23));
	}

	TEST(ArdaRHITensor, ContiguousPermutedSlicedAndReversedViewsUseSharedBufferCoordinates)
	{
		const auto Contiguous = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {2, 3, 4}, 16);
		ASSERT_TRUE(Contiguous);
		EXPECT_EQ(Contiguous.mValue.mStrides, (eastl::vector<int64_t>{12, 4, 1}));
		EXPECT_EQ(GetArdaRHITensorByteRange(Contiguous.mValue).mValue, (FArdaRHIBufferRange{16, 96}));

		const FArdaRHITensorDesc Permuted{EArdaRHIDataType::Float32, {4, 2, 3}, {1, 12, 4}, 16};
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Permuted, 112, true));
		const FArdaRHITensorDesc Sliced{EArdaRHIDataType::Float32, {2, 3}, {12, 2}, 16};
		EXPECT_EQ(GetArdaRHITensorByteRange(Sliced).mValue, (FArdaRHIBufferRange{16, 68}));
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Sliced, 84, true));
		const FArdaRHITensorDesc Reversed{EArdaRHIDataType::Float32, {2, 3}, {-3, -1}, 20};
		EXPECT_EQ(GetArdaRHITensorByteRange(Reversed).mValue, (FArdaRHIBufferRange{0, 24}));
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Reversed, 24, true));
	}

	TEST(ArdaRHITensor, BroadcastAndOverlappingViewsAreReadableButNotIndependentWritableElements)
	{
		const FArdaRHITensorDesc Broadcast{EArdaRHIDataType::Float32, {100, 4}, {0, 1}, 0};
		EXPECT_EQ(GetArdaRHITensorElementCount(Broadcast).mValue, 400u);
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Broadcast, 16));
		EXPECT_FALSE(ValidateArdaRHITensorDesc(Broadcast, 16, true));
		const FArdaRHITensorDesc Overlap{EArdaRHIDataType::Float32, {2, 3}, {2, 1}, 0};
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Overlap, 20));
		EXPECT_FALSE(IsArdaRHITensorNonOverlapping(Overlap));
		const FArdaRHITensorDesc Singleton{EArdaRHIDataType::UInt8, {1, 2}, {INT64_MIN, 1}, 0};
		EXPECT_TRUE(ValidateArdaRHITensorDesc(Singleton, 2, true));
	}

	TEST(ArdaRHITensor, InvalidLayoutsAndArithmeticOverflowAreRejected)
	{
		EXPECT_FALSE(GetArdaRHITensorByteRange({}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::Float32, {2}, {}, 0}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::Float32, {2}, {1}, 2}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::Float32, {2}, {-1}, 0}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::Float32, {UINT64_MAX, 2}, {0, 0}, 0}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::Float64, {2}, {INT64_MAX}, 0}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::UInt8, {}, {}, UINT64_MAX}));
		EXPECT_FALSE(GetArdaRHITensorByteRange({EArdaRHIDataType::UInt8, {4}, {INT64_MIN}, UINT64_MAX}));
		EXPECT_FALSE(MakeArdaContiguousTensorDesc(EArdaRHIDataType::UInt8, {1, uint64_t(INT64_MAX) + 1}));
		EXPECT_FALSE(MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float64, {UINT64_MAX}));
		const FArdaRHITensorDesc MinimumStride{EArdaRHIDataType::UInt8, {2}, {INT64_MIN}, uint64_t(INT64_MAX) + 1};
		const auto Range = GetArdaRHITensorByteRange(MinimumStride);
		ASSERT_TRUE(Range);
		EXPECT_EQ(Range.mValue.mByteOffset, 0u);
		EXPECT_EQ(Range.mValue.mByteSize, uint64_t(INT64_MAX) + 2);
	}

	TEST(ArdaRHITensor, CoveringRangesAndNonOverlapProofAgreeWithEnumeratedAddresses)
	{
		for (uint64_t Height = 1; Height <= 4; ++Height)
		{
			for (uint64_t Width = 1; Width <= 4; ++Width)
			{
				for (int64_t RowStride = -4; RowStride <= 4; ++RowStride)
				{
					for (int64_t ColumnStride = -4; ColumnStride <= 4; ++ColumnStride)
					{
						const FArdaRHITensorDesc Desc{EArdaRHIDataType::UInt8,
						    {Height, Width},
						    {RowStride, ColumnStride},
						    64};
						std::set<uint64_t> Addresses;
						for (uint64_t Row = 0; Row < Height; ++Row)
						{
							for (uint64_t Column = 0; Column < Width; ++Column)
							{
								Addresses.insert(
								    uint64_t(64 + int64_t(Row) * RowStride + int64_t(Column) * ColumnStride));
							}
						}
						const auto Range = GetArdaRHITensorByteRange(Desc);
						ASSERT_TRUE(Range);
						EXPECT_EQ(Range.mValue.mByteOffset, *Addresses.begin());
						EXPECT_EQ(Range.mValue.mByteSize, *Addresses.rbegin() - *Addresses.begin() + 1);
						if (IsArdaRHITensorNonOverlapping(Desc))
						{
							EXPECT_EQ(Addresses.size(), Height * Width);
						}
					}
				}
			}
		}
	}

	TEST(ArdaRHITensor, ComputeMetadataRetainsStorageAndValidatesTensorDependenciesAtomically)
	{
		FArdaTensorTestBuffer Buffer;
		Buffer.mDesc.mByteSize = 64;
		eastl::vector<FArdaComputeResourceAccess> Accesses;
		{
			FArdaTensorComputeParameters Parameters;
			Parameters.mOutput = {FArdaRHIBufferRef(&Buffer), {EArdaRHIDataType::Float32, {4}, {1}, 16}};
			Parameters.mInputs[0] = {FArdaRHIBufferRef(&Buffer), {EArdaRHIDataType::Float32, {4, 4}, {0, 1}, 16}};
			EXPECT_TRUE(ValidateArdaRHITensorView(Parameters.mOutput, true));
			EXPECT_FALSE(ValidateArdaRHITensorView(Parameters.mInputs[1]));
			const auto& Metadata = FArdaTensorComputeParameters::GetStaticMetadata();
			ASSERT_TRUE(Metadata.GetStatus()) << Metadata.GetStatus().mMessage.c_str();
			ASSERT_TRUE(Metadata.GetResourceAccesses(&Parameters, Accesses));
			ASSERT_EQ(Accesses.size(), 3u);
			EXPECT_EQ(Accesses[0].mResource.Get(), &Buffer);
			EXPECT_EQ(Accesses[1].mResource.Get(), &Buffer);
			EXPECT_EQ(Accesses[0].mBufferRange, (FArdaRHIBufferRange{16, 16}));
			EXPECT_EQ(Accesses[1].mTensorDesc, Parameters.mInputs[0].mDesc);
			EXPECT_FALSE(Accesses[2].mResource);
			EXPECT_EQ(Accesses[2].mPath, "mInputs[1]");
			Parameters.mOutput.mDesc.mStrides = {0};
			EXPECT_FALSE(Metadata.GetResourceAccesses(&Parameters, Accesses));
			EXPECT_EQ(Accesses[0].mBufferRange, (FArdaRHIBufferRange{16, 16}));
			Parameters.mOutput.mDesc = {EArdaRHIDataType::Float32, {4}, {1}, 60};
			EXPECT_FALSE(Metadata.GetResourceAccesses(&Parameters, Accesses));
		}
		EXPECT_EQ(Buffer.mReferences, 2);
		Accesses.clear();
		EXPECT_EQ(Buffer.mReferences, 0);
	}
}
