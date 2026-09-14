#include "ArdaDependencyGraphInternal.h"
#include <EASTL/algorithm.h>
#include <EASTL/unordered_set.h>

namespace arda
{
	namespace
	{
		void RequirePipeline(FArdaRHIFeatureRequirements& R, EArdaPipelineStateKind Kind)
		{
			R.mbRequireGraphicsQueue |=
			    Kind == EArdaPipelineStateKind::Graphics || Kind == EArdaPipelineStateKind::Meshlet;
			R.mbRequireMeshShaders |= Kind == EArdaPipelineStateKind::Meshlet;
			R.mbRequireWorkGraphs |= Kind == EArdaPipelineStateKind::WorkGraph;
			R.mbRequireRayTracingPipelines |= Kind == EArdaPipelineStateKind::RayTracing;
			R.mbRequirePersistentShaderTables |= Kind == EArdaPipelineStateKind::RayTracing;
		}

		void RequireShader(FArdaRHIFeatureRequirements& R, const FArdaRHIShaderRef& Shader)
		{
			if (!Shader)
			{
				return;
			}
			const auto S = Shader->GetStage();
			R.mbRequireGeometryShaders |= S == EArdaRHIShaderStage::Geometry;
			R.mbRequireTessellationShaders |= S == EArdaRHIShaderStage::Hull || S == EArdaRHIShaderStage::Domain;
			R.mbRequireMeshShaders |= S == EArdaRHIShaderStage::Mesh || S == EArdaRHIShaderStage::Amplification;
			R.mbRequireWorkGraphs |= S == EArdaRHIShaderStage::WorkGraph;
			R.mbRequireRayTracingPipelines |= IsArdaRHIRayTracingShaderStage(S);
		}

		void RequireBindless(FArdaRHIFeatureRequirements& R, const FArdaRHIBindlessLayoutDesc& B)
		{
			R.mbRequireBindless = true;
			R.mbRequireUnboundedDescriptors |= B.mbUnbounded;
			R.mbRequireRuntimeDescriptorArrays |= B.mbUnbounded;
			R.mbRequireUpdateAfterBind |= B.mbUpdateAfterBind;
			R.mbRequireVariableDescriptorCount |= B.mbVariableDescriptorCount;
			R.mbRequireDescriptorBuffer |= B.mbDescriptorBuffer;
			for (const auto& Bank : B.mRegisterSpaces)
			{
				const bool Sampler = Bank.mType == EArdaRHIBindingType::Sampler;
				R.mbRequireDirectSamplerHeapIndexing |= B.mbDirectHeapIndexing && Sampler;
				R.mbRequireDirectDescriptorIndexing |= B.mbDirectHeapIndexing && !Sampler;
				auto& Count = Sampler ? R.mMinSamplerDescriptors : R.mMinResourceDescriptors;
				Count = eastl::max(Count, B.mMaxCapacity);
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
				R.mbRequireAccelerationStructures |= I.mType == EArdaRHIBindingType::RayTracingAccelStruct;
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
		    const FArdaRHICapabilities* Capabilities)
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
				R.mbRequireOpacityMicromaps |= Ray.mbAllowOpacityMicromaps;
				RequireLayouts(R, Ray.mGlobalBindingLayouts);
				R.mMinRayRecursionDepth = eastl::max(R.mMinRayRecursionDepth, Ray.mMaxRecursionDepth);
				// An unqueryable native payload limit is checked by pipeline creation, not invented here.
				if (Capabilities && Capabilities->mRayTracing.mMaxRayPayloadSize)
				{
					R.mMinRayPayloadSize = eastl::max(R.mMinRayPayloadSize, Ray.mMaxPayloadSize);
				}
				for (const auto& S : Ray.mShaders)
				{
					RequireLayout(R, S.mLocalBindingLayout);
					R.mbRequireLocalShaderTableArguments |= bool(S.mLocalBindingLayout);
				}
				for (const auto& H : Ray.mHitGroups)
				{
					RequireLayout(R, H.mLocalBindingLayout);
					R.mbRequireLocalShaderTableArguments |= bool(H.mLocalBindingLayout);
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
		D.mRequirements.mbRequireCuda |= Kind == EArdaDependencyNodeKind::Cuda && !D.mbPipelineStageOnly;
		const auto* Caps = G.mDevice ? &G.mDevice->GetCapabilities() : nullptr;
		const auto Resource = [&](FArdaDependencyResourceHandle H)
		{
			if (!G.HasResource(H))
			{
				return; // Ordinary attachment validation diagnoses stale handles.
			}
			const auto& V = G.mResources[H.mIndex];
			if (V.mExternalAccelerationStructure)
			{
				R.mbRequireAccelerationStructures = true;
			}
			else if (V.mbTexture)
			{
				R.mbRequireVirtualResources |= V.mTexture.mbVirtual;
				R.mbRequireSparseResidency |= V.mTexture.mbTiled;
				R.mbRequireReservedTexture3D |=
				    V.mTexture.mbTiled && V.mTexture.mDimension == EArdaRHITextureDimension::Texture3D;
				R.mbRequireReservedTexture2D |=
				    V.mTexture.mbTiled && V.mTexture.mDimension != EArdaRHITextureDimension::Texture3D;
			}
			else
			{
				R.mbRequireVirtualResources |= V.mBuffer.mbVirtual;
				R.mbRequireSparseResidency |= V.mBuffer.mbTiled;
				R.mbRequireReservedBuffers |= V.mBuffer.mbTiled;
			}
		};
		for (const auto& A : D.mAccesses)
		{
			Resource(A.mResource);
			R.mbRequireIndirectCommands |= HasAnyFlags(A.mState, EArdaRHIResourceState::IndirectArgument);
			R.mbRequireVariableRateShading |= HasAnyFlags(A.mState, EArdaRHIResourceState::ShadingRateSource);
			R.mbRequireOpacityMicromaps |= HasAnyFlags(A.mState,
			    EArdaRHIResourceState::OpacityMicromapWrite | EArdaRHIResourceState::OpacityMicromapBuildInput);
			R.mbRequireAccelerationStructures |= HasAnyFlags(A.mState,
			    EArdaRHIResourceState::AccelStructRead | EArdaRHIResourceState::AccelStructWrite |
			        EArdaRHIResourceState::AccelStructBuildInput | EArdaRHIResourceState::AccelStructBuildBlas);
			if (G.HasResource(A.mResource) && G.mResources[A.mResource.mIndex].mbTexture)
			{
				R.mbRequireTextureCopies |=
				    HasAnyFlags(A.mState, EArdaRHIResourceState::CopySource | EArdaRHIResourceState::CopyDest);
				R.mbRequireTextureResolve |=
				    HasAnyFlags(A.mState, EArdaRHIResourceState::ResolveSource | EArdaRHIResourceState::ResolveDest);
			}
		}
		for (const auto& B : D.mShaderBindings)
		{
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
				if (!LayoutCapacity && Caps && !B.mLayout.mRegisterSpaces.empty())
				{
					LayoutCapacity = B.mLayout.mRegisterSpaces.front().mType == EArdaRHIBindingType::Sampler
					    ? Caps->mDescriptors.mMaxSamplerDescriptors
					    : Caps->mDescriptors.mMaxResourceDescriptors;
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
				R.mbRequirePartiallyBoundDescriptors |= Populated.size() < Capacity;
			}
			for (const auto& E : B.mEntries)
			{
				Resource(E.mResource);
			}
		}
		for (const auto& T : D.mShaderTables)
		{
			R.mbRequireRayTracingPipelines = R.mbRequirePersistentShaderTables = true;
			for (const auto& Record : T.mRecords)
			{
				R.mbRequireLocalShaderTableArguments |=
				    Record.mLocalBindings.mMetadata != nullptr || !Record.mLocalArguments.empty();
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
			R.mbRequireLocalShaderTableArguments |= bool(S.mLocalBindingLayout);
			RequireConfiguration(R, S.mConfiguration, Caps);
		}
		for (const auto& P : D.mPipelines)
		{
			RequirePipeline(R, P.mKind);
			RequireLayouts(R, P.mBindingLayouts);
			for (const auto& L : P.mLocalBindingLayouts)
			{
				RequireLayout(R, L.second);
				R.mbRequireLocalShaderTableArguments = true;
			}
			RequireConfiguration(R, P.mConfiguration, Caps);
		}
	}
}
