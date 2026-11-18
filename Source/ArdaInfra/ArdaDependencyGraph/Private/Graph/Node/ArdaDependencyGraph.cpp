#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include <EASTL/atomic.h>

namespace arda
{
	namespace
	{
		thread_local eastl::unordered_map<const FArdaDependencyGraph*, const FArdaDependencyGraph*> ThreadEdits;
	}

	const FArdaDependencyGraph* GetArdaThreadGraphEdit(const FArdaDependencyGraph* Graph)
	{
		const auto Found = ThreadEdits.find(Graph);
		return Found == ThreadEdits.end() ? nullptr : Found->second;
	}

	void SetArdaThreadGraphEdit(const FArdaDependencyGraph* Graph, const FArdaDependencyGraph* Recording)
	{
		if (Recording)
		{
			ThreadEdits[Graph] = Recording;
		}
		else
		{
			ThreadEdits.erase(Graph);
		}
	}

	const FArdaDependencyGraph::FArdaImpl& FArdaDependencyGraph::GetReadImpl() const
	{
		const auto* Editing = GetArdaThreadGraphEdit(this);
		return *(Editing ? Editing->mImpl : mImpl);
	}

	FArdaDependencyGraph::FArdaDependencyGraph(FArdaRHIDeviceRef Device)
	    : mRevisions(eastl::make_shared<FArdaRevisionState>()),
	      mImpl(mRevisions->mCurrent)
	{
		static eastl::atomic<uint64_t> Next{1};
		mImpl->mIdentity = Next.fetch_add(1);
		mImpl->mCompile.mExecution->mDevice = eastl::move(Device);
		if (mImpl->mCompile.mExecution->mDevice)
		{
			mImpl->mCompile.mExecution->mPipelineCache =
			    eastl::make_shared<FArdaPipelineStateCache>(mImpl->mCompile.mExecution->mDevice);
		}
	}

	FArdaDependencyGraph::~FArdaDependencyGraph() = default;

	FArdaDependencyGraph::FArdaRevisionState::~FArdaRevisionState()
	{
		const auto Retire = [](FArdaImpl& Graph)
		{
			CancelArdaInductorTimingOptimization(Graph);
			if (Graph.mCompile.mExecution->mRuntime)
			{
				(void)Graph.mCompile.mExecution->mRuntime->WaitAll();
			}
		};
		Retire(*mCurrent);
		for (const auto& Entry : mCache)
		{
			Retire(*Entry.mGraph);
		}
		if (mPersistent)
		{
			Retire(*mPersistent);
		}
	}

	bool FArdaDependencyGraph::IsEditing() const noexcept
	{
		// Recording admission requires closed edits, and its parent owns the revision mutex.
		if (FArdaGraphRecordingScope::Find(mRevisions.get()))
		{
			return false;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		return mImpl->mbEditing || mRevisions->mActiveEdits != 0;
	}

	const FArdaInductorOptions& FArdaDependencyGraph::GetOptions() const
	{
		return GetReadImpl().mOptions;
	}

	FArdaRHIDeviceRef FArdaDependencyGraph::GetDevice() const
	{
		return GetReadImpl().mCompile.mExecution->mDevice;
	}

	const FArdaDependencyTopology& FArdaDependencyGraph::GetTopology() const
	{
		return GetReadImpl().mTopology;
	}

	const FArdaDependencyNodeDesc& FArdaDependencyGraph::GetNodeDescription(FArdaGraphNodeHandle Node) const
	{
		const auto& G = GetReadImpl();
		return G.GetNodeDescription(G.mTopology.TryGetNode(G.ResolveNode(Node))->mPayload);
	}

	const FArdaInductorCompileResult& FArdaDependencyGraph::GetCompileResult() const
	{
		if (const auto* Editing = GetArdaThreadGraphEdit(this))
		{
			return Editing->mImpl->mCompile;
		}
		return mRevisions->mPersistent ? mRevisions->mPersistent->mCompile : mImpl->mCompile;
	}

	FArdaPipelineStateCacheStats FArdaDependencyGraph::GetPipelineCacheStats() const
	{
		const auto& Graph = GetReadImpl();
		return Graph.mCompile.mExecution->mPipelineCache ? Graph.mCompile.mExecution->mPipelineCache->GetStats()
		                                                 : FArdaPipelineStateCacheStats{};
	}

	FArdaGraphNodeHandle FArdaDependencyGraph::FindNode(const eastl::string& Name) const
	{
		const auto& Graph = GetReadImpl();
		auto I = Graph.mNames.find(Name);
		return I == Graph.mNames.end() ? FArdaGraphNodeHandle{} : I->second;
	}

	FArdaDependencyResourceHandle FArdaDependencyGraph::FindOutput(FArdaGraphNodeHandle Node,
	    const eastl::string& Name) const
	{
		const auto& Graph = GetReadImpl();
		const auto* N = Graph.mTopology.TryGetNode(Graph.ResolveNode(Node));
		if (N)
		{
			for (const auto& O : N->mPayload.mOutputs)
			{
				if (O.mName == Name)
				{
					return O.mResource;
				}
			}
		}
		return {};
	}
}
