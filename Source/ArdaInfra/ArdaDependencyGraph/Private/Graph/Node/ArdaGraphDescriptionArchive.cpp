#include "Graph/Node/ArdaGraphDescriptionArchive.h"

namespace arda
{
	namespace
	{
		template <class Archive, class Values, class Visit>
		void VisitValues(Archive& A, Values& Items, Visit Function)
		{
			if constexpr (eastl::is_same_v<Archive, FArdaGraphArchiveWriter>)
			{
				if (Items.size() > 1024 * 1024)
				{
					A.Fail("Graph archive collection exceeds the element limit.");
					return;
				}
				A.WriteInteger(Items.size(), 8);
				for (auto& Item : Items)
				{
					if (!A.GetStatus())
					{
						break;
					}
					Function(Item);
				}
			}
			else
			{
				const auto Count = A.ReadCount();
				for (size_t Index = 0; Index < Count && A.GetStatus(); ++Index)
				{
					if (Index == Items.size())
					{
						Items.emplace_back();
					}
					Function(Items[Index]);
				}
				if (Count < Items.size())
				{
					Items.resize(Count);
				}
			}
		}

		template <class Archive, class Origin>
		void DeclarationFields(Archive& A, Origin& D)
		{
			if constexpr (eastl::is_same_v<Archive, FArdaGraphArchiveWriter>)
			{
				if (D.mNode && !A.mNodeIndex)
				{
					A.Fail("Declaration provenance requires a graph archive context.");
					return;
				}
				A.Write(D.mNode ? A.mNodeIndex(D.mNode) : UINT32_MAX);
			}
			else
			{
				D.mNode = A.ReadNode();
			}
			A.Fields(D.mIndex, D.mParentIndex);
		}

		template <class Archive, class Binding>
		void ShaderBindings(Archive& A, Binding& B)
		{
			// Static schemas and native sampler objects are reconstructed during compilation.
			A.Fields(B.mPipelineSlot);
			DeclarationFields(A, B.mDeclaration);
			VisitValues(A,
			    B.mResources,
			    [&](auto& R)
			    {
				    A.Fields(R.mMember, R.mResource, R.mArrayElement, R.mAccess, R.mView);
			    });
			VisitValues(A,
			    B.mValues,
			    [&](auto& V)
			    {
				    A.Fields(V.mMember, V.mBytes);
				    VisitValues(A,
				        V.mDescriptorIndices,
				        [&](auto& I)
				        {
					        A.Fields(I.mTable, I.mSlot, I.mArrayElement, I.mType, I.mByteOffset, I.mbAbsoluteHeapIndex);
				        });
			    });
		}

		template <class Archive, class Description>
		void RasterPipelineFields(Archive& A, Description& D)
		{
			A.Fields(D.mTopology,
			    D.mBlendState.mbAlphaToCoverage,
			    D.mRasterState.mFillMode,
			    D.mRasterState.mCullMode,
			    D.mRasterState.mbFrontCounterClockwise,
			    D.mRasterState.mbDepthClip,
			    D.mRasterState.mbScissor,
			    D.mDepthStencilState.mbDepthTest,
			    D.mDepthStencilState.mbDepthWrite,
			    D.mDepthStencilState.mDepthFunc,
			    D.mColorFormats,
			    D.mDepthFormat,
			    D.mSampleCount,
			    D.mPersistentCacheKey,
			    D.mDebugName);
			for (auto& Target : D.mBlendState.mTargets)
			{
				A.Fields(Target.mbEnable,
				    Target.mSourceColor,
				    Target.mDestinationColor,
				    Target.mSourceAlpha,
				    Target.mDestinationAlpha);
			}
		}

		template <class Archive, class Configuration>
		void PipelineFields(Archive& A, Configuration& C)
		{
			A.Fields(C.mKind);
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				A.Fields(C.mCompute.mDesc.mPersistentCacheKey, C.mCompute.mDesc.mDebugName);
				break;
			case EArdaPipelineStateKind::Graphics:
				RasterPipelineFields(A, C.mGraphics.mDesc);
				A.Fields(C.mGraphics.mDesc.mPatchControlPoints);
				break;
			case EArdaPipelineStateKind::Meshlet:
				RasterPipelineFields(A, C.mMeshlet.mDesc);
				break;
			case EArdaPipelineStateKind::RayTracing:
			{
				auto& D = C.mRayTracing.mDesc;
				A.Fields(D.mMaxPayloadSize,
				    D.mMaxAttributeSize,
				    D.mMaxRecursionDepth,
				    D.mbAllowOpacityMicromaps,
				    D.mPersistentCacheKey,
				    D.mDebugName);
				VisitValues(A,
				    D.mShaders,
				    [&](auto& S)
				    {
					    A.Fields(S.mExportName);
				    });
				VisitValues(A,
				    D.mHitGroups,
				    [&](auto& G)
				    {
					    A.Fields(G.mExportName, G.mbProceduralPrimitive);
				    });
				break;
			}
			case EArdaPipelineStateKind::WorkGraph:
			{
				auto& D = C.mWorkGraph.mDesc;
				A.Fields(D.mProgramName, D.mEntryPoint, D.mMaxInputRecords, D.mPersistentCacheKey, D.mDebugName);
				break;
			}
			default:
				A.Fail("Archived pipeline configuration has an invalid family.");
				break;
			}
		}

		template <class Archive, class Configuration>
		void PipelineConfiguration(Archive& A, Configuration& C)
		{
			bool bPresent = bool(C);
			A.Fields(bPresent);
			if constexpr (eastl::is_same_v<Archive, FArdaGraphArchiveWriter>)
			{
				if (bPresent)
				{
					PipelineFields(A, *C);
				}
			}
			else
			{
				if (!bPresent)
				{
					C.reset();
					return;
				}
				auto Value = C ? eastl::make_shared<FArdaInductorPipelineConfiguration>(*C)
				               : eastl::make_shared<FArdaInductorPipelineConfiguration>();
				PipelineFields(A, *Value);
				C = eastl::move(Value);
			}
		}

		template <class Archive, class Requirements>
		void FeatureFields(Archive& A, Requirements& R)
		{
			A.Fields(R.mbRequireRayTracingInfrastructure,
			    R.mbRequireHardwareRayTracing,
			    R.mbRequireRayTracingPipelines,
			    R.mbRequireAccelerationStructures,
			    R.mbRequireAccelerationStructureUpdate,
			    R.mbRequireAccelerationStructureCompaction,
			    R.mbRequireIndirectRayDispatch,
			    R.mbRequireLocalShaderTableArguments,
			    R.mbRequireOpacityMicromaps,
			    R.mbRequireMeshShaders,
			    R.mbRequireGeometryShaders,
			    R.mbRequireTessellationShaders,
			    R.mbRequireVertexShaderStoresAndAtomics,
			    R.mbRequireFragmentShaderStoresAndAtomics,
			    R.mbRequireUnboundedDescriptors,
			    R.mbRequireUpdateAfterBind,
			    R.mbRequireDirectDescriptorIndexing,
			    R.mbRequireDedicatedComputeQueue,
			    R.mbRequireDedicatedCopyQueue,
			    R.mbRequireGpuQueueWaits,
			    R.mbRequireSparseResidency,
			    R.mbRequireStreamingBudget,
			    R.mbRequireSamplerFeedback,
			    R.mbRequireWorkGraphs,
			    R.mbRequireShaderBundles,
			    R.mbRequireCustomPresent,
			    R.mbRequireNativeFloat16,
			    R.mbRequireNativeInt8,
			    R.mbRequireInlineRayQueries,
			    R.mbRequireBottomLevelAccelerationStructures,
			    R.mbRequireTopLevelAccelerationStructures,
			    R.mbRequireIndirectTopLevelBuild,
			    R.mbRequirePersistentShaderTables,
			    R.mbRequireBindless,
			    R.mbRequireRuntimeDescriptorArrays,
			    R.mbRequirePartiallyBoundDescriptors,
			    R.mbRequireUpdateUnusedWhilePending,
			    R.mbRequireVariableDescriptorCount,
			    R.mbRequireDirectSamplerHeapIndexing,
			    R.mbRequireDescriptorBuffer,
			    R.mbRequireDescriptorHeap,
			    R.mbRequireGraphicsQueue,
			    R.mbRequireComputeQueue,
			    R.mbRequireCopyQueue,
			    R.mbRequireTimelineSynchronization,
			    R.mbRequireQueueFamilyOwnershipTransfer,
			    R.mbRequireSparseBindingQueue,
			    R.mbRequireReservedBuffers,
			    R.mbRequireReservedTexture2D,
			    R.mbRequireReservedTexture3D,
			    R.mbRequireAliasedMappings,
			    R.mbRequireBudgetReservation,
			    R.mbRequireResourceCollections,
			    R.mbRequireConservativeRasterization,
			    R.mbRequireVariableRateShading,
			    R.mbRequireVirtualResources,
			    R.mbRequireHeaps,
			    R.mbRequireStagingTextures,
			    R.mbRequireTextureCopies,
			    R.mbRequireTextureResolve,
			    R.mbRequireExplicitTransitions,
			    R.mbRequireSplitTransitions,
			    R.mbRequireIndirectCommands,
			    R.mbRequireIndirectFirstInstance,
			    R.mbRequireAliasingBarriers,
			    R.mbRequireQueries,
			    R.mbRequireShaderLibraries,
			    R.mbRequirePipelineCachePersistence,
			    R.mbRequireSubgroupOperations,
			    R.mbRequireBufferDeviceAddress,
			    R.mMinRayTracingTier,
			    R.mMinMeshShaderTier,
			    R.mMinWorkGraphTier,
			    R.mMinSamplerFeedbackTier,
			    R.mMinResourceDescriptors,
			    R.mMinSamplerDescriptors,
			    R.mMinRayRecursionDepth,
			    R.mMinRayPayloadSize,
			    R.mMinRayDispatchInvocations,
			    R.mMinGraphicsTimestampValidBits,
			    R.mMinComputeTimestampValidBits,
			    R.mMinCopyTimestampValidBits,
			    R.mMinSubgroupSize,
			    R.mMaxSubgroupSize);
		}

		template <class Archive, class Requirements>
		void RequirementFields(Archive& A, Requirements& R)
		{
			FeatureFields(A, R.mFeatures);
			A.Fields(R.mbRequireCuda,
			    R.mbRequireCudaSurfaces,
			    R.mbRequireCudaLayeredSurfaces,
			    R.mMinCudaComputeCapability,
			    R.mMinCudaThreadsPerBlock,
			    R.mMinCudaSharedMemoryBytes,
			    R.mAllowedCudaLaunchModes);
			// Predicates are application code owned by the registered definition, never serialized functions.
			VisitValues(A,
			    R.mEnvironment,
			    [&](auto& E)
			    {
				    A.Fields(E.mName);
			    });
		}

		template <class Archive, class Description>
		void DescriptionFields(Archive& A, Description& D)
		{
			A.Fields(D.mGraphicsStage,
			    D.mGraphicsInput,
			    D.mbAutomaticRasterFamily,
			    D.mRayStage,
			    D.mRayInputs,
			    D.mColorTargets,
			    D.mDepthTarget,
			    D.mEstimatedCost,
			    D.mWorkspaceBytes,
			    D.mTransientWorkspaceBytes,
			    D.mbSideEffect,
			    D.mbPipelineStageOnly);
			VisitValues(A,
			    D.mAccesses,
			    [&](auto& X)
			    {
				    A.Fields(X.mResource, X.mAccess, X.mState, X.mBufferRange, X.mTextureRange);
				    if (!X.mResource || X.mAccess > EArdaDependencyAccess::AutoDeduction)
				    {
					    A.Fail("Archived node access is invalid.");
				    }
			    });
			VisitValues(A,
			    D.mShaderBindings,
			    [&](auto& B)
			    {
				    ShaderBindings(A, B);
			    });
			VisitValues(A,
			    D.mBindlessTables,
			    [&](auto& T)
			    {
				    DeclarationFields(A, T.mDeclaration);
				    A.Fields(T.mName,
				        T.mPipelineSlot,
				        T.mCapacity,
				        T.mLayout.mVisibility,
				        T.mLayout.mFirstSlot,
				        T.mLayout.mRegisterSpace,
				        T.mLayout.mMaxCapacity,
				        T.mLayout.mbUnbounded,
				        T.mLayout.mbUpdateAfterBind,
				        T.mLayout.mbVariableDescriptorCount,
				        T.mLayout.mbDirectHeapIndexing,
				        T.mLayout.mbDescriptorBuffer,
				        T.mLayout.mLayoutType,
				        T.mLayout.mDebugName);
				    VisitValues(A,
				        T.mLayout.mRegisterSpaces,
				        [&](auto& B)
				        {
					        A.Fields(B.mSlot, B.mArraySize, B.mType);
				        });
				    VisitValues(A,
				        T.mEntries,
				        [&](auto& E)
				        {
					        A.Fields(E.mSlot, E.mArrayElement, E.mType, E.mResource, E.mView, E.mAccess);
				        });
			    });
			VisitValues(A,
			    D.mShaderTables,
			    [&](auto& T)
			    {
				    DeclarationFields(A, T.mDeclaration);
				    A.Fields(T.mPipelineSlot);
				    VisitValues(A,
				        T.mRecords,
				        [&](auto& R)
				        {
					        A.Fields(R.mType,
					            R.mRecordIndex,
					            R.mExportName,
					            R.mLocalArguments,
					            R.mUserData,
					            R.mGeometry,
					            R.mGeometrySegment);
					        ShaderBindings(A, R.mLocalBindings);
				        });
			    });
			VisitValues(A,
			    D.mPipelineStages,
			    [&](auto& S)
			    {
				    DeclarationFields(A, S.mDeclaration);
				    A.Fields(S.mGroup, S.mExportName, S.mHitGroupName);
				    PipelineConfiguration(A, S.mConfiguration);
			    });
			VisitValues(A,
			    D.mPipelines,
			    [&](auto& P)
			    {
				    DeclarationFields(A, P.mDeclaration);
				    A.Fields(P.mSlot, P.mKind, P.mGroup);
				    PipelineConfiguration(A, P.mConfiguration);
			    });
			RequirementFields(A, D.mRequirements);
			if (!D.mEstimatedCost || D.mGraphicsStage > EArdaGraphicsStage::Mesh)
			{
				A.Fail("Archived node description is invalid.");
			}
		}
	}

	void WriteArdaGraphDescription(FArdaGraphArchiveWriter& Archive, const FArdaDependencyNodeDesc& Description)
	{
		DescriptionFields(Archive, Description);
	}

	void ReadArdaGraphDescription(FArdaGraphArchiveReader& Archive, FArdaDependencyNodeDesc& Description)
	{
		DescriptionFields(Archive, Description);
		Description.mbArchivedShaderAccesses = true;
	}

	void WriteArdaGraphRequirements(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyNodeRequirements& Requirements)
	{
		RequirementFields(Archive, Requirements);
	}

	void ReadArdaGraphRequirements(FArdaGraphArchiveReader& Archive, FArdaDependencyNodeRequirements& Requirements)
	{
		RequirementFields(Archive, Requirements);
	}

}
