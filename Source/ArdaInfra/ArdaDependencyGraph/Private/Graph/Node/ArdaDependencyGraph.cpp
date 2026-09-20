#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorTelemetry.h"
#include <EASTL/atomic.h>

namespace arda
{
	FArdaDependencyGraph::FArdaDependencyGraph(FArdaRHIDeviceRef Device)
	    : mImpl(eastl::make_shared<FArdaImpl>()),
	      mRevisions(eastl::make_unique<FArdaRevisionState>())
	{
		static eastl::atomic<uint64_t> Next{1};
		mImpl->mIdentity = Next.fetch_add(1);
		mImpl->mDevice = eastl::move(Device);
		if (mImpl->mDevice)
		{
			mImpl->mPipelineCache = eastl::make_shared<FArdaPipelineStateCache>(mImpl->mDevice);
		}
	}

	FArdaDependencyGraph::~FArdaDependencyGraph()
	{
		CancelArdaInductorTimingOptimization(*mImpl);
		for (const auto& Entry : mRevisions->mCache)
		{
			CancelArdaInductorTimingOptimization(*Entry.mGraph);
		}
		if (mRevisions->mPersistent)
		{
			CancelArdaInductorTimingOptimization(*mRevisions->mPersistent);
		}
	}

	bool FArdaDependencyGraph::IsEditing() const noexcept
	{
		return mImpl->mbEditing;
	}

	const FArdaInductorOptions& FArdaDependencyGraph::GetOptions() const
	{
		return mImpl->mOptions;
	}

	FArdaRHIDeviceRef FArdaDependencyGraph::GetDevice() const
	{
		return mImpl->mDevice;
	}

	const FArdaDependencyTopology& FArdaDependencyGraph::GetTopology() const
	{
		return mImpl->mTopology;
	}

	const FArdaInductorCompileResult& FArdaDependencyGraph::GetCompileResult() const
	{
		return mRevisions->mPersistent ? mRevisions->mPersistent->mCompile : mImpl->mCompile;
	}

	FArdaPipelineStateCacheStats FArdaDependencyGraph::GetPipelineCacheStats() const
	{
		return mImpl->mPipelineCache ? mImpl->mPipelineCache->GetStats() : FArdaPipelineStateCacheStats{};
	}

	FArdaGraphNodeHandle FArdaDependencyGraph::FindNode(const eastl::string& Name) const
	{
		auto I = mImpl->mNames.find(Name);
		return I == mImpl->mNames.end() ? FArdaGraphNodeHandle{} : I->second;
	}

	FArdaDependencyResourceHandle FArdaDependencyGraph::FindOutput(FArdaGraphNodeHandle Node,
	    const eastl::string& Name) const
	{
		const auto* N = mImpl->mTopology.TryGetNode(Node);
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
