#include "Graph/Node/ArdaGraphDescriptionArchive.h"
#include <gtest/gtest.h>

namespace arda
{
	TEST(ArdaGraphSerialization, PreservesPortablePipelineSettingsWithoutNativeObjects)
	{
		FArdaDependencyNodeDesc Description;
		for (const auto Kind : {EArdaPipelineStateKind::Compute,
		         EArdaPipelineStateKind::Graphics,
		         EArdaPipelineStateKind::Meshlet,
		         EArdaPipelineStateKind::RayTracing,
		         EArdaPipelineStateKind::WorkGraph})
		{
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = Kind;
			Configuration->mCompute.mDesc.mPersistentCacheKey = 19;
			Configuration->mCompute.mDesc.mDebugName = "Compute override";
			auto& Graphics = Configuration->mGraphics.mDesc;
			Graphics.mTopology = EArdaRHIPrimitiveTopology::PatchList;
			Graphics.mPatchControlPoints = 3;
			Graphics.mSampleCount = 4;
			Graphics.mColorFormats = {EArdaRHIFormat::RGBA8UNorm};
			Graphics.mDepthStencilState.mbDepthWrite = false;
			Graphics.mRasterState.mbScissor = true;
			Graphics.mBlendState.mbAlphaToCoverage = true;
			Graphics.mBlendState.mTargets[0].mbEnable = true;
			Configuration->mMeshlet.mDesc.mSampleCount = 2;
			Configuration->mMeshlet.mDesc.mRasterState.mbFrontCounterClockwise = true;
			Configuration->mRayTracing.mDesc.mMaxPayloadSize = 96;
			Configuration->mRayTracing.mDesc.mMaxAttributeSize = 16;
			Configuration->mRayTracing.mDesc.mMaxRecursionDepth = 7;
			Configuration->mRayTracing.mDesc.mbAllowOpacityMicromaps = true;
			Configuration->mRayTracing.mDesc.mShaders.push_back({"Trace"});
			FArdaRHIRayTracingHitGroupDesc Group;
			Group.mExportName = "Procedural";
			Group.mbProceduralPrimitive = true;
			Configuration->mRayTracing.mDesc.mHitGroups.push_back(Group);
			Configuration->mWorkGraph.mDesc.mProgramName = "Program";
			Configuration->mWorkGraph.mDesc.mEntryPoint = "Entry";
			Configuration->mWorkGraph.mDesc.mMaxInputRecords = 31;
			FArdaInductorPipelineRequest Request;
			Request.mSlot = "Slot";
			Request.mKind = Kind;
			Request.mConfiguration = Configuration;
			Description.mPipelines.push_back(Request);
			FArdaInductorPipelineContribution Contribution;
			Contribution.mGroup = "Group";
			Contribution.mConfiguration = Configuration;
			Description.mPipelineStages.push_back(Contribution);
		}
		FArdaGraphArchiveWriter Writer;
		WriteArdaGraphDescription(Writer, Description);
		ASSERT_TRUE(Writer.GetStatus()) << Writer.GetStatus().mMessage.c_str();
		FArdaDependencyNodeDesc Restored;
		FArdaGraphArchiveReader Reader(Writer.GetData());
		ReadArdaGraphDescription(Reader, Restored);
		ASSERT_TRUE(Reader.Finish()) << Reader.GetStatus().mMessage.c_str();
		ASSERT_EQ(Restored.mPipelines.size(), 5u);
		ASSERT_EQ(Restored.mPipelineStages.size(), 5u);
		for (size_t Index = 0; Index < Restored.mPipelines.size(); ++Index)
		{
			const auto& Original = *Description.mPipelines[Index].mConfiguration;
			const auto& Loaded = *Restored.mPipelines[Index].mConfiguration;
			EXPECT_EQ(Loaded.mKind, Original.mKind);
			switch (Loaded.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				EXPECT_EQ(Loaded.mCompute.mDesc.mPersistentCacheKey, 19u);
				EXPECT_EQ(Loaded.mCompute.mDesc.mDebugName, "Compute override");
				break;
			case EArdaPipelineStateKind::Graphics:
				EXPECT_TRUE(Loaded.mGraphics.mDesc == Original.mGraphics.mDesc);
				break;
			case EArdaPipelineStateKind::Meshlet:
				EXPECT_TRUE(Loaded.mMeshlet.mDesc == Original.mMeshlet.mDesc);
				break;
			case EArdaPipelineStateKind::RayTracing:
				EXPECT_TRUE(Loaded.mRayTracing.mDesc == Original.mRayTracing.mDesc);
				break;
			case EArdaPipelineStateKind::WorkGraph:
				EXPECT_TRUE(Loaded.mWorkGraph.mDesc == Original.mWorkGraph.mDesc);
				break;
			}
		}
		FArdaGraphArchiveWriter Reencoded;
		WriteArdaGraphDescription(Reencoded, Restored);
		ASSERT_TRUE(Reencoded.GetStatus());
		EXPECT_EQ(Reencoded.GetData(), Writer.GetData());
	}
}
