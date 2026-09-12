#pragma once
#include "ArdaInductorCommandProgram.h"
#include <mutex>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>

namespace arda
{
	/** Stable storage for materialized resources and emitted commands. */
	template <class T, class H>
	class TArdaInductorRecordStore
	{
	public:
		template <class... A>
		H Append(A&&... Args)
		{
			if (mEntries.size() >= UINT32_MAX)
			{
				ARDA_CHECK_MSG("Inductor native program exceeds its index domain.");
			}
			H Handle(static_cast<uint32_t>(mEntries.size()));
			auto Record = eastl::make_unique<T>(Handle, eastl::forward<A>(Args)...);
			mEntries.push_back(Record.get());
			mStorage.push_back(eastl::move(Record));
			return Handle;
		}

		T* TryGet(H Handle) const
		{
			return Handle && Handle.GetIndex() < mEntries.size() ? mEntries[Handle.GetIndex()] : nullptr;
		}

		T& Get(H Handle) const
		{
			auto* R = TryGet(Handle);
			if (!R)
			{
				ARDA_CHECK_MSG("Invalid Inductor native record index.");
			}
			return *R;
		}

		const eastl::vector<T*>& GetEntries() const
		{
			return mEntries;
		}

		size_t GetCount() const
		{
			return mEntries.size();
		}

	private:
		eastl::vector<eastl::unique_ptr<T>> mStorage;
		eastl::vector<T*> mEntries;
	};

	struct FArdaInductorCommandProgram::FImpl final
	{
		explicit FImpl(FArdaRHIDeviceRef Device)
		    : mDevice(eastl::move(Device))
		{
		}

		FArdaRHIDeviceRef mDevice;
		TArdaInductorRecordStore<FArdaInductorCommand, FArdaInductorCommandHandle> mPasses;
		TArdaInductorRecordStore<FArdaInductorTexture, FArdaInductorTextureHandle> mTextures;
		TArdaInductorRecordStore<FArdaInductorBuffer, FArdaInductorBufferHandle> mBuffers;
		TArdaInductorRecordStore<FArdaInductorAccelerationStructure, FArdaInductorAccelerationStructureHandle>
		    mAccelStructs;
		eastl::unordered_map<const void*, FArdaInductorTexture*> mImportedTextures;
		eastl::unordered_map<const void*, FArdaInductorBuffer*> mImportedBuffers;
		eastl::unordered_map<const void*, FArdaInductorAccelerationStructure*> mImportedAccelStructs;
		FArdaInductorCommandPlan mPlan;
		FArdaGraphExecutionResult mExecutionResult;

		struct FReusableAliasActivation
		{
			FArdaInductorCommandHandle mConsumer;
			EArdaGraphResourceType mType;
			uint32_t mResourceIndex;
		};

		eastl::vector<FReusableAliasActivation> mReusableAliasActivations;
		int32_t mSubmittingQueue = -1;
		std::mutex mPassAccessMutex;
		eastl::unordered_set<uint32_t> mActivePassAccess;
		bool mbCompiled = false, mbExecutionStarted = false, mbExecuted = false, mbFailed = false;
	};
}
