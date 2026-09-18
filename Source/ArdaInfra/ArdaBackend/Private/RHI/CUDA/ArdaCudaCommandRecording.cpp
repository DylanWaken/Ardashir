#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaRHIStatus FArdaCommandList::DispatchCuda(const FArdaCudaDispatch& Dispatch)
	{
		if (auto Status = ValidateArdaCudaKernels(Dispatch.mKernels,
		        Dispatch.mBindings.size(),
		        mDevice->GetCudaCapabilities());
		    !Status)
		{
			return Status;
		}
		return RecordCudaBatch(Dispatch.mBindings, Dispatch.mKernels);
	}

	FArdaRHIStatus FArdaCommandList::DispatchCudaSequence(const eastl::vector<FArdaCudaDispatch>& Dispatches)
	{
		eastl::vector<FArdaCudaBinding> Bindings;
		eastl::vector<FArdaCudaKernel> Kernels;
		const auto Capabilities = mDevice->GetCudaCapabilities();
		for (const auto& Dispatch : Dispatches)
		{
			if (auto Status = ValidateArdaCudaKernels(Dispatch.mKernels, Dispatch.mBindings.size(), Capabilities);
			    !Status)
			{
				return Status;
			}
			eastl::vector<uint32_t> Indices;
			for (const auto& Binding : Dispatch.mBindings)
			{
				auto Found = eastl::find_if(Bindings.begin(),
				    Bindings.end(),
				    [&](const auto& Existing)
				    {
					    return Existing.mResource == Binding.mResource && Existing.mMipLevel == Binding.mMipLevel &&
					        Existing.mBufferRange.mByteOffset == Binding.mBufferRange.mByteOffset &&
					        Existing.mBufferRange.mByteSize == Binding.mBufferRange.mByteSize;
				    });
				if (Binding.mAccess > EArdaComputeAccess::ReadWrite)
				{
					return Invalid("Invalid CUDA resource access.");
				}
				if (Found == Bindings.end())
				{
					if (Bindings.size() >= UINT32_MAX)
					{
						return Invalid("CUDA sequence has too many resource views.");
					}
					Indices.push_back(static_cast<uint32_t>(Bindings.size()));
					Bindings.push_back(Binding);
				}
				else
				{
					Indices.push_back(static_cast<uint32_t>(Found - Bindings.begin()));
					if (Found->mAccess != Binding.mAccess)
					{
						Found->mAccess = EArdaComputeAccess::ReadWrite;
					}
				}
			}
			auto Kernel = Dispatch.mKernels.front();
			for (auto& Patch : Kernel.mPatches)
			{
				Patch.mBindingIndex = Indices[Patch.mBindingIndex];
			}
			Kernels.push_back(eastl::move(Kernel));
		}
		return RecordCudaBatch(Bindings, Kernels);
	}

	FArdaRHIStatus FArdaCommandList::RecordCudaBatch(const eastl::vector<FArdaCudaBinding>& Resources,
	    const eastl::vector<FArdaCudaKernel>& Kernels)
	{
		if (mQueue == EArdaRHIQueueType::Copy)
		{
			return Invalid("CUDA kernels cannot be recorded on a copy command list.");
		}
		if (!mNative->IsOpen())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "CUDA requires an open command list.");
		}
		if (mDevice->GetCudaCapabilities().mLaunchMode == EArdaCudaLaunchMode::D3D12CiG &&
		    mQueue != EArdaRHIQueueType::Graphics)
		{
			return Unsupported("D3D12 CiG requires its graphics context queue.");
		}
		if (!mDevice->GetCudaCapabilities())
		{
			return Unsupported("CUDA recording is unavailable.");
		}
		if (Kernels.empty())
		{
			return {};
		}
		eastl::vector<FArdaProviderCudaBinding> Bindings;
		for (const auto& B : Resources)
		{
			if (!B.mResource)
			{
				return Invalid("CUDA binding has no resource.");
			}
			if (B.mAccess > EArdaComputeAccess::ReadWrite)
			{
				return Invalid("Invalid CUDA resource access.");
			}
			if (!B.mResource->GetCudaResourceInfo().mbSharingEnabled)
			{
				return Unsupported(
				    "Resource was not created for CUDA access; acceleration structures have no generic CUDA representation.");
			}
			FArdaProviderCudaBinding Native;
			Native.mAccess = B.mAccess;
			if (auto* Buffer = Cast<FArdaBuffer>(B.mResource.Get()))
			{
				if (!RetainOwned(Buffer))
				{
					return WrongDevice();
				}
				const auto& R = B.mBufferRange;
				if (R.mByteOffset >= Buffer->mDesc.mByteSize ||
				    (R.mByteSize != ArdaRHIWholeBuffer &&
				        (!R.mByteSize || R.mByteSize > Buffer->mDesc.mByteSize - R.mByteOffset)))
				{
					return Invalid("CUDA buffer range is out of bounds.");
				}
				Native.mObject = Buffer->mNative;
				Native.mBufferRange = R.Resolve(Buffer->mDesc);
			}
			else if (auto* Texture = Cast<FArdaTexture>(B.mResource.Get()))
			{
				if (!RetainOwned(Texture))
				{
					return WrongDevice();
				}
				if (B.mMipLevel >= Texture->mDesc.mMipLevels)
				{
					return Invalid("CUDA texture mip is out of bounds.");
				}
				Native.mType = EArdaComputeBindingType::Surface;
				Native.mObject = Texture->mNative;
				Native.mMipLevel = B.mMipLevel;
			}
			else
			{
				return Unsupported("CUDA accepts buffer or surface bindings only.");
			}
			Bindings.push_back(eastl::move(Native));
		}

		// Both launch modes participate in ordinary graphics state tracking. Native
		// recording emits the CUDA-specific barriers in addition to these transitions.
		for (const auto& B : Resources)
		{
			FArdaRHIStatus Status;
			if (auto* Buffer = Cast<FArdaBuffer>(B.mResource.Get()))
			{
				Status = SetBufferState(*Buffer, EArdaRHIResourceState::UnorderedAccess);
			}
			else
			{
				FArdaRHITextureSubresourceRange Range;
				Range.mBaseMipLevel = B.mMipLevel;
				Range.mMipLevelCount = 1;
				Status = SetTextureState(*Cast<FArdaTexture>(B.mResource.Get()),
				    Range,
				    EArdaRHIResourceState::UnorderedAccess);
			}
			if (!Status)
			{
				return Status;
			}
		}
		if (Kernels.front().mGraphBatch)
		{
			auto Batch = eastl::make_shared<FArdaCudaGraphBatch>();
			Batch->mCache = Kernels.front().mGraphBatch->mCache;
			Batch->mTimingQuery = Kernels.front().mGraphBatch->mTimingQuery;
			Batch->mTimingRegions = Kernels.front().mGraphBatch->mTimingRegions;
			for (const auto& Resource : Resources)
			{
				Batch->mResources.push_back(Resource.mResource);
			}
			auto NativeKernels = Kernels;
			for (auto& Kernel : NativeKernels)
			{
				if (Kernel.mGraphBatch != Kernels.front().mGraphBatch)
				{
					return Invalid("CUDA Graph metadata must identify one common sequence batch.");
				}
				Kernel.mGraphBatch = Batch;
			}
			return mNative->DispatchCuda(Bindings, NativeKernels);
		}
		return mNative->DispatchCuda(Bindings, Kernels);
	}
}
