#pragma once

#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"

namespace arda
{
	struct FArdaInductorRuntimeTransitions
	{
		struct FArdaAliasingResource
		{
			EArdaGraphResourceType mType = EArdaGraphResourceType::Texture;
			uint32_t mResourceIndex = 0;
			bool mbDiscardTexture = false;
		};

		struct FArdaTextureQueueTransfer
		{
			FArdaInductorTextureHandle mTexture;
			arda::FArdaRHITextureSubresourceRange mSubresources;
			arda::EArdaRHIQueueType mSourceQueue = arda::EArdaRHIQueueType::Graphics;
			arda::EArdaRHIQueueType mDestinationQueue = arda::EArdaRHIQueueType::Graphics;
		};

		struct FArdaBufferQueueTransfer
		{
			FArdaInductorBufferHandle mBuffer;
			arda::EArdaRHIQueueType mSourceQueue = arda::EArdaRHIQueueType::Graphics;
			arda::EArdaRHIQueueType mDestinationQueue = arda::EArdaRHIQueueType::Graphics;
		};

		/** Physical texture transitions emitted while recording this pass. */
		eastl::vector<FArdaInductorTextureTransition> mTextures;
		/** Physical buffer transitions emitted while recording this pass. */
		eastl::vector<FArdaInductorBufferTransition> mBuffers;
		eastl::vector<FArdaInductorAccelerationStructureTransition> mAccelStructs;
		/** Queue-ownership acquire barriers emitted before pass transitions. */
		eastl::vector<FArdaTextureQueueTransfer> mTextureAcquires;
		eastl::vector<FArdaBufferQueueTransfer> mBufferAcquires;
		/** Common-state releases emitted after pass work on the producer queue. */
		eastl::vector<FArdaTextureQueueTransfer> mTextureReleases;
		eastl::vector<FArdaBufferQueueTransfer> mBufferReleases;
		/** Placed resources whose overlapping heap range becomes active in this pass. */
		eastl::vector<FArdaAliasingResource> mAliasingResources;
	};

	struct FArdaInductorRecordedCommand
	{
		/** First failure from command recording or the pass callback. */
		arda::FArdaRHIStatus mStatus;
		/** RHI command list populated during the recording stage. */
		arda::FArdaRHICommandListRef mCommandList;
		/** Submission queue selected from the pass pipeline; graphics is the default. */
		arda::EArdaRHIQueueType mQueue = arda::EArdaRHIQueueType::Graphics;
		/** State evidence captured while recording this pass. */
		eastl::vector<FArdaGraphStateConformanceRecord> mStateConformanceRecords;
	};

	eastl::vector<FArdaInductorRuntimeTransitions> BuildPhysicalTransitions(
	    FArdaInductorCommandProgram::FArdaImpl& Graph);

	[[nodiscard]] FArdaInductorRecordedCommand RecordPass(FArdaInductorCommandProgram& Program,
	    FArdaInductorCommandProgram::FArdaImpl& Graph,
	    FArdaInductorCommandHandle Handle,
	    const FArdaInductorRuntimeTransitions& Transitions,
	    bool bValidateResourceStates,
	    FArdaInductorRecordedCommand Recorded = {},
	    bool bClose = true);
}
