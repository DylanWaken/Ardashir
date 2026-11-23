#pragma once

#include "../../ArdaBackend/Private/RHI/Device/ArdaRHIDevicePrivate.h"

namespace arda::requirements_tests
{
	/** Admission uses a real facade and capability snapshots; every native operation is counted and rejected. */
	class FArdaAdmissionProvider final : public IArdaRHIProviderDevice
	{
	public:
		FArdaRHICapabilities mCapabilities;
		FArdaCudaCapabilities mCuda;
		uint32_t mNativeAttempts = 0, mWaits = 0, mBufferQueries = 0;

		static FArdaRHIStatus Unexpected()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Admission must not perform native GPU work.");
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mCapabilities;
		}

		FArdaCudaCapabilities GetCudaCapabilities() const override
		{
			return mCuda;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return {};
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return {};
		}

#define ARDA_ADMISSION_NO_OBJECT(Method, Parameter)                                                                    \
	FArdaProviderObjectResult Method(const Parameter&) override                                                        \
	{                                                                                                                  \
		++mNativeAttempts;                                                                                             \
		return {{}, Unexpected()};                                                                                     \
	}
		ARDA_ADMISSION_NO_OBJECT(CreateTexture, FArdaRHITextureDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateBuffer, FArdaRHIBufferDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateHeap, FArdaRHIHeapDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateStagingTexture, FArdaRHIStagingTextureDesc)
		ARDA_ADMISSION_NO_OBJECT(ImportTexture, FArdaRHINativeTextureImportDesc)
		ARDA_ADMISSION_NO_OBJECT(ImportBuffer, FArdaRHINativeBufferImportDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateSampler, FArdaRHISamplerDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateShader, FArdaRHIShaderDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateBindingLayout, FArdaRHIBindingLayoutDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateFramebuffer, FArdaProviderFramebufferCreateInfo)
		ARDA_ADMISSION_NO_OBJECT(CreateGraphicsPipeline, FArdaProviderGraphicsPipelineCreateInfo)
		ARDA_ADMISSION_NO_OBJECT(CreateComputePipeline, FArdaProviderComputePipelineCreateInfo)
#undef ARDA_ADMISSION_NO_OBJECT

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(
		    const FArdaRHIBufferDesc& Desc) override
		{
			++mBufferQueries;
			return {{Desc.mByteSize, 1, 1}, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&,
		    const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderBinding>&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureSlice&,
		    EArdaRHICpuAccess) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef&) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef&, uint64_t, size_t) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		void UnmapBuffer(const FArdaProviderObjectRef&) noexcept override
		{
			++mNativeAttempts;
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			++mWaits;
			return {};
		}

		FArdaRHIStatus WaitForSubmission(uint64_t) override
		{
			++mWaits;
			return Unexpected();
		}

		void RunGarbageCollection() override
		{
		}

		void FlushPipelineCache() noexcept override
		{
		}
	};
}
