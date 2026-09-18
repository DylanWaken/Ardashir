/** Shader, binding, bindless table and shader-table facade resources. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceImpl.h"
#include "RHI/Pipelines/ArdaRHIRayTracingPipeline.h"
#include "RHI/Shaders/ArdaShaderIdentity.h"
#include "RHI/Shaders/ArdaRHIShaderTable.h"
#include "RHI/Shaders/ArdaRHIShaderBundle.h"
#include "RHI/Shaders/ArdaRHIDescriptorTable.h"
#include "RHI/Shaders/ArdaRHIInputLayout.h"
#include <EASTL/algorithm.h>
#include <mutex>
#include <EASTL/optional.h>

namespace arda::detail
{
	using FArdaBindingLayoutBase =
	    TArdaNativeResource<IArdaRHIBindingLayout, FArdaRHIBindingLayoutDesc, EArdaRHIResourceType::BindingLayout>;

	class FArdaBindingLayout final : public FArdaBindingLayoutBase
	{
	public:
		using FArdaBindingLayoutBase::FArdaBindingLayoutBase;

		const FArdaRHIBindlessLayoutDesc* GetBindlessDesc() const noexcept override
		{
			return mbBindless ? &mBindlessDesc : nullptr;
		}

		bool mbBindless = false;
		FArdaRHIBindlessLayoutDesc mBindlessDesc;
	};

	using FArdaBindingSet =
	    TArdaNativeResource<IArdaRHIBindingSet, FArdaRHIBindingSetDesc, EArdaRHIResourceType::BindingSet>;

	class FArdaDescriptorTable final : public FArdaResource, public IArdaRHIDescriptorTable
	{
	public:
		FArdaDescriptorTable(FArdaRHIBindingSetDesc Desc,
		    FArdaProviderObjectRef Native,
		    uint32_t Capacity,
		    uint32_t MaxCapacity,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::DescriptorTable,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mNative(eastl::move(Native)),
		      mCapacity(Capacity),
		      mMaxCapacity(MaxCapacity)
		{
		}

		const FArdaRHIBindingSetDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		uint32_t GetCapacity() const noexcept override
		{
			return mCapacity;
		}

		uint32_t GetFirstDescriptorIndexInHeap() const noexcept override
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			return mNative ? mNative->GetDescriptorBaseIndex() : 0u;
		}

		FArdaRHIBindingSetDesc mDesc;
		FArdaProviderObjectRef mNative;
		uint32_t mCapacity = 0;
		uint32_t mMaxCapacity = 0;
		mutable std::mutex mMutex;
	};

	inline FArdaProviderObjectRef CaptureNativeBindings(const FArdaResource* Resource)
	{
		if (const auto* Set = dynamic_cast<const FArdaBindingSet*>(Resource))
		{
			return Set->mNative;
		}
		if (const auto* Table = dynamic_cast<const FArdaDescriptorTable*>(Resource))
		{
			std::lock_guard<std::mutex> Lock(Table->mMutex);
			return Table->mNative;
		}
		return {};
	}

	class FArdaShaderTable final : public FArdaResource, public IArdaRHIShaderTable
	{
	public:
		FArdaShaderTable(FArdaRHIShaderTableDesc Desc,
		    FArdaRHIRayTracingPipelineRef Pipeline,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::ShaderTable,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mPipeline(eastl::move(Pipeline)),
		      mNative(eastl::move(Native)),
		      mRecordTypes(mDesc.mMaxEntries)
		{
		}

		const FArdaRHIShaderTableDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		uint32_t GetEntryCount() const noexcept override
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			return CountEntries();
		}

		uint32_t CountEntries() const noexcept
		{
			return static_cast<uint32_t>(eastl::count_if(mRecordTypes.begin(),
			    mRecordTypes.end(),
			    [](const auto& Type)
			    {
				    return Type.has_value();
			    }));
		}

		bool HasRayGeneration() const noexcept
		{
			return eastl::find(mRecordTypes.begin(),
			           mRecordTypes.end(),
			           EArdaRHIShaderTableRecordType::RayGeneration) != mRecordTypes.end();
		}

		FArdaRHIShaderTableDesc mDesc;
		FArdaRHIRayTracingPipelineRef mPipeline;
		FArdaProviderObjectRef mNative;
		mutable std::mutex mMutex;
		eastl::vector<eastl::optional<EArdaRHIShaderTableRecordType>> mRecordTypes;
	};

	class FArdaShaderBundle final : public FArdaResource, public IArdaRHIShaderBundle
	{
	public:
		FArdaShaderBundle(FArdaRHIShaderBundleDesc Desc,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::ShaderBundle,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc))
		{
		}

		const FArdaRHIShaderBundleDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		uint32_t GetRecordCount() const noexcept override
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			return static_cast<uint32_t>(mRecords.size());
		}

		FArdaRHIShaderBundleDesc mDesc;
		eastl::vector<FArdaRHIShaderBundleRecord> mRecords;
		mutable std::mutex mMutex;
	};

	class FArdaShader final : public FArdaResource, public IArdaRHIShader
	{
	public:
		FArdaShader(const FArdaRHIShaderDesc& Desc,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::Shader, Desc.mDebugName, Owner, eastl::move(LifetimeTracker)),
		      mStage(Desc.mStage),
		      mEntryPoint(Desc.mEntryPoint),
		      mPersistentCacheHash(PersistentShaderHash(Desc)),
		      mNative(eastl::move(Native))
		{
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return mPersistentCacheHash;
		}

		EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
		eastl::string mEntryPoint;
		uint64_t mPersistentCacheHash = 0;
		FArdaProviderObjectRef mNative;
	};

	class FArdaShaderLibrary final : public FArdaResource, public IArdaRHIShaderLibrary
	{
	public:
		FArdaShaderLibrary(const void* Bytecode,
		    size_t Size,
		    const char* Name,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::ShaderLibrary,
		          Name ? Name : "",
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mBytecode(static_cast<const uint8_t*>(Bytecode), static_cast<const uint8_t*>(Bytecode) + Size)
		{
		}

		eastl::vector<uint8_t> mBytecode;
	};

	class FArdaInputLayout final : public FArdaResource, public IArdaRHIInputLayout
	{
	public:
		FArdaInputLayout(FArdaRHIInputLayoutDesc Desc,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::InputLayout, "InputLayout", Owner, eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc))
		{
		}

		const FArdaRHIInputLayoutDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		FArdaRHIInputLayoutDesc mDesc;
	};
}
