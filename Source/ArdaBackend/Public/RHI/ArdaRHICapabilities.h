/** @file ArdaRHICapabilities.h
 * Declares structured desktop-GPU capabilities and feature requirements.
 */

#pragma once

#include "ArdaRHITypes.h"

namespace arda::rhi
{
    inline constexpr uint32_t ArdaRHIInvalidQueueFamily = 0xffffffffu;

    /** Native ray-tracing implementation level. */
    enum class EArdaRHIRayTracingTier : uint8_t
    {
        /** No native or software ray-tracing implementation. */
        None,
        /** Software-emulated ray tracing. */
        Software,
        /** Hardware acceleration structures and shader traversal. */
        HardwareAccelerationStructures,
        /** Hardware traversal including inline ray queries. */
        HardwareInlineQueries,
        /** Hardware traversal including opacity micromaps. */
        HardwareOpacityMicromaps
    };

    /** Native mesh-shader implementation level. */
    enum class EArdaRHIMeshShaderTier : uint8_t
    {
        None,
        /** Mesh and optional amplification shader stages are available. */
        MeshAndAmplificationShaders
    };
    /** Native work-graph implementation level. */
    enum class EArdaRHIWorkGraphTier : uint8_t
    {
        None,
        /** Compute-style work-graph nodes are available. */
        ComputeNodes,
        /** Work graphs can feed mesh nodes into rasterization. */
        MeshNodes
    };
    /** Native sampler-feedback implementation level. */
    enum class EArdaRHISamplerFeedbackTier : uint8_t
    {
        None,
        /** Feedback is limited to wrap/clamp addressing and full-resource views. */
        RestrictedAddressingAndViews,
        /** Feedback supports every addressing mode and arbitrary resource views. */
        UnrestrictedAddressingAndViews
    };

    /** Ray-tracing abilities and native limits reported by one device. */
    struct FArdaRHIRayTracingCapabilities
    {
        bool mbInfrastructure = false;
        bool mbHardwareAccelerated = false;
        bool mbPipelineShaders = false;
        bool mbInlineRayQueries = false;
        bool mbAccelerationStructures = false;
        bool mbBottomLevel = false;
        bool mbTopLevel = false;
        bool mbBuildUpdate = false;
        bool mbCompaction = false;
        bool mbIndirectDispatch = false;
        bool mbIndirectTopLevelBuild = false;
        bool mbLocalShaderTableArguments = false;
        bool mbPersistentShaderTables = false;
        bool mbOpacityMicromaps = false;
        uint32_t mShaderIdentifierSize = 0;
        uint32_t mShaderRecordAlignment = 0;
        uint32_t mShaderTableAlignment = 0;
        uint32_t mAccelerationStructureAlignment = 0;
        uint32_t mMaxRecursionDepth = 0;
        uint32_t mMaxRayPayloadSize = 0;
        /** Maximum ray-generation invocations accepted by one direct dispatch. */
        uint32_t mMaxRayDispatchInvocations = 0;

        /**
         * Derives the summary tier from the authoritative individual abilities.
         * @return The highest fully reported ray-tracing implementation level.
         */
        [[nodiscard]] EArdaRHIRayTracingTier GetTier() const noexcept
        {
            if (!mbInfrastructure)
                return EArdaRHIRayTracingTier::None;
            if (!mbHardwareAccelerated)
                return EArdaRHIRayTracingTier::Software;
            if (!mbAccelerationStructures)
                return EArdaRHIRayTracingTier::None;
            if (mbOpacityMicromaps)
                return EArdaRHIRayTracingTier::HardwareOpacityMicromaps;
            if (mbInlineRayQueries)
                return EArdaRHIRayTracingTier::HardwareInlineQueries;
            return EArdaRHIRayTracingTier::HardwareAccelerationStructures;
        }
    };

    /** Bindless and direct descriptor-indexing abilities. */
    struct FArdaRHIDescriptorCapabilities
    {
        bool mbBindless = false;
        bool mbRuntimeDescriptorArrays = false;
        bool mbUnboundedArrays = false;
        bool mbPartiallyBound = false;
        bool mbUpdateAfterBind = false;
        bool mbUpdateUnusedWhilePending = false;
        bool mbVariableDescriptorCount = false;
        bool mbDirectResourceHeapIndexing = false;
        bool mbDirectSamplerHeapIndexing = false;
        bool mbDescriptorBuffer = false;
        bool mbDescriptorHeap = false;
        uint32_t mMaxResourceDescriptors = 0;
        uint32_t mMaxSamplerDescriptors = 0;
    };

    /** Queue topology and native GPU synchronization abilities. */
    struct FArdaRHIQueueCapabilities
    {
        bool mbGraphics = true;
        bool mbCompute = false;
        bool mbCopy = false;
        bool mbDedicatedComputeFamily = false;
        bool mbDedicatedCopyFamily = false;
        bool mbGpuWaits = false;
        bool mbTimelineSynchronization = false;
        bool mbQueueFamilyOwnershipTransfer = false;
        bool mbSparseBindingQueue = false;
        uint32_t mGraphicsFamily = ArdaRHIInvalidQueueFamily;
        uint32_t mComputeFamily = ArdaRHIInvalidQueueFamily;
        uint32_t mCopyFamily = ArdaRHIInvalidQueueFamily;

        [[nodiscard]] bool IsSupported(EArdaRHIQueueType Queue) const noexcept
        {
            switch (Queue)
            {
            case EArdaRHIQueueType::Graphics: return mbGraphics;
            case EArdaRHIQueueType::Compute: return mbCompute;
            case EArdaRHIQueueType::Copy: return mbCopy;
            }
            return false;
        }

        [[nodiscard]] uint32_t GetFamily(EArdaRHIQueueType Queue) const noexcept
        {
            switch (Queue)
            {
            case EArdaRHIQueueType::Graphics: return mGraphicsFamily;
            case EArdaRHIQueueType::Compute: return mComputeFamily;
            case EArdaRHIQueueType::Copy: return mCopyFamily;
            }
            return ArdaRHIInvalidQueueFamily;
        }
    };

    /** Sparse/reserved-resource and streaming-budget abilities. */
    struct FArdaRHIResidencyCapabilities
    {
        bool mbSparseBinding = false;
        bool mbReservedBuffers = false;
        bool mbReservedTexture2D = false;
        bool mbReservedTexture3D = false;
        bool mbAliasedMappings = false;
        bool mbStreamingBudget = false;
        bool mbBudgetReservation = false;
        uint64_t mTileSizeInBytes = 0;
    };

    /** Portable compute facts used to admit ML-oriented modules. */
    struct FArdaRHIMachineLearningCapabilities
    {
        bool mbSubgroupOperations = false;
        bool mbNativeFloat16 = false;
        bool mbNativeInt8 = false;
        bool mbBufferDeviceAddress = false;
        uint32_t mSubgroupMinSize = 0;
        uint32_t mSubgroupMaxSize = 0;
    };

    /** A module's explicit desktop-GPU admission requirements. */
    struct FArdaRHIFeatureRequirements
    {
        bool mbRequireRayTracingInfrastructure = false;
        bool mbRequireHardwareRayTracing = false;
        bool mbRequireRayTracingPipelines = false;
        bool mbRequireAccelerationStructures = false;
        bool mbRequireAccelerationStructureUpdate = false;
        bool mbRequireAccelerationStructureCompaction = false;
        bool mbRequireIndirectRayDispatch = false;
        bool mbRequireLocalShaderTableArguments = false;
        bool mbRequireOpacityMicromaps = false;
        bool mbRequireMeshShaders = false;
        bool mbRequireUnboundedDescriptors = false;
        bool mbRequireUpdateAfterBind = false;
        bool mbRequireDirectDescriptorIndexing = false;
        bool mbRequireDedicatedComputeQueue = false;
        bool mbRequireDedicatedCopyQueue = false;
        bool mbRequireGpuQueueWaits = false;
        bool mbRequireSparseResidency = false;
        bool mbRequireStreamingBudget = false;
        bool mbRequireSamplerFeedback = false;
        bool mbRequireWorkGraphs = false;
        bool mbRequireShaderBundles = false;
        bool mbRequireCustomPresent = false;
        bool mbRequireNativeFloat16 = false;
        bool mbRequireNativeInt8 = false;
    };

    /** Detailed result of evaluating a feature requirement set. */
    struct FArdaRHIFeatureSupportReport
    {
        eastl::vector<eastl::string> mMissingAbilities;

        [[nodiscard]] bool IsSupported() const noexcept
        {
            return mMissingAbilities.empty();
        }

        [[nodiscard]] FArdaRHIStatus ToStatus() const
        {
            if (mMissingAbilities.empty()) return {};
            eastl::string Message = "Missing RHI abilities: ";
            for (size_t Index = 0; Index < mMissingAbilities.size(); ++Index)
            {
                if (Index) Message += ", ";
                Message += mMissingAbilities[Index];
            }
            return { EArdaRHIResult::Unsupported, eastl::move(Message) };
        }
    };

    /** Describes all capabilities reported by an RHI device. */
    struct FArdaRHICapabilities
    {
        FArdaRHIRayTracingCapabilities mRayTracing;
        FArdaRHIDescriptorCapabilities mDescriptors;
        FArdaRHIQueueCapabilities mQueues;
        FArdaRHIResidencyCapabilities mResidency;
        FArdaRHIMachineLearningCapabilities mMachineLearning;
        EArdaRHIMeshShaderTier mMeshShaderTier = EArdaRHIMeshShaderTier::None;
        EArdaRHIWorkGraphTier mWorkGraphTier = EArdaRHIWorkGraphTier::None;
        EArdaRHISamplerFeedbackTier mSamplerFeedbackTier =
            EArdaRHISamplerFeedbackTier::None;
        bool mbShaderBundleDispatch = false;
        bool mbCustomPresent = false;
        bool mbResourceCollections = false;

        // Independent portable abilities not represented by a structured tier.
        bool mbConservativeRasterization = false;
        bool mbVariableRateShading = false;
        bool mbVirtualResources = false;
        bool mbHeaps = false;
        bool mbStagingTextures = false;
        bool mbTextureCopies = false;
        bool mbTextureResolve = false;
        bool mbExplicitTransitions = false;
        bool mbSplitTransitions = false;
        bool mbIndirectCommands = false;
        bool mbAliasingBarriers = false;
        bool mbQueries = false;
        bool mbShaderLibraries = false;
        bool mbPipelineCachePersistence = false;

        [[nodiscard]] bool IsQueueSupported(EArdaRHIQueueType Queue) const noexcept
        {
            return mQueues.IsSupported(Queue);
        }

        /** Evaluates every requested ability and returns all failures. */
        [[nodiscard]] FArdaRHIFeatureSupportReport Evaluate(
            const FArdaRHIFeatureRequirements& R) const
        {
            FArdaRHIFeatureSupportReport Report;
            const auto Need = [&Report](bool Required, bool Present, const char* Name)
            {
                if (Required && !Present) Report.mMissingAbilities.push_back(Name);
            };
            Need(R.mbRequireRayTracingInfrastructure,
                mRayTracing.mbInfrastructure, "ray-tracing infrastructure");
            Need(R.mbRequireHardwareRayTracing,
                mRayTracing.mbHardwareAccelerated, "hardware ray tracing");
            Need(R.mbRequireRayTracingPipelines,
                mRayTracing.mbPipelineShaders, "ray-tracing pipelines");
            Need(R.mbRequireAccelerationStructures,
                mRayTracing.mbAccelerationStructures, "acceleration structures");
            Need(R.mbRequireAccelerationStructureUpdate,
                mRayTracing.mbBuildUpdate, "acceleration-structure update");
            Need(R.mbRequireAccelerationStructureCompaction,
                mRayTracing.mbCompaction, "acceleration-structure compaction");
            Need(R.mbRequireIndirectRayDispatch,
                mRayTracing.mbIndirectDispatch, "indirect ray dispatch");
            Need(R.mbRequireLocalShaderTableArguments,
                mRayTracing.mbLocalShaderTableArguments,
                "local shader-table arguments");
            Need(R.mbRequireOpacityMicromaps,
                mRayTracing.mbOpacityMicromaps, "opacity micromaps");
            Need(R.mbRequireMeshShaders,
                mMeshShaderTier != EArdaRHIMeshShaderTier::None, "mesh shaders");
            Need(R.mbRequireUnboundedDescriptors,
                mDescriptors.mbUnboundedArrays, "unbounded descriptors");
            Need(R.mbRequireUpdateAfterBind,
                mDescriptors.mbUpdateAfterBind, "descriptor update-after-bind");
            Need(R.mbRequireDirectDescriptorIndexing,
                mDescriptors.mbDirectResourceHeapIndexing,
                "direct descriptor indexing");
            Need(R.mbRequireDedicatedComputeQueue,
                mQueues.mbDedicatedComputeFamily, "dedicated compute queue");
            Need(R.mbRequireDedicatedCopyQueue,
                mQueues.mbDedicatedCopyFamily, "dedicated copy queue");
            Need(R.mbRequireGpuQueueWaits, mQueues.mbGpuWaits, "GPU queue waits");
            Need(R.mbRequireSparseResidency,
                mResidency.mbSparseBinding, "sparse residency");
            Need(R.mbRequireStreamingBudget,
                mResidency.mbStreamingBudget, "streaming budget telemetry");
            Need(R.mbRequireSamplerFeedback,
                mSamplerFeedbackTier != EArdaRHISamplerFeedbackTier::None,
                "native sampler feedback");
            Need(R.mbRequireWorkGraphs,
                mWorkGraphTier != EArdaRHIWorkGraphTier::None, "work graphs");
            Need(R.mbRequireShaderBundles,
                mbShaderBundleDispatch, "shader bundles");
            Need(R.mbRequireCustomPresent, mbCustomPresent, "custom present");
            Need(R.mbRequireNativeFloat16,
                mMachineLearning.mbNativeFloat16, "native float16");
            Need(R.mbRequireNativeInt8,
                mMachineLearning.mbNativeInt8, "native int8");
            return Report;
        }
    };
}
