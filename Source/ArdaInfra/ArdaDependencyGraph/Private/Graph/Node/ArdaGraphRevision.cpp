#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"

namespace arda
{
	namespace
	{
		eastl::vector<eastl::vector<uint64_t>> CollectResourceAliases(const FArdaDependencyGraph::FArdaImpl& Source,
		    const eastl::vector<uint64_t>& CanonicalGenerations)
		{
			eastl::vector<eastl::vector<uint64_t>> Aliases(Source.mResources.size());
			for (size_t Index = 0; Index < Aliases.size(); ++Index)
			{
				const auto AddGeneration = [&](uint64_t Generation)
				{
					if (Generation != CanonicalGenerations[Index] &&
					    eastl::find(Aliases[Index].begin(), Aliases[Index].end(), Generation) == Aliases[Index].end())
					{
						Aliases[Index].push_back(Generation);
					}
				};
				AddGeneration(Source.mResourceGenerations[Index]);
				for (const auto* Generations : {&Source.mResourceAliases, &Source.mCompiledResourceAliases})
				{
					if (Index < Generations->size())
					{
						for (const auto Generation : (*Generations)[Index])
						{
							AddGeneration(Generation);
						}
					}
				}
			}
			return Aliases;
		}

		bool IsReusableRuntime(const FArdaInductorRuntime* Runtime)
		{
			if (!Runtime)
			{
				return true;
			}
			if (Runtime->mPrevious && !Runtime->mPrevious->mExecution.mStatus)
			{
				return false;
			}
			for (const auto& Frame : Runtime->mFrames)
			{
				// An older slot can fail during completion after a newer frame submitted successfully.
				if (Frame->mActive && !Frame->mActive->mExecution.mStatus)
				{
					return false;
				}
			}
			return true;
		}

		eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> SnapshotConstruction(
		    const FArdaDependencyGraph::FArdaImpl& Source)
		{
			auto Snapshot = eastl::make_shared<FArdaDependencyGraph::FArdaImpl>();
			const auto& Authored = Source.mCompilerSource ? *Source.mCompilerSource : Source;
			Snapshot->mDevice = Source.mDevice;
			Snapshot->mIdentity = Source.mIdentity;
			Snapshot->mNextFrameSequence = Source.mNextFrameSequence;
			Snapshot->mNextResourceGeneration = Source.mNextResourceGeneration;
			Snapshot->mNextAttachmentOrder = Source.mNextAttachmentOrder;
			Snapshot->mNextOperationName = Source.mNextOperationName;
			Snapshot->mOptions = Source.mOptions;
			Snapshot->mTopology = Authored.mTopology.CloneSnapshot();
			Snapshot->mResources = Authored.mResources;
			Snapshot->mResourceGenerations = Source.mResourceGenerations;
			Snapshot->mResourceAliases = Source.mResourceAliases;
			Snapshot->mCompiledResourceAliases = Source.mCompiledResourceAliases;
			Snapshot->mNames = Authored.mNames;
			Snapshot->mResourceNames = Authored.mResourceNames;
			Snapshot->mManualEdges = Authored.mManualEdges;
			Snapshot->mAutomaticLayouts = Source.mAutomaticLayouts;
			Snapshot->mPersistentBuffers = Source.mPersistentBuffers;
			Snapshot->mPersistentTextures = Source.mPersistentTextures;
			Snapshot->mPipelineCache = Source.mPipelineCache;
			if (Snapshot->mDevice && !Snapshot->mPipelineCache)
			{
				Snapshot->mPipelineCache = eastl::make_shared<FArdaPipelineStateCache>(Snapshot->mDevice);
			}
			return Snapshot;
		}

		eastl::string ConstructionQueueKey(const eastl::vector<FArdaQueuedGraphEdit>& Queue)
		{
			FArdaDependencyKeyBuilder Key;
			Key.Value(static_cast<uint64_t>(Queue.size()));
			for (const auto& Edit : Queue)
			{
				// The complete bytes disambiguate hash collisions and preserve edit boundaries/order.
				Key.Value(Edit.mHash).String(Edit.mKey);
			}
			return Key.Build();
		}
	}

	FArdaRHIStatus FArdaDependencyGraph::BeginGraphEdit(bool bExtendPersistent)
	{
		auto& State = *mRevisions;
		if (mImpl->mbCompiling)
		{
			return GraphCompiling();
		}
		if (State.mbLocked || mImpl->mbEditing || mImpl->mbExecuting)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Graph edits require an unlocked queue; execute the staged revision before recording again.");
		}
		if (!State.mbBuilding)
		{
			State.mNextResourceGeneration = eastl::max(State.mNextResourceGeneration, mImpl->mNextResourceGeneration);
			State.mNextFrameSequence = eastl::max(State.mNextFrameSequence, mImpl->mNextFrameSequence);
			if (bExtendPersistent && State.mPersistent)
			{
				mImpl = SnapshotConstruction(*State.mPersistent);
			}
			else
			{
				auto Draft = eastl::make_shared<FArdaImpl>();
				Draft->mDevice = mImpl->mDevice;
				Draft->mIdentity = mImpl->mIdentity;
				Draft->mOptions = mImpl->mOptions;
				Draft->mPipelineCache = mImpl->mPipelineCache;
				Draft->mAutomaticLayouts = mImpl->mAutomaticLayouts;
				mImpl = eastl::move(Draft);
			}
			mImpl->mNextResourceGeneration = State.mNextResourceGeneration;
			mImpl->mNextFrameSequence = State.mNextFrameSequence;
			State.mbBuilding = true;
		}
		auto& Graph = *mImpl;
		Graph.mSavedTopology = eastl::make_unique<FArdaDependencyTopology>(Graph.mTopology.CloneSnapshot());
		Graph.mSavedResources = Graph.mResources;
		Graph.mSavedManualEdges = Graph.mManualEdges;
		Graph.mSavedOptions = Graph.mOptions;
		Graph.mSavedResourceGenerations = Graph.mResourceGenerations;
		State.mSavedOperationName = Graph.mNextOperationName;
		Graph.mEditGeneration = State.mNextEditGeneration++;
		Graph.mbEditing = true;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::EndGraphEdit()
	{
		auto& Graph = *mImpl;
		if (Graph.mbCompiling)
		{
			return GraphCompiling();
		}
		if (!Graph.mbEditing || mRevisions->mbLocked)
		{
			return GraphNotEditing();
		}
		FArdaQueuedGraphEdit Edit;
		Edit.mKey = BuildArdaGraphConstructionKey(Graph);
		Edit.mHash = HashArdaGraphConstructionKey(Edit.mKey);
		Edit.mSnapshot = SnapshotConstruction(Graph);
		mRevisions->mQueue.push_back(eastl::move(Edit));
		mRevisions->mNextResourceGeneration = Graph.mNextResourceGeneration;
		Graph.mbEditing = false;
		Graph.mSavedTopology.reset();
		Graph.mSavedResources.clear();
		Graph.mSavedManualEdges.clear();
		Graph.mSavedResourceGenerations.clear();
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::CancelGraphEdit()
	{
		auto& Graph = *mImpl;
		if (Graph.mbCompiling)
		{
			return GraphCompiling();
		}
		if (!Graph.mbEditing || !Graph.mSavedTopology)
		{
			return GraphNotEditing();
		}
		Graph.mTopology = Graph.mSavedTopology->CloneSnapshot();
		Graph.mResources = Graph.mSavedResources;
		Graph.mManualEdges = Graph.mSavedManualEdges;
		Graph.mOptions = Graph.mSavedOptions;
		Graph.mResourceGenerations = Graph.mSavedResourceGenerations;
		Graph.mNextOperationName = mRevisions->mSavedOperationName;
		Graph.mNames.clear();
		for (auto Node : Graph.mTopology.GetNodes())
		{
			Graph.mNames.emplace(Graph.mTopology.TryGetNode(Node)->mPayload.mName, Node);
		}
		Graph.mResourceNames.clear();
		for (uint32_t Index = 0; Index < Graph.mResources.size(); ++Index)
		{
			Graph.mResourceNames.emplace(Graph.mResources[Index].mName, Index);
		}
		Graph.mSavedTopology.reset();
		Graph.mSavedResources.clear();
		Graph.mSavedManualEdges.clear();
		Graph.mSavedResourceGenerations.clear();
		Graph.mbEditing = false;
		mRevisions->mNextResourceGeneration = Graph.mNextResourceGeneration;
		if (mRevisions->mQueue.empty())
		{
			return DiscardPendingEdits();
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::Compile(bool bForceRecompile)
	{
		auto& State = *mRevisions;
		if (mImpl->mbCompiling)
		{
			return GraphCompiling();
		}
		if (IsEditing() || mImpl->mbExecuting || State.mbLocked)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Compile requires closed edits and an unlocked execution stage.");
		}
		const auto Key =
		    State.mbBuilding || !State.mPersistent ? ConstructionQueueKey(State.mQueue) : State.mPersistentKey;
		const auto Hash = HashArdaGraphConstructionKey(Key);
		eastl::shared_ptr<FArdaImpl> Selected;
		uint64_t SelectedRevision = 0;
		if (!bForceRecompile)
		{
			for (size_t Index = 0; Index < State.mCache.size(); ++Index)
			{
				if (State.mCache[Index].mHash == Hash && State.mCache[Index].mKey == Key)
				{
					auto Entry = State.mCache[Index];
					if (!IsReusableRuntime(Entry.mGraph->mRuntime.get()))
					{
						// Recompilation is also the recovery path after failed native recording/submission.
						continue;
					}
					Selected = Entry.mGraph;
					SelectedRevision = Entry.mRevision;
					State.mCache.erase(State.mCache.begin() + Index);
					State.mCache.push_back(eastl::move(Entry));
					break;
				}
			}
		}
		const auto Draft = mImpl;
		// Capability/environment verdicts are never cached with compiled topology. Check every
		// attached node before revision retirement can wait or a cached result can be published.
		const auto FeatureSource =
		    Selected ? Selected : (!State.mQueue.empty() ? State.mQueue.back().mSnapshot : Draft);
		if (auto Status = CheckArdaGraphRequiredFeatures(*FeatureSource); !Status)
		{
			return Status;
		}
		// A revision switch must retire shared imports and mutable node state before another runtime uses them.
		if (State.mPersistent && State.mPersistent != Selected)
		{
			CancelArdaInductorTimingOptimization(*State.mPersistent);
			if (State.mPersistent->mRuntime)
			{
				if (auto Status = State.mPersistent->mRuntime->WaitAll(); !Status)
				{
					return Status;
				}
			}
		}
		if (!Selected)
		{
			const auto Source = !State.mQueue.empty() ? State.mQueue.back().mSnapshot : Draft;
			Selected = SnapshotConstruction(*Source);
			Selected->mCompilerSource = SnapshotConstruction(*Selected);
			Selected->mCompiledResourceAliases = CollectResourceAliases(*Selected, Selected->mResourceGenerations);
			if (State.mPersistent)
			{
				State.mNextRevision = eastl::max(State.mNextRevision, State.mPersistent->mCompile.mRevision + 1);
			}
			Selected->mCompile.mRevision = State.mNextRevision - 1;
			Selected->mbEditing = true;
			mImpl = Selected;
			auto Status = FArdaInductor::CompilePending(*this);
			Selected->mbEditing = false;
			mImpl = Draft;
			if (!Status)
			{
				return Status;
			}
			++State.mCompileCount;
			SelectedRevision = State.mCompileCount;
			State.mNextRevision = Selected->mCompile.mRevision + 1;
			Selected->mTiming->Configure(Selected->mOptions.mGpuTimingEmaAlpha,
			    Selected->mOptions.mGpuTimingHistoryCapacity);
			State.mCache.erase(eastl::remove_if(State.mCache.begin(),
			                       State.mCache.end(),
			                       [&](const auto& Entry)
			                       {
				                       return Entry.mHash == Hash && Entry.mKey == Key;
			                       }),
			    State.mCache.end());
			State.mCache.push_back({Hash, Key, Selected, SelectedRevision});
			while (State.mCache.size() > State.mCacheCapacity)
			{
				CancelArdaInductorTimingOptimization(*State.mCache.front().mGraph);
				State.mCache.erase(State.mCache.begin());
			}
		}
		else
		{
			++State.mCacheHitCount;
			if (State.mbBuilding)
			{
				// Preserve every incoming accepted handle, including extension aliases from another
				// revision. Fresh scripts replace authoring aliases instead of accumulating each frame.
				Selected->mResourceAliases = CollectResourceAliases(*Draft, Selected->mResourceGenerations);
			}
		}
		State.mNextResourceGeneration = eastl::max(State.mNextResourceGeneration, Draft->mNextResourceGeneration);
		State.mNextFrameSequence = eastl::max(State.mNextFrameSequence, Draft->mNextFrameSequence);
		Selected->mNextFrameSequence = State.mNextFrameSequence;
		State.mPersistent = Selected;
		State.mPersistentKey = Key;
		State.mPersistentHash = Hash;
		State.mPersistentRevision = SelectedRevision;
		State.mQueue.clear();
		State.mbBuilding = false;
		State.mbLocked = true;
		mImpl = eastl::move(Selected);
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::DiscardPendingEdits()
	{
		auto& State = *mRevisions;
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbExecuting || State.mbLocked)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Discard requires closed edits and an unlocked queue.");
		}
		State.mNextResourceGeneration = eastl::max(State.mNextResourceGeneration, mImpl->mNextResourceGeneration);
		State.mQueue.clear();
		State.mbBuilding = false;
		if (State.mPersistent)
		{
			mImpl = State.mPersistent;
		}
		else
		{
			auto Empty = eastl::make_shared<FArdaImpl>();
			Empty->mDevice = mImpl->mDevice;
			Empty->mIdentity = mImpl->mIdentity;
			Empty->mOptions = mImpl->mOptions;
			Empty->mPipelineCache = mImpl->mPipelineCache;
			Empty->mNextResourceGeneration = State.mNextResourceGeneration;
			mImpl = eastl::move(Empty);
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::InvalidatePersistentCache()
	{
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbExecuting)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Invalidate the revision cache outside graph callbacks and edits.");
		}
		for (const auto& Entry : mRevisions->mCache)
		{
			CancelArdaInductorTimingOptimization(*Entry.mGraph);
		}
		mRevisions->mCache.clear();
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::SetRevisionCacheCapacity(uint32_t Capacity)
	{
		if (!Capacity)
		{
			return GraphInvalid("The revision cache must retain at least one revision.");
		}
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbExecuting)
		{
			return GraphNotEditing();
		}
		mRevisions->mCacheCapacity = Capacity;
		while (mRevisions->mCache.size() > Capacity)
		{
			CancelArdaInductorTimingOptimization(*mRevisions->mCache.front().mGraph);
			mRevisions->mCache.erase(mRevisions->mCache.begin());
		}
		return {};
	}

	FArdaGraphRevisionStats FArdaDependencyGraph::GetRevisionStats() const
	{
		FArdaGraphRevisionStats Result;
		Result.mConstructionHash = mRevisions->mbBuilding
		    ? HashArdaGraphConstructionKey(ConstructionQueueKey(mRevisions->mQueue))
		    : mRevisions->mPersistentHash;
		Result.mPersistentRevision = mRevisions->mPersistentRevision;
		Result.mCompileCount = mRevisions->mCompileCount;
		Result.mCacheHitCount = mRevisions->mCacheHitCount;
		Result.mCachedRevisionCount = static_cast<uint32_t>(mRevisions->mCache.size());
		Result.mPendingEditCount = static_cast<uint32_t>(mRevisions->mQueue.size());
		Result.mbEditQueueLocked = mRevisions->mbLocked;
		return Result;
	}
}
