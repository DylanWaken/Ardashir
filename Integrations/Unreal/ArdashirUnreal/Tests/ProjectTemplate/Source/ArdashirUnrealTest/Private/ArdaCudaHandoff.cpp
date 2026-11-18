#include "ArdaCudaHandoff.h"
#include "ArdaUnrealGraphNodes.h"

#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "Windows/WindowsHWrapper.h"

namespace arda
{
	namespace
	{
		bool CheckCuda(CUresult Result, const TCHAR* Operation, FString& OutError)
		{
			if (Result == CUDA_SUCCESS)
			{
				return true;
			}
			const char* Description = nullptr;
			cuGetErrorString(Result, &Description);
			OutError = FString::Printf(TEXT("%s failed: %s (%d)"),
			    Operation,
			    UTF8_TO_TCHAR(Description ? Description : "unknown CUDA error"),
			    int32(Result));
			return false;
		}

		class FArdaCudaContextScope final
		{
		public:
			explicit FArdaCudaContextScope(CUcontext Context)
			{
				const CUresult Result = cuCtxPushCurrent(Context);
				checkf(Result == CUDA_SUCCESS, TEXT("Cannot activate matched CUDA context: %d"), int32(Result));
			}

			~FArdaCudaContextScope()
			{
				CUcontext Previous = nullptr;
				cuCtxPopCurrent(&Previous);
			}
		};
	}

	FArdaCudaHandoff::~FArdaCudaHandoff()
	{
		if (mContext)
		{
			{
				FArdaCudaContextScope ContextScope(mContext);
				// The owner has retired its UE readback, or explicitly drained the GPU at shutdown.
				if (mSurface)
				{
					cuSurfObjectDestroy(mSurface);
				}
				if (mMipmappedArray)
				{
					cuMipmappedArrayDestroy(mMipmappedArray);
				}
				if (mMemory)
				{
					cuDestroyExternalMemory(mMemory);
				}
				if (mGraphicsSemaphore)
				{
					cuDestroyExternalSemaphore(mGraphicsSemaphore);
				}
				if (mCudaSemaphore)
				{
					cuDestroyExternalSemaphore(mCudaSemaphore);
				}
				if (mModule)
				{
					cuModuleUnload(mModule);
				}
				if (mStream)
				{
					cuStreamDestroy(mStream);
				}
			}
			cuDevicePrimaryCtxRelease(mDevice);
		}
	}

	bool FArdaCudaHandoff::ImportFence(ID3D12Device* Device,
	    TRefCountPtr<ID3D12Fence>& OutFence,
	    CUexternalSemaphore& OutSemaphore,
	    FString& OutError)
	{
		HRESULT Result = Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(OutFence.GetInitReference()));
		HANDLE Handle = nullptr;
		if (SUCCEEDED(Result))
		{
			Result = Device->CreateSharedHandle(OutFence, nullptr, GENERIC_ALL, nullptr, &Handle);
		}
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("Create shared D3D12 fence failed: 0x%08x"), uint32(Result));
			return false;
		}
		CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC Description{};
		Description.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
		Description.handle.win32.handle = Handle;
		const CUresult ImportResult = cuImportExternalSemaphore(&OutSemaphore, &Description);
		CloseHandle(Handle);
		return CheckCuda(ImportResult, TEXT("cuImportExternalSemaphore"), OutError);
	}

	bool FArdaCudaHandoff::Initialize(FIntPoint Extent, FString& OutError)
	{
		if (!IsRHID3D12() || GNumExplicitGPUsForRendering != 1)
		{
			OutError = TEXT("CUDA handoff requires D3D12 on one explicit GPU.");
			return false;
		}
		mExtent = Extent;
		ID3D12Device* Device = GetID3D12DynamicRHI()->RHIGetDevice(0);
		const LUID AdapterLuid = Device->GetAdapterLuid();
		int DeviceCount = 0;
		if (!CheckCuda(cuInit(0), TEXT("cuInit"), OutError) ||
		    !CheckCuda(cuDeviceGetCount(&DeviceCount), TEXT("cuDeviceGetCount"), OutError))
		{
			return false;
		}
		bool bFound = false;
		for (int Index = 0; Index < DeviceCount; ++Index)
		{
			CUdevice Candidate = 0;
			char CudaLuid[8]{};
			unsigned int NodeMask = 0;
			if (cuDeviceGet(&Candidate, Index) == CUDA_SUCCESS &&
			    cuDeviceGetLuid(CudaLuid, &NodeMask, Candidate) == CUDA_SUCCESS &&
			    FMemory::Memcmp(CudaLuid, &AdapterLuid, sizeof(AdapterLuid)) == 0 &&
			    (NodeMask & GetID3D12DynamicRHI()->RHIGetDeviceNodeMask(0)) != 0)
			{
				mDevice = Candidate;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			OutError = TEXT("No CUDA device matches Unreal's D3D12 adapter LUID/node mask.");
			return false;
		}
		if (!CheckCuda(cuDevicePrimaryCtxRetain(&mContext, mDevice), TEXT("cuDevicePrimaryCtxRetain"), OutError))
		{
			return false;
		}
		FArdaCudaContextScope ContextScope(mContext);
		const FString KernelPath =
		    FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ArdaCuda/ArdaPostProcess.cubin"));
		if (!CheckCuda(cuStreamCreate(&mStream, CU_STREAM_NON_BLOCKING), TEXT("cuStreamCreate"), OutError) ||
		    !CheckCuda(cuModuleLoad(&mModule, TCHAR_TO_UTF8(*KernelPath)), TEXT("cuModuleLoad"), OutError) ||
		    !CheckCuda(cuModuleGetFunction(&mKernel, mModule, "ArdaPostProcess"),
		        TEXT("cuModuleGetFunction"),
		        OutError))
		{
			return false;
		}
		auto ReadAttribute = [this](CUdevice_attribute Attribute)
		{
			int Value = 0;
			cuDeviceGetAttribute(&Value, Attribute, mDevice);
			return uint32(Value);
		};
		mCapabilities.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		mCapabilities.mComputeCapability = ReadAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR) * 10 +
		    ReadAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
		mCapabilities.mMaxThreadsPerBlock = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
		mCapabilities.mMaxBlockSize[0] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X);
		mCapabilities.mMaxBlockSize[1] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y);
		mCapabilities.mMaxBlockSize[2] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z);
		mCapabilities.mMaxGridSize[0] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X);
		mCapabilities.mMaxGridSize[1] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y);
		mCapabilities.mMaxGridSize[2] = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z);
		mCapabilities.mMaxSharedMemoryBytes = ReadAttribute(CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK);
		mCapabilities.mFallbackReason =
		    "Unreal RHI has no CUDA-in-Graphics context/capture interface; explicit GPU fence handoff is required.";
		mCapabilities.mUnavailableReason.clear();
		mBuildInfo.mName = "ArdaUnrealTest.CUDA";
		mBuildInfo.mIdentity = "ArdaPostProcess.cubin";
		mBuildInfo.mArchitectures.push_back({ARDA_TEST_CUDA_ARCH, false});
		D3D12_HEAP_PROPERTIES Heap{};
		Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		Heap.CreationNodeMask = GetID3D12DynamicRHI()->RHIGetDeviceNodeMask(0);
		Heap.VisibleNodeMask = Heap.CreationNodeMask;
		D3D12_RESOURCE_DESC Description{};
		Description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		Description.Width = Extent.X;
		Description.Height = Extent.Y;
		Description.DepthOrArraySize = 1;
		Description.MipLevels = 1;
		Description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		Description.SampleDesc.Count = 1;
		Description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		Description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		HRESULT Result = Device->CreateCommittedResource(&Heap,
		    D3D12_HEAP_FLAG_SHARED,
		    &Description,
		    D3D12_RESOURCE_STATE_COMMON,
		    nullptr,
		    IID_PPV_ARGS(mResource.GetInitReference()));
		HANDLE Handle = nullptr;
		if (SUCCEEDED(Result))
		{
			Result = Device->CreateSharedHandle(mResource, nullptr, GENERIC_ALL, nullptr, &Handle);
		}
		if (FAILED(Result))
		{
			OutError = FString::Printf(TEXT("Create shared D3D12 texture failed: 0x%08x"), uint32(Result));
			return false;
		}
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC MemoryDescription{};
		MemoryDescription.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
		MemoryDescription.handle.win32.handle = Handle;
		MemoryDescription.size = Device->GetResourceAllocationInfo(Heap.VisibleNodeMask, 1, &Description).SizeInBytes;
		MemoryDescription.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
		const CUresult ImportResult = cuImportExternalMemory(&mMemory, &MemoryDescription);
		CloseHandle(Handle);
		if (!CheckCuda(ImportResult, TEXT("cuImportExternalMemory"), OutError))
		{
			return false;
		}
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC ArrayDescription{};
		ArrayDescription.arrayDesc.Width = Extent.X;
		ArrayDescription.arrayDesc.Height = Extent.Y;
		ArrayDescription.arrayDesc.Format = CU_AD_FORMAT_FLOAT;
		ArrayDescription.arrayDesc.NumChannels = 4;
		ArrayDescription.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST;
		ArrayDescription.numLevels = 1;
		CUarray Array = nullptr;
		if (!CheckCuda(cuExternalMemoryGetMappedMipmappedArray(&mMipmappedArray, mMemory, &ArrayDescription),
		        TEXT("cuExternalMemoryGetMappedMipmappedArray"),
		        OutError) ||
		    !CheckCuda(cuMipmappedArrayGetLevel(&Array, mMipmappedArray, 0),
		        TEXT("cuMipmappedArrayGetLevel"),
		        OutError))
		{
			return false;
		}
		CUDA_RESOURCE_DESC SurfaceDescription{};
		SurfaceDescription.resType = CU_RESOURCE_TYPE_ARRAY;
		SurfaceDescription.res.array.hArray = Array;
		if (!CheckCuda(cuSurfObjectCreate(&mSurface, &SurfaceDescription), TEXT("cuSurfObjectCreate"), OutError) ||
		    !ImportFence(Device, mGraphicsFence, mGraphicsSemaphore, OutError) ||
		    !ImportFence(Device, mCudaFence, mCudaSemaphore, OutError))
		{
			return false;
		}
		mTexture = GetID3D12DynamicRHI()->RHICreateTexture2DFromResource(PF_A32B32G32R32F,
		    ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV | ETextureCreateFlags::Shared,
		    FClearValueBinding::None,
		    mResource);
		if (!mTexture.IsValid())
		{
			OutError = TEXT("Unreal could not wrap the CUDA-shared D3D12 allocation.");
			return false;
		}
		mCapabilities.mbSurfaceAccess = true;
		mCapabilities.mSurfaceUnavailableReason.clear();
		return true;
	}

	FArdaCudaKernelSignature FArdaCudaHandoff::GetSignature() const noexcept
	{
		using FArdaArguments = FArdaUnrealCudaParameters::FArdaCuda;
		return {&typeid(FArdaArguments), sizeof(FArdaArguments), alignof(FArdaArguments), true};
	}

	TArdaRHIResult<FArdaCudaKernelLimits> FArdaCudaHandoff::GetLimits() const
	{
		FArdaCudaContextScope ContextScope(mContext);
		FArdaCudaKernelLimits Limits;
		int Value = 0;
		FString Error;
		if (!CheckCuda(cuFuncGetAttribute(&Value, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, mKernel),
		        TEXT("cuFuncGetAttribute"),
		        Error))
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, TCHAR_TO_UTF8(*Error))};
		}
		Limits.mMaxThreadsPerBlock = Value;
		cuFuncGetAttribute(&Value, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, mKernel);
		Limits.mStaticSharedMemoryBytes = Value;
		cuFuncGetAttribute(&Value, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, mKernel);
		Limits.mMaxDynamicSharedMemoryBytes = Value;
		return {Limits, {}};
	}

	FArdaRHIStatus FArdaCudaHandoff::Launch(void* Stream,
	    const FArdaCudaLaunchConfig& Configuration,
	    const void* Parameters,
	    size_t ParameterSize) const
	{
		if (!Stream || !Parameters || ParameterSize != sizeof(FArdaUnrealCudaParameters::FArdaCuda))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid CUDA postprocess kernel arguments.");
		}
		FArdaUnrealCudaParameters::FArdaCuda Values{};
		FMemory::Memcpy(&Values, Parameters, sizeof(Values));
		void* Arguments[] = {&Values.mSurface, &Values.mWidth, &Values.mHeight};
		FString Error;
		const bool bSuccess = CheckCuda(cuLaunchKernel(mKernel,
		                                    Configuration.mGridSize[0],
		                                    Configuration.mGridSize[1],
		                                    Configuration.mGridSize[2],
		                                    Configuration.mBlockSize[0],
		                                    Configuration.mBlockSize[1],
		                                    Configuration.mBlockSize[2],
		                                    Configuration.mSharedMemoryBytes,
		                                    static_cast<CUstream>(Stream),
		                                    Arguments,
		                                    nullptr),
		    TEXT("cuLaunchKernel"),
		    Error);
		return bSuccess ? FArdaRHIStatus{}
		                : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, TCHAR_TO_UTF8(*Error));
	}

	FArdaRHIStatus FArdaCudaHandoff::Enqueue(FRHICommandListImmediate& RHICmdList,
	    const eastl::vector<FArdaCudaKernel>& Kernels)
	{
		// Present is COMMON in legacy D3D12 barriers. The RDG pass enters/exits in UAVCompute.
		RHICmdList.Transition(FRHITransitionInfo(mTexture, ERHIAccess::UAVCompute, ERHIAccess::Present));
		const uint64 FenceValue = ++mFenceValue;
		RHICmdList.EnqueueLambda(TEXT("ArdaGraphicsCudaHandoff"),
		    [this, Owner = shared_from_this(), FenceValue, Kernels](FRHICommandList& ExecutingList)
		    {
			    ID3D12DynamicRHI* D3D12RHI = GetID3D12DynamicRHI();
			    D3D12RHI->RHIFlushResourceBarriers(ExecutingList, 0);
			    D3D12RHI->RHISignalManualFence(ExecutingList, mGraphicsFence, FenceValue);
			    FArdaCudaContextScope ContextScope(mContext);
			    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS Wait{};
			    Wait.params.fence.value = FenceValue;
			    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS Signal{};
			    Signal.params.fence.value = FenceValue;
			    FString Error;
			    if (!CheckCuda(cuWaitExternalSemaphoresAsync(&mGraphicsSemaphore, &Wait, 1, mStream),
			            TEXT("cuWaitExternalSemaphoresAsync"),
			            Error))
			    {
				    UE_LOG(LogTemp, Fatal, TEXT("Arda CUDA handoff failed: %s"), *Error);
				    return;
			    }
			    for (const FArdaCudaKernel& Kernel : Kernels)
			    {
				    const FArdaRHIStatus Status =
				        Kernel.mEntry->Launch(mStream, Kernel, Kernel.mParameters.data(), Kernel.mParameters.size());
				    if (!Status)
				    {
					    UE_LOG(LogTemp,
					        Fatal,
					        TEXT("Arda graph CUDA kernel failed: %s"),
					        UTF8_TO_TCHAR(Status.mMessage.c_str()));
					    return;
				    }
			    }
			    if (!CheckCuda(cuSignalExternalSemaphoresAsync(&mCudaSemaphore, &Signal, 1, mStream),
			            TEXT("cuSignalExternalSemaphoresAsync"),
			            Error))
			    {
				    // Never enqueue a graphics wait for a CUDA signal that failed to submit.
				    UE_LOG(LogTemp, Fatal, TEXT("Arda CUDA handoff failed: %s"), *Error);
				    return;
			    }
			    // The CUDA signal has now been submitted; no CPU wait or queue/device idle is needed.
			    D3D12RHI->RHIWaitManualFence(ExecutingList, mCudaFence, FenceValue, nullptr);
		    });
		RHICmdList.Transition(FRHITransitionInfo(mTexture, ERHIAccess::Present, ERHIAccess::UAVCompute));
		return {};
	}

	bool FArdaUnrealTestCudaInterop::Initialize(FString& OutError, bool bLegacyBarriers)
	{
		if (!bLegacyBarriers)
		{
			OutError = TEXT(
			    "The CUDA fixture requires r.D3D12.PreferredBarrierImplementation=1 (legacy barriers); restart after changing DefaultEngine.ini.");
			return false;
		}
		mProbe = eastl::make_shared<FArdaCudaHandoff>();
		return mProbe->Initialize(FIntPoint(8, 8), OutError);
	}

	void FArdaUnrealTestCudaInterop::RegisterSurface(const eastl::shared_ptr<FArdaCudaHandoff>& Surface)
	{
		mSurfaces.Add(Surface->GetTexture(), Surface);
	}

	FArdaCudaCapabilities FArdaUnrealTestCudaInterop::GetCapabilities() const
	{
		return mProbe ? mProbe->GetCapabilities() : FArdaCudaCapabilities{};
	}

	FArdaCudaResourceInfo FArdaUnrealTestCudaInterop::DescribeTexture(FRHITexture* Texture) const
	{
		const auto* Surface = mSurfaces.Find(Texture);
		if (Surface && !Surface->expired())
		{
			return {EArdaCudaRepresentation::Surface, EArdaResourceRepresentations::GraphicsAndCuda, true};
		}
		return {};
	}

	FArdaRHIStatus FArdaUnrealTestCudaInterop::Dispatch(FRHICommandListImmediate& CommandList,
	    const eastl::vector<FArdaUnrealCudaBinding>& Bindings,
	    const eastl::vector<FArdaCudaKernel>& Kernels)
	{
		if (Bindings.size() != 1 || Bindings[0].mType != EArdaComputeBindingType::Surface ||
		    Bindings[0].mMipLevel != 0 || Kernels.empty())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "The test CUDA bridge supports one mip-zero shared surface per batch.");
		}
		const auto* WeakSurface = mSurfaces.Find(Bindings[0].mTexture);
		const auto Surface = WeakSurface ? WeakSurface->lock() : eastl::shared_ptr<FArdaCudaHandoff>{};
		if (!Surface)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "The graph CUDA surface has no live shared allocation.");
		}
		eastl::vector<FArdaCudaKernel> PatchedKernels = Kernels;
		for (FArdaCudaKernel& Kernel : PatchedKernels)
		{
			if (!Kernel.mEntry || Kernel.mExternalCall || Kernel.mEntry.get() != Surface.get())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "The fixture accepts its own compiled kernel and no CUDA graph capture/external calls.");
			}
			if (Kernel.mGraphBatch &&
			    ((Kernel.mGraphBatch->mCache &&
			         Kernel.mGraphBatch->mCache->GetMode() != EArdaCudaGraphMode::Disabled) ||
			        Kernel.mGraphBatch->mTimingQuery || !Kernel.mGraphBatch->mTimingRegions.empty()))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "The fixture supports ordinary CUDA sequences with capture and timing disabled.");
			}
			if (Kernel.mPatches.size() != 1 || Kernel.mPatches[0].mBindingIndex != 0 ||
			    Kernel.mPatches[0].mOffset != offsetof(FArdaUnrealCudaParameters::FArdaCuda, mSurface) ||
			    Kernel.mParameters.size() != sizeof(FArdaUnrealCudaParameters::FArdaCuda))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Unexpected CUDA surface parameter patches.");
			}
			FArdaUnrealCudaParameters::FArdaCuda Arguments{};
			FMemory::Memcpy(&Arguments, Kernel.mParameters.data(), sizeof(Arguments));
			if (Arguments.mWidth != uint32(Surface->GetExtent().X) ||
			    Arguments.mHeight != uint32(Surface->GetExtent().Y))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA kernel dimensions must match the mapped surface.");
			}
			const auto Limits = Kernel.mEntry->GetLimits();
			if (!Limits)
			{
				return Limits.mStatus;
			}
			const uint64 Threads = uint64(Kernel.mBlockSize[0]) * Kernel.mBlockSize[1] * Kernel.mBlockSize[2];
			if (Threads > Limits.mValue.mMaxThreadsPerBlock ||
			    Kernel.mSharedMemoryBytes > Limits.mValue.mMaxDynamicSharedMemoryBytes)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "CUDA dispatch exceeds this compiled kernel's native limits.");
			}
			const uint64 Address = Surface->GetSurface();
			FMemory::Memcpy(Kernel.mParameters.data() + Kernel.mPatches[0].mOffset, &Address, sizeof(Address));
		}
		return Surface->Enqueue(CommandList, PatchedKernels);
	}
}
