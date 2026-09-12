#pragma once
#include "ArdaDependencyGraphExecution.h"
#include "ArdaInductorLog.h"
#include <EASTL/algorithm.h>
#include <EASTL/functional.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>

namespace arda
{
	/** Typed indices into an immutable native command program. */
	template <class Tag>
	class TArdaInductorIndex
	{
	public:
		static constexpr uint32_t InvalidIndex = UINT32_MAX;
		TArdaInductorIndex() = default;

		explicit TArdaInductorIndex(uint32_t Index)
		    : mIndex(Index)
		{
		}

		uint32_t GetIndex() const
		{
			return mIndex;
		}

		bool IsValid() const
		{
			return mIndex != InvalidIndex;
		}

		explicit operator bool() const
		{
			return IsValid();
		}

		friend bool operator==(TArdaInductorIndex A, TArdaInductorIndex B)
		{
			return A.mIndex == B.mIndex;
		}

		friend bool operator!=(TArdaInductorIndex A, TArdaInductorIndex B)
		{
			return !(A == B);
		}

		friend bool operator<(TArdaInductorIndex A, TArdaInductorIndex B)
		{
			return A.mIndex < B.mIndex;
		}

	private:
		uint32_t mIndex = InvalidIndex;
	};
	struct FArdaInductorCommandTag;
	using FArdaInductorCommandHandle = TArdaInductorIndex<FArdaInductorCommandTag>;
	struct FArdaInductorTextureTag;
	using FArdaInductorTextureHandle = TArdaInductorIndex<FArdaInductorTextureTag>;
	struct FArdaInductorBufferTag;
	using FArdaInductorBufferHandle = TArdaInductorIndex<FArdaInductorBufferTag>;
	struct FArdaInductorAccelerationStructureTag;
	using FArdaInductorAccelerationStructureHandle = TArdaInductorIndex<FArdaInductorAccelerationStructureTag>;

	/** Pool objects are already materialized; this record stores only entry/exit state and diagnostics. */
	class FArdaInductorResourceState
	{
	public:
		FArdaInductorResourceState(eastl::string Name, EArdaRHIResourceState State)
		    : mName(eastl::move(Name)),
		      mState(State)
		{
		}

		const eastl::string& GetName() const
		{
			return mName;
		}

		EArdaRHIResourceState GetInitialState() const
		{
			return mState;
		}

		EArdaRHIResourceState GetFinalState() const
		{
			return mState;
		}

		FArdaInductorCommandHandle GetFirstUse() const
		{
			return mFirstUse;
		}

		void MarkUsed(FArdaInductorCommandHandle Command)
		{
			if (!mFirstUse)
			{
				mFirstUse = Command;
			}
		}

	private:
		eastl::string mName;
		EArdaRHIResourceState mState;
		FArdaInductorCommandHandle mFirstUse;
	};

	class FArdaInductorTexture final : public FArdaInductorResourceState
	{
	public:
		FArdaInductorTexture(FArdaInductorTextureHandle Handle,
		    FArdaRHITextureRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name)
		    : FArdaInductorResourceState(eastl::move(Name), EntryState),
		      mHandle(Handle),
		      mResource(eastl::move(Resource))
		{
		}

		FArdaInductorTextureHandle GetHandle() const
		{
			return mHandle;
		}

		const FArdaRHITextureDesc& GetDesc() const
		{
			return mResource->GetDesc();
		}

		const FArdaRHITextureRef& GetTexture() const
		{
			return mResource;
		}

	private:
		FArdaInductorTextureHandle mHandle;
		FArdaRHITextureRef mResource;
	};

	class FArdaInductorBuffer final : public FArdaInductorResourceState
	{
	public:
		FArdaInductorBuffer(FArdaInductorBufferHandle Handle,
		    FArdaRHIBufferRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name)
		    : FArdaInductorResourceState(eastl::move(Name), EntryState),
		      mHandle(Handle),
		      mResource(eastl::move(Resource))
		{
		}

		FArdaInductorBufferHandle GetHandle() const
		{
			return mHandle;
		}

		const FArdaRHIBufferDesc& GetDesc() const
		{
			return mResource->GetDesc();
		}

		const FArdaRHIBufferRef& GetBuffer() const
		{
			return mResource;
		}

	private:
		FArdaInductorBufferHandle mHandle;
		FArdaRHIBufferRef mResource;
	};

	class FArdaInductorAccelerationStructure final : public FArdaInductorResourceState
	{
	public:
		FArdaInductorAccelerationStructure(FArdaInductorAccelerationStructureHandle Handle,
		    FArdaRHIAccelStructRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name)
		    : FArdaInductorResourceState(eastl::move(Name), EntryState),
		      mHandle(Handle),
		      mResource(eastl::move(Resource))
		{
		}

		FArdaInductorAccelerationStructureHandle GetHandle() const
		{
			return mHandle;
		}

		const FArdaRHIAccelStructDesc& GetDesc() const
		{
			return mResource->GetDesc();
		}

		const FArdaRHIAccelStructRef& GetAccelStruct() const
		{
			return mResource;
		}

	private:
		FArdaInductorAccelerationStructureHandle mHandle;
		FArdaRHIAccelStructRef mResource;
	};

	/** Records one texture-state requirement contributed by a pass. */
	struct FArdaInductorTextureAccess
	{
		/** The logical texture whose state is required. */
		FArdaInductorTextureHandle mTexture;

		/** The affected texture subresources. */
		arda::FArdaRHITextureSubresourceRange mSubresources;

		/** The RHI state required while the pass executes. */
		arda::EArdaRHIResourceState mState = arda::EArdaRHIResourceState::Unknown;

		/** Whether the state permits the pass to modify the resource. */
		bool mbWrite = false;
	};

	/** Records one buffer-state requirement contributed by a pass. */
	struct FArdaInductorBufferAccess
	{
		/** The logical buffer whose state is required. */
		FArdaInductorBufferHandle mBuffer;

		/** The affected byte range. */
		arda::FArdaRHIBufferRange mRange;

		/** The RHI state required while the pass executes. */
		arda::EArdaRHIResourceState mState = arda::EArdaRHIResourceState::Unknown;

		/** Whether the state permits the pass to modify the resource. */
		bool mbWrite = false;
	};

	struct FArdaInductorAccelerationStructureAccess
	{
		FArdaInductorAccelerationStructureHandle mAccelStruct;
		arda::EArdaRHIResourceState mState = arda::EArdaRHIResourceState::Unknown;
		bool mbWrite = false;
	};

	/** Describes one compiled texture transition before a pass executes. */
	struct FArdaInductorTextureTransition
	{
		/** The logical texture being transitioned. */
		FArdaInductorTextureHandle mTexture;

		/** The texture subresources covered by the transition. */
		arda::FArdaRHITextureSubresourceRange mSubresources;

		/** The state known before the transition. */
		arda::EArdaRHIResourceState mStateBefore = arda::EArdaRHIResourceState::Unknown;

		/** The state required after the transition. */
		arda::EArdaRHIResourceState mStateAfter = arda::EArdaRHIResourceState::Unknown;

		/** Whether equal UAV states still require an ordering barrier. */
		bool mbUAVBarrier = false;
	};

	/** Describes one compiled whole-buffer transition before a pass executes. */
	struct FArdaInductorBufferTransition
	{
		/** The logical buffer being transitioned. */
		FArdaInductorBufferHandle mBuffer;

		/** The state known before the transition. */
		arda::EArdaRHIResourceState mStateBefore = arda::EArdaRHIResourceState::Unknown;

		/** The state required after the transition. */
		arda::EArdaRHIResourceState mStateAfter = arda::EArdaRHIResourceState::Unknown;

		/** Whether equal UAV states still require an ordering barrier. */
		bool mbUAVBarrier = false;
	};

	struct FArdaInductorAccelerationStructureTransition
	{
		FArdaInductorAccelerationStructureHandle mAccelStruct;
		arda::EArdaRHIResourceState mStateBefore = arda::EArdaRHIResourceState::Unknown;
		arda::EArdaRHIResourceState mStateAfter = arda::EArdaRHIResourceState::Unknown;
	};

	struct FArdaInductorCommandState
	{
		eastl::vector<FArdaInductorCommandHandle> mProducers;
		eastl::vector<FArdaInductorTextureAccess> mTextureStates;
		eastl::vector<FArdaInductorBufferAccess> mBufferStates;
		eastl::vector<FArdaInductorAccelerationStructureAccess> mAccelStructStates;
		eastl::vector<FArdaInductorTextureTransition> mTextureTransitions;
		eastl::vector<FArdaInductorBufferTransition> mBufferTransitions;
		eastl::vector<FArdaInductorAccelerationStructureTransition> mAccelStructTransitions;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		bool mbSentinel = false;
	};

	struct FArdaInductorCommandAccesses
	{
		eastl::vector<FArdaInductorTextureAccess> mTextures;
		eastl::vector<FArdaInductorBufferAccess> mBuffers;
		eastl::vector<FArdaInductorAccelerationStructureAccess> mAccelerationStructures;
	};
	class FArdaInductorCommandProgram;

	class FArdaInductorPassContext final
	{
	public:
		FArdaInductorPassContext(FArdaInductorCommandProgram& Program,
		    FArdaInductorCommandHandle Command,
		    IArdaRHICommandList& Commands,
		    EArdaRHIQueueType Queue);
		~FArdaInductorPassContext() noexcept;
		FArdaInductorPassContext(const FArdaInductorPassContext&) = delete;
		FArdaInductorPassContext& operator=(const FArdaInductorPassContext&) = delete;

		FArdaInductorCommandHandle GetPass() const
		{
			return mPass;
		}

		IArdaRHITexture* GetTexture(FArdaInductorTexture* Texture) const;
		IArdaRHIBuffer* GetBuffer(FArdaInductorBuffer* Buffer) const;
		IArdaRHIAccelStruct* GetAccelStruct(FArdaInductorAccelerationStructure* Structure) const;

		void ReportStatus(FArdaRHIStatus Status)
		{
			if (mStatus && !Status)
			{
				mStatus = eastl::move(Status);
			}
		}

		const FArdaRHIStatus& GetStatus() const
		{
			return mStatus;
		}

		IArdaRHICommandList& mUnsafeRawCommandList;
		EArdaRHIQueueType mQueue;

	private:
		FArdaRHIStatus mStatus;
		FArdaInductorCommandProgram& mGraph;
		FArdaInductorCommandHandle mPass;
	};

	using FArdaInductorRecordFunction = eastl::function<void(FArdaInductorPassContext&)>;

	class FArdaInductorCommand final
	{
	public:
		FArdaInductorCommand(FArdaInductorCommandHandle Handle,
		    eastl::string Name,
		    EArdaRHIQueueType Queue,
		    FArdaInductorRecordFunction Record = {},
		    bool Serial = false,
		    bool AtSubmit = false)
		    : mHandle(Handle),
		      mName(eastl::move(Name)),
		      mRecord(eastl::move(Record)),
		      mbSerialRecord(Serial),
		      mbRecordAtSubmit(AtSubmit)
		{
			mState.mQueue = Queue;
		}

		FArdaInductorCommandHandle GetHandle() const
		{
			return mHandle;
		}

		const eastl::string& GetName() const
		{
			return mName;
		}

		FArdaInductorCommandState& GetState()
		{
			return mState;
		}

		const FArdaInductorCommandState& GetState() const
		{
			return mState;
		}

		void Execute(FArdaInductorPassContext& Context)
		{
			if (mRecord)
			{
				mRecord(Context);
			}
		}

		bool mbSerialRecord = false;
		bool mbRecordAtSubmit = false;

	private:
		FArdaInductorCommandHandle mHandle;
		eastl::string mName;
		FArdaInductorRecordFunction mRecord;
		FArdaInductorCommandState mState;
	};

	struct FArdaInductorCommandPlan
	{
		FArdaInductorCommandHandle mPrologue, mEpilogue;
		eastl::vector<FArdaInductorCommandHandle> mExecutionOrder;
		eastl::vector<FArdaGraphQueueDependency> mQueueDependencies;
	};

	/** Internal output of ArdaInductor: no authoring, resource allocation, culling or schedule selection. */
	class FArdaInductorCommandProgram final
	{
	public:
		struct FImpl;
		explicit FArdaInductorCommandProgram(FArdaRHIDeviceRef Device);
		~FArdaInductorCommandProgram();
		FArdaInductorTexture* BindTexture(FArdaRHITextureRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name);
		FArdaInductorBuffer* BindBuffer(FArdaRHIBufferRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name);
		FArdaInductorAccelerationStructure* BindAccelerationStructure(FArdaRHIAccelStructRef Resource,
		    EArdaRHIResourceState EntryState,
		    eastl::string Name);
		FArdaInductorCommandHandle AppendCommand(eastl::string Name,
		    EArdaRHIQueueType Queue,
		    FArdaInductorCommandAccesses Accesses,
		    FArdaInductorRecordFunction Record,
		    bool Serial = false,
		    bool AtSubmit = false);
		void AddDependency(FArdaInductorCommandHandle Producer, FArdaInductorCommandHandle Consumer);
		const FArdaInductorCommandPlan& Finalize();
		/** Read-only native lowering evidence for diagnostics and CPU verification. */
		const FArdaInductorCommand& GetCommand(FArdaInductorCommandHandle Command) const;

	private:
		friend struct FArdaInductorRuntime;
		friend class FArdaInductorCommandExecutor;
		friend class FArdaInductorPassContext;
		void BeginPassAccess(FArdaInductorCommandHandle Command);
		void EndPassAccess(FArdaInductorCommandHandle Command) noexcept;
		IArdaRHITexture* ResolveTextureForPass(FArdaInductorCommandHandle Command, FArdaInductorTexture* Texture) const;
		IArdaRHIBuffer* ResolveBufferForPass(FArdaInductorCommandHandle Command, FArdaInductorBuffer* Buffer) const;
		IArdaRHIAccelStruct* ResolveAccelStructForPass(FArdaInductorCommandHandle Command,
		    FArdaInductorAccelerationStructure* Structure) const;
		eastl::unique_ptr<FImpl> mImpl;
	};
}
