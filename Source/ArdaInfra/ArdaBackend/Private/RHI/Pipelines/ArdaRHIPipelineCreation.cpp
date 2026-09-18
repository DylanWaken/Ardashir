#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIFramebufferRef> FArdaRHIDeviceImpl::CreateFramebuffer(
	    const FArdaRHIFramebufferDesc& Desc)
	{
		if (Desc.mColorAttachments.empty() && !Desc.mDepthAttachment.mTexture)
		{
			return Failure<FArdaRHIFramebufferRef>(Invalid("A framebuffer requires at least one attachment."));
		}
		FArdaProviderFramebufferCreateInfo Info{Desc};
		Info.mColors.reserve(Desc.mColorAttachments.size());
		for (const auto& Target : Desc.mColorAttachments)
		{
			auto* Texture = Cast<FArdaTexture>(Target.mTexture.Get());
			if (!Texture || !Owns(Texture))
			{
				return Failure<FArdaRHIFramebufferRef>(WrongDevice());
			}
			Info.mColors.push_back({Target, Texture->mNative});
		}
		if (Desc.mDepthAttachment.mTexture)
		{
			auto* Texture = Cast<FArdaTexture>(Desc.mDepthAttachment.mTexture.Get());
			if (!Texture || !Owns(Texture))
			{
				return Failure<FArdaRHIFramebufferRef>(WrongDevice());
			}
			Info.mDepth = {Desc.mDepthAttachment, Texture->mNative};
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIFramebufferRef>(Status);
		}
		const uint32_t MaxAttachments = GetCapabilities().mLimits.mMaxColorAttachments;
		if (MaxAttachments && Desc.mColorAttachments.size() > MaxAttachments)
		{
			return Failure<FArdaRHIFramebufferRef>(
			    Unsupported("Framebuffer exceeds the device color-attachment limit."));
		}
		auto Native = mDevice->CreateFramebuffer(Info);
		if (!Native)
		{
			return Failure<FArdaRHIFramebufferRef>(eastl::move(Native.mStatus));
		}
		return {
		    FArdaRHIFramebufferRef(new FArdaFramebuffer(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	template <typename TDesc>
	FArdaRHIStatus ValidatePipelineAttachments(const TDesc& Desc, const IArdaRHIDevice& Device)
	{
		const auto& Capabilities = Device.GetCapabilities();
		if (Capabilities.mLimits.mMaxColorAttachments &&
		    Desc.mColorFormats.size() > Capabilities.mLimits.mMaxColorAttachments)
		{
			return Unsupported("Pipeline exceeds the device color-attachment limit.");
		}
		const auto ValidateFormat = [&](EArdaRHIFormat Format, bool bDepth, bool bBlend)
		{
			FArdaRHITextureDesc Texture;
			Texture.mFormat = Format;
			Texture.mSampleCount = Desc.mSampleCount;
			Texture.mUsage = bDepth ? EArdaRHITextureUsage::DepthStencil : EArdaRHITextureUsage::RenderTarget;
			const auto Support = Device.QueryFormatSupport(Format);
			if (auto Status = ValidateResourceCapabilities(Texture, Capabilities, Support); !Status)
			{
				return Status;
			}
			return Capabilities.mbFormatSupportReported && bBlend && !Support.mbBlendable
			    ? Unsupported("Blending is unsupported for a pipeline attachment format.")
			    : FArdaRHIStatus{};
		};
		for (size_t Index = 0; Index < Desc.mColorFormats.size(); ++Index)
		{
			if (auto Status =
			        ValidateFormat(Desc.mColorFormats[Index], false, Desc.mBlendState.mTargets[Index].mbEnable);
			    !Status)
			{
				return Status;
			}
		}
		return Desc.mDepthFormat == EArdaRHIFormat::Unknown ? FArdaRHIStatus{}
		                                                    : ValidateFormat(Desc.mDepthFormat, true, false);
	}

	TArdaRHIResult<FArdaRHIGraphicsPipelineRef> FArdaRHIDeviceImpl::CreateGraphicsPipeline(
	    const FArdaRHIGraphicsPipelineDesc& Desc)
	{
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Status));
		}
		FArdaProviderGraphicsPipelineCreateInfo Info{Desc};
		if (auto Status = ValidatePipelineAttachments(Desc, *this); !Status)
		{
			return Failure<FArdaRHIGraphicsPipelineRef>(Status);
		}
		if (Desc.mInputLayout)
		{
			auto* Layout = Cast<FArdaInputLayout>(Desc.mInputLayout.Get());
			if (!Layout || !Owns(Layout))
			{
				return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
			}
			Info.mInputLayout = &Layout->mDesc;
		}
		if (!ResolveShader(Desc.mVertexShader, Info.mVertexShader) ||
		    !ResolveShader(Desc.mHullShader, Info.mHullShader) ||
		    !ResolveShader(Desc.mDomainShader, Info.mDomainShader) ||
		    !ResolveShader(Desc.mGeometryShader, Info.mGeometryShader) ||
		    !ResolveShader(Desc.mPixelShader, Info.mPixelShader))
		{
			return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
		}
		if (auto Status = ResolveBindingLayouts(Desc.mBindingLayouts, Info.mBindingLayouts); !Status)
		{
			return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Status));
		}
		auto Native = mDevice->CreateGraphicsPipeline(Info);
		if (!Native)
		{
			return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIGraphicsPipelineRef(
		            new FArdaGraphicsPipeline(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIComputePipelineRef> FArdaRHIDeviceImpl::CreateComputePipeline(
	    const FArdaRHIComputePipelineDesc& Desc)
	{
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIComputePipelineRef>(eastl::move(Status));
		}
		auto* Shader = Cast<FArdaShader>(Desc.mComputeShader.Get());
		if (!Shader || !Owns(Shader))
		{
			return Failure<FArdaRHIComputePipelineRef>(WrongDevice());
		}
		FArdaProviderComputePipelineCreateInfo Info{Desc, Shader->mNative};
		if (auto Status = ResolveBindingLayouts(Desc.mBindingLayouts, Info.mBindingLayouts); !Status)
		{
			return Failure<FArdaRHIComputePipelineRef>(eastl::move(Status));
		}
		auto Native = mDevice->CreateComputePipeline(Info);
		if (!Native)
		{
			return Failure<FArdaRHIComputePipelineRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIComputePipelineRef(
		            new FArdaComputePipeline(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIMeshletPipelineRef> FArdaRHIDeviceImpl::CreateMeshletPipeline(
	    const FArdaRHIMeshletPipelineDesc& Desc)
	{
		if (!GetCapabilities().SupportsMeshShaderTier(Desc.mAmplificationShader
		            ? EArdaRHIMeshShaderTier::MeshAndAmplificationShaders
		            : EArdaRHIMeshShaderTier::MeshShadersOnly))
		{
			return UnsupportedResult<FArdaRHIMeshletPipelineRef>("Mesh shaders are unsupported by this device.");
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIMeshletPipelineRef>(eastl::move(Status));
		}

		FArdaProviderMeshletPipelineCreateInfo Info{Desc};
		if (auto Status = ValidatePipelineAttachments(Desc, *this); !Status)
		{
			return Failure<FArdaRHIMeshletPipelineRef>(Status);
		}
		if (!ResolveShader(Desc.mAmplificationShader, Info.mAmplificationShader) ||
		    !ResolveShader(Desc.mMeshShader, Info.mMeshShader) ||
		    !ResolveShader(Desc.mPixelShader, Info.mPixelShader))
		{
			return Failure<FArdaRHIMeshletPipelineRef>(WrongDevice());
		}
		if (auto Status = ResolveBindingLayouts(Desc.mBindingLayouts, Info.mBindingLayouts); !Status)
		{
			return Failure<FArdaRHIMeshletPipelineRef>(eastl::move(Status));
		}
		auto Native = mDevice->CreateMeshletPipeline(Info);
		if (!Native)
		{
			return Failure<FArdaRHIMeshletPipelineRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIMeshletPipelineRef(
		            new FArdaMeshletPipeline(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIWorkGraphPipelineRef> FArdaRHIDeviceImpl::CreateWorkGraphPipeline(
	    const FArdaRHIWorkGraphPipelineDesc& Desc)
	{
		if (GetCapabilities().mWorkGraphTier == EArdaRHIWorkGraphTier::None)
		{
			return UnsupportedResult<FArdaRHIWorkGraphPipelineRef>("Work graphs are unsupported by this device.");
		}
		if (Desc.mProgramName.empty() || Desc.mShaders.empty() || !Desc.mMaxInputRecords)
		{
			return Failure<FArdaRHIWorkGraphPipelineRef>(
			    Invalid("A work graph requires a program name, shaders, and input-record capacity."));
		}
		FArdaProviderWorkGraphPipelineCreateInfo Info;
		Info.mDesc = Desc;
		Info.mDesc.mShaders.clear();
		Info.mDesc.mGlobalBindingLayouts.clear();
		for (const auto& ShaderRef : Desc.mShaders)
		{
			auto* Shader = Cast<FArdaShader>(ShaderRef.Get());
			if (!Shader || !Owns(Shader) || Shader->mStage != EArdaRHIShaderStage::WorkGraph)
			{
				return Failure<FArdaRHIWorkGraphPipelineRef>(
				    Invalid("Work-graph shaders must belong to this device and use the WorkGraph stage."));
			}
			Info.mShaders.push_back(Shader->mNative);
		}
		if (auto Status = ResolveBindingLayouts(Desc.mGlobalBindingLayouts, Info.mBindingLayouts); !Status)
		{
			return Failure<FArdaRHIWorkGraphPipelineRef>(eastl::move(Status));
		}
		auto Native = mDevice->CreateWorkGraphPipeline(Info);
		if (!Native)
		{
			return Failure<FArdaRHIWorkGraphPipelineRef>(eastl::move(Native.mStatus));
		}
		const uint64_t BackingSize = Native.mValue->GetWorkGraphBackingMemorySize();
		return {
		    FArdaRHIWorkGraphPipelineRef(
		        new FArdaWorkGraphPipeline(Desc, eastl::move(Native.mValue), BackingSize, this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIRayTracingPipelineRef> FArdaRHIDeviceImpl::CreateRayTracingPipeline(
	    const FArdaRHIRayTracingPipelineDesc& Desc)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
		{
			return UnsupportedResult<FArdaRHIRayTracingPipelineRef>("Ray tracing is unsupported by this device.");
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIRayTracingPipelineRef>(eastl::move(Status));
		}
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mRayTracingPipelineCache.Find(Desc))
		{
			return {Existing, {}};
		}
		FArdaProviderRayTracingPipelineCreateInfo Info{Desc};
		for (const auto& ShaderDesc : Desc.mShaders)
		{
			auto* Shader = Cast<FArdaShader>(ShaderDesc.mShader.Get());
			if (!Shader || !Owns(Shader))
			{
				return Failure<FArdaRHIRayTracingPipelineRef>(WrongDevice());
			}
			FArdaProviderObjectRef LocalLayout;
			if (ShaderDesc.mLocalBindingLayout)
			{
				auto* Layout = Cast<FArdaBindingLayout>(ShaderDesc.mLocalBindingLayout.Get());
				if (!Layout || !Owns(Layout))
				{
					return Failure<FArdaRHIRayTracingPipelineRef>(WrongDevice());
				}
				LocalLayout = Layout->mNative;
			}
			Info.mShaders.push_back(
			    {ShaderDesc.mExportName, Shader->mEntryPoint, Shader->mNative, eastl::move(LocalLayout)});
		}
		for (const auto& HitDesc : Desc.mHitGroups)
		{
			FArdaProviderRayTracingHitGroup Hit;
			Hit.mExportName = HitDesc.mExportName;
			Hit.mbProceduralPrimitive = HitDesc.mbProceduralPrimitive;
			const auto ResolveHitShader = [this, &HitDesc](const FArdaRHIShaderRef& Ref,
			                                  const char* Suffix,
			                                  FArdaProviderRayTracingShader& Output) -> FArdaRHIStatus
			{
				if (!Ref)
				{
					return {};
				}
				auto* Shader = Cast<FArdaShader>(Ref.Get());
				if (!Shader || !Owns(Shader))
				{
					return WrongDevice();
				}
				Output.mExportName = HitDesc.mExportName;
				Output.mExportName += Suffix;
				Output.mEntryPoint = Shader->mEntryPoint;
				Output.mShader = Shader->mNative;
				return {};
			};
			if (auto Status = ResolveHitShader(HitDesc.mClosestHitShader, ".closesthit", Hit.mClosestHit); !Status)
			{
				return Failure<FArdaRHIRayTracingPipelineRef>(Status);
			}
			if (auto Status = ResolveHitShader(HitDesc.mAnyHitShader, ".anyhit", Hit.mAnyHit); !Status)
			{
				return Failure<FArdaRHIRayTracingPipelineRef>(Status);
			}
			if (auto Status = ResolveHitShader(HitDesc.mIntersectionShader, ".intersection", Hit.mIntersection);
			    !Status)
			{
				return Failure<FArdaRHIRayTracingPipelineRef>(Status);
			}
			if (HitDesc.mLocalBindingLayout)
			{
				auto* Layout = Cast<FArdaBindingLayout>(HitDesc.mLocalBindingLayout.Get());
				if (!Layout || !Owns(Layout))
				{
					return Failure<FArdaRHIRayTracingPipelineRef>(WrongDevice());
				}
				Hit.mLocalBindingLayout = Layout->mNative;
			}
			Info.mHitGroups.push_back(eastl::move(Hit));
		}
		for (const auto& LayoutRef : Desc.mGlobalBindingLayouts)
		{
			auto* Layout = Cast<FArdaBindingLayout>(LayoutRef.Get());
			if (!Layout || !Owns(Layout))
			{
				return Failure<FArdaRHIRayTracingPipelineRef>(WrongDevice());
			}
			Info.mGlobalBindingLayouts.push_back(Layout->mNative);
		}
		auto Native = mDevice->CreateRayTracingPipeline(Info);
		if (!Native)
		{
			return Failure<FArdaRHIRayTracingPipelineRef>(eastl::move(Native.mStatus));
		}
		FArdaRHIRayTracingPipelineRef Result(
		    new FArdaRayTracingPipeline(Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
		mRayTracingPipelineCache.Insert(Desc, Result);
		return {Result, {}};
	}

	TArdaRHIResult<FArdaRHIRasterStateRef> FArdaRHIDeviceImpl::CreateRasterState(const FArdaRHIRasterState& Desc)
	{
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mRasterStateCache.Find(Desc))
		{
			return {Existing, {}};
		}
		FArdaRHIRasterStateRef Result(new FArdaRasterState(Desc, this, mLifetimeTracker));
		mRasterStateCache.Insert(Desc, Result);
		return {Result, {}};
	}

	TArdaRHIResult<FArdaRHIBlendStateRef> FArdaRHIDeviceImpl::CreateBlendState(const FArdaRHIBlendState& Desc)
	{
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mBlendStateCache.Find(Desc))
		{
			return {Existing, {}};
		}
		FArdaRHIBlendStateRef Result(new FArdaBlendState(Desc, this, mLifetimeTracker));
		mBlendStateCache.Insert(Desc, Result);
		return {Result, {}};
	}

	TArdaRHIResult<FArdaRHIDepthStencilStateRef> FArdaRHIDeviceImpl::CreateDepthStencilState(
	    const FArdaRHIDepthStencilState& Desc)
	{
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mDepthStateCache.Find(Desc))
		{
			return {Existing, {}};
		}
		FArdaRHIDepthStencilStateRef Result(new FArdaDepthStencilState(Desc, this, mLifetimeTracker));
		mDepthStateCache.Insert(Desc, Result);
		return {Result, {}};
	}
}
