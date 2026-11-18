#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Compiler/ArdaInductorPipelineCompiler.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"
#include "Inductor/Runtime/ArdaInductorBindings.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	FArdaRHIStatus FArdaInductorRuntime::PrepareFramePipelines(FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaInductorFrame* Frame,
	    const eastl::unordered_set<uint32_t>& SkippedNodes,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse)
	{
		const auto FrameIndex = Frame->mIndex;
		if (Reuse)
		{
			Frame->mNodeBindings = (*Reuse)[FrameIndex].mNodeBindings;
		}
		else
		{
			if (!Graph.mCompile.mExecution->mPipelineCache)
			{
				Graph.mCompile.mExecution->mPipelineCache =
				    eastl::make_shared<FArdaPipelineStateCache>(Graph.mCompile.mExecution->mDevice);
			}
			for (auto Handle : Graph.mCompile.mExecutionOrder)
			{
				if (SkippedNodes.count(Handle.GetIndex()))
				{
					continue;
				}
				const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
				FArdaInductorPipelineNode Projection;
				Projection.mNodeId = Handle.GetIndex();
				Projection.mContributions = Graph.GetNodeDescription(Node).mPipelineStages;
				for (const auto& Request : Graph.GetNodeDescription(Node).mPipelines)
				{
					Projection.mPipelineBoundaries.push_back(Request.mKind);
				}
				const eastl::vector<FArdaInductorPipelineNode> PipelineNodes{eastl::move(Projection)};
				FArdaRHIFramebufferRef Framebuffer;
				if (!Graph.GetNodeDescription(Node).mColorTargets.empty() ||
				    Graph.GetNodeDescription(Node).mDepthTarget)
				{
					FArdaRHIFramebufferDesc Desc;
					Desc.mDebugName = Node.mName;
					const auto Attachment = [&Graph, &Node](FArdaDependencyResourceHandle Target, bool bDepth)
					{
						FArdaRHIFramebufferAttachment Result;
						for (const auto& Access : Graph.GetNodeDescription(Node).mAccesses)
						{
							if (Access.mResource != Target)
							{
								continue;
							}
							if ((!bDepth && Access.mState == EArdaRHIResourceState::RenderTarget) ||
							    (bDepth &&
							        (Access.mState == EArdaRHIResourceState::DepthRead ||
							            Access.mState == EArdaRHIResourceState::DepthWrite)))
							{
								Result.mSubresources = Access.mTextureRange;
								Result.mbReadOnly = bDepth && Access.mState == EArdaRHIResourceState::DepthRead;
								break;
							}
						}
						return Result;
					};
					for (auto Target : Graph.GetNodeDescription(Node).mColorTargets)
					{
						if (!Graph.HasResource(Target) || !Frame->mMemory.mTextures[Target.mIndex])
						{
							return MakeArdaInductorRuntimeError("A framebuffer target has no materialized texture.");
						}
						Desc.mColorAttachments.push_back(
						    {Frame->mMemory.mTextures[Target.mIndex], Attachment(Target, false)});
					}
					if (Graph.GetNodeDescription(Node).mDepthTarget)
					{
						const auto Target = Graph.GetNodeDescription(Node).mDepthTarget;
						if (!Graph.HasResource(Target) || !Frame->mMemory.mTextures[Target.mIndex])
						{
							return MakeArdaInductorRuntimeError("A depth target has no materialized texture.");
						}
						Desc.mDepthAttachment.mTexture = Frame->mMemory.mTextures[Target.mIndex];
						Desc.mDepthAttachment.mAttachment = Attachment(Target, true);
					}
					auto Created = Graph.mCompile.mExecution->mDevice->CreateFramebuffer(Desc);
					if (!Created)
					{
						return Created.mStatus;
					}
					Framebuffer = Created.mValue;
					Frame->mNodeBindings[Handle.GetIndex()].mFramebuffer = Framebuffer;
				}
				for (auto Request : Graph.GetNodeDescription(Node).mPipelines)
				{
					Request.mTerminalNodeId = Handle.GetIndex();
					if (Request.mSlot.empty())
					{
						Request.mSlot = "default";
					}
					auto& Slots = Frame->mNodeBindings[Handle.GetIndex()].mPipelines;
					if (Slots.count(Request.mSlot))
					{
						return MakeArdaInductorRuntimeError("A node requests duplicate pipeline slots.");
					}
					auto Pattern = InferArdaInductorPipeline(PipelineNodes, Request);
					if (!Pattern)
					{
						return Pattern.mStatus;
					}
					auto Resolved = ResolveArdaInductorPipeline(*Graph.mCompile.mExecution->mPipelineCache,
					    Pattern.mValue,
					    Framebuffer);
					if (!Resolved)
					{
						return Resolved.mStatus;
					}
					Slots.emplace(Request.mSlot, eastl::move(Resolved.mValue));
				}
			}
		}

		if (!Reuse)
		{
			if (auto S = PrepareArdaShaderBindings(Graph, *Frame); !S)
			{
				return S;
			}
		}
		return {};
	}
}
