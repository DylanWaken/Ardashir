#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <EASTL/unordered_set.h>
#include <cstring>
#include <EASTL/numeric_limits.h>

namespace arda
{
	namespace
	{
		const FArdaInductorNodeBindings& GetNodeBindings(const FArdaInductorFrame* Frame, FArdaGraphNodeHandle Node)
		{
			static const FArdaInductorNodeBindings Empty;
			if (!Frame)
			{
				return Empty;
			}
			const auto Found = Frame->mNodeBindings.find(Node.GetIndex());
			return Found == Frame->mNodeBindings.end() ? Empty : Found->second;
		}

		template <class Value>
		const Value& GetSlot(const eastl::unordered_map<eastl::string, Value>& Slots, const eastl::string& Name)
		{
			static const Value Empty;
			const auto Found = Slots.find(Name);
			return Found == Slots.end() ? Empty : Found->second;
		}

		bool DeclaresResource(const FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaGraphNodeHandle Node,
		    FArdaDependencyResourceHandle Resource)
		{
			const auto* Record = Graph.mTopology.TryGetNode(Node);
			if (!Record || !Graph.HasResource(Resource))
			{
				return false;
			}
			const auto& Desc = Record->mPayload.mDesc;
			return eastl::any_of(Desc.mAccesses.begin(),
			           Desc.mAccesses.end(),
			           [Resource](const auto& Access)
			           {
				           return Access.mResource == Resource;
			           }) ||
			    eastl::find(Desc.mColorTargets.begin(), Desc.mColorTargets.end(), Resource) !=
			    Desc.mColorTargets.end() ||
			    Desc.mDepthTarget == Resource;
		}
	}

	IArdaRHICommandList& FArdaDependencyExecutionContext::GetCommands() const
	{
		if (!mPass)
		{
			ARDA_CHECK_MSG("Dependency execution context is outside a node callback.");
		}
		return mPass->mUnsafeRawCommandList;
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::ReadbackBuffer(FArdaDependencyResourceHandle Resource,
	    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
	    uint64_t SourceOffset,
	    uint64_t Size) const
	{
		if (!Destination)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Readback destination is null.");
		}
		if (!mGraph || !mFrame || !mPass || !mGraph->mbExecuting)
		{
			return MakeArdaInductorRuntimeError("Graph readback requires an active node callback.");
		}
		auto Callback =
		    mFrame->mActive->mReadbackCompletions.Register(eastl::move(Destination), mPass->GetPass().GetIndex());
		const auto Buffer = GetBuffer(Resource);
		if (!Buffer)
		{
			return mPass->GetStatus();
		}
		// No callback/promise copy escapes this call except the one owned by the
		// native list (or its submitted completion thread). Failed recording destroys
		// that owner and makes the registered future ready with broken_promise.
		return GetCommands().CopyBufferDeviceToHostAsync(*Buffer, eastl::move(Callback), SourceOffset, Size);
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::ReadbackTexture(FArdaDependencyResourceHandle Resource,
	    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
	    const FArdaRHITextureSlice& Slice) const
	{
		if (!Destination)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Readback destination is null.");
		}
		if (!mGraph || !mFrame || !mPass || !mGraph->mbExecuting)
		{
			return MakeArdaInductorRuntimeError("Graph readback requires an active node callback.");
		}
		auto Callback =
		    mFrame->mActive->mReadbackCompletions.Register(eastl::move(Destination), mPass->GetPass().GetIndex());
		auto& Commands = GetCommands();
		// The graph prepares the workspace in UAV state; graphics supports each required transition.
		if (Commands.GetQueueType() != EArdaRHIQueueType::Graphics)
		{
			return MakeArdaInductorRuntimeError("Texture readback requires a graphics-queue node.");
		}
		const auto Texture = GetTexture(Resource);
		if (!Texture)
		{
			return mPass->GetStatus();
		}
		const auto Resolved = GetArdaRHITextureBufferFootprint(Texture->GetDesc(), Slice);
		if (!Resolved)
		{
			return Resolved.mStatus;
		}
		const auto Footprint = Resolved.mValue;
		if (Footprint.mByteSize > eastl::numeric_limits<eastl::vector<uint8_t>::size_type>::max())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Texture readback footprint exceeds the host vector size limit.");
		}
		const auto Buffer = GetWorkspaceBuffer();
		if (!Buffer || Buffer->GetDesc().mByteSize < Footprint.mByteSize)
		{
			return MakeArdaInductorRuntimeError(
			    "Texture readback requires transient workspace containing the full pitched footprint.");
		}
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::CopyDest); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		if (auto Status = Commands.CopyTextureToBuffer(*Buffer, Footprint.mLayout, *Texture, Slice); !Status)
		{
			return Status;
		}
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::CopySource); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		const auto ReadbackStatus = Commands.CopyBufferDeviceToHostAsync(
		    *Buffer,
		    [Callback = eastl::move(Callback), Footprint](FArdaRHIBufferReadbackResult Result)
		    {
			    if (Result)
			    {
				    if (Result.mValue.size() != Footprint.mByteSize)
				    {
					    // Report malformed native output without allocating inside a completion callback.
					    Result.mStatus.mCode = EArdaRHIResult::BackendFailure;
				    }
				    else
				    {
					    // Compact in place so completion never allocates or leaves a promise unresolved.
					    for (uint64_t Row = 1; Row < Footprint.mRowCount; ++Row)
					    {
						    std::memmove(Result.mValue.data() + Row * Footprint.mRowBytes,
						        Result.mValue.data() + Row * Footprint.mLayout.mRowPitch,
						        static_cast<size_t>(Footprint.mRowBytes));
					    }
					    Result.mValue.resize(
					        static_cast<eastl::vector<uint8_t>::size_type>(Footprint.mRowBytes * Footprint.mRowCount));
				    }
			    }
			    Callback(eastl::move(Result));
		    },
		    0,
		    Footprint.mByteSize);
		if (!ReadbackStatus)
		{
			return ReadbackStatus;
		}
		// The CPU copy is now encoded; restore the state expected by the graph before scratch aliasing.
		if (auto Status = Commands.SetBufferState(*Buffer, EArdaRHIResourceState::UnorderedAccess); !Status)
		{
			return Status;
		}
		Commands.CommitBarriers();
		return {};
	}

	FArdaRHIDeviceRef FArdaDependencyExecutionContext::GetDevice() const
	{
		return mGraph ? mGraph->mDevice : FArdaRHIDeviceRef{};
	}

	uint32_t FArdaDependencyExecutionContext::GetFrameIndex() const
	{
		return mFrame ? mFrame->mIndex : UINT32_MAX;
	}

	uint64_t FArdaDependencyExecutionContext::GetFrameSequence() const
	{
		return mFrame && mFrame->mActive ? mFrame->mActive->mSequence : 0;
	}

	FArdaRHIBufferRef FArdaDependencyExecutionContext::GetWorkspaceBuffer() const
	{
		if (!mGraph || !mFrame || !mPass)
		{
			return {};
		}
		const auto Found = mGraph->mCompile.mWorkspaceResourceIds.find(mNode.GetIndex());
		if (Found == mGraph->mCompile.mWorkspaceResourceIds.end())
		{
			return {};
		}
		const auto Index = Found->second;
		if (Index >= mFrame->mBuffers.size() || !mFrame->mBuffers[Index])
		{
			mPass->ReportStatus(MakeArdaInductorRuntimeError("A declared workspace has no physical allocation."));
			return {};
		}
		return FArdaRHIBufferRef(mPass->GetBuffer(mFrame->mBuffers[Index]));
	}

	FArdaRHIBufferRef FArdaDependencyExecutionContext::GetBuffer(FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    mGraph->mResources[Resource.mIndex].mbTexture || !mFrame->mBuffers[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(
				    MakeArdaInductorRuntimeError("A node requested an undeclared or unavailable buffer."));
			}
			return {};
		}
		return FArdaRHIBufferRef(mPass->GetBuffer(mFrame->mBuffers[Resource.mIndex]));
	}

	FArdaRHIAccelStructRef FArdaDependencyExecutionContext::GetAccelerationStructure(
	    FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    Resource.mIndex >= mFrame->mAccelerationStructures.size() ||
		    !mFrame->mAccelerationStructures[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(MakeArdaInductorRuntimeError(
				    "A node requested an undeclared or unavailable acceleration structure."));
			}
			return {};
		}
		return FArdaRHIAccelStructRef(mPass->GetAccelStruct(mFrame->mAccelerationStructures[Resource.mIndex]));
	}

	FArdaRHITextureRef FArdaDependencyExecutionContext::GetTexture(FArdaDependencyResourceHandle Resource) const
	{
		if (!mGraph || !mFrame || !mPass || !DeclaresResource(*mGraph, mNode, Resource) ||
		    !mGraph->mResources[Resource.mIndex].mbTexture || !mFrame->mTextures[Resource.mIndex])
		{
			if (mPass)
			{
				mPass->ReportStatus(
				    MakeArdaInductorRuntimeError("A node requested an undeclared or unavailable texture."));
			}
			return {};
		}
		return FArdaRHITextureRef(mPass->GetTexture(mFrame->mTextures[Resource.mIndex]));
	}

	const FArdaInductorResolvedPipeline* FArdaDependencyExecutionContext::GetPipeline(const eastl::string& Slot) const
	{
		const auto& Pipelines = GetNodeBindings(mGraph ? mFrame : nullptr, mNode).mPipelines;
		const auto Found = Pipelines.find(Slot);
		return Found == Pipelines.end() ? nullptr : &Found->second;
	}

	FArdaRHIFramebufferRef FArdaDependencyExecutionContext::GetFramebuffer() const
	{
		return GetNodeBindings(mGraph ? mFrame : nullptr, mNode).mFramebuffer;
	}

	const eastl::vector<FArdaRHIBindingSetRef>& FArdaDependencyExecutionContext::GetBindings(
	    const eastl::string& Slot) const
	{
		return GetSlot(GetNodeBindings(mFrame, mNode).mBindings, Slot);
	}

	const FArdaRHIShaderTableRef& FArdaDependencyExecutionContext::GetShaderTable(const eastl::string& Slot) const
	{
		return GetSlot(GetNodeBindings(mFrame, mNode).mShaderTables, Slot);
	}

	const FArdaRHIDescriptorTableRef& FArdaDependencyExecutionContext::GetDescriptorTable(
	    const eastl::string& Name) const
	{
		return GetSlot(GetNodeBindings(mFrame, mNode).mDescriptorTables, Name);
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::ApplyShaderParameters(const eastl::string& Slot) const
	{
		const auto* N = mGraph ? mGraph->mTopology.TryGetNode(mNode) : nullptr;
		if (!N || !GetPipeline(Slot))
		{
			return MakeArdaInductorRuntimeError("Missing compiled shader parameter slot.");
		}
		const auto& Bytes = GetSlot(GetNodeBindings(mFrame, mNode).mShaderParameters, Slot);
		if (!Bytes.empty())
		{
			GetCommands().SetPushConstants(Bytes.data(), Bytes.size());
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::SetRayTracingState(const eastl::string& Slot) const
	{
		if (!GetShaderTable(Slot))
		{
			return MakeArdaInductorRuntimeError("Missing compiled ray shader table.");
		}
		FArdaRHIRayTracingState State;
		State.mShaderTable = GetShaderTable(Slot);
		State.mBindings = GetBindings(Slot);
		if (auto S = GetCommands().SetRayTracingState(State); !S)
		{
			return S;
		}
		return ApplyShaderParameters(Slot);
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::DispatchWorkGraph(const void* Records,
	    uint32_t RecordCount,
	    uint32_t RecordStride,
	    const eastl::string& Slot) const
	{
		const auto* P = GetPipeline(Slot);
		if (!P || !P->mWorkGraph)
		{
			return MakeArdaInductorRuntimeError("Missing compiled work-graph pipeline slot.");
		}
		if (auto S = ApplyShaderParameters(Slot); !S)
		{
			return S;
		}
		return GetCommands().DispatchWorkGraph(*P->mWorkGraph, Records, RecordCount, RecordStride, GetBindings(Slot));
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::SetComputeState(const eastl::string& Slot) const
	{
		const auto* P = GetPipeline(Slot);
		if (!P || !P->mCompute)
		{
			return MakeArdaInductorRuntimeError("Missing compiled compute pipeline slot.");
		}
		FArdaRHIComputeState State;
		State.mPipeline = P->mCompute;
		State.mBindings = GetBindings(Slot);
		if (auto S = GetCommands().SetComputeState(State); !S)
		{
			return S;
		}
		return ApplyShaderParameters(Slot);
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::SetGraphicsState(FArdaRHIGraphicsState State,
	    const eastl::string& Slot) const
	{
		const auto* P = GetPipeline(Slot);
		if (!P || !P->mGraphics)
		{
			return MakeArdaInductorRuntimeError("Missing compiled graphics pipeline slot.");
		}
		State.mPipeline = P->mGraphics;
		State.mFramebuffer = GetFramebuffer();
		State.mBindings = GetBindings(Slot);
		if (auto S = GetCommands().SetGraphicsState(State); !S)
		{
			return S;
		}
		return ApplyShaderParameters(Slot);
	}

	FArdaRHIStatus FArdaDependencyExecutionContext::SetMeshletState(FArdaRHIMeshletState State,
	    const eastl::string& Slot) const
	{
		const auto* P = GetPipeline(Slot);
		if (!P || !P->mMeshlet)
		{
			return MakeArdaInductorRuntimeError("Missing compiled meshlet pipeline slot.");
		}
		State.mPipeline = P->mMeshlet;
		State.mFramebuffer = GetFramebuffer();
		State.mBindings = GetBindings(Slot);
		if (auto S = GetCommands().SetMeshletState(State); !S)
		{
			return S;
		}
		return ApplyShaderParameters(Slot);
	}

	FArdaGraphNodeHandle FArdaDependencyExecutionContext::GetNode() const
	{
		return mNode;
	}
}
