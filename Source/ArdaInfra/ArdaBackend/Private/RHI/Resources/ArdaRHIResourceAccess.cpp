/** Provider-object resolution kept out of resource interface dependencies. */

#include "RHI/Resources/ArdaRHIResourceAccess.h"
#include "RHI/Resources/ArdaRHISamplerImpl.h"
#include "RHI/Resources/ArdaRHIFramebufferImpl.h"
#include "RHI/Resources/ArdaRHITextureBufferImpl.h"
#include "RHI/Resources/ArdaRHIAccelerationImpl.h"
#include "RHI/Shaders/ArdaRHIShaderImpl.h"
#include "RHI/Pipelines/ArdaRHIPipelineImpl.h"

namespace arda::detail
{
	FArdaProviderObjectRef GetNativeObject(IArdaRHIResource* Resource)
	{
		if (auto* Texture = Cast<FArdaTexture>(Resource))
		{
			return Texture->mNative;
		}
		if (auto* Buffer = Cast<FArdaBuffer>(Resource))
		{
			return Buffer->mNative;
		}
		if (auto* Texture = Cast<FArdaStagingTexture>(Resource))
		{
			return Texture->mNative;
		}
		if (auto* Sampler = Cast<FArdaSampler>(Resource))
		{
			return Sampler->mNative;
		}
		if (auto* Layout = Cast<FArdaBindingLayout>(Resource))
		{
			return Layout->mNative;
		}
		if (auto* Set = Cast<FArdaBindingSet>(Resource))
		{
			return Set->mNative;
		}
		if (auto* Table = Cast<FArdaDescriptorTable>(Resource))
		{
			return Table->mNative;
		}
		if (auto* Framebuffer = Cast<FArdaFramebuffer>(Resource))
		{
			return Framebuffer->mNative;
		}
		if (auto* Pipeline = Cast<FArdaGraphicsPipeline>(Resource))
		{
			return Pipeline->mNative;
		}
		if (auto* Pipeline = Cast<FArdaComputePipeline>(Resource))
		{
			return Pipeline->mNative;
		}
		if (auto* Pipeline = Cast<FArdaMeshletPipeline>(Resource))
		{
			return Pipeline->mNative;
		}
		if (auto* Pipeline = Cast<FArdaRayTracingPipeline>(Resource))
		{
			return Pipeline->mNative;
		}
		if (auto* AccelStruct = Cast<FArdaAccelStruct>(Resource))
		{
			return AccelStruct->mNative;
		}
		if (auto* Micromap = Cast<FArdaOpacityMicromap>(Resource))
		{
			return Micromap->mNative;
		}
		if (auto* Table = Cast<FArdaShaderTable>(Resource))
		{
			return Table->mNative;
		}
		if (auto* Feedback = Cast<FArdaSamplerFeedbackTexture>(Resource))
		{
			return Feedback->mNative;
		}
		if (auto* Shader = Cast<FArdaShader>(Resource))
		{
			return Shader->mNative;
		}
		if (auto* View = Cast<FArdaShaderResourceView>(Resource))
		{
			return GetNativeObject(View->mResource.Get());
		}
		if (auto* View = Cast<FArdaUnorderedAccessView>(Resource))
		{
			return GetNativeObject(View->mResource.Get());
		}
		return {};
	}
}
