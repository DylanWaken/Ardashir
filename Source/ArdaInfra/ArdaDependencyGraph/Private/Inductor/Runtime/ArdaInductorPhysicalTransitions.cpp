#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"
#include "Inductor/Runtime/ArdaInductorCommandRecording.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	void AddExecutionDependency(FArdaInductorCommandProgram::FArdaImpl& Graph,
	    FArdaInductorCommandHandle Producer,
	    FArdaInductorCommandHandle Consumer)
	{
		const auto Source = Graph.mPasses.Get(Producer).GetState().mQueue;
		const auto Destination = Graph.mPasses.Get(Consumer).GetState().mQueue;
		auto& Edges = Graph.mExecutionResult.mQueueDependencies;
		if (Source != Destination &&
		    eastl::none_of(Edges.begin(),
		        Edges.end(),
		        [Producer, Consumer](const auto& Edge)
		        {
			        return Edge.mProducer == Producer.GetIndex() && Edge.mConsumer == Consumer.GetIndex();
		        }))
		{
			Edges.push_back({Producer.GetIndex(), Consumer.GetIndex(), Source, Destination});
		}
	}

	[[nodiscard]] eastl::vector<FArdaInductorRuntimeTransitions> BuildPhysicalTransitions(
	    FArdaInductorCommandProgram::FArdaImpl& Graph)
	{
		struct FArdaTextureQueueHistory
		{
			eastl::vector<FArdaInductorCommandHandle> mPasses;
			eastl::vector<arda::EArdaRHIQueueType> mQueues;
			eastl::vector<FArdaInductorTextureHandle> mTextures;
		};

		struct FArdaBufferQueueHistory
		{
			FArdaInductorCommandHandle mPass;
			arda::EArdaRHIQueueType mQueue = arda::EArdaRHIQueueType::Graphics;
			FArdaInductorBufferHandle mBuffer;
		};

		eastl::unordered_map<const void*, eastl::vector<arda::EArdaRHIResourceState>> TextureStates;
		eastl::unordered_map<const void*, FArdaTextureQueueHistory> TextureQueues;
		eastl::unordered_map<const void*, arda::EArdaRHIResourceState> BufferStates;
		eastl::unordered_map<const void*, FArdaBufferQueueHistory> BufferQueues;
		// Outstanding accesses since the last memory/state barrier, by physical allocation.
		eastl::unordered_map<const void*, eastl::vector<FArdaInductorBufferAccess>> PendingBufferAccesses;
		eastl::vector<FArdaInductorRuntimeTransitions> Runtime(Graph.mPasses.GetCount());
		for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
		{
			const FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
			auto& Out = Runtime[Handle.GetIndex()];
			const arda::EArdaRHIQueueType Queue = Pass.GetState().mQueue;
			for (const FArdaInductorTextureTransition& Compiled : Pass.GetState().mTextureTransitions)
			{
				const FArdaInductorTexture& Texture = Graph.mTextures.Get(Compiled.mTexture);
				const void* Physical = Texture.GetResource()->GetPhysicalIdentity();
				if (Physical == nullptr)
				{
					ARDA_CHECK_MSG("A live graph texture was not materialized.");
				}
				auto& States = TextureStates[Physical];
				const arda::FArdaRHITextureDesc& Desc = Texture.GetDesc();
				if (States.empty())
				{
					// A physical texture enters history once; later logical
					// aliases continue from the state left in this vector.
					States.resize(GetInductorTextureStateCount(Desc), NormalizeInitialState(Texture.GetInitialState()));
					auto& History = TextureQueues[Physical];
					History.mPasses.assign(States.size(), Graph.mPlan.mPrologue);
					History.mQueues.assign(States.size(), arda::EArdaRHIQueueType::Graphics);
					History.mTextures.assign(States.size(), Compiled.mTexture);
				}
				auto& History = TextureQueues[Physical];
				VisitInductorTextureCells(Desc,
				    Compiled.mSubresources,
				    [&](const auto& Cell, size_t Index)
				    {
					    if (History.mQueues[Index] != Queue)
					    {
						    AddExecutionDependency(Graph, History.mPasses[Index], Handle);
						    const auto Transfer =
						        FArdaInductorRuntimeTransitions::FArdaTextureQueueTransfer{History.mTextures[Index],
						            Cell,
						            History.mQueues[Index],
						            Queue};
						    Runtime[History.mPasses[Index].GetIndex()].mTextureReleases.push_back(Transfer);
						    Out.mTextureAcquires.push_back({Compiled.mTexture, Cell, History.mQueues[Index], Queue});
						    States[Index] = arda::EArdaRHIResourceState::Common;
					    }
					    Out.mTextures.push_back({Compiled.mTexture,
					        Cell,
					        States[Index],
					        Compiled.mStateAfter,
					        States[Index] == Compiled.mStateAfter && IsUAVState(Compiled.mStateAfter)});
					    States[Index] = Compiled.mStateAfter;
					    History.mPasses[Index] = Handle;
					    History.mQueues[Index] = Queue;
					    History.mTextures[Index] = Compiled.mTexture;
				    });
			}

			for (const FArdaInductorBufferTransition& Compiled : Pass.GetState().mBufferTransitions)
			{
				const FArdaInductorBuffer& Buffer = Graph.mBuffers.Get(Compiled.mBuffer);
				const void* Physical = Buffer.GetResource()->GetPhysicalIdentity();
				if (Physical == nullptr)
				{
					ARDA_CHECK_MSG("A live graph buffer was not materialized.");
				}
				auto Existing = BufferStates.find(Physical);
				if (Existing == BufferStates.end())
				{
					Existing = BufferStates.emplace(Physical, NormalizeInitialState(Buffer.GetInitialState())).first;
					BufferQueues.emplace(Physical,
					    FArdaBufferQueueHistory{Graph.mPlan.mPrologue,
					        arda::EArdaRHIQueueType::Graphics,
					        Compiled.mBuffer});
				}
				auto& History = BufferQueues.at(Physical);
				if (History.mQueue != Queue)
				{
					AddExecutionDependency(Graph, History.mPass, Handle);
					Runtime[History.mPass.GetIndex()].mBufferReleases.push_back(
					    {History.mBuffer, History.mQueue, Queue});
					Out.mBufferAcquires.push_back({Compiled.mBuffer, History.mQueue, Queue});
					Existing->second = arda::EArdaRHIResourceState::Common;
				}
				auto& Pending = PendingBufferAccesses[Physical];
				const bool SameUAV = Existing->second == Compiled.mStateAfter && IsUAVState(Compiled.mStateAfter);
				bool UAVBarrier = SameUAV && Pass.GetState().mbSentinel;
				if (SameUAV)
				{
					// An imported UAV's first access must order work preceding this graph.
					UAVBarrier |= History.mPass == Graph.mPlan.mPrologue;
					for (const auto& A : Pass.GetState().mBufferStates)
					{
						if (A.mBuffer != Compiled.mBuffer)
						{
							continue;
						}
						const auto X = A.mRange.Resolve(Buffer.GetDesc());
						for (const auto& B : Pending)
						{
							const auto Y = B.mRange.Resolve(Buffer.GetDesc());
							UAVBarrier |= (A.mbWrite || B.mbWrite) && X.mByteOffset < Y.mByteOffset + Y.mByteSize &&
							    Y.mByteOffset < X.mByteOffset + X.mByteSize;
						}
					}
				}
				if (!SameUAV || UAVBarrier)
				{
					Pending.clear();
				}
				for (const auto& A : Pass.GetState().mBufferStates)
				{
					if (A.mBuffer == Compiled.mBuffer)
					{
						Pending.push_back(A);
					}
				}
				Out.mBuffers.push_back({Compiled.mBuffer, Existing->second, Compiled.mStateAfter, UAVBarrier});
				Existing->second = Compiled.mStateAfter;
				History = {Handle, Queue, Compiled.mBuffer};
			}
			for (const FArdaInductorAccelerationStructureTransition& Compiled : Pass.GetState().mAccelStructTransitions)
			{
				Out.mAccelStructs.push_back(Compiled);
			}
		}

		return Runtime;
	}
}
