#pragma once

#include "RHI/Providers/ArdaRHIProviderDevice.h"

namespace arda
{
	/** Explicitly rejects provider operations outside the qualified Unreal compute subset. */
	class FArdaUnrealUnsupportedDevice : public IArdaRHIProviderDevice
	{
	public:
		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			static const FArdaRHICapabilities Capabilities;
			return Capabilities;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc& Desc) override
		{
			return {{}, Unsupported("CreateTexture")};
		}

		FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc& Desc) override
		{
			return {{}, Unsupported("CreateBuffer")};
		}

		FArdaProviderObjectResult CreateHeap(const FArdaRHIHeapDesc& Desc) override
		{
			return {{}, Unsupported("CreateHeap")};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc) override
		{
			return {{}, Unsupported("GetTextureMemoryRequirements")};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc) override
		{
			return {{}, Unsupported("GetBufferMemoryRequirements")};
		}

		FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset) override
		{
			return Unsupported("BindTextureMemory");
		}

		FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset) override
		{
			return Unsupported("BindBufferMemory");
		}

		FArdaProviderObjectResult CreateStagingTexture(const FArdaRHIStagingTextureDesc& Desc) override
		{
			return {{}, Unsupported("CreateStagingTexture")};
		}

		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureSlice& Slice,
		    EArdaRHICpuAccess Access) override
		{
			return {{}, Unsupported("MapStagingTexture")};
		}

		FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef& Texture) override
		{
			return Unsupported("UnmapStagingTexture");
		}

		TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef& Buffer, uint64_t Offset, size_t Size) override
		{
			return {{}, Unsupported("MapBuffer")};
		}

		void UnmapBuffer(const FArdaProviderObjectRef& Buffer) noexcept override
		{
			/* Unreal owns this operation; no supported creation path requires it. */
		}

		FArdaProviderObjectResult ImportTexture(const FArdaRHINativeTextureImportDesc& Desc) override
		{
			return {{}, Unsupported("ImportTexture")};
		}

		FArdaProviderObjectResult ImportBuffer(const FArdaRHINativeBufferImportDesc& Desc) override
		{
			return {{}, Unsupported("ImportBuffer")};
		}

		FArdaProviderObjectResult CreateSampler(const FArdaRHISamplerDesc& Desc) override
		{
			return {{}, Unsupported("CreateSampler")};
		}

		FArdaProviderObjectResult CreateShader(const FArdaRHIShaderDesc& Desc) override
		{
			return {{}, Unsupported("CreateShader")};
		}

		FArdaProviderObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc& Desc) override
		{
			return {{}, Unsupported("CreateBindingLayout")};
		}

		FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc& Desc,
		    const FArdaProviderObjectRef& Layout,
		    const eastl::vector<FArdaProviderBinding>& Bindings) override
		{
			return {{}, Unsupported("CreateBindingSet")};
		}

		FArdaProviderObjectResult CreateFramebuffer(const FArdaProviderFramebufferCreateInfo& Info) override
		{
			return {{}, Unsupported("CreateFramebuffer")};
		}

		FArdaProviderObjectResult CreateGraphicsPipeline(const FArdaProviderGraphicsPipelineCreateInfo& Info) override
		{
			return {{}, Unsupported("CreateGraphicsPipeline")};
		}

		FArdaProviderObjectResult CreateComputePipeline(const FArdaProviderComputePipelineCreateInfo& Info) override
		{
			return {{}, Unsupported("CreateComputePipeline")};
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType Queue,
		    bool bImmediate) override
		{
			return {{}, Unsupported("CreateCommandList")};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList& CommandList,
		    EArdaRHIQueueType Queue) override
		{
			return {{}, Unsupported("ExecuteCommandList")};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			return Unsupported("WaitForIdle");
		}

		void RunGarbageCollection() override
		{
			/* Unreal owns this operation; no supported creation path requires it. */
		}

		void FlushPipelineCache() noexcept override
		{
			/* Unreal owns this operation; no supported creation path requires it. */
		}

	protected:
		static FArdaRHIStatus Unsupported(const char* Operation)
		{
			eastl::string Message = "unreal-rhi does not support ";
			Message += Operation;
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message.c_str());
		}
	};
}
