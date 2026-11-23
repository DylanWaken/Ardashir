/** @file ArdaBackend.h
 * Complete public entry point for backend configuration, devices and RHI calls.
 * Individual modules may be included directly to limit dependencies.
 */
#pragma once

#include "NativeCompute/ArdaNativeCompute.h"
#include "RHI/Config/ArdaAssert.h"
#include "RHI/Config/ArdaLog.h"
#include "RHI/Config/ArdaBackendConfiguration.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Scheduling/ArdaSwapChain.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Pipelines/ArdaRHIProviderPipelineCache.h"
#include "RHI/Pipelines/ArdaPipelineStateCache.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include "RHI/Shaders/ArdaShaderDirectories.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Resources/ArdaRHISubresources.h"
#include "RHI/Resources/ArdaRHITensor.h"
#include "RHI/Memory/ArdaMemoryPlanner.h"
#include "FileOperations/ArdaFileOperations.h"
#include "RHI/Providers/ArdaExternalDeviceProvider.h"
#include "RHI/Providers/ArdaExternalResourceProvider.h"

namespace arda
{
	// ================================
	// Backend configuration and adapter selection
	// ================================

	/** Entry returned by GetDevices(), pairing an initialized RHI device with its physical adapter. */
	struct FArdaBackendDeviceReg
	{
		/** Actual adapter selected, including when selection was automatic or host-owned. */
		FArdaAdapterInfo mAdapter;
		/** Device to pass to a graph, shader map, or resource factory. */
		FArdaRHIDeviceRef mDevice;
		/** Feature values registered by the backend for this device. */
		FArdaBackendFeatureListRef mFeatures;
	};

	/**
     * Replaces the backend configuration before initialization.
     * Configuration and lifetime calls must not run concurrently with device use.
     * @param Configuration The complete configuration to apply.
     * @return True when the configuration was accepted.
     */
	[[nodiscard]] bool ConfigureBackend(const FArdaBackendConfiguration& Configuration);

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

	// ================================
	// Initialization and shutdown
	// ================================

	/** @return True when a headless backend was initialized successfully. */
	[[nodiscard]] bool InitializeBackend();

	/**
     * Initializes the backend and a swap chain for a window surface.
     * @param WindowSurface Platform adapter used to create the native surface.
     * @param Width Initial presentation width in pixels.
     * @param Height Initial presentation height in pixels.
     * @param OutSwapChain Receives the new swap chain on success. Destroy it before shutdown.
     * @return The backend initialization outcome.
     */
	[[nodiscard]] EArdaInitializeResult InitializeBackendForPresentation(IArdaWindowSurface& WindowSurface,
	    uint32_t Width,
	    uint32_t Height,
	    eastl::unique_ptr<IArdaSwapChain>& OutSwapChain);

	/** Outcome of the most recent headless initialization attempt. */
	[[nodiscard]] EArdaInitializeResult GetBackendInitializeResult() noexcept;

	/** Releases the process-wide backend and its device resources. */
	void ShutdownBackend() noexcept;

	/** @return True when the process-wide backend is initialized. */
	[[nodiscard]] bool IsBackendInitialized() noexcept;

	// ================================
	// Device access and default selection
	// ================================

	/** @return The current default RHI device, or an empty reference. */
	[[nodiscard]] arda::FArdaRHIDeviceRef GetDevice() noexcept;

	/** @return The device at its configured index, or an empty reference for an invalid index. */
	[[nodiscard]] FArdaRHIDeviceRef GetDevice(uint32_t DeviceIndex) noexcept;

	/** @return A snapshot of initialized devices and their adapters, in configured order. */
	[[nodiscard]] eastl::vector<FArdaBackendDeviceReg> GetDevices();

	/** Returns the default device's immutable Arda feature list, or null before initialization or after shutdown. */
	[[nodiscard]] FArdaBackendFeatureListRef GetBackendFeatures() noexcept;

	/** Returns the Arda feature list for one configured device, or null for an invalid index. */
	[[nodiscard]] FArdaBackendFeatureListRef GetBackendFeatures(uint32_t DeviceIndex) noexcept;

	/**
	 * Changes the default returned by GetDevice. Does not migrate existing resources, graphs,
	 * or the swap chain, which remain bound to their original device. Serialize with device use.
	 * @return True if DeviceIndex names an initialized device; otherwise preserves the default.
	 */
	[[nodiscard]] bool SetDefaultDevice(uint32_t DeviceIndex);

	// ================================
	// Backend diagnostics and identity
	// ================================

	/** @return The most recent backend error message. */
	[[nodiscard]] eastl::string GetBackendError();

	/** @return The stable name of the backend module. */
	[[nodiscard]] const char* GetBackendModuleName() noexcept;

	// ================================
	// Graphics backend modules and shader targets
	// ================================

	/**
     * Registers a linkable backend module by its stable descriptor name.
     * Re-registering the same object is idempotent; name collisions are rejected.
     * @param Module Module that remains alive until it is unregistered.
     * @return True when the module is registered.
     */
	[[nodiscard]] bool RegisterBackendModule(IArdaBackendModule& Module);

	/**
     * Removes an inactive backend module from the process registry.
     * @param Module Exact module object previously registered.
     * @return True when absent or successfully removed.
     */
	[[nodiscard]] bool UnregisterBackendModule(IArdaBackendModule& Module);

	/**
     * Finds a registered backend by stable name.
     * @param Name Stable module name.
     * @return Non-owning module pointer, or null.
     */
	[[nodiscard]] IArdaBackendModule* FindBackendModule(const char* Name) noexcept;

	/** @return The highest-priority registered backend module, or null. */
	[[nodiscard]] IArdaBackendModule* FindDefaultBackendModule() noexcept;

	/** Resolves a registered module into an immutable shader target. */
	[[nodiscard]] bool ResolveShaderTarget(const char* BackendName, FArdaShaderTarget& OutTarget) noexcept;

	/** Resolves the highest-priority registered module into a shader target. */
	[[nodiscard]] bool ResolveDefaultShaderTarget(FArdaShaderTarget& OutTarget) noexcept;

	/** @return Copies of all registered descriptors in stable name order. */
	[[nodiscard]] eastl::vector<FArdaBackendModuleDescriptor> EnumerateBackendModules();

	/** @return Selected module after initialization, or null. */
	[[nodiscard]] const IArdaBackendModule* GetActiveBackendModule() noexcept;

	// ================================
	// External device providers
	// ================================

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

	// ================================
	// External resource providers
	// ================================

	/**
     * Registers an external resource provider by its stable name.
     * The same object/name registration is idempotent; name collisions are rejected.
     * @param Provider Host-owned provider that remains valid until unregistered.
     * @return True when the provider is registered; inspect GetBackendError on failure.
     */
	[[nodiscard]] bool RegisterExternalResourceProvider(IArdaExternalResourceProvider& Provider);

	/**
     * Unregisters an external resource provider.
     * @param Provider Exact provider object to unregister.
     * @return True when absent or removed; false on a name collision/mismatch.
     */
	[[nodiscard]] bool UnregisterExternalResourceProvider(IArdaExternalResourceProvider& Provider);

	/**
     * Looks up a resource provider by stable name.
     * The pointer is non-owning and must not race provider unregistration.
     * @param Name Null-terminated provider name.
     * @return Matching provider, or null.
     */
	[[nodiscard]] const IArdaExternalResourceProvider* GetExternalResourceProvider(const char* Name) noexcept;
}
