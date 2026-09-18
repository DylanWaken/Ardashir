/** Native provider object identity, allocation metadata and shared ownership. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Interop/ArdaRHICudaResourceInfo.h"
#include "RHI/Memory/ArdaRHIMemoryTypes.h"
#include <EASTL/shared_ptr.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	class IArdaProviderObject
	{
	public:
		virtual ~IArdaProviderObject() = default;
		[[nodiscard]] virtual const void* GetIdentity() const noexcept = 0;

		/** Entire retained allocation, including parent heap capacity for placed resources. */
		[[nodiscard]] virtual FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept
		{
			return {};
		}

		[[nodiscard]] virtual FArdaCudaResourceInfo GetCudaResourceInfo() const noexcept
		{
			return {};
		}

		[[nodiscard]] virtual uint32_t GetDescriptorBaseIndex() const noexcept
		{
			return 0;
		}

		[[nodiscard]] virtual uint64_t GetWorkGraphBackingMemorySize() const noexcept
		{
			return 0;
		}
	};

	using FArdaProviderObjectRef = eastl::shared_ptr<IArdaProviderObject>;
	using FArdaProviderObjectResult = TArdaRHIResult<FArdaProviderObjectRef>;

	struct FArdaProviderLifetimeStats
	{
		size_t mResourceDescriptors = 0;
		size_t mSamplerDescriptors = 0;
		size_t mDescriptorSets = 0;
		size_t mPendingSubmissions = 0;
	};
}
