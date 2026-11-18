#include "Inductor/Runtime/ArdaGraphExecutor.h"
#include "Inductor/Compiler/ArdaInductorCompiledGraph.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphRevisionState.h"

namespace arda
{
	FArdaGraphExecutor::FArdaGraphExecutor(FArdaInductorCompileResult Result)
	{
		if (!Result.mStatus || !Result.mRevision || !Result.mGraph || !Result.mGraph->mGraph || !Result.mExecution)
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Executor requires a successful standalone compilation result.");
			return;
		}
		if (Result.mExecution->mbExecutorClaimed.exchange(true))
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "A compile result already belongs to an executor; compile again for an independent execution.");
			return;
		}
		mGraph = eastl::make_unique<FArdaDependencyGraph>();
		mGraph->mImpl = CloneArdaGraphConstruction(*Result.mGraph->mGraph);
		mGraph->mImpl->mCompile = eastl::move(Result);
		mGraph->mImpl->mbUsingCompiledDescriptions = true;
		mGraph->mImpl->mbEditing = false;
		mGraph->mRevisions->mPersistent = mGraph->mImpl;
		mGraph->mRevisions->mPersistentRevision = mGraph->mImpl->mCompile.mRevision;
		mGraph->mRevisions->mbBuilding = false;
	}

	FArdaGraphExecutor::~FArdaGraphExecutor() = default;

	const FArdaRHIStatus& FArdaGraphExecutor::GetStatus() const noexcept
	{
		return mStatus;
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaGraphExecutor::Submit(const FArdaGraphExecuteOptions& Options)
	{
		return mStatus ? mGraph->Submit(Options) : TArdaRHIResult<FArdaDependencyFrameTicket>{{}, mStatus};
	}

	FArdaGraphExecutionResult FArdaGraphExecutor::Wait(const FArdaDependencyFrameTicket& Ticket)
	{
		if (mStatus)
		{
			return mGraph->Wait(Ticket);
		}
		FArdaGraphExecutionResult Result;
		Result.mStatus = mStatus;
		return Result;
	}

	TArdaRHIResult<bool> FArdaGraphExecutor::IsComplete(const FArdaDependencyFrameTicket& Ticket) const
	{
		return mStatus ? mGraph->IsComplete(Ticket) : TArdaRHIResult<bool>{false, mStatus};
	}

	FArdaGraphExecutionResult FArdaGraphExecutor::Execute(const FArdaGraphExecuteOptions& Options)
	{
		if (mStatus)
		{
			return mGraph->Execute(Options);
		}
		FArdaGraphExecutionResult Result;
		Result.mStatus = mStatus;
		return Result;
	}
}
