/** Descriptor validation and resolution before provider calls. */
#pragma once

#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include "RHI/Resources/ArdaRHIResourceCollection.h"

namespace arda::detail
{
	FArdaRHIStatus ResolveBindingItem(const FArdaRHIBindingLayoutDesc& Layout, FArdaRHIBindingItem& Item, const FArdaRHIDeviceLimits& Limits);
	size_t PushConstantCapacity(const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts);
	TArdaRHIResult<FArdaRHIBindingItem> MakeCollectionBinding(const FArdaRHIResourceCollectionItem& Item, uint32_t ArrayElement);
}
