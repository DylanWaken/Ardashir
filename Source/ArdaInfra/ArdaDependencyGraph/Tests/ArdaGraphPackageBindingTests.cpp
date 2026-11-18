#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> MakePackageBindingTestPackage(
		    bool bSharedStorage = false,
		    uint8_t InputRole = 1)
		{
			FArdaGraphArchiveWriter Archive;
			Archive.WriteBytes("ARDAGPU\0", 8);
			Archive.Fields(uint32_t{1}, eastl::string("binding-validation"), uint64_t{0});
			Archive.Write(uint64_t(bSharedStorage ? 1 : 2));
			Archive.Fields(eastl::string("input-storage"), uint64_t{64}, eastl::vector<uint8_t>{});
			if (!bSharedStorage)
			{
				Archive.Fields(eastl::string("output-storage"), uint64_t{64}, eastl::vector<uint8_t>{});
			}
			Archive.Write(uint64_t{2});
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				Archive.Fields(eastl::string(Index ? "output" : "input"),
				    bSharedStorage ? uint32_t{0} : Index,
				    Index ? uint8_t{2} : InputRole,
				    EArdaRHIDataType::Float32,
				    eastl::vector<uint64_t>{4},
				    eastl::vector<int64_t>{1},
				    uint64_t{0});
			}
			Archive.Fields(uint64_t{0},
			    uint64_t{1},
			    uint8_t{1},
			    uint32_t(bSharedStorage ? 0 : 1),
			    uint64_t{0},
			    uint64_t{64},
			    eastl::vector<uint8_t>{0});
			return FArdaGraphPackage::Deserialize(Archive.GetData());
		}

		FArdaGraphBuffer AllocatePackageBindingTestBuffer(FArdaGraphEdit& Edit, uint64_t ByteSize = 128)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = ByteSize;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			Desc.mbCudaInterop = true;
			return AllocateBuffer(Edit, Desc);
		}

		FArdaGraphTensor MakePackageBindingTestTensor(FArdaGraphBuffer Buffer, uint64_t Offset)
		{
			FArdaRHITensorDesc Desc;
			Desc.mDataType = EArdaRHIDataType::Float32;
			Desc.mDimensions = {4};
			Desc.mStrides = {1};
			Desc.mByteOffset = Offset;
			return {Buffer, eastl::move(Desc)};
		}
	}

	TEST(ArdaGraphPackageBinding, RejectsMisalignedTensorOriginBeforeRecordingCommands)
	{
		const auto Package = MakePackageBindingTestPackage();
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocatePackageBindingTestBuffer(Edit);
		EXPECT_FALSE(Package.mValue->Invoke(Edit, {{"input", MakePackageBindingTestTensor(Buffer, 1)}}));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
	}

	TEST(ArdaGraphPackageBinding, RejectsOverlappingBackingExtentsEvenWhenTensorElementsDoNotOverlap)
	{
		for (const uint8_t InputRole : {uint8_t{1}, uint8_t{3}})
		{
			const auto Package = MakePackageBindingTestPackage(false, InputRole);
			ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
			for (const uint64_t OutputOffset : {uint64_t{0}, uint64_t{32}})
			{
				FArdaDependencyGraph Graph;
				FArdaGraphEdit Edit(Graph);
				const auto Buffer = AllocatePackageBindingTestBuffer(Edit);
				EXPECT_FALSE(Package.mValue->Invoke(Edit,
				    {{"input", MakePackageBindingTestTensor(Buffer, 0)},
				        {"output", MakePackageBindingTestTensor(Buffer, OutputOffset)}}));
				EXPECT_FALSE(Edit.Commit());
				ASSERT_TRUE(Edit.Cancel());
				EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
			}
		}
	}

	TEST(ArdaGraphPackageBinding, RequiresCapacityForTheWholeExportedStorageExtent)
	{
		const auto Package = MakePackageBindingTestPackage();
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocatePackageBindingTestBuffer(Edit, 32);
		EXPECT_FALSE(Package.mValue->Invoke(Edit, {{"input", MakePackageBindingTestTensor(Buffer, 0)}}));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
	}

	TEST(ArdaGraphPackageBinding, AcceptsDisjointSlicesOfOneGraphBuffer)
	{
		const auto Package = MakePackageBindingTestPackage();
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocatePackageBindingTestBuffer(Edit);
		const auto Invocation = Package.mValue->Invoke(Edit,
		    {{"input", MakePackageBindingTestTensor(Buffer, 0)}, {"output", MakePackageBindingTestTensor(Buffer, 64)}});
		ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
		const auto* Output = Invocation.mValue.Find("output");
		ASSERT_NE(Output, nullptr);
		EXPECT_EQ(Output->mBuffer.GetHandle(), Buffer.GetHandle());
		EXPECT_EQ(Output->mDesc.mByteOffset, 64u);
		ASSERT_TRUE(Edit.Commit());
	}

	TEST(ArdaGraphPackageBinding, PreservesAliasesDeclaredByOnePackageBuffer)
	{
		const auto Package = MakePackageBindingTestPackage(true, 3);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocatePackageBindingTestBuffer(Edit);
		const auto Tensor = MakePackageBindingTestTensor(Buffer, 64);
		const auto Invocation = Package.mValue->Invoke(Edit, {{"input", Tensor}, {"output", Tensor}});
		ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
		ASSERT_TRUE(Edit.Commit());
	}
}
