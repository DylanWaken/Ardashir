/** Texture, buffer, staging and view facade resources. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceImpl.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Resources/ArdaRHISamplerFeedback.h"
#include "RHI/Resources/ArdaRHIViews.h"
#include "RHI/Memory/ArdaRHIHeap.h"
#include <EASTL/vector.h>
#include <mutex>

namespace arda::detail
{
	using FArdaTextureBase =
	    TArdaNativeResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;

	class FArdaTexture final : public FArdaTextureBase
	{
	public:
		using FArdaTextureBase::FArdaTextureBase;
		mutable std::mutex mFacadeStateMutex;
		mutable eastl::vector<EArdaRHIResourceState> mFacadeStates;
		EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
		bool mbFacadeQueueOwnerKnown = true;
		FArdaRHIHeapRef mHeap;
		uint64_t mHeapOffset = 0;
	};

	using FArdaBufferBase = TArdaNativeResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;

	class FArdaBuffer final : public FArdaBufferBase
	{
	public:
		using FArdaBufferBase::FArdaBufferBase;
		mutable std::mutex mFacadeStateMutex;
		EArdaRHIResourceState mFacadeState = EArdaRHIResourceState::Unknown;
		bool mbFacadeStateKnown = false;
		EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
		bool mbFacadeQueueOwnerKnown = true;
		FArdaRHIHeapRef mHeap;
		uint64_t mHeapOffset = 0;
	};

	class FArdaSamplerFeedbackTexture final : public FArdaResource, public IArdaRHISamplerFeedbackTexture
	{
	public:
		FArdaSamplerFeedbackTexture(FArdaRHISamplerFeedbackTextureDesc Desc,
		    FArdaRHITextureRef PairedTexture,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::SamplerFeedbackTexture,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mPairedTexture(eastl::move(PairedTexture)),
		      mNative(eastl::move(Native))
		{
		}

		const FArdaRHISamplerFeedbackTextureDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const FArdaRHITextureRef& GetPairedTexture() const noexcept override
		{
			return mPairedTexture;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mNative ? mNative->GetIdentity() : nullptr;
		}

		FArdaRHISamplerFeedbackTextureDesc mDesc;
		FArdaRHITextureRef mPairedTexture;
		FArdaProviderObjectRef mNative;
		mutable std::mutex mStateMutex;
		EArdaRHIResourceState mFacadeState = EArdaRHIResourceState::Unknown;
		bool mbFacadeStateKnown = false;
	};

	class FArdaTextureReference final : public FArdaResource, public IArdaRHITextureReference
	{
	public:
		FArdaTextureReference(FArdaRHITextureRef Texture,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::TextureReference,
		          "TextureReference",
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mTexture(eastl::move(Texture))
		{
		}

		const FArdaRHITextureRef& GetTexture() const noexcept override
		{
			return mTexture;
		}

		FArdaRHITextureRef mTexture;
	};

	class FArdaUniformBuffer final : public FArdaResource, public IArdaRHIUniformBuffer
	{
	public:
		FArdaUniformBuffer(FArdaRHIUniformBufferDesc Desc,
		    FArdaRHIBufferRef Buffer,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::UniformBuffer,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mBuffer(eastl::move(Buffer))
		{
		}

		const FArdaRHIUniformBufferDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const FArdaRHIBufferRef& GetBuffer() const noexcept override
		{
			return mBuffer;
		}

		FArdaRHIUniformBufferDesc mDesc;
		FArdaRHIBufferRef mBuffer;
	};

	class FArdaStagingTexture final : public FArdaResource, public IArdaRHIStagingTexture
	{
	public:
		FArdaStagingTexture(FArdaRHIStagingTextureDesc Desc,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::StagingTexture,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mNative(eastl::move(Native))
		{
		}

		const FArdaRHIStagingTextureDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		FArdaRHIStagingTextureDesc mDesc;
		FArdaProviderObjectRef mNative;
	};

	template <typename Interface, EArdaRHIResourceType Type>
	class TArdaView final : public FArdaResource, public Interface
	{
	public:
		TArdaView(TArdaRHIRef<IArdaRHIResource> Resource,
		    FArdaRHIViewDesc Desc,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(Type,
		          Type == EArdaRHIResourceType::ShaderResourceView ? "SRV" : "UAV",
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mResource(eastl::move(Resource)),
		      mDesc(eastl::move(Desc))
		{
		}

		IArdaRHIResource* GetResource() const noexcept override
		{
			return mResource.Get();
		}

		const FArdaRHIViewDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		TArdaRHIRef<IArdaRHIResource> mResource;
		FArdaRHIViewDesc mDesc;
	};

	using FArdaShaderResourceView = TArdaView<IArdaRHIShaderResourceView, EArdaRHIResourceType::ShaderResourceView>;
	using FArdaUnorderedAccessView =
	    TArdaView<IArdaRHIUnorderedAccessView, EArdaRHIResourceType::UnorderedAccessView>;
}
