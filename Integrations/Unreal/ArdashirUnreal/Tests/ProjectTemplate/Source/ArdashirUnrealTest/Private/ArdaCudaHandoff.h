#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "ID3D12DynamicRHI.h"
#include "BackendImpl/ArdaUnrealBackend.h"

#include <cuda.h>

namespace arda
{
	/** A dedicated D3D12 allocation shared with a matched CUDA device; retire after GPU readback. */
	class FArdaCudaHandoff final : public IArdaCudaKernelEntry, public eastl::enable_shared_from_this<FArdaCudaHandoff>
	{
	public:
		FArdaCudaHandoff() = default;
		~FArdaCudaHandoff() override;
		FArdaCudaHandoff(const FArdaCudaHandoff&) = delete;
		FArdaCudaHandoff& operator=(const FArdaCudaHandoff&) = delete;
		bool Initialize(FIntPoint Extent, FString& OutError);
		FArdaRHIStatus Enqueue(FRHICommandListImmediate& RHICmdList, const eastl::vector<FArdaCudaKernel>& Kernels);

		FRHITexture* GetTexture() const
		{
			return mTexture;
		}

		FIntPoint GetExtent() const
		{
			return mExtent;
		}

		uint64 GetSurface() const
		{
			return mSurface;
		}

		const FArdaCudaCapabilities& GetCapabilities() const
		{
			return mCapabilities;
		}

		FArdaCudaKernelSignature GetSignature() const noexcept override;

		const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
		{
			return mBuildInfo;
		}

		TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override;
		FArdaRHIStatus Launch(void* Stream,
		    const FArdaCudaLaunchConfig& Configuration,
		    const void* Parameters,
		    size_t ParameterSize) const override;

	private:
		bool ImportFence(ID3D12Device* Device,
		    TRefCountPtr<ID3D12Fence>& OutFence,
		    CUexternalSemaphore& OutSemaphore,
		    FString& OutError);
		FIntPoint mExtent = FIntPoint::ZeroValue;
		FTextureRHIRef mTexture;
		TRefCountPtr<ID3D12Resource> mResource;
		TRefCountPtr<ID3D12Fence> mGraphicsFence;
		TRefCountPtr<ID3D12Fence> mCudaFence;
		CUdevice mDevice = 0;
		CUcontext mContext = nullptr;
		CUstream mStream = nullptr;
		CUmodule mModule = nullptr;
		CUfunction mKernel = nullptr;
		CUexternalMemory mMemory = nullptr;
		CUmipmappedArray mMipmappedArray = nullptr;
		CUsurfObject mSurface = 0;
		CUexternalSemaphore mGraphicsSemaphore = nullptr;
		CUexternalSemaphore mCudaSemaphore = nullptr;
		uint64 mFenceValue = 0;
		FArdaCudaCapabilities mCapabilities;
		FArdaCudaBuildInfo mBuildInfo;
	};

	/** Project-owned adapter for already shared surfaces; the provider executes its graph CUDA batches. */
	class FArdaUnrealTestCudaInterop final : public IArdaUnrealCudaInterop
	{
	public:
		bool Initialize(FString& OutError, bool bLegacyBarriers);
		void RegisterSurface(const eastl::shared_ptr<FArdaCudaHandoff>& Surface);
		FArdaCudaCapabilities GetCapabilities() const override;
		FArdaCudaResourceInfo DescribeTexture(FRHITexture* Texture) const override;
		FArdaRHIStatus Dispatch(FRHICommandListImmediate& CommandList,
		    const eastl::vector<FArdaUnrealCudaBinding>& Bindings,
		    const eastl::vector<FArdaCudaKernel>& Kernels) override;

	private:
		eastl::shared_ptr<FArdaCudaHandoff> mProbe;
		TMap<FRHITexture*, eastl::weak_ptr<FArdaCudaHandoff>> mSurfaces;
	};
}
