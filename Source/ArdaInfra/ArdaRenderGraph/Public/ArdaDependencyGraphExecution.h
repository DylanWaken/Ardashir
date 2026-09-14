#pragma once

#include "RHI/ArdaRHI.h"

#include <cstdint>
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Physical resource kind observed by an execution diagnostic. */
	enum class EArdaGraphResourceType : uint8_t
	{
		Texture,
		Buffer,
		AccelStruct
	};

	/** One native execution dependency. Indices identify this report's lowered work,
	 * including generated boundary operations; they are not semantic node handles.
	 */
	struct FArdaGraphQueueDependency
	{
		uint32_t mProducer = UINT32_MAX;
		uint32_t mConsumer = UINT32_MAX;
		EArdaRHIQueueType mProducerQueue = EArdaRHIQueueType::Graphics;
		EArdaRHIQueueType mConsumerQueue = EArdaRHIQueueType::Graphics;
	};

	/** Configures command-list recording for graph execution. */
	struct FArdaGraphExecuteOptions
	{
		/** Records independent pass waves concurrently when true. */
		bool mbParallelRecording = true;

		/** Maximum recording workers, or zero to use hardware concurrency. */
		uint32_t mMaxRecordingThreads = 0;

		/** Captures and validates RHI/native state at every graph checkpoint. */
		bool mbValidateResourceStates = true;
	};

	/** Identifies when a graph resource-state snapshot was captured. */
	enum class EArdaGraphStateCheckpoint : uint8_t
	{
		/** Captured before lowering a physical transition. */
		BeforeTransition,
		/** Captured after transition lowering. */
		AfterTransition,
		/** Captured after the pass callback completes. */
		AfterPass,
		/** Captured after the producer releases a resource in Common state. */
		QueueRelease,
		/** Captured after the consumer acquires a resource in Common state. */
		QueueAcquire
	};

	/** Records one expected graph state and the state observed through ArdaRHI. */
	struct FArdaGraphStateConformanceRecord
	{
		/** Pass associated with the state checkpoint. */
		uint32_t mPass = UINT32_MAX;
		/** Human-readable pass name. */
		eastl::string mPassName;
		/** Texture or buffer resource kind. */
		EArdaGraphResourceType mResourceType = EArdaGraphResourceType::Texture;
		/** Resource registry index for mResourceType. */
		uint32_t mResourceIndex = 0;
		/** Human-readable logical resource name. */
		eastl::string mResourceName;
		/** Texture subresources, or the default range for a buffer. */
		arda::FArdaRHITextureSubresourceRange mTextureSubresources;
		/** Checkpoint within transition recording or pass execution. */
		EArdaGraphStateCheckpoint mCheckpoint = EArdaGraphStateCheckpoint::BeforeTransition;
		/** State expected by physical graph transition lowering. */
		arda::EArdaRHIResourceState mExpectedState = arda::EArdaRHIResourceState::Unknown;
		/** Queue expected to own the resource at an ownership checkpoint. */
		arda::EArdaRHIQueueType mExpectedQueueOwner = arda::EArdaRHIQueueType::Graphics;
		/** Expected Vulkan family, or the invalid-family sentinel on D3D12. */
		uint32_t mExpectedQueueFamily = arda::ArdaRHIInvalidQueueFamily;
		/** Whether queue and native-family ownership participate in consistency. */
		bool mbValidateQueueOwnership = false;
		/** Independently observed facade/backend/native state. */
		arda::FArdaRHIResourceStateSnapshot mObserved;
		/** Query status when the observation could not be produced. */
		arda::FArdaRHIStatus mStatus;

		/**
         * Tests whether graph, facade, backend, and native encoding agree.
         * @return True when every state source matches.
         */
		[[nodiscard]] bool IsConsistent() const noexcept
		{
			if (!mStatus.IsSuccess() || !mObserved.IsConsistent() || mObserved.mFacadeState != mExpectedState)
			{
				return false;
			}
			if (!mbValidateQueueOwnership)
			{
				return true;
			}
			return mObserved.mbFacadeQueueOwnerKnown && mObserved.mFacadeQueueOwner == mExpectedQueueOwner &&
			    (mExpectedQueueFamily == arda::ArdaRHIInvalidQueueFamily ||
			        mObserved.mNative.mQueueFamily == mExpectedQueueFamily);
		}
	};

	/** Reports one graph execution attempt, including failures and accepted submissions. */
	struct FArdaGraphExecutionResult
	{
		/** Overall recording, conformance-validation, and submission status. */
		arda::FArdaRHIStatus mStatus;

		/** Number of pass and boundary-barrier command lists submitted. */
		uint32_t mSubmittedCommandListCount = 0;

		/** Number of command lists rejected by the RHI during submission. */
		uint32_t mSubmissionFailureCount = 0;

		/** Number of explicit waits inserted between different queues. */
		uint32_t mQueueWaitCount = 0;

		/** Execution dependencies, including physical ownership and memory aliasing edges. */
		eastl::vector<FArdaGraphQueueDependency> mQueueDependencies;

		/** Whether pass command lists were recorded concurrently. */
		bool mbUsedParallelRecording = false;

		/** Number of placed resources activated over a previously occupied range. */
		uint32_t mAliasingBarrierCount = 0;

		/** Per-checkpoint graph/facade/backend/native state evidence. */
		eastl::vector<FArdaGraphStateConformanceRecord> mStateConformanceRecords;

		/** Number of state checkpoints that did not agree across all layers. */
		uint32_t mStateConformanceFailureCount = 0;

		/** Last submitted RHI instance for graphics, compute, and copy queues. */
		eastl::array<uint64_t, arda::ArdaRHIQueueTypeCount> mLastSubmittedInstances{};
	};

}
