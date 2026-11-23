/** Extensible native operations recorded by a graphics backend. */
#pragma once

#include "RHI/Resources/ArdaComputeAccess.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

namespace arda
{
	// ================================
	// Native operations and resource bindings
	// ================================

	/** Immutable, backend-defined command payload. */
	class IArdaRHINativeOperation
	{
	public:
		virtual ~IArdaRHINativeOperation() = default;
		/** Stable, nonempty operation identifier shared with the recording backend. */
		[[nodiscard]] virtual const char* GetId() const noexcept = 0;
	};

	/** Buffer or texture view consumed by the native operation. */
	struct FArdaRHINativeOperationBinding
	{
		/** Buffer or texture owned by the recording device. */
		FArdaRHIResourceRef mResource;
		/** Access the operation performs on the selected view. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		/** Nonempty byte range for a buffer binding. */
		FArdaRHIBufferRange mBufferRange;
		/** Selected mip levels, array slices, and planes for a texture binding. */
		FArdaRHITextureSubresourceRange mTextureRange;
	};

	/** Command and ordered resource bindings for one recording. */
	struct FArdaRHINativeOperationDesc
	{
		/** Immutable backend payload with a nonempty operation ID. */
		eastl::shared_ptr<const IArdaRHINativeOperation> mOperation;
		/** Resource views ordered according to the operation ABI. */
		eastl::vector<FArdaRHINativeOperationBinding> mBindings;
	};
}
