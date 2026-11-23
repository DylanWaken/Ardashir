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

		void ReadLegacyFeatures(FArdaGraphArchiveReader& A, FArdaRHIFeatureRequirements& R)
		{
			// Versions 2 through 4 serialized fields positionally. Preserve their exact byte order.
			static constexpr const char* BooleanKeys[] = {
			    "ray-tracing.infrastructure",
			    "ray-tracing.hardware",
			    "ray-tracing.pipeline",
			    "ray-tracing.acceleration-structure",
			    "ray-tracing.acceleration-structure-update",
			    "ray-tracing.acceleration-structure-compaction",
			    "ray-tracing.indirect-dispatch",
			    "ray-tracing.local-shader-table-arguments",
			    "ray-tracing.opacity-micromap",
			    "shader.mesh",
			    "shader.geometry",
			    "shader.tessellation",
			    "shader.vertex-stores-and-atomics",
			    "shader.fragment-stores-and-atomics",
			    "descriptor.unbounded-arrays",
			    "descriptor.update-after-bind",
			    "descriptor.direct-resource-heap-indexing",
			    "queue.dedicated-compute",
			    "queue.dedicated-copy",
			    "queue.gpu-waits",
			    "resource.sparse-binding",
			    "memory.streaming-budget",
			    "sampler.feedback",
			    "work-graph.dispatch",
			    "shader.bundle-dispatch",
			    "presentation.custom-present",
			    "shader.float16",
			    "shader.integer-dot-product",
			    "ray-tracing.inline-query",
			    "ray-tracing.bottom-level-acceleration-structure",
			    "ray-tracing.top-level-acceleration-structure",
			    "ray-tracing.indirect-top-level-build",
			    "ray-tracing.persistent-shader-tables",
			    "descriptor.bindless",
			    "descriptor.runtime-arrays",
			    "descriptor.partially-bound",
			    "descriptor.update-unused-while-pending",
			    "descriptor.variable-count",
			    "descriptor.direct-sampler-heap-indexing",
			    "descriptor.buffer",
			    "descriptor.heap",
			    "queue.graphics",
			    "queue.compute",
			    "queue.copy",
			    "synchronization.timeline-semaphore",
			    "queue.family-ownership-transfer",
			    "queue.sparse-binding",
			    "resource.reserved-buffer",
			    "resource.reserved-texture-2d",
			    "resource.reserved-texture-3d",
			    "resource.aliased-sparse-mapping",
			    "memory.budget-reservation",
			    "resource.collections",
			    "rendering.conservative-rasterization",
			    "rendering.variable-rate-shading",
			    "resource.virtual",
			    "memory.explicit-heap",
			    "texture.staging",
			    "texture.copy",
			    "texture.resolve",
			    "synchronization.explicit-transition",
			    "synchronization.split-transition",
			    "command.indirect",
			    "command.indirect-first-instance",
			    "synchronization.aliasing-barrier",
			    "query.gpu",
			    "shader.library",
			    "pipeline.cache-persistence",
			    "shader.wave-operations",
			    "resource.buffer-device-address",
			};
			for (const char* Key : BooleanKeys)
			{
				bool Value = false;
				A.Read(Value);
				if (Value)
				{
					R.Require(Key);
				}
			}
			const auto ReadLegacyMinimum = [&](auto Value, const char* Key)
			{
				A.Read(Value);
				if (Value)
				{
					R.Require(Key, Value);
				}
			};
			ReadLegacyMinimum(uint8_t{}, "ray-tracing.tier");
			// The old enum stored amplification support as 1 and mesh-only as 2.
			uint8_t MeshTier = 0;
			A.Read(MeshTier);
			if (MeshTier == 1 || MeshTier == 2)
			{
				R.Require("shader.mesh-tier", 3 - MeshTier);
			}
			else if (MeshTier > 2)
			{
				A.Fail("Archived mesh shader tier is invalid.");
			}
			ReadLegacyMinimum(uint8_t{}, "work-graph.tier");
			ReadLegacyMinimum(uint8_t{}, "sampler.feedback-tier");
			ReadLegacyMinimum(uint32_t{}, "descriptor.resource-capacity");
			ReadLegacyMinimum(uint32_t{}, "descriptor.sampler-capacity");
			ReadLegacyMinimum(uint32_t{}, "ray-tracing.recursion-depth");
			ReadLegacyMinimum(uint32_t{}, "ray-tracing.max-payload-bytes");
			ReadLegacyMinimum(uint32_t{}, "ray-tracing.max-dispatch-invocations");
			ReadLegacyMinimum(uint32_t{}, "queue.graphics.timestamp-bits");
			ReadLegacyMinimum(uint32_t{}, "queue.compute.timestamp-bits");
			ReadLegacyMinimum(uint32_t{}, "queue.copy.timestamp-bits");
			ReadLegacyMinimum(uint32_t{}, "shader.subgroup-min-size");
			uint32_t MaxSubgroupSize = 0;
			A.Read(MaxSubgroupSize);
			if (MaxSubgroupSize)
			{
				R.Require("shader.subgroup-max-size", 1, 0, MaxSubgroupSize);
			}
		}

		void FeatureFields(FArdaGraphArchiveWriter& A, const FArdaRHIFeatureRequirements& R, bool)
		{
			VisitValues(A, R.GetEntries(), [&](const auto& Value)
			{
				if (!Value.IsValid())
				{
					A.Fail("Archived feature requirement has an invalid name or impossible bounds.");
					return;
				}
				A.Fields(Value.mName, Value.mMinimumValue, Value.mRequiredBits, Value.mMaximumValue);
			});
		}

		void FeatureFields(FArdaGraphArchiveReader& A, FArdaRHIFeatureRequirements& R, bool LegacyFeatures)
		{
			R = FArdaRHIFeatureRequirements{};
			if (LegacyFeatures)
			{
				ReadLegacyFeatures(A, R);
				return;
			}
			const auto Count = A.ReadCount();
			for (size_t Index = 0; Index < Count && A.GetStatus(); ++Index)
			{
				FArdaBackendFeatureRequirement Value;
				A.Fields(Value.mName, Value.mMinimumValue, Value.mRequiredBits, Value.mMaximumValue);
				if (!A.GetStatus())
				{
					return;
				}
				if (!Value.IsValid())
				{
					A.Fail("Archived feature requirement has an invalid name or impossible bounds.");
					return;
				}
				if (R.Contains(Value.mName.c_str()))
				{
					A.Fail("Archived feature name is repeated.");
					return;
				}
				R.Require(Value.mName.c_str(), Value.mMinimumValue, Value.mRequiredBits, Value.mMaximumValue);
			}
		}

		template <class Archive, class Requirements>
		void RequirementFields(Archive& A, Requirements& R, bool LegacyFeatures)
		{
			FeatureFields(A, R.mFeatures, LegacyFeatures);
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
		void DescriptionFields(Archive& A, Description& D, bool LegacyFeatures)
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
			RequirementFields(A, D.mRequirements, LegacyFeatures);
			if (!D.mEstimatedCost || D.mGraphicsStage > EArdaGraphicsStage::Mesh)
			{
				A.Fail("Archived node description is invalid.");
			}
		}
	}

	void WriteArdaGraphDescription(FArdaGraphArchiveWriter& Archive, const FArdaDependencyNodeDesc& Description)
	{
		DescriptionFields(Archive, Description, false);
	}

	void ReadArdaGraphDescription(FArdaGraphArchiveReader& Archive, FArdaDependencyNodeDesc& Description, bool LegacyFeatures)
	{
		DescriptionFields(Archive, Description, LegacyFeatures);
		Description.mbArchivedShaderAccesses = true;
	}

	void WriteArdaGraphRequirements(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyNodeRequirements& Requirements)
	{
		RequirementFields(Archive, Requirements, false);
	}

	void ReadArdaGraphRequirements(FArdaGraphArchiveReader& Archive, FArdaDependencyNodeRequirements& Requirements, bool LegacyFeatures)
	{
		RequirementFields(Archive, Requirements, LegacyFeatures);
	}

}
