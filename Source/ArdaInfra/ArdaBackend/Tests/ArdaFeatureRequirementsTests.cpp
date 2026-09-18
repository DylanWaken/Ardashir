#include "RHI/Config/ArdaRHICapabilities.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FArdaBooleanRequirementCase
	{
		const char* mName;
		bool FArdaRHIFeatureRequirements::* mRequirement;
		bool& (*mCapability)(FArdaRHICapabilities&);
	};

	const FArdaBooleanRequirementCase BooleanRequirements[] = {
	    {"IndirectFirstInstance",
	        &FArdaRHIFeatureRequirements::mbRequireIndirectFirstInstance,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbIndirectFirstInstance;
	        }},
	    {"InlineRayQueries",
	        &FArdaRHIFeatureRequirements::mbRequireInlineRayQueries,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbInlineRayQueries;
	        }},
	    {"BottomLevelAccelerationStructures",
	        &FArdaRHIFeatureRequirements::mbRequireBottomLevelAccelerationStructures,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbBottomLevel;
	        }},
	    {"TopLevelAccelerationStructures",
	        &FArdaRHIFeatureRequirements::mbRequireTopLevelAccelerationStructures,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbTopLevel;
	        }},
	    {"IndirectTopLevelBuild",
	        &FArdaRHIFeatureRequirements::mbRequireIndirectTopLevelBuild,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbIndirectTopLevelBuild;
	        }},
	    {"PersistentShaderTables",
	        &FArdaRHIFeatureRequirements::mbRequirePersistentShaderTables,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mRayTracing.mbPersistentShaderTables;
	        }},
	    {"Bindless",
	        &FArdaRHIFeatureRequirements::mbRequireBindless,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbBindless;
	        }},
	    {"RuntimeDescriptorArrays",
	        &FArdaRHIFeatureRequirements::mbRequireRuntimeDescriptorArrays,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbRuntimeDescriptorArrays;
	        }},
	    {"PartiallyBoundDescriptors",
	        &FArdaRHIFeatureRequirements::mbRequirePartiallyBoundDescriptors,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbPartiallyBound;
	        }},
	    {"UpdateUnusedWhilePending",
	        &FArdaRHIFeatureRequirements::mbRequireUpdateUnusedWhilePending,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbUpdateUnusedWhilePending;
	        }},
	    {"VariableDescriptorCount",
	        &FArdaRHIFeatureRequirements::mbRequireVariableDescriptorCount,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbVariableDescriptorCount;
	        }},
	    {"DirectSamplerHeapIndexing",
	        &FArdaRHIFeatureRequirements::mbRequireDirectSamplerHeapIndexing,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDirectSamplerHeapIndexing;
	        }},
	    {"DescriptorBuffer",
	        &FArdaRHIFeatureRequirements::mbRequireDescriptorBuffer,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDescriptorBuffer;
	        }},
	    {"DescriptorHeap",
	        &FArdaRHIFeatureRequirements::mbRequireDescriptorHeap,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mDescriptors.mbDescriptorHeap;
	        }},
	    {"GraphicsQueue",
	        &FArdaRHIFeatureRequirements::mbRequireGraphicsQueue,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbGraphics;
	        }},
	    {"ComputeQueue",
	        &FArdaRHIFeatureRequirements::mbRequireComputeQueue,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbCompute;
	        }},
	    {"CopyQueue",
	        &FArdaRHIFeatureRequirements::mbRequireCopyQueue,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbCopy;
	        }},
	    {"TimelineSynchronization",
	        &FArdaRHIFeatureRequirements::mbRequireTimelineSynchronization,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbTimelineSynchronization;
	        }},
	    {"QueueFamilyOwnershipTransfer",
	        &FArdaRHIFeatureRequirements::mbRequireQueueFamilyOwnershipTransfer,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbQueueFamilyOwnershipTransfer;
	        }},
	    {"SparseBindingQueue",
	        &FArdaRHIFeatureRequirements::mbRequireSparseBindingQueue,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mQueues.mbSparseBindingQueue;
	        }},
	    {"ReservedBuffers",
	        &FArdaRHIFeatureRequirements::mbRequireReservedBuffers,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedBuffers;
	        }},
	    {"ReservedTexture2D",
	        &FArdaRHIFeatureRequirements::mbRequireReservedTexture2D,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedTexture2D;
	        }},
	    {"ReservedTexture3D",
	        &FArdaRHIFeatureRequirements::mbRequireReservedTexture3D,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbReservedTexture3D;
	        }},
	    {"AliasedMappings",
	        &FArdaRHIFeatureRequirements::mbRequireAliasedMappings,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbAliasedMappings;
	        }},
	    {"BudgetReservation",
	        &FArdaRHIFeatureRequirements::mbRequireBudgetReservation,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mResidency.mbBudgetReservation;
	        }},
	    {"GeometryShaders",
	        &FArdaRHIFeatureRequirements::mbRequireGeometryShaders,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbGeometryShaders;
	        }},
	    {"TessellationShaders",
	        &FArdaRHIFeatureRequirements::mbRequireTessellationShaders,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTessellationShaders;
	        }},
	    {"ResourceCollections",
	        &FArdaRHIFeatureRequirements::mbRequireResourceCollections,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbResourceCollections;
	        }},
	    {"ConservativeRasterization",
	        &FArdaRHIFeatureRequirements::mbRequireConservativeRasterization,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbConservativeRasterization;
	        }},
	    {"VariableRateShading",
	        &FArdaRHIFeatureRequirements::mbRequireVariableRateShading,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbVariableRateShading;
	        }},
	    {"VirtualResources",
	        &FArdaRHIFeatureRequirements::mbRequireVirtualResources,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbVirtualResources;
	        }},
	    {"Heaps",
	        &FArdaRHIFeatureRequirements::mbRequireHeaps,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbHeaps;
	        }},
	    {"StagingTextures",
	        &FArdaRHIFeatureRequirements::mbRequireStagingTextures,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbStagingTextures;
	        }},
	    {"TextureCopies",
	        &FArdaRHIFeatureRequirements::mbRequireTextureCopies,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTextureCopies;
	        }},
	    {"TextureResolve",
	        &FArdaRHIFeatureRequirements::mbRequireTextureResolve,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbTextureResolve;
	        }},
	    {"ExplicitTransitions",
	        &FArdaRHIFeatureRequirements::mbRequireExplicitTransitions,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbExplicitTransitions;
	        }},
	    {"SplitTransitions",
	        &FArdaRHIFeatureRequirements::mbRequireSplitTransitions,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbSplitTransitions;
	        }},
	    {"IndirectCommands",
	        &FArdaRHIFeatureRequirements::mbRequireIndirectCommands,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbIndirectCommands;
	        }},
	    {"AliasingBarriers",
	        &FArdaRHIFeatureRequirements::mbRequireAliasingBarriers,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbAliasingBarriers;
	        }},
	    {"Queries",
	        &FArdaRHIFeatureRequirements::mbRequireQueries,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbQueries;
	        }},
	    {"ShaderLibraries",
	        &FArdaRHIFeatureRequirements::mbRequireShaderLibraries,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbShaderLibraries;
	        }},
	    {"PipelineCachePersistence",
	        &FArdaRHIFeatureRequirements::mbRequirePipelineCachePersistence,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mbPipelineCachePersistence;
	        }},
	    {"SubgroupOperations",
	        &FArdaRHIFeatureRequirements::mbRequireSubgroupOperations,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mMachineLearning.mbSubgroupOperations;
	        }},
	    {"BufferDeviceAddress",
	        &FArdaRHIFeatureRequirements::mbRequireBufferDeviceAddress,
	        [](FArdaRHICapabilities& C) -> bool&
	        {
		        return C.mMachineLearning.mbBufferDeviceAddress;
	        }}};

	struct FArdaLimitRequirementCase
	{
		const char* mName;
		uint32_t FArdaRHIFeatureRequirements::* mRequirement;
		uint32_t& (*mCapability)(FArdaRHICapabilities&);
	};

	const FArdaLimitRequirementCase LimitRequirements[] = {{"ResourceDescriptors",
	                                                           &FArdaRHIFeatureRequirements::mMinResourceDescriptors,
	                                                           [](FArdaRHICapabilities& C) -> uint32_t&
	                                                           {
		                                                           return C.mDescriptors.mMaxResourceDescriptors;
	                                                           }},
	    {"SamplerDescriptors",
	        &FArdaRHIFeatureRequirements::mMinSamplerDescriptors,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mDescriptors.mMaxSamplerDescriptors;
	        }},
	    {"RayRecursionDepth",
	        &FArdaRHIFeatureRequirements::mMinRayRecursionDepth,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRecursionDepth;
	        }},
	    {"RayPayloadSize",
	        &FArdaRHIFeatureRequirements::mMinRayPayloadSize,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRayPayloadSize;
	        }},
	    {"RayDispatchInvocations",
	        &FArdaRHIFeatureRequirements::mMinRayDispatchInvocations,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mRayTracing.mMaxRayDispatchInvocations;
	        }},
	    {"GraphicsTimestampValidBits",
	        &FArdaRHIFeatureRequirements::mMinGraphicsTimestampValidBits,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mGraphicsTimestampValidBits;
	        }},
	    {"ComputeTimestampValidBits",
	        &FArdaRHIFeatureRequirements::mMinComputeTimestampValidBits,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mComputeTimestampValidBits;
	        }},
	    {"CopyTimestampValidBits",
	        &FArdaRHIFeatureRequirements::mMinCopyTimestampValidBits,
	        [](FArdaRHICapabilities& C) -> uint32_t&
	        {
		        return C.mQueues.mCopyTimestampValidBits;
	        }}};

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
			Requirements.*(Case.mRequirement) = true;
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
		Requirements.mbRequireTextureCopies = true;
		Requirements.mbRequireResourceCollections = true;
		Requirements.mbRequireDirectSamplerHeapIndexing = true;
		Requirements.mMinResourceDescriptors = 4096;
		Requirements.mMinRayPayloadSize = 64;

		FArdaRHICapabilities Capabilities;
		const auto Report = Capabilities.Evaluate(Requirements);
		ASSERT_EQ(Report.mMissingAbilities.size(), 5u);
		const auto Status = Report.ToStatus();
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		for (const char* Label : {"texture copies",
		         "resource collections",
		         "direct sampler heap indexing",
		         "resource descriptor capacity",
		         "ray payload size"})
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
			Requirements.*(Case.mRequirement) = 16;
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
		Requirements.mMinCopyTimestampValidBits = 32;
		FArdaRHICapabilities Capabilities;
		Capabilities.mQueues.mCopyTimestampValidBits = 64;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Capabilities.mQueues.mbCopy = true;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, MinimumTiersRejectWeakerQualifiedImplementations)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.mMinRayTracingTier = EArdaRHIRayTracingTier::HardwareInlineQueries;
		Requirements.mMinMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		Requirements.mMinWorkGraphTier = EArdaRHIWorkGraphTier::MeshNodes;
		Requirements.mMinSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::UnrestrictedAddressingAndViews;

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
		Requirements.mbRequireMeshShaders = true;
		FArdaRHICapabilities Capabilities;
		Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshShadersOnly;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinMeshShaderTier = EArdaRHIMeshShaderTier::MeshShadersOnly;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinMeshShaderTier = static_cast<EArdaRHIMeshShaderTier>(255);
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
	}

	TEST(ArdaFeatureRequirements, SubgroupContractCoversEveryPossibleNativeWidth)
	{
		FArdaRHIFeatureRequirements Requirements;
		Requirements.mMinSubgroupSize = 32;
		Requirements.mMaxSubgroupSize = 64;
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
		Requirements.mMaxSubgroupSize = 32;
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

		Requirements.mMinSubgroupSize = 16;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinSubgroupSize = 0;
		Requirements.mMaxSubgroupSize = 128;
		EXPECT_TRUE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinSubgroupSize = 64;
		Requirements.mMaxSubgroupSize = 32;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
		Requirements.mMinSubgroupSize = 0;
		ML.mSubgroupMinSize = 64;
		ML.mSubgroupMaxSize = 32;
		EXPECT_FALSE(Capabilities.Evaluate(Requirements).IsSupported());
	}
}
