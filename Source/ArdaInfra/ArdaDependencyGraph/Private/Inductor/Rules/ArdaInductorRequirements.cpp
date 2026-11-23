#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include <EASTL/algorithm.h>
#include <EASTL/unordered_set.h>

namespace arda
{
	namespace
	{
		void RequireShaderStorage(FArdaRHIFeatureRequirements& R,
		    EArdaRHIBindingType Type,
		    EArdaRHIShaderStage Visibility)
		{
			if (Type != EArdaRHIBindingType::TextureUAV && Type != EArdaRHIBindingType::TypedBufferUAV &&
			    Type != EArdaRHIBindingType::StructuredBufferUAV && Type != EArdaRHIBindingType::RawBufferUAV)
			{
				return;
			}
			if (HasAnyFlags(Visibility,
			    EArdaRHIShaderStage::Vertex | EArdaRHIShaderStage::Hull | EArdaRHIShaderStage::Domain |
			        EArdaRHIShaderStage::Geometry))
			{
				R.Require("shader.vertex-stores-and-atomics");
			}
			if (HasAnyFlags(Visibility, EArdaRHIShaderStage::Pixel))
			{
				R.Require("shader.fragment-stores-and-atomics");
			}
		}

		void RequirePipeline(FArdaRHIFeatureRequirements& R, EArdaPipelineStateKind Kind)
		{
			if (Kind == EArdaPipelineStateKind::Graphics || Kind == EArdaPipelineStateKind::Meshlet)
			{
				R.Require("queue.graphics");
			}
			if (Kind == EArdaPipelineStateKind::Meshlet)
			{
				R.Require("shader.mesh");
			}
			if (Kind == EArdaPipelineStateKind::WorkGraph)
			{
				R.Require("work-graph.dispatch");
			}
			if (Kind == EArdaPipelineStateKind::RayTracing)
			{
				R.Require("ray-tracing.pipeline");
			}
			if (Kind == EArdaPipelineStateKind::RayTracing)
			{
				R.Require("ray-tracing.persistent-shader-tables");
			}
		}

		void RequireShader(FArdaRHIFeatureRequirements& R, const FArdaRHIShaderRef& Shader)
		{
			if (!Shader)
			{
				return;
			}
			const auto S = Shader->GetStage();
			if (S == EArdaRHIShaderStage::Geometry)
			{
				R.Require("shader.geometry");
			}
			if (S == EArdaRHIShaderStage::Hull || S == EArdaRHIShaderStage::Domain)
			{
				R.Require("shader.tessellation");
			}
			if (S == EArdaRHIShaderStage::Mesh || S == EArdaRHIShaderStage::Amplification)
			{
				R.Require("shader.mesh");
			}
			if (S == EArdaRHIShaderStage::WorkGraph)
			{
				R.Require("work-graph.dispatch");
			}
			if (IsArdaRHIRayTracingShaderStage(S))
			{
				R.Require("ray-tracing.pipeline");
			}
		}

		void RequireBindless(FArdaRHIFeatureRequirements& R, const FArdaRHIBindlessLayoutDesc& B)
		{
			R.Require("descriptor.bindless");
			if (B.mbUnbounded)
			{
				R.Require("descriptor.unbounded-arrays");
			}
			if (B.mbUnbounded)
			{
				R.Require("descriptor.runtime-arrays");
			}
			if (B.mbUpdateAfterBind)
			{
				R.Require("descriptor.update-after-bind");
			}
			if (B.mbVariableDescriptorCount)
			{
				R.Require("descriptor.variable-count");
			}
			if (B.mbDescriptorBuffer)
			{
				R.Require("descriptor.buffer");
			}
			for (const auto& Bank : B.mRegisterSpaces)
			{
				RequireShaderStorage(R, Bank.mType, B.mVisibility);
				const bool Sampler = Bank.mType == EArdaRHIBindingType::Sampler;
				if (B.mbDirectHeapIndexing && Sampler)
				{
					R.Require("descriptor.direct-sampler-heap-indexing");
				}
				if (B.mbDirectHeapIndexing && !Sampler)
				{
					R.Require("descriptor.direct-resource-heap-indexing");
				}
				if (B.mMaxCapacity)
				{
					R.Require(Sampler ? "descriptor.sampler-capacity" : "descriptor.resource-capacity", B.mMaxCapacity);
				}
			}
		}

		void RequireLayout(FArdaRHIFeatureRequirements& R, const FArdaRHIBindingLayoutRef& Layout)
		{
			if (!Layout)
			{
				return;
			}
			if (const auto* B = Layout->GetBindlessDesc())
			{
				RequireBindless(R, *B);
			}
			for (const auto& I : Layout->GetDesc().mItems)
			{
				RequireShaderStorage(R, I.mType, Layout->GetDesc().mVisibility);
				if (I.mType == EArdaRHIBindingType::RayTracingAccelStruct)
				{
					R.Require("ray-tracing.acceleration-structure");
				}
			}
		}

		template <class Layouts>
		void RequireLayouts(FArdaRHIFeatureRequirements& R, const Layouts& L)
		{
			for (const auto& Layout : L)
			{
				RequireLayout(R, Layout);
			}
		}

		void RequireConfiguration(FArdaRHIFeatureRequirements& R,
		    const eastl::shared_ptr<const FArdaInductorPipelineConfiguration>& Config,
		    bool HasRayPayloadLimit)
		{
			if (!Config)
			{
				return;
			}
			RequirePipeline(R, Config->mKind);
			switch (Config->mKind)
			{
			case EArdaPipelineStateKind::Compute:
				RequireLayouts(R, Config->mCompute.mDesc.mBindingLayouts);
				RequireShader(R, Config->mCompute.mDesc.mComputeShader);
				break;
			case EArdaPipelineStateKind::Graphics:
				RequireLayouts(R, Config->mGraphics.mDesc.mBindingLayouts);
				RequireShader(R, Config->mGraphics.mDesc.mGeometryShader);
				RequireShader(R, Config->mGraphics.mDesc.mHullShader);
				RequireShader(R, Config->mGraphics.mDesc.mDomainShader);
				break;
			case EArdaPipelineStateKind::Meshlet:
				RequireLayouts(R, Config->mMeshlet.mDesc.mBindingLayouts);
				break;
			case EArdaPipelineStateKind::WorkGraph:
				RequireLayouts(R, Config->mWorkGraph.mDesc.mGlobalBindingLayouts);
				break;
			case EArdaPipelineStateKind::RayTracing:
			{
				const auto& Ray = Config->mRayTracing.mDesc;
				if (Ray.mbAllowOpacityMicromaps)
				{
					R.Require("ray-tracing.opacity-micromap");
				}
				RequireLayouts(R, Ray.mGlobalBindingLayouts);
				if (Ray.mMaxRecursionDepth)
				{
					R.Require("ray-tracing.recursion-depth", Ray.mMaxRecursionDepth);
				}
				// An unqueryable native payload limit is checked by pipeline creation, not invented here.
				if (HasRayPayloadLimit && Ray.mMaxPayloadSize)
				{
					R.Require("ray-tracing.max-payload-bytes", Ray.mMaxPayloadSize);
				}
				for (const auto& S : Ray.mShaders)
				{
					RequireLayout(R, S.mLocalBindingLayout);
					if (bool(S.mLocalBindingLayout))
					{
						R.Require("ray-tracing.local-shader-table-arguments");
					}
				}
				for (const auto& H : Ray.mHitGroups)
				{
					RequireLayout(R, H.mLocalBindingLayout);
					if (bool(H.mLocalBindingLayout))
					{
						R.Require("ray-tracing.local-shader-table-arguments");
					}
				}
				break;
			}
			}
		}
	}

	void InferArdaNodeRequirements(const FArdaDependencyGraph::FArdaImpl& G,
	    EArdaDependencyNodeKind Kind,
	    FArdaDependencyNodeDesc& D)
	{
		auto& R = D.mRequirements.mFeatures;
		AddArdaNodeIntrinsicRequirements(D.mRequirements, Kind, D.mGraphicsStage, D.mRayStage);
		const auto& Device = G.mCompile.mExecution->mDevice;
		const auto Features = Device ? Device->GetFeatures() : FArdaBackendFeatureListRef{};
		const bool HasRayPayloadLimit = Features && Features->Supports("ray-tracing.max-payload-bytes");
		const auto Resource = [&](FArdaDependencyResourceHandle H)
		{
			H = G.ResolveResource(H);
			if (!G.HasResource(H))
			{
				return; // Ordinary attachment validation diagnoses stale handles.
			}
			const auto& V = G.mResources[H.mIndex];
			if (V.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
			{
				R.Require("ray-tracing.acceleration-structure");
			}
			else if (V.mbTexture)
			{
				if (V.mTexture.mbVirtual)
				{
					R.Require("resource.virtual");
				}
				if (V.mTexture.mbTiled)
				{
					R.Require("resource.sparse-binding");
				}
				if (V.mTexture.mbTiled && V.mTexture.mDimension == EArdaRHITextureDimension::Texture3D)
				{
					R.Require("resource.reserved-texture-3d");
				}
				if (V.mTexture.mbTiled && V.mTexture.mDimension != EArdaRHITextureDimension::Texture3D)
				{
					R.Require("resource.reserved-texture-2d");
				}
			}
			else
			{
				if (V.mBuffer.mbVirtual)
				{
					R.Require("resource.virtual");
				}
				if (V.mBuffer.mbTiled)
				{
					R.Require("resource.sparse-binding");
				}
				if (V.mBuffer.mbTiled)
				{
					R.Require("resource.reserved-buffer");
				}
			}
		};
		for (const auto& A : D.mAccesses)
		{
			Resource(A.mResource);
			if (HasAnyFlags(A.mState, EArdaRHIResourceState::IndirectArgument))
			{
				R.Require("command.indirect");
			}
			if (HasAnyFlags(A.mState, EArdaRHIResourceState::ShadingRateSource))
			{
				R.Require("rendering.variable-rate-shading");
			}
			if (HasAnyFlags(A.mState,
			    EArdaRHIResourceState::OpacityMicromapWrite | EArdaRHIResourceState::OpacityMicromapBuildInput))
			{
				R.Require("ray-tracing.opacity-micromap");
			}
			if (HasAnyFlags(A.mState,
			    EArdaRHIResourceState::AccelStructRead | EArdaRHIResourceState::AccelStructWrite |
			        EArdaRHIResourceState::AccelStructBuildInput | EArdaRHIResourceState::AccelStructBuildBlas))
			{
				R.Require("ray-tracing.acceleration-structure");
			}
			if (G.HasResource(A.mResource) && G.mResources[A.mResource.mIndex].mbTexture)
			{
				if (HasAnyFlags(A.mState, EArdaRHIResourceState::CopySource | EArdaRHIResourceState::CopyDest))
				{
					R.Require("texture.copy");
				}
				if (HasAnyFlags(A.mState, EArdaRHIResourceState::ResolveSource | EArdaRHIResourceState::ResolveDest))
				{
					R.Require("texture.resolve");
				}
			}
		}
		for (const auto& B : D.mShaderBindings)
		{
			// Typed graphics stages defer physical binding assembly until preprocessing. Their
			// metadata still exposes exact stage visibility for admission before that lowering.
			if (B.mMetadata)
			{
				eastl::vector<FArdaFlattenedShaderParameterMember> Members;
				B.mMetadata->GetFlattenedMembers(Members);
				for (const auto& Member : Members)
				{
					RequireShaderStorage(R, Member.mMember->mBindingType, Member.mMember->mVisibility);
				}
			}
			for (const auto& A : B.mResources)
			{
				Resource(A.mResource);
			}
		}
		for (const auto& B : D.mBindlessTables)
		{
			RequireBindless(R, B.mLayout);
			uint64_t Capacity = B.mCapacity;
			if (!Capacity)
			{
				for (const auto& E : B.mEntries)
				{
					Capacity = eastl::max(Capacity, uint64_t(E.mArrayElement) + 1);
				}
			}
			// Fixed banks retain the layout's full extent. A multi-bank layout can vary only one
			// native binding; the others still have full extent, while logical table capacity bounds
			// every bank's entries. Such a shortened table therefore necessarily contains holes.
			if (!B.mLayout.mbVariableDescriptorCount || B.mLayout.mRegisterSpaces.size() > 1)
			{
				uint64_t LayoutCapacity = B.mLayout.mMaxCapacity;
				if (!LayoutCapacity && Features && !B.mLayout.mRegisterSpaces.empty())
				{
					const bool IsSampler = B.mLayout.mRegisterSpaces.front().mType == EArdaRHIBindingType::Sampler;
					LayoutCapacity = Features->GetValueOr(
					    IsSampler ? "descriptor.sampler-capacity" : "descriptor.resource-capacity", 0);
				}
				Capacity = eastl::max(Capacity, LayoutCapacity);
			}
			for (const auto& Bank : B.mLayout.mRegisterSpaces)
			{
				eastl::unordered_set<uint32_t> Populated;
				for (const auto& E : B.mEntries)
				{
					if (uint64_t(E.mSlot) == uint64_t(Bank.mSlot) + B.mLayout.mFirstSlot && E.mType == Bank.mType)
					{
						Populated.insert(E.mArrayElement);
					}
				}
				if (Populated.size() < Capacity)
				{
					R.Require("descriptor.partially-bound");
				}
			}
			for (const auto& E : B.mEntries)
			{
				Resource(E.mResource);
			}
		}
		for (const auto& T : D.mShaderTables)
		{
			R.Require("ray-tracing.pipeline");
			R.Require("ray-tracing.persistent-shader-tables");
			for (const auto& Record : T.mRecords)
			{
				if (Record.mLocalBindings.mMetadata != nullptr || !Record.mLocalArguments.empty())
				{
					R.Require("ray-tracing.local-shader-table-arguments");
				}
				for (const auto& A : Record.mLocalBindings.mResources)
				{
					Resource(A.mResource);
				}
				Resource(Record.mGeometry);
			}
		}
		for (const auto& S : D.mPipelineStages)
		{
			RequireShader(R, S.mShader);
			RequireLayouts(R, S.mBindingLayouts);
			RequireLayout(R, S.mLocalBindingLayout);
			if (bool(S.mLocalBindingLayout))
			{
				R.Require("ray-tracing.local-shader-table-arguments");
			}
			RequireConfiguration(R, S.mConfiguration, HasRayPayloadLimit);
		}
		for (const auto& P : D.mPipelines)
		{
			RequirePipeline(R, P.mKind);
			RequireLayouts(R, P.mBindingLayouts);
			for (const auto& L : P.mLocalBindingLayouts)
			{
				RequireLayout(R, L.second);
				R.Require("ray-tracing.local-shader-table-arguments");
			}
			RequireConfiguration(R, P.mConfiguration, HasRayPayloadLimit);
		}
	}
}
