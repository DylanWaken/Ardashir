/** Physical adapter identity and enumeration information. */
#pragma once

#include <EASTL/string.h>
#include <cstdint>

namespace arda
{
	/** Opaque physical-adapter identity. Obtain it from EnumerateAdapters; do not use list positions as IDs. */
	struct FArdaAdapterId
	{
		/** Exact module that owns this identity. */
		eastl::string mBackendName;
		/** Module-defined identity, independent of enumeration order. Re-enumerate after hardware changes. */
		eastl::string mValue;

		/** Compares both the backend and its adapter identity. */
		[[nodiscard]] bool operator==(const FArdaAdapterId& Other) const noexcept
		{
			return mBackendName == Other.mBackendName && mValue == Other.mValue;
		}
	};

	/** Physical adapter information; device creation still validates features and presentation support. */
	struct FArdaAdapterInfo
	{
		/** Selection identity, including the owning backend. */
		FArdaAdapterId mId;
		/** UTF-8 display name reported by the graphics API. */
		eastl::string mName;
		/** Hardware vendor ID reported by the graphics API. */
		uint32_t mVendorId = 0;
		/** Hardware device ID reported by the graphics API. */
		uint32_t mDeviceId = 0;
		/** Static device-local memory capacity; not a live allocation budget. */
		uint64_t mDeviceLocalMemoryBytes = 0;
		/** True for a software implementation such as D3D12 WARP. */
		bool mbSoftware = false;
	};
}
