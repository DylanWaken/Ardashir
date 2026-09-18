#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHITextureRef> FArdaRHIDeviceImpl::ImportNativeTexture(
	    const FArdaRHINativeTextureImportDesc& Desc)
	{
		return ImportNativeResource<FArdaTexture>(Desc,
		    Desc.mTexture,
		    mTextureImportCache,
		    mDevice->GetTextureImportType(),
		    &IArdaRHIProviderDevice::ImportTexture,
		    &IArdaRHIProviderDevice::GetTextureMemoryRequirements);
	}

	TArdaRHIResult<FArdaRHIBufferRef> FArdaRHIDeviceImpl::ImportNativeBuffer(
	    const FArdaRHINativeBufferImportDesc& Desc)
	{
		return ImportNativeResource<FArdaBuffer>(Desc,
		    Desc.mBuffer,
		    mBufferImportCache,
		    mDevice->GetBufferImportType(),
		    &IArdaRHIProviderDevice::ImportBuffer,
		    &IArdaRHIProviderDevice::GetBufferMemoryRequirements);
	}
}
