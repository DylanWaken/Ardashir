#include "Graph/Node/ArdaGraphDescriptionArchive.h"
#include <gtest/gtest.h>

namespace arda
{
	TEST(ArdaGraphSerialization, PreservesPortablePipelineSettingsWithoutNativeObjects)
	{
		FArdaDependencyNodeDesc Description;
		Description.mRequirements.mFeatures.Require("ray-tracing.pipeline");
		Description.mRequirements.mFeatures.Require("descriptor.resource-capacity", 48);
		Description.mRequirements.mFeatures.Require("test.flag-mask", 1, 4, 9);
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
		const auto& Features = Restored.mRequirements.mFeatures;
		ASSERT_EQ(Features.GetEntries().size(), 3u);
		EXPECT_TRUE(Features.Contains("ray-tracing.pipeline"));
		EXPECT_EQ(Features.GetEntries()[0].mMinimumValue, 48u);
		EXPECT_EQ(Features.GetEntries()[2].mRequiredBits, 4u);
		EXPECT_EQ(Features.GetEntries()[2].mMaximumValue, 9u);
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

	TEST(ArdaGraphSerialization, ReadsPositionalFeatureRequirementsFromOlderArchives)
	{
		FArdaGraphArchiveWriter Writer;
		// Legacy order: 70 booleans, four one-byte tiers, nine 32-bit minima, one 32-bit maximum.
		for (unsigned Index = 0; Index < 70; ++Index)
		{
			Writer.Write(Index == 9 || Index == 27 || Index == 69);
		}
		Writer.Fields(uint8_t{0}, uint8_t{1}, uint8_t{0}, uint8_t{0}); // Legacy mesh + amplification.
		for (unsigned Index = 0; Index < 9; ++Index)
		{
			Writer.Write(Index == 0 ? uint32_t{128} : uint32_t{0});
		}
		Writer.Write(uint32_t{64}); // Maximum subgroup size.
		const FArdaDependencyNodeRequirements Defaults;
		Writer.Fields(Defaults.mbRequireCuda,
		    Defaults.mbRequireCudaSurfaces,
		    Defaults.mbRequireCudaLayeredSurfaces,
		    Defaults.mMinCudaComputeCapability,
		    Defaults.mMinCudaThreadsPerBlock,
		    Defaults.mMinCudaSharedMemoryBytes,
		    Defaults.mAllowedCudaLaunchModes);
		Writer.WriteInteger(0, 8); // No environment predicates.
		ASSERT_TRUE(Writer.GetStatus());

		FArdaDependencyNodeRequirements Restored;
		FArdaGraphArchiveReader Reader(Writer.GetData());
		ReadArdaGraphRequirements(Reader, Restored, true);
		ASSERT_TRUE(Reader.Finish()) << Reader.GetStatus().mMessage.c_str();
		ASSERT_EQ(Restored.mFeatures.GetEntries().size(), 6u);
		EXPECT_TRUE(Restored.mFeatures.Contains("shader.mesh"));
		EXPECT_TRUE(Restored.mFeatures.Contains("shader.integer-dot-product"));
		EXPECT_TRUE(Restored.mFeatures.Contains("resource.buffer-device-address"));
		for (const auto& Requirement : Restored.mFeatures.GetEntries())
		{
			if (Requirement.mName == "shader.mesh-tier")
			{
				EXPECT_EQ(Requirement.mMinimumValue, 2u);
			}
			if (Requirement.mName == "descriptor.resource-capacity")
			{
				EXPECT_EQ(Requirement.mMinimumValue, 128u);
			}
			if (Requirement.mName == "shader.subgroup-max-size")
			{
				EXPECT_EQ(Requirement.mMaximumValue, 64u);
			}
		}
	}

	TEST(ArdaGraphSerialization, RejectsMalformedNamedFeatureRequirements)
	{
		for (const auto& Invalid : {FArdaBackendFeatureRequirement{"Shader.Mesh"},
		         FArdaBackendFeatureRequirement{"test.limit", 5, 0, 4},
		         FArdaBackendFeatureRequirement{"test.mask", 5, 2, 5}})
		{
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mFeatures = {Invalid};
			FArdaGraphArchiveWriter Writer;
			WriteArdaGraphRequirements(Writer, Requirements);
			EXPECT_EQ(Writer.GetStatus().mCode, EArdaRHIResult::InvalidArgument);

			FArdaGraphArchiveWriter Raw;
			Raw.WriteInteger(1, 8);
			Raw.Fields(Invalid.mName, Invalid.mMinimumValue, Invalid.mRequiredBits, Invalid.mMaximumValue);
			ASSERT_TRUE(Raw.GetStatus());
			FArdaGraphArchiveReader Reader(Raw.GetData());
			ReadArdaGraphRequirements(Reader, Requirements);
			EXPECT_EQ(Reader.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_NE(Reader.GetStatus().mMessage.find("invalid name or impossible bounds"), eastl::string::npos);
		}
	}
}
