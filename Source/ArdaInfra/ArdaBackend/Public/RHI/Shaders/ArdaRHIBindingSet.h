/** @file ArdaRHIBindingSet.h
 * Declares BindingSet definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Resources/ArdaRHIViews.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	/** Describes binding item. */
	struct FArdaRHIBindingItem
	{
		/** Stores the slot. */
		uint32_t mSlot = 0;
		/** Stores the array element. */
		uint32_t mArrayElement = 0;
		/** Stores the type. */
		EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
		/** Stores the resource. */
		TArdaRHIRef<IArdaRHIResource> mResource;
		/** Inline descriptor for raw resources. For an SRV/UAV resource, the retained view is authoritative:
         * a default descriptor adopts it, an identical descriptor confirms it, and any conflict is rejected.
         * The binding type must match the retained view's SRV/UAV access kind.
         */
		FArdaRHIViewDesc mView;
	};

	/** Describes binding set desc. */
	struct FArdaRHIBindingSetDesc
	{
		/** Stores the layout. */
		FArdaRHIBindingLayoutRef mLayout;
		/** Stores the items. */
		eastl::vector<FArdaRHIBindingItem> mItems;
		/** Actual count allocated for a variable-count bindless binding. */
		uint32_t mVariableDescriptorCount = 0;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for binding set. */
	class IArdaRHIBindingSet : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIBindingSetDesc& GetDesc() const noexcept = 0;
	};
}
