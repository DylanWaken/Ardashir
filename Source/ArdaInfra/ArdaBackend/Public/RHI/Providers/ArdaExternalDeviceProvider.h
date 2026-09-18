/** Host-owned external device provider contract and registration API. */
#pragma once

#include "RHI/Interop/ArdaExternalInterop.h"
#include <EASTL/shared_ptr.h>

namespace arda
{
	/**
     * Supplies one externally owned native graphics device.
     * The provider is never owned by Arda and is called only while initialization holds the
     * backend registry lock. Implementations must not reenter backend registration or lifetime
     * APIs. Arda copies the selected descriptor and lifetime token before the call returns.
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

	/**
     * Registers the process-wide external device provider.
     * Re-registering the same object is idempotent; a different provider or an initialized
     * backend is rejected. Registration is thread-safe, but the provider remains host-owned.
     * @param Provider Provider that remains valid until unregistered after shutdown.
     * @return True when the provider is registered.
     */
	[[nodiscard]] bool RegisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider);

	/**
     * Unregisters the process-wide external device provider.
     * @param Provider The exact registered provider object.
     * @return True when absent or successfully unregistered; false on mismatch or active backend.
     */
	[[nodiscard]] bool UnregisterExternalDeviceProvider(IArdaExternalDeviceProvider& Provider);

	/**
     * Gets the registered provider without transferring ownership.
     * The host must prevent concurrent unregister while using the returned pointer.
     * @return The registered provider, or null.
     */
	[[nodiscard]] const IArdaExternalDeviceProvider* GetExternalDeviceProvider() noexcept;
}
