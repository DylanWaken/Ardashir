#pragma once
#include <atomic>

#include "RHI/Memory/ArdaMemoryPlanner.h"
#include "Inductor/Compiler/ArdaInductorCommandProgram.h"
#include "Inductor/Runtime/ArdaInductorFrameTicketState.h"
#include "Inductor/Runtime/ArdaInductorNodeBindings.h"

namespace arda
{
	/** One independently reusable physical pool and cached native lowering. */
	struct FArdaInductorFrame
	{
		/** Compiler instancing candidate group for each eligible draw. */
		eastl::unordered_map<uint32_t, uint32_t> mInstancingGroups;
		std::atomic<uint32_t> mRecordedGraphicsDrawCount{0}, mMergedGraphicsDrawCount{0};
		FArdaMemoryResources mMemory;
		eastl::unique_ptr<FArdaInductorCommandProgram> mLowered;
		eastl::vector<FArdaInductorBuffer*> mBuffers;
		eastl::vector<FArdaInductorTexture*> mTextures;
		eastl::vector<FArdaInductorAccelerationStructure*> mAccelerationStructures;
		eastl::unordered_map<uint32_t, FArdaInductorNodeBindings> mNodeBindings;
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mActive;
		/** At most one outstanding timing generation; busy queries skip subsequent instrumentation. */
		eastl::shared_ptr<FArdaDependencyFrameTicket::FArdaState> mTimingState;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		uint32_t mIndex = 0;
		eastl::unordered_map<uint32_t, eastl::shared_ptr<FArdaCudaGraphCache>> mCudaCaches;
	};

	/** Immutable native references used to prepare a different schedule without reallocating GPU storage. */
	struct FArdaInductorReuseFrame
	{
		FArdaMemoryResources mMemory;
		eastl::vector<EArdaRHIResourceState> mBufferStates, mTextureStates;
		eastl::unordered_map<uint32_t, FArdaInductorNodeBindings> mNodeBindings;
		eastl::vector<FArdaInductorFrameTimer> mTimers;
		eastl::vector<FArdaInductorCudaFrameTimer> mCudaTimers;
		eastl::unordered_map<uint32_t, eastl::shared_ptr<FArdaCudaGraphCache>> mCudaCaches;
	};
}
