/** Backend configuration and device lifecycle entry points. */
#pragma once

#include "RHI/Config/ArdaBackendConfiguration.h"
#include "RHI/Device/ArdaRHIDevice.h"

namespace arda
{
	/** One initialized logical device and the physical adapter that backs it. */
	struct FArdaBackendDevice
	{
		/** Actual adapter selected, including when selection was automatic or host-owned. */
		FArdaAdapterInfo mAdapter;
		/** Device to pass to a graph, shader map, or resource factory. */
		FArdaRHIDeviceRef mDevice;
	};

	/**
     * Replaces the backend configuration before initialization.
     * Configuration and lifetime calls must not run concurrently with device use.
     * @param configuration The complete configuration to apply.
     * @return True when the configuration was accepted.
     */
	[[nodiscard]] bool ConfigureBackend(const FArdaBackendConfiguration& configuration);

	/**
     * Selects a registered backend module by stable name.
     * @param BackendName Name returned by EnumerateBackendModules.
     * @return True when the named module exists and configuration was accepted.
     */
	[[nodiscard]] bool ConfigureBackend(const char* BackendName);

	/** @return The current process-wide backend configuration. */
	[[nodiscard]] const FArdaBackendConfiguration& GetBackendConfiguration() noexcept;

	/**
	 * Enumerates physical adapters without creating a graphics device or changing configuration.
	 * @param BackendName Exact backend name, or null to use the configured/default backend.
	 * @return Adapter descriptors or a diagnostic status. Safe while devices are initialized.
	 */
	[[nodiscard]] TArdaRHIResult<eastl::vector<FArdaAdapterInfo>> EnumerateAdapters(const char* BackendName = nullptr);

	/** @return True when a headless backend was initialized successfully. */
	[[nodiscard]] bool InitializeBackend();

	/** Outcome of the most recent headless initialization attempt. */
	[[nodiscard]] EArdaInitializeResult GetBackendInitializeResult() noexcept;

	/** Releases the process-wide backend and its device resources. */
	void ShutdownBackend() noexcept;

	/** @return True when the process-wide backend is initialized. */
	[[nodiscard]] bool IsBackendInitialized() noexcept;

	/** @return The current default RHI device, or an empty reference. */
	[[nodiscard]] arda::FArdaRHIDeviceRef GetDevice() noexcept;

	/** @return The device at its configured index, or an empty reference for an invalid index. */
	[[nodiscard]] FArdaRHIDeviceRef GetDevice(uint32_t DeviceIndex) noexcept;

	/** @return A snapshot of initialized devices and their adapters, in configured order. */
	[[nodiscard]] eastl::vector<FArdaBackendDevice> GetDevices();

	/**
	 * Changes the default returned by GetDevice. Does not migrate existing resources, graphs,
	 * or the swap chain, which remain bound to their original device. Serialize with device use.
	 * @return True if DeviceIndex names an initialized device; otherwise preserves the default.
	 */
	[[nodiscard]] bool SetDefaultDevice(uint32_t DeviceIndex);

	/** @return The most recent backend error message. */
	[[nodiscard]] eastl::string GetBackendError();

	/** @return The stable name of the backend module. */
	[[nodiscard]] const char* GetBackendModuleName() noexcept;
}
