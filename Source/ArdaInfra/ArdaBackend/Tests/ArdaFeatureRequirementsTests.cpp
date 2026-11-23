#include "RHI/Config/ArdaRHICapabilities.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FArdaBooleanRequirementCase
	{
		const char* mName;
		const char* mRequirement;
		bool& (*mCapability)(FArdaRHICapabilities&);
	};

	const FArdaBooleanRequirementCase BooleanRequirements[] = {
	    {"IndirectFirstInstance",
	        "command.indirect-first-instance",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbIndirectFirstInstance;
	        }},
	    {"InlineRayQueries",
	        "ray-tracing.inline-query",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbInlineRayQueries;
	        }},
	    {"BottomLevelAccelerationStructures",
	        "ray-tracing.bottom-level-acceleration-structure",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbBottomLevel;
	        }},
	    {"TopLevelAccelerationStructures",
	        "ray-tracing.top-level-acceleration-structure",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbTopLevel;
	        }},
	    {"IndirectTopLevelBuild",
	        "ray-tracing.indirect-top-level-build",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbIndirectTopLevelBuild;
	        }},
	    {"PersistentShaderTables",
	        "ray-tracing.persistent-shader-tables",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbPersistentShaderTables;
	        }},
	    {"Bindless",
	        "descriptor.bindless",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbBindless;
	        }},
	    {"RuntimeDescriptorArrays",
	        "descriptor.runtime-arrays",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbRuntimeDescriptorArrays;
	        }},
	    {"PartiallyBoundDescriptors",
	        "descriptor.partially-bound",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbPartiallyBound;
	        }},
	    {"UpdateUnusedWhilePending",
	        "descriptor.update-unused-while-pending",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbUpdateUnusedWhilePending;
	        }},
	    {"VariableDescriptorCount",
	        "descriptor.variable-count",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbVariableDescriptorCount;
	        }},
	    {"DirectSamplerHeapIndexing",
	        "descriptor.direct-sampler-heap-indexing",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDirectSamplerHeapIndexing;
	        }},
	    {"DescriptorBuffer",
	        "descriptor.buffer",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDescriptorBuffer;
	        }},
	    {"DescriptorHeap",
	        "descriptor.heap",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDescriptorHeap;
	        }},
	    {"GraphicsQueue",
	        "queue.graphics",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbGraphics;
	        }},
	    {"ComputeQueue",
	        "queue.compute",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbCompute;
	        }},
	    {"CopyQueue",
	        "queue.copy",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbCopy;
	        }},
	    {"TimelineSynchronization",
	        "synchronization.timeline-semaphore",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbTimelineSynchronization;
	        }},
	    {"QueueFamilyOwnershipTransfer",
	        "queue.family-ownership-transfer",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbQueueFamilyOwnershipTransfer;
	        }},
	    {"SparseBindingQueue",
	        "queue.sparse-binding",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbSparseBindingQueue;
	        }},
	    {"ReservedBuffers",
	        "resource.reserved-buffer",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedBuffers;
	        }},
	    {"ReservedTexture2D",
	        "resource.reserved-texture-2d",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedTexture2D;
	        }},
	    {"ReservedTexture3D",
	        "resource.reserved-texture-3d",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedTexture3D;
	        }},
	    {"AliasedMappings",
	        "resource.aliased-sparse-mapping",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbAliasedMappings;
	        }},
	    {"BudgetReservation",
	        "memory.budget-reservation",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbBudgetReservation;
	        }},
	    {"GeometryShaders",
	        "shader.geometry",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbGeometryShaders;
	        }},
	    {"TessellationShaders",
	        "shader.tessellation",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTessellationShaders;
	        }},
	    {"VertexShaderStoresAndAtomics",
	        "shader.vertex-stores-and-atomics",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbVertexShaderStoresAndAtomics;
	        }},
	    {"FragmentShaderStoresAndAtomics",
	        "shader.fragment-stores-and-atomics",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbFragmentShaderStoresAndAtomics;
	        }},
	    {"ResourceCollections",
	        "resource.collections",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbResourceCollections;
	        }},
	    {"ConservativeRasterization",
	        "rendering.conservative-rasterization",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbConservativeRasterization;
	        }},
	    {"VariableRateShading",
	        "rendering.variable-rate-shading",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbVariableRateShading;
	        }},
	    {"VirtualResources",
	        "resource.virtual",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbVirtualResources;
	        }},
	    {"Heaps",
	        "memory.explicit-heap",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbHeaps;
	        }},
	    {"StagingTextures",
	        "texture.staging",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbStagingTextures;
	        }},
	    {"TextureCopies",
	        "texture.copy",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTextureCopies;
	        }},
	    {"TextureResolve",
	        "texture.resolve",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTextureResolve;
	        }},
	    {"ExplicitTransitions",
	        "synchronization.explicit-transition",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbExplicitTransitions;
	        }},
	    {"SplitTransitions",
	        "synchronization.split-transition",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbSplitTransitions;
	        }},
	    {"IndirectCommands",
	        "command.indirect",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbIndirectCommands;
	        }},
	    {"AliasingBarriers",
	        "synchronization.aliasing-barrier",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbAliasingBarriers;
	        }},
	    {"Queries",
	        "query.gpu",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbQueries;
	        }},
	    {"ShaderLibraries",
	        "shader.library",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbShaderLibraries;
	        }},
	    {"PipelineCachePersistence",
	        "pipeline.cache-persistence",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbPipelineCachePersistence;
	        }},
	    {"SubgroupOperations",
	        "shader.wave-operations",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mMachineLearning.mbSubgroupOperations;
	        }},
	    {"BufferDeviceAddress",
	        "resource.buffer-device-address",
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mMachineLearning.mbBufferDeviceAddress;
	        }}};

	struct FArdaLimitRequirementCase
	{
		const char* mName;
		const char* mRequirement;
		uint32_t& (*mCapability)(FArdaRHICapabilities&);
	};

	const FArdaLimitRequirementCase LimitRequirements[] = {{"ResourceDescriptors",
	                                                           "descriptor.resource-capacity",
	                                                           [](FArdaRHICapabilities& C) -> uint32_t&
	                                                           {
		                                                           return C.mDescriptors.mMaxResourceDescriptors;
	                                                           }},
	    {"SamplerDescriptors",
	        "descriptor.sampler-capacity",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mDescriptors.mMaxSamplerDescriptors;
	        }},
	    {"RayRecursionDepth",
	        "ray-tracing.recursion-depth",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRecursionDepth;
	        }},
	    {"RayPayloadSize",
	        "ray-tracing.max-payload-bytes",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRayPayloadSize;
	        }},
	    {"RayDispatchInvocations",
	        "ray-tracing.max-dispatch-invocations",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRayDispatchInvocations;
	        }},
	    {"GraphicsTimestampValidBits",
	        "queue.graphics.timestamp-bits",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mGraphicsTimestampValidBits;
	        }},
	    {"ComputeTimestampValidBits",
	        "queue.compute.timestamp-bits",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mComputeTimestampValidBits;
	        }},
	    {"CopyTimestampValidBits",
	        "queue.copy.timestamp-bits",
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mCopyTimestampValidBits;
	        }}};

	TEST(ArdaFeatureRequirements, NamedConstraintsIntersectBoundsAndCombineRequiredBits)
	{
		FArdaRHIFeatureRequirements Requirements{{"example.mask", 3, 1, 15}, {"shader.float64"}};
		Requirements.Require("example.mask", 5, 4, 12);
		ASSERT_EQ(Requirements.GetEntries().size(), 2u);
		const auto* Mask = Requirements.Find("example.mask");
		ASSERT_NE(Mask, nullptr);
		EXPECT_EQ(Mask->mMinimumValue, 5u);
		EXPECT_EQ(Mask->mRequiredBits, 5u);
		EXPECT_EQ(Mask->mMaximumValue, 12u);
		FArdaRHICapabilities Device;
		auto Features = eastl::make_shared<FArdaBackendFeatureList>();
		Features->Add("example.mask", 5);
		Features->Add("shader.float64", 1);
		Device.PublishBackendFeatures(eastl::move(Features));
		EXPECT_TRUE(Device.Evaluate(Requirements).IsSupported());
		Requirements.Require("example.mask", 13);
		EXPECT_FALSE(Device.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, QualifiedOperationAndStartupUseTheSamePublishedValue)
	{
		FArdaRHICapabilities Device;
		auto Features = eastl::make_shared<FArdaBackendFeatureList>();
		Features->Add("ray-tracing.pipeline", 1);
		Features->Add("example.driver-ability", 1);
		Device.mRayTracing.mbPipelineShaders = false;
		Device.PublishBackendFeatures(eastl::move(Features));
		EXPECT_FALSE(Device.mBackendFeatures->Supports("ray-tracing.pipeline"));
		EXPECT_FALSE(Device.Evaluate({{"ray-tracing.pipeline"}}).IsSupported());
		EXPECT_TRUE(Device.Evaluate({{"example.driver-ability"}}).IsSupported());
	}

	TEST(ArdaFeatureRequirements, PublishedSnapshotIsIndependentOfARetainedProviderBuilder)
	{
		auto Builder = eastl::make_shared<FArdaBackendFeatureList>();
		Builder->Add("example.optional", 1);
		FArdaRHICapabilities Device;
		Device.PublishBackendFeatures(Builder);
		Builder->Add("example.optional", 0);
		EXPECT_TRUE(Device.GetBackendFeatureList()->Supports("example.optional"));
	}

	TEST(ArdaFeatureRequirements, BoundsMustAdmitTheRequestedFlagBits)
	{
		FArdaBackendFeatureRequirement Mask{"example.mask", 7, 8, 8};
		EXPECT_TRUE(Mask.IsValid());
		Mask.mMaximumValue = 7;
		EXPECT_FALSE(Mask.IsValid());
		Mask.mMaximumValue = UINT64_MAX;
		Mask.mRequiredBits = uint64_t{1} << 63;
		EXPECT_TRUE(Mask.IsValid());
		Mask.mMaximumValue = Mask.mRequiredBits - 1;
		EXPECT_FALSE(Mask.IsValid());
	}

	TEST(ArdaFeatureRequirements, ProviderMustRegisterCanonicalNames)
	{
		FArdaBackendFeatureList Features;
		for (const char* Name : {"", "shader", ".shader", "shader.", "shader..mesh", "Shader.mesh", "VK_KHR_ray_query"})
		{
			EXPECT_THROW(Features.Add(Name, 1), std::invalid_argument) << Name;
		}
		EXPECT_NO_THROW(Features.Add("shader.float16", 0));
		EXPECT_NO_THROW(Features.Add("format.rgba8-unorm.sampled", 1));
	}

	TEST(ArdaFeatureRequirements, EmptyRequirementsAdmitAnUnqualifiedProvider)
	{
		FArdaRHICapabilities Capabilities;
		Capabilities.mQueues.mbGraphics = false;
		const auto Report = Capabilities.Evaluate({});
		EXPECT_TRUE(Report.IsSupported());
		EXPECT_TRUE(Report.ToStatus());
		EXPECT_TRUE(Report.mMissingAbilities.empty());
	}

	TEST(ArdaFeatureRequirements, AdditionalProviderAbilitiesAreIndependentlyRequired)
	{
		for (const auto& Case : BooleanRequirements)
		{
			SCOPED_TRACE(Case.mName);
			FArdaRHIFeatureRequirements Requirements;
			Requirements.Require(Case.mRequirement);
			FArdaRHICapabilities Capabilities;
			Case.mCapability(Capabilities) = false;
			const auto Missing = Capabilities.Evaluate(Requirements);
			EXPECT_FALSE(Missing.IsSupported());
			EXPECT_EQ(Missing.mMissingAbilities.size(), 1u);
			EXPECT_EQ(Missing.ToStatus().mCode, EArdaRHIResult::Unsupported);

			Case.mCapability(Capabilities) = true;
			EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		}
	}

	TEST(ArdaFeatureRequirements, ReportsAllMissingOperationAndCapacityRequirementsTogether)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.Require("texture.copy");
		Requirements.Require("resource.collections");
		Requirements.Require("descriptor.direct-sampler-heap-indexing");
		Requirements.Require("descriptor.resource-capacity", 4096);
		Requirements.Require("ray-tracing.max-payload-bytes", 64);

		FArdaRHICapabilities Capabilities;
		const auto Report = Capabilities.Evaluate(Requirements);
		ASSERT_EQ(Report.mMissingAbilities.size(), 5u);
		const auto Status = Report.ToStatus();
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		for (const char* Label : {"texture.copy",
		         "resource.collections",
		         "descriptor.direct-sampler-heap-indexing",
		         "descriptor.resource-capacity",
		         "ray-tracing.max-payload-bytes"})
		{
			EXPECT_NE(Status.mMessage.find(Label), eastl::string::npos) << Label;
		}

		Capabilities.mbTextureCopies = true;
		Capabilities.mbResourceCollections = true;
		Capabilities.mDescriptors.mbDirectSamplerHeapIndexing = true;
		Capabilities.mDescriptors.mMaxResourceDescriptors = 4096;
		Capabilities.mRayTracing.mMaxRayPayloadSize = 64;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, CapacityLimitsRejectUnknownAndInsufficientValues)
	{
		for (const auto& Case : LimitRequirements)
		{
			SCOPED_TRACE(Case.mName);
			FArdaRHIFeatureRequirements Requirements;
			Requirements.Require(Case.mRequirement, 16);
			FArdaRHICapabilities Capabilities;
			Capabilities.mQueues.mbGraphics = true;
			Capabilities.mQueues.mbCompute = true;
			Capabilities.mQueues.mbCopy = true;
			// Zero is unknown or unavailable, never an unlimited-capacity promise.
			const auto Unknown = Capabilities.Evaluate(Requirements);
			EXPECT_FALSE(Unknown.IsSupported());
			EXPECT_EQ(Unknown.mMissingAbilities.size(), 1u);
			Case.mCapability(Capabilities) = 15;
			EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
			Case.mCapability(Capabilities) = 16;
			EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
			Case.mCapability(Capabilities) = 32;
			EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		}
	}

	TEST(ArdaFeatureRequirements, TimestampWidthsDoNotAdmitAnUnavailableQueue)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.Require("queue.copy.timestamp-bits", 32);
		FArdaRHICapabilities Capabilities;
		Capabilities.mQueues.mCopyTimestampValidBits = 64;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Capabilities.mQueues.mbCopy = true;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, MinimumTiersRejectWeakerQualifiedImplementations)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.Require("ray-tracing.tier", static_cast<uint64_t>(EArdaRHIRayTracingTier::HardwareInlineQueries));
		Requirements.Require("shader.mesh-tier", 2);
		Requirements.Require("work-graph.tier", static_cast<uint64_t>(EArdaRHIWorkGraphTier::MeshNodes));
		Requirements.Require("sampler.feedback-tier", static_cast<uint64_t>(EArdaRHISamplerFeedbackTier::UnrestrictedAddressingAndViews));

		FArdaRHICapabilities Capabilities;
		EXPECT_EQ(Capabilities.Evaluate(Requirements).mMissingAbilities.size(), 4u);
		Capabilities.mRayTracing.mbInfrastructure = true;
		Capabilities.mRayTracing.mbHardwareAccelerated = true;
		Capabilities.mRayTracing.mbAccelerationStructures = true;
		Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		Capabilities.mWorkGraphTier = EArdaRHIWorkGraphTier::ComputeNodes;
		Capabilities.mSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::RestrictedAddressingAndViews;
		EXPECT_EQ(Capabilities.Evaluate(Requirements).mMissingAbilities.size(), 3u);

		Capabilities.mRayTracing.mbInlineRayQueries = true;
		Capabilities.mWorkGraphTier = EArdaRHIWorkGraphTier::MeshNodes;
		Capabilities.mSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::UnrestrictedAddressingAndViews;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Capabilities.mRayTracing.mbOpacityMicromaps = true;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, MeshOnlyDoesNotSatisfyAmplificationDespiteItsNumericValue)
	{
		static_assert(static_cast<uint8_t>(EArdaRHIMeshShaderTier::MeshAndAmplificationShaders) == 1);
		static_assert(static_cast<uint8_t>(EArdaRHIMeshShaderTier::MeshShadersOnly) == 2);
		FArdaRHIFeatureRequirements Requirements;
		Requirements.Require("shader.mesh");
		FArdaRHICapabilities Capabilities;
		Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshShadersOnly;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.Require("shader.mesh-tier", 2);
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements = {{"shader.mesh-tier", 1}};
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.Require("shader.mesh-tier", 255);
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, SubgroupContractCoversEveryPossibleNativeWidth)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.Require("shader.subgroup-min-size", 32);
		Requirements.Require("shader.subgroup-max-size", 1, 0, 64);
		FArdaRHICapabilities Capabilities;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		auto& ML = Capabilities.mMachineLearning;
		ML.mbSubgroupOperations = true;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mSubgroupMinSize = 32;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mSubgroupMaxSize = 64;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mSubgroupMinSize = 16;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mSubgroupMinSize = 32;
		ML.mSubgroupMaxSize = 128;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());

		// An exposed [32,64] interval does not promise a selectable 32-lane subgroup.
		Requirements.Require("shader.subgroup-max-size", 1, 0, 32);
		ML.mSubgroupMaxSize = 64;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mSubgroupMaxSize = 32;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		ML.mbSubgroupOperations = false;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, SubgroupContractSupportsOpenBoundsAndRejectsInvalidIntervals)
	{
		FArdaRHIFeatureRequirements Requirements;
		FArdaRHICapabilities Capabilities;
		auto& ML = Capabilities.mMachineLearning;
		ML.mbSubgroupOperations = true;
		ML.mSubgroupMinSize = 32;
		ML.mSubgroupMaxSize = 64;

		Requirements.Require("shader.subgroup-min-size", 16);
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements = {};
		Requirements.Require("shader.subgroup-max-size", 1, 0, 128);
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.Require("shader.subgroup-min-size", 64);
		Requirements.Require("shader.subgroup-max-size", 1, 0, 32);
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements = {{"shader.subgroup-max-size", 1, 0, 32}};
		ML.mSubgroupMinSize = 64;
		ML.mSubgroupMaxSize = 32;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
	}
}
