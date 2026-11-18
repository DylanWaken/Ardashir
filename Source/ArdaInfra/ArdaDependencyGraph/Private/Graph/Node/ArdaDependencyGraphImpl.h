#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Inductor/Runtime/ArdaInductorExecutionState.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"

#include <atomic>

namespace arda
{
	/** Commit-order edit boundaries retained independently from node and resource storage positions. */
	struct FArdaCommittedGraphEdit
	{
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		eastl::vector<FArdaGraphResourceRevision> mResources;
	};

	struct FArdaDependencyGraph::FArdaImpl
	{
		FArdaImpl()
		{
			mCompile.mExecution = eastl::make_shared<FArdaInductorExecutionState>();
		}

		uint64_t mIdentity = 0;
		bool mbEditing = false;
		/** Prevent captured-graph mutation and recursive compilation from extension callbacks. */
		bool mbCompiling = false;
		bool mbPreprocessed = false;
		bool mbPreprocessing = false;
		bool mbPreprocessEditing = false;
		/** Compiler/runtime descriptions are an overlay; preprocessing always uses graph declarations. */
		bool mbUsingCompiledDescriptions = false;
		/** Only the expansion rule grants its borrowed content edit access to internal authoring primitives. */
		bool mbExpandingSubgraphs = false;
		bool mbExecuting = false;
		uint64_t mNextResourceGeneration = 1;
		uint64_t mNextAttachmentOrder = 1;
		/** Facade naming and diagnostic scope identity within this private construction. */
		uint64_t mNextOperationName = 1;
		uint64_t mEditGeneration = 0;
		eastl::vector<uint64_t> mResourceGenerations;
		/** Current construction handles accepted after selecting an equivalent persistent revision. */
		eastl::vector<eastl::vector<uint64_t>> mResourceAliases;
		/** Aliases retained by immutable node parameters; authoring alias transfers cannot invalidate them. */
		eastl::vector<eastl::vector<uint64_t>> mCompiledResourceAliases;
		FArdaInductorOptions mOptions;
		FArdaDependencyTopology mTopology;
		eastl::unordered_map<eastl::string, FArdaGraphExternalValueBinding> mExternalValues;
		/** Archive-local node order retained to resolve deferred contextual parameter payloads. */
		eastl::vector<FArdaGraphNodeHandle> mArchiveNodes;
		/** Immutable authoring preparation only; no per-execution instance state is retained here. */
		eastl::unordered_map<FArdaGraphNodeHandle, eastl::shared_ptr<const void>, FArdaGraphNodeHandleHash>
		    mNodePreparations;
		eastl::vector<FArdaDependencyResourceDesc> mResources;
		eastl::vector<FArdaCommittedGraphEdit> mCommittedEdits;
		eastl::unordered_map<eastl::string, FArdaGraphNodeHandle> mNames;
		eastl::unordered_map<eastl::string, uint32_t> mResourceNames;
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> mManualEdges;
		FArdaInductorCompileResult mCompile;
		/** Requirements retained for subgraph declarations removed by preprocessing. */
		eastl::vector<FArdaInductorSubgraphAdmission> mSubgraphAdmissions;
		/** Original construction, before preprocess transformations, for forced recompilation and extension. */
		eastl::shared_ptr<const FArdaImpl> mCompilerSource;

		/** Shared atomic generations distinguish overlapping private edit allocations without locking recording. */
		eastl::shared_ptr<std::atomic<uint64_t>> mResourceGenerationSource;
		eastl::unordered_map<FArdaDependencyResourceHandle,
		    FArdaDependencyResourceHandle,
		    FArdaDependencyResourceHandleHash>
		    mResourceRemaps, mCompiledResourceRemaps;
		eastl::unordered_map<FArdaGraphNodeHandle, FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> mNodeRemaps;
		/** Auto-generated names can be renamed on merge; explicit names remain collision checked. */
		eastl::unordered_set<eastl::string> mGeneratedNames;
		/** Native import identity indexes avoid quadratic duplicate checks during large edit merges. */
		eastl::unordered_set<const void*> mImportedBuffers, mImportedTextures, mImportedAccelerationStructures;

		FArdaDependencyNodeDesc& GetNodeDescription(const FArdaDependencyNode& Node)
		{
			return mbUsingCompiledDescriptions ? mCompile.mNodes.at(Node.mHandle).mDesc
			                                   : mTopology.TryGetNode(Node.mHandle)->mPayload.mDesc;
		}

		const FArdaDependencyNodeDesc& GetNodeDescription(const FArdaDependencyNode& Node) const
		{
			return mbUsingCompiledDescriptions ? mCompile.mNodes.at(Node.mHandle).mDesc : Node.mDesc;
		}

		FArdaDependencyResourceHandle ResolveResource(FArdaDependencyResourceHandle Handle) const
		{
			const auto Found = mResourceRemaps.find(Handle);
			if (Found != mResourceRemaps.end())
			{
				Handle = Found->second;
			}
			const auto Compiled = mCompiledResourceRemaps.find(Handle);
			return Compiled == mCompiledResourceRemaps.end() ? Handle : Compiled->second;
		}

		FArdaGraphNodeHandle ResolveNode(FArdaGraphNodeHandle Handle) const
		{
			const auto Found = mNodeRemaps.find(Handle);
			return Found == mNodeRemaps.end() ? Handle : Found->second;
		}

		void NormalizeDescription(FArdaDependencyNodeDesc& Desc) const
		{
			Desc.mGraphicsInput = {ResolveNode(Desc.mGraphicsInput.GetProducer()), Desc.mGraphicsInput.GetKind()};
			for (auto& Input : Desc.mRayInputs)
			{
				Input = {ResolveNode(Input.GetProducer()), Input.GetKind()};
			}
			for (auto& Access : Desc.mAccesses)
			{
				Access.mResource = ResolveResource(Access.mResource);
			}
			const auto Bindings = [&](FArdaDependencyShaderBindings& Binding)
			{
				Binding.mDeclaration.mNode = ResolveNode(Binding.mDeclaration.mNode);
				for (auto& Resource : Binding.mResources)
				{
					Resource.mResource = ResolveResource(Resource.mResource);
				}
			};
			for (auto& Binding : Desc.mShaderBindings)
			{
				Bindings(Binding);
			}
			for (auto& Table : Desc.mBindlessTables)
			{
				Table.mDeclaration.mNode = ResolveNode(Table.mDeclaration.mNode);
				for (auto& Entry : Table.mEntries)
				{
					Entry.mResource = ResolveResource(Entry.mResource);
				}
			}
			for (auto& Table : Desc.mShaderTables)
			{
				Table.mDeclaration.mNode = ResolveNode(Table.mDeclaration.mNode);
				for (auto& Record : Table.mRecords)
				{
					Bindings(Record.mLocalBindings);
					Record.mGeometry = ResolveResource(Record.mGeometry);
				}
			}
			for (auto& Target : Desc.mColorTargets)
			{
				Target = ResolveResource(Target);
			}
			Desc.mDepthTarget = ResolveResource(Desc.mDepthTarget);
			for (auto& Stage : Desc.mPipelineStages)
			{
				Stage.mDeclaration.mNode = ResolveNode(Stage.mDeclaration.mNode);
			}
			for (auto& Pipeline : Desc.mPipelines)
			{
				Pipeline.mDeclaration.mNode = ResolveNode(Pipeline.mDeclaration.mNode);
			}
		}

		bool HasResource(FArdaDependencyResourceHandle H) const
		{
			H = ResolveResource(H);
			const auto HasAlias = [&](const auto& Aliases)
			{
				return H.mIndex < Aliases.size() &&
				    eastl::find(Aliases[H.mIndex].begin(), Aliases[H.mIndex].end(), H.mGeneration) !=
				    Aliases[H.mIndex].end();
			};
			return H.mGraph == mIdentity && H.mIndex < mResources.size() &&
			    (H.mGeneration == mResourceGenerations[H.mIndex] || HasAlias(mResourceAliases) ||
			        HasAlias(mCompiledResourceAliases));
		}
	};

}
