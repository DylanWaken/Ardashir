/** @file ArdaRHIDescriptorTable.h
 * Declares DescriptorTable definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Shaders/ArdaRHIBindingSet.h"

#include <cstdint>

namespace arda
{
	/**
     * Mutable bounded descriptor table. Each native table version retains its
     * written resources through all command-list uses of that version.
     */
	class IArdaRHIDescriptorTable : public virtual IArdaRHIBindingSet
	{
	public:
		/**
         * Returns the capacity.
         * @return The requested numeric value.
         */
		[[nodiscard]] virtual uint32_t GetCapacity() const noexcept = 0;

		/**
         * Returns the first descriptor index in heap.
         * @return The requested numeric value.
         */
		[[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap() const noexcept = 0;
	};
}
