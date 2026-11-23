/** Host-owned external device provider contract. */
#pragma once

#include "RHI/Interop/ArdaExternalInterop.h"
#include <EASTL/shared_ptr.h>

namespace arda
{
	// ================================
	// External device provider
	// ================================

	/**
     * Supplies one externally owned native graphics device.
     * The host owns the provider. Backend configuration queries its name and initialization
     * copies its device descriptor and retains its lifetime token. These callbacks run under the
     * backend lifecycle lock. They can inspect published device snapshots. Reentering lifecycle
     * operations or queries such as GetBackendError or GetExternalDeviceProvider would reacquire that lock.
     */
	class IArdaExternalDeviceProvider
	{
	public:
		/** Destroys the provider after the host has unregistered it. */
		virtual ~IArdaExternalDeviceProvider() = default;

		/**
         * @return Exact registered backend module that consumes the device.
         */
		[[nodiscard]] virtual const char* GetBackendName() const noexcept = 0;

		/**
         * Copies a universal external-device descriptor when supported.
         * Backend modules should prefer this path for engine and custom RHI hosts.
         * @param OutDesc Receives a self-contained descriptor.
         * @return True when a descriptor was supplied.
         */
		[[nodiscard]] virtual bool GetExternalDeviceDesc(FArdaExternalDeviceDesc& OutDesc) const
		{
			return false;
		}

		/**
         * Returns an optional token retaining the native device lifetime.
         * @return A shared token copied and retained through backend shutdown, or empty.
         */
		[[nodiscard]] virtual eastl::shared_ptr<void> GetLifetimeToken() const
		{
			return {};
		}
	};
}
