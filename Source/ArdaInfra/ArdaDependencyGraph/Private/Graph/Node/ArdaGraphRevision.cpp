#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphExternalValues.h"
#include "Graph/Node/ArdaGraphEditOrdering.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Inductor/Preprocess/ArdaPreprocessor.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"

namespace arda
{
	namespace
	{
		void RetireCachedRevision(FArdaDependencyGraph::FArdaImpl& Graph,
		    const FArdaDependencyGraph::FArdaImpl* Persistent)
		{
			CancelArdaInductorTimingOptimization(Graph);
			if (&Graph != Persistent && Graph.mCompile.mExecution->mRuntime)
			{
				// A copied compile result may retain execution ownership after cache eviction.
				(void)Graph.mCompile.mExecution->mRuntime->WaitAll();
			}
		}

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

		eastl::string ConstructionQueueKey(const eastl::vector<eastl::string>& EditKeys)
		{
			FArdaDependencyKeyBuilder Key;
			Key.Value(static_cast<uint64_t>(EditKeys.size()));
			for (const auto& EditKey : EditKeys)
			{
				// The complete bytes disambiguate hash collisions and preserve edit boundaries/order.
				Key.Value(HashArdaGraphConstructionKey(EditKey)).String(EditKey);
			}
			return Key.Build();
		}
	}

	FArdaRHIStatus FArdaDependencyGraph::BeginGraphEdit(FArdaGraphEdit& Edit)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		auto& State = *mRevisions;
		{
			std::lock_guard<std::recursive_mutex> Lock(State.mMutex);
			if (State.IsLocked() || mImpl->mbEditing || mImpl->mbCompiling || mImpl->mbPreprocessing ||
			    mImpl->mbExecuting || GetArdaThreadGraphEdit(this))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Graph edits require an unlocked execution stage and at most one scope per graph on each calling thread.");
			}
			if (!State.mbBuilding)
			{
				State.mNextFrameSequence =
				    eastl::max(State.mNextFrameSequence, mImpl->mCompile.mExecution->mNextFrameSequence);
				auto Draft = eastl::make_shared<FArdaImpl>();
				Draft->mCompile.mExecution->mDevice = mImpl->mCompile.mExecution->mDevice;
				Draft->mIdentity = mImpl->mIdentity;
				Draft->mOptions = mImpl->mOptions;
				Draft->mExternalValues = mImpl->mExternalValues;
				Draft->mCompile.mExecution->mPipelineCache = mImpl->mCompile.mExecution->mPipelineCache;
				Draft->mCompile.mExecution->mAutomaticLayouts = mImpl->mCompile.mExecution->mAutomaticLayouts;
				Draft->mCompile.mExecution->mNextFrameSequence = State.mNextFrameSequence;
				Draft->mResourceGenerationSource = State.mResourceGenerations;
				mImpl = eastl::move(Draft);
				State.mbBuilding = true;
			}
			Edit.mBase = mImpl;
			Edit.mOptionsVersion = State.mOptionsVersion;
			Edit.mGeneration = State.mNextEditGeneration++;
			++State.mActiveEdits;
		}
		try
		{
			// Committed construction snapshots never mutate. Clone outside the publication lock.
			Edit.mRecordingGraph = eastl::make_unique<FArdaDependencyGraph>();
			Edit.mRecordingGraph->mImpl = CloneArdaGraphConstruction(*Edit.mBase);
			Edit.mRecordingGraph->mImpl->mbPreprocessed = false;
			Edit.mRecordingGraph->mImpl->mCompilerSource.reset();
			Edit.mRecordingGraph->mImpl->mbEditing = true;
			Edit.mRecordingGraph->mImpl->mEditGeneration = Edit.mGeneration;
			SetArdaThreadGraphEdit(this, Edit.mRecordingGraph.get());
		}
		catch (...)
		{
			std::lock_guard<std::recursive_mutex> Lock(State.mMutex);
			if (State.CancelEditReservation(Edit.mBase->mbPreprocessed && !State.mPersistent))
			{
				(void)DiscardPendingEdits();
			}
			throw;
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::EndGraphEdit(FArdaGraphEdit& Edit)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		auto& State = *mRevisions;
		std::lock_guard<std::recursive_mutex> Lock(State.mMutex);
		if (!Edit.IsOpen() || State.IsLocked() || mImpl->mbCompiling || mImpl->mbPreprocessing)
		{
			return GraphNotEditing();
		}
		if (Edit.mbOptionsChanged && Edit.mOptionsVersion != State.mOptionsVersion)
		{
			return GraphInvalid("Concurrent edits changed compiler options. Cancel this edit and reopen it.");
		}
		eastl::shared_ptr<FArdaImpl> Combined;
		if (Edit.mBase == mImpl)
		{
			Combined = CloneArdaGraphConstruction(*Edit.mRecordingGraph->mImpl);
		}
		else
		{
			FArdaDependencyGraph Destination;
			Destination.mImpl = CloneArdaGraphConstruction(*mImpl);
			Destination.mImpl->mbEditing = true;
			if (auto Status = MergeGraphEdit(Edit, Destination); !Status)
			{
				return Status;
			}
			Destination.mImpl->mbEditing = false;
			Combined = Destination.mImpl;
		}
		Combined->mbPreprocessed = false;
		Combined->mCompilerSource.reset();
		const auto& Source = *Edit.mRecordingGraph->mImpl;
		const auto& Base = *Edit.mBase;
		// Archives may restore several edit groups into one authoring transaction. Preserve those
		// boundaries instead of adding an overlapping synthetic group for the import transaction.
		Combined->mCommittedEdits.resize(mImpl->mCommittedEdits.size());
		const auto AppendEdit = [&](FArdaCommittedGraphEdit Record)
		{
			for (auto& Node : Record.mNodes)
			{
				Node = Combined->ResolveNode(Node);
			}
			for (auto& Resource : Record.mResources)
			{
				Resource.mResource = Combined->ResolveResource(Resource.mResource);
				Combined->mResources[Resource.mResource.mIndex].mbRevisionOrdered = true;
			}
			Combined->mCommittedEdits.push_back(eastl::move(Record));
		};
		if (Source.mCommittedEdits.size() > Base.mCommittedEdits.size())
		{
			for (size_t Index = Base.mCommittedEdits.size(); Index < Source.mCommittedEdits.size(); ++Index)
			{
				AppendEdit(Source.mCommittedEdits[Index]);
			}
		}
		else
		{
			FArdaCommittedGraphEdit Record;
			Record.mResources = Edit.mResourceRevisions;
			for (const auto Node : Source.mTopology.GetNodes())
			{
				if (!Base.mTopology.ContainsNode(Node))
				{
					Record.mNodes.push_back(Node);
				}
			}
			eastl::sort(Record.mNodes.begin(),
			    Record.mNodes.end(),
			    [&](const auto Left, const auto Right)
			    {
				    return Source.mTopology.TryGetNode(Left)->mPayload.mAttachmentOrder <
				        Source.mTopology.TryGetNode(Right)->mPayload.mAttachmentOrder;
			    });
			AppendEdit(eastl::move(Record));
		}
		RefreshConstructionKeys(*Combined);
		State.mEditKeys.push_back(BuildArdaGraphConstructionKey(*Combined));
		mImpl = eastl::move(Combined);
		if (Edit.mbOptionsChanged)
		{
			++State.mOptionsVersion;
		}
		Edit.mRecordingGraph->mImpl->mbEditing = false;
		--State.mActiveEdits;
		SetArdaThreadGraphEdit(this, nullptr);
		Edit.mRecordingGraph.reset();
		Edit.mBase.reset();
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::CancelGraphEdit(FArdaGraphEdit& Edit)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		auto& State = *mRevisions;
		std::lock_guard<std::recursive_mutex> Lock(State.mMutex);
		if (!Edit.IsOpen())
		{
			return GraphNotEditing();
		}
		const bool bPreservePreprocessedGraph = Edit.mBase->mbPreprocessed && !State.mPersistent;
		Edit.mRecordingGraph->mImpl->mbEditing = false;
		const bool bDiscard = State.CancelEditReservation(bPreservePreprocessedGraph);
		SetArdaThreadGraphEdit(this, nullptr);
		Edit.mRecordingGraph.reset();
		Edit.mBase.reset();
		if (bDiscard)
		{
			return DiscardPendingEdits();
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::Compile(bool bForceRecompile)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		auto& State = *mRevisions;
		if (mImpl->mbCompiling || mImpl->mbPreprocessing)
		{
			return GraphCompiling();
		}
		if (IsEditing() || mImpl->mbExecuting || State.IsLocked())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Compile requires closed edits and an unlocked execution stage.");
		}

		// Validate version ordering before cache retirement can wait or mutate retained runtimes.
		// Ordering only touches this private clone; commit-order snapshots stay immutable.
		struct FArdaConstructionRestore
		{
			eastl::shared_ptr<FArdaImpl>& mCurrent;
			eastl::shared_ptr<FArdaImpl> mOriginal;
			bool mbPublished = false;

			~FArdaConstructionRestore()
			{
				if (!mbPublished)
				{
					mCurrent = eastl::move(mOriginal);
				}
			}
		} Restore{mImpl, mImpl};

		auto Draft = mImpl;
		if (!mImpl->mbPreprocessed &&
		    eastl::any_of(mImpl->mCommittedEdits.begin(),
		        mImpl->mCommittedEdits.end(),
		        [](const auto& Edit)
		        {
			        return !Edit.mResources.empty();
		        }))
		{
			Draft = CloneArdaGraphConstruction(*mImpl);
			OrderArdaGraphEdits(*Draft);
		}
		const auto Key =
		    State.mbBuilding || !State.mPersistent ? ConstructionQueueKey(State.mEditKeys) : State.mPersistentKey;
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
					if (!IsReusableRuntime(Entry.mGraph->mCompile.mExecution->mRuntime.get()))
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
		// Capability/environment verdicts are never cached with compiled topology. Check every
		// attached node before revision retirement can wait or a cached result can be published.
		const auto FeatureSource = Selected ? Selected : Draft;
		if (auto Status = ValidateArdaGraphExternalBindings(*FeatureSource); !Status)
		{
			return Status;
		}
		if (auto Status = CheckArdaGraphRequiredFeatures(*FeatureSource); !Status)
		{
			return Status;
		}
		// A revision switch must retire shared imports and mutable node state before another runtime uses them.
		if (State.mPersistent && State.mPersistent != Selected)
		{
			CancelArdaInductorTimingOptimization(*State.mPersistent);
			if (State.mPersistent->mCompile.mExecution->mRuntime)
			{
				if (auto Status = State.mPersistent->mCompile.mExecution->mRuntime->WaitAll(); !Status)
				{
					return Status;
				}
			}
		}
		if (!Selected)
		{
			Selected = CloneArdaGraphConstruction(*Draft, false);
			Selected->mCompilerSource = CloneArdaGraphConstruction(*Selected, false);
			Selected->mCompiledResourceAliases = CollectResourceAliases(*Selected, Selected->mResourceGenerations);
			Selected->mCompiledResourceRemaps = Selected->mResourceRemaps;
			if (State.mPersistent)
			{
				State.mNextRevision = eastl::max(State.mNextRevision, State.mPersistent->mCompile.mRevision + 1);
			}
			Selected->mCompile.mRevision = State.mNextRevision - 1;
			mImpl = Selected;
			auto Preprocessed = FArdaPreprocessor::Preprocess(*this);
			if (!Preprocessed)
			{
				mImpl = Draft;
				return Preprocessed.mStatus;
			}
			Selected = Preprocessed.mValue->mImpl;
			Selected->mCompile.mRevision = State.mNextRevision - 1;
			mImpl = Selected;
			FArdaInductorExecutionState Reuse;
			const auto& Previous = State.mPersistent ? *State.mPersistent : *Draft;
			Reuse.mPipelineCache = Previous.mCompile.mExecution->mPipelineCache;
			Reuse.mAutomaticLayouts = Previous.mCompile.mExecution->mAutomaticLayouts;
			Reuse.mPersistentBuffers.resize(Selected->mResources.size());
			Reuse.mPersistentTextures.resize(Selected->mResources.size());
			const bool bSameConstruction =
			    State.mPersistent && State.mPersistentHash == Hash && State.mPersistentKey == Key;
			for (size_t Index = 0; Index < Selected->mResources.size(); ++Index)
			{
				const auto& Resource = Selected->mResources[Index];
				const auto SameStorage = [&](const FArdaDependencyResourceDesc& Other)
				{
					return Resource.mbPersistent && Other.mbPersistent && Resource.mbTexture == Other.mbTexture &&
					    Resource.mBufferInitializer == Other.mBufferInitializer &&
					    (Resource.mbTexture ? Resource.mTexture == Other.mTexture : Resource.mBuffer == Other.mBuffer);
				};
				const auto Found = eastl::find(Previous.mResourceGenerations.begin(),
				    Previous.mResourceGenerations.end(),
				    Selected->mResourceGenerations[Index]);
				size_t PreviousIndex = static_cast<size_t>(Found - Previous.mResourceGenerations.begin());
				if (Found == Previous.mResourceGenerations.end() && bSameConstruction)
				{
					// Expanding the same subgraph body creates fresh internal generations. Its stable
					// qualified name may retain storage only when rebuilding this exact construction.
					const auto Named = eastl::find_if(Previous.mResources.begin(),
					    Previous.mResources.end(),
					    [&](const auto& Other)
					    {
						    return Other.mName == Resource.mName && SameStorage(Other);
					    });
					PreviousIndex = static_cast<size_t>(Named - Previous.mResources.begin());
				}
				if (PreviousIndex >= Previous.mResources.size() || !SameStorage(Previous.mResources[PreviousIndex]))
				{
					continue;
				}
				if (PreviousIndex < Previous.mCompile.mExecution->mPersistentBuffers.size())
				{
					Reuse.mPersistentBuffers[Index] = Previous.mCompile.mExecution->mPersistentBuffers[PreviousIndex];
				}
				if (PreviousIndex < Previous.mCompile.mExecution->mPersistentTextures.size())
				{
					Reuse.mPersistentTextures[Index] = Previous.mCompile.mExecution->mPersistentTextures[PreviousIndex];
				}
			}
			auto Status = FArdaInductor::CompilePending(*this, &Reuse);
			mImpl = Draft;
			if (!Status)
			{
				return Status;
			}
			++State.mCompileCount;
			SelectedRevision = State.mCompileCount;
			State.mNextRevision = Selected->mCompile.mRevision + 1;
			Selected->mCompile.mExecution->mTiming->Configure(Selected->mOptions.mGpuTimingEmaAlpha,
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
				RetireCachedRevision(*State.mCache.front().mGraph, State.mPersistent.get());
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
				Selected->mResourceRemaps = Draft->mResourceRemaps;
				for (auto& Mapping : Selected->mResourceRemaps)
				{
					Mapping.second.mGeneration = Selected->mResourceGenerations[Mapping.second.mIndex];
				}
			}
		}
		State.mNextResourceGeneration = eastl::max(State.mNextResourceGeneration, Draft->mNextResourceGeneration);
		State.mNextFrameSequence = eastl::max(State.mNextFrameSequence, Draft->mCompile.mExecution->mNextFrameSequence);
		Selected->mCompile.mExecution->mNextFrameSequence = State.mNextFrameSequence;
		State.mPersistent = Selected;
		State.mPersistentKey = Key;
		State.mPersistentHash = Hash;
		State.mPersistentRevision = SelectedRevision;
		State.mEditKeys.clear();
		State.mbBuilding = false;
		State.mbLocked = true;
		mImpl = eastl::move(Selected);
		Restore.mbPublished = true;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::DiscardPendingEdits()
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		auto& State = *mRevisions;
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting || State.IsLocked())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Discard requires closed edits and an unlocked execution stage.");
		}
		State.mNextResourceGeneration = eastl::max(State.mNextResourceGeneration, mImpl->mNextResourceGeneration);
		State.mEditKeys.clear();
		State.mbBuilding = false;
		State.mbLocked = false;
		if (State.mPersistent)
		{
			mImpl = State.mPersistent;
		}
		else
		{
			auto Empty = eastl::make_shared<FArdaImpl>();
			Empty->mCompile.mExecution->mDevice = mImpl->mCompile.mExecution->mDevice;
			Empty->mIdentity = mImpl->mIdentity;
			Empty->mOptions = mImpl->mOptions;
			Empty->mCompile.mExecution->mPipelineCache = mImpl->mCompile.mExecution->mPipelineCache;
			Empty->mNextResourceGeneration = State.mNextResourceGeneration;
			mImpl = eastl::move(Empty);
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::InvalidatePersistentCache()
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Invalidate the revision cache outside graph callbacks and edits.");
		}
		for (const auto& Entry : mRevisions->mCache)
		{
			RetireCachedRevision(*Entry.mGraph, mRevisions->mPersistent.get());
		}
		mRevisions->mCache.clear();
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::SetRevisionCacheCapacity(uint32_t Capacity)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		if (!Capacity)
		{
			return GraphInvalid("The revision cache must retain at least one revision.");
		}
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting)
		{
			return GraphNotEditing();
		}
		mRevisions->mCacheCapacity = Capacity;
		while (mRevisions->mCache.size() > Capacity)
		{
			RetireCachedRevision(*mRevisions->mCache.front().mGraph, mRevisions->mPersistent.get());
			mRevisions->mCache.erase(mRevisions->mCache.begin());
		}
		return {};
	}

	FArdaGraphRevisionStats FArdaDependencyGraph::FArdaRevisionState::Snapshot() const
	{
		const auto& State = *this;
		FArdaGraphRevisionStats Result;
		Result.mConstructionHash = State.mbBuilding
		    ? HashArdaGraphConstructionKey(ConstructionQueueKey(State.mEditKeys))
		    : State.mPersistentHash;
		Result.mPersistentRevision = State.mPersistentRevision;
		Result.mCompileCount = State.mCompileCount;
		Result.mCacheHitCount = State.mCacheHitCount;
		Result.mCachedRevisionCount = static_cast<uint32_t>(State.mCache.size());
		Result.mPendingEditCount = static_cast<uint32_t>(State.mEditKeys.size());
		Result.mbEditQueueLocked = State.IsLocked();
		return Result;
	}

	FArdaGraphRevisionStats FArdaDependencyGraph::GetRevisionStats() const
	{
		if (const auto Recording = FArdaGraphRecordingScope::Find(mRevisions.get()))
		{
			return Recording->mRevisionStats;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		return mRevisions->Snapshot();
	}
}
