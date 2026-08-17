/** @file ArdaExternalInterop.h
 *  @brief Declares cross-platform external device and native-resource interoperation.
 */
#pragma once

#include "ArdaDevice.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace arda::backend
{
    /** Names a backend-specific native object carried by an external device. */
    struct FArdaExternalNativeObject
    {
        /** Stable semantic name defined by the consuming backend module. */
        eastl::string mName;
        /** Opaque pointer or integer native handle. */
        FArdaNativeObject mObject;
    };

    /** Describes one queue supplied with a generic external device. */
    struct FArdaExternalQueueDesc
    {
        /** Arda queue role represented by this native queue. */
        rhi::EArdaRHIQueueType mType = rhi::EArdaRHIQueueType::Graphics;
        /** Opaque native queue handle or engine-RHI queue object. */
        FArdaNativeObject mQueue;
        /** Native queue-family identifier when the API uses families. */
        uint32_t mFamilyIndex = 0;
        /** Queue index within its family. */
        uint32_t mQueueIndex = 0;
    };

    /**
     * Backend-extensible description of a host-owned device.
     *
     * Standard roles use the explicit instance, adapter, device, and queue
     * fields. Engine integrations place objects such as FRHIDevice,
     * IDynamicRHI, Unity interfaces, or Godot RenderingDevice pointers in
     * mAdditionalObjects under names documented by their backend module.
     */
    struct FArdaExternalDeviceDesc
    {
        /** Stable backend module name expected to consume this descriptor. */
        eastl::string mBackendName;
        /** Native API or host-RHI name, such as d3d12, vulkan, or unreal-rhi. */
        eastl::string mNativeApi;
        /** Optional native instance or factory object. */
        FArdaNativeObject mInstance;
        /** Optional physical adapter or physical-device object. */
        FArdaNativeObject mAdapter;
        /** Required native logical device or host-RHI device object. */
        FArdaNativeObject mDevice;
        /** Queues exposed by the host. */
        eastl::vector<FArdaExternalQueueDesc> mQueues;
        /** Backend-specific named native objects. */
        eastl::vector<FArdaExternalNativeObject> mAdditionalObjects;
        /** Opaque immutable bytes interpreted only by the named backend module. */
        eastl::vector<uint8_t> mBackendData;
    };

    /**
     * Describes an externally owned Direct3D 12 device and its NVRHI configuration.
     * All native objects are opaque and non-owning. They must remain valid through backend
     * shutdown, either by host ownership or by the provider's shared lifetime token.
     */
    struct FArdaExternalD3D12DeviceDesc
    {
        /** Required opaque ID3D12Device pointer. */
        FArdaNativeObject mDevice;
        /** Required opaque graphics ID3D12CommandQueue pointer. */
        FArdaNativeObject mGraphicsQueue;
        /** Optional opaque compute ID3D12CommandQueue pointer. */
        FArdaNativeObject mComputeQueue;
        /** Optional opaque copy ID3D12CommandQueue pointer. */
        FArdaNativeObject mCopyQueue;
        /** Optional opaque IDXGIFactory pointer used by presentation implementations. */
        FArdaNativeObject mDxgiFactory;
        /** NVRHI render-target-view descriptor heap capacity. */
        uint32_t mRenderTargetViewHeapSize = 1024;
        /** NVRHI depth-stencil-view descriptor heap capacity. */
        uint32_t mDepthStencilViewHeapSize = 1024;
        /** NVRHI shader-resource-view descriptor heap capacity. */
        uint32_t mShaderResourceViewHeapSize = 16384;
        /** NVRHI sampler descriptor heap capacity. */
        uint32_t mSamplerHeapSize = 1024;
        /** NVRHI timer-query capacity. */
        uint32_t mMaxTimerQueries = 256;
        /** Enables directly indexed descriptor heaps when supported by the device. */
        bool mbEnableHeapDirectlyIndexed = false;
        /** Enables NVRHI Aftermath integration when configured by the host. */
        bool mbAftermathEnabled = false;
        /** Enables NVRHI buffer-lifetime diagnostics. */
        bool mbLogBufferLifetime = false;
        /** Enables NVAPI ray-tracing validation when available. */
        bool mbEnableRayTracingValidation = false;
        /** Enables D3D12 enhanced barriers when supported. */
        bool mbEnableEnhancedBarriers = true;
    };

    /**
     * Identifies an externally owned Vulkan queue.
     * The host must externally synchronize queue submissions and destruction with Arda use.
     */
    struct FArdaExternalVulkanQueueDesc
    {
        /** Opaque VkQueue value, or null for an optional queue. */
        FArdaNativeObject mQueue;
        /** Vulkan queue-family index containing the queue. */
        uint32_t mFamilyIndex = 0;
        /** Queue index within the family, retained for host diagnostics and validation. */
        uint32_t mQueueIndex = 0;
    };

    /**
     * Describes an externally owned Vulkan device for NVRHI.
     * Raw Vulkan objects are never destroyed by Arda. The host must keep the instance,
     * physical device, logical device, queues, allocation callbacks, and enabled-extension
     * contracts valid until backend shutdown, and must obey Vulkan host-synchronization rules.
     * The physical device and logical device must use Vulkan 1.3 or newer, and the logical
     * device must have dynamicRendering, synchronization2, and timelineSemaphore enabled.
     */
    struct FArdaExternalVulkanDeviceDesc
    {
        /** Required opaque VkInstance value. */
        FArdaNativeObject mInstance;
        /** Required opaque VkPhysicalDevice value. */
        FArdaNativeObject mPhysicalDevice;
        /** Required opaque VkDevice value. */
        FArdaNativeObject mDevice;
        /** Required graphics queue descriptor. */
        FArdaExternalVulkanQueueDesc mGraphicsQueue;
        /** Optional compute queue descriptor. */
        FArdaExternalVulkanQueueDesc mComputeQueue;
        /** Optional copy/transfer queue descriptor. */
        FArdaExternalVulkanQueueDesc mCopyQueue;
        /** Copied names of instance extensions enabled by the host. */
        eastl::vector<eastl::string> mInstanceExtensions;
        /** Copied names of device extensions enabled by the host. */
        eastl::vector<eastl::string> mDeviceExtensions;
        /** Optional opaque pointer to persistent VkAllocationCallbacks storage. */
        FArdaNativeObject mAllocationCallbacks;
        /** Whether bufferDeviceAddress was enabled when the host created the device. */
        bool mbBufferDeviceAddressSupported = false;
        /** Enables NVRHI Aftermath integration when configured by the host. */
        bool mbAftermathEnabled = false;
        /** Enables NVRHI buffer-lifetime diagnostics. */
        bool mbLogBufferLifetime = false;
        /** NVRHI timer-query capacity. */
        uint32_t mMaxTimerQueries = 256;
        /** Vulkan loader library name, or empty to use NVRHI's platform default. */
        eastl::string mVulkanLibraryName;
    };

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
        /** @return The graphics backend represented by this provider. */
        [[nodiscard]] virtual EArdaBackendType GetBackendType() const noexcept = 0;
        /**
         * @return Required module name for module-specific devices, or null when
         * any compatible module may consume the descriptor.
         */
        [[nodiscard]] virtual const char* GetBackendName() const noexcept { return nullptr; }
        /**
         * Copies a universal external-device descriptor when supported.
         * Backend modules should prefer this path for engine and custom RHI hosts.
         * @param OutDesc Receives a self-contained descriptor.
         * @return True when a descriptor was supplied.
         */
        [[nodiscard]] virtual bool GetExternalDeviceDesc(
            FArdaExternalDeviceDesc& OutDesc) const { return false; }
        /**
         * Copies the Direct3D 12 descriptor when supported.
         * @param OutDesc Receives a self-contained descriptor.
         * @return True when a valid Direct3D 12 descriptor was supplied.
         */
        [[nodiscard]] virtual bool GetD3D12DeviceDesc(
            FArdaExternalD3D12DeviceDesc& OutDesc) const { return false; }
        /**
         * Copies the Vulkan descriptor when supported.
         * @param OutDesc Receives a descriptor whose extension names are copied values.
         * @return True when a valid Vulkan descriptor was supplied.
         */
        [[nodiscard]] virtual bool GetVulkanDeviceDesc(
            FArdaExternalVulkanDeviceDesc& OutDesc) const { return false; }
        /**
         * Returns an optional token retaining the native device lifetime.
         * @return A shared token copied and retained through backend shutdown, or empty.
         */
        [[nodiscard]] virtual eastl::shared_ptr<void> GetLifetimeToken() const { return {}; }
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

    /**
     * Resolves stable external resource identifiers into native import descriptors.
     * Providers are host-owned. Resolve callbacks execute under the registry lock; they must
     * not reenter this registry, and the host must not concurrently unregister the provider.
     * Vulkan providers must also enforce raw-handle lifetime and host synchronization.
     */
    class IArdaExternalResourceProvider
    {
    public:
        /** Destroys the provider after the host has unregistered it. */
        virtual ~IArdaExternalResourceProvider() = default;
        /** @return Stable, non-empty registry name owned by the provider. */
        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
        /** @return Graphics backend required by the provider's native handles. */
        [[nodiscard]] virtual EArdaBackendType GetBackendType() const noexcept = 0;
        /**
         * @return Required module name for module-specific resources, or null
         * when any compatible module may consume the descriptors.
         */
        [[nodiscard]] virtual const char* GetBackendName() const noexcept { return nullptr; }
        /**
         * Resolves a stable texture identifier.
         * @param Id Provider-defined stable identifier.
         * @param OutDesc Receives the complete native texture import descriptor.
         * @return Success or a provider-specific failure status.
         */
        [[nodiscard]] virtual rhi::FArdaRHIStatus ResolveNativeTexture(
            uint64_t Id, rhi::FArdaRHINativeTextureImportDesc& OutDesc) = 0;
        /**
         * Resolves a stable buffer identifier.
         * @param Id Provider-defined stable identifier.
         * @param OutDesc Receives the complete native buffer import descriptor.
         * @return Success or a provider-specific failure status.
         */
        [[nodiscard]] virtual rhi::FArdaRHIStatus ResolveNativeBuffer(
            uint64_t Id, rhi::FArdaRHINativeBufferImportDesc& OutDesc) = 0;
    };

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
    [[nodiscard]] const IArdaExternalResourceProvider* GetExternalResourceProvider(
        const char* Name) noexcept;
    /**
     * Resolves and imports an external texture into the active RHI device.
     * @param ProviderName Stable registered provider name.
     * @param Id Provider-defined texture identifier.
     * @return Imported texture and status using normal RHI result conventions.
     */
    [[nodiscard]] rhi::TArdaRHIResult<rhi::FArdaRHITextureRef> ImportExternalTexture(
        const char* ProviderName, uint64_t Id);
    /**
     * Resolves and imports an external buffer into the active RHI device.
     * @param ProviderName Stable registered provider name.
     * @param Id Provider-defined buffer identifier.
     * @return Imported buffer and status using normal RHI result conventions.
     */
    [[nodiscard]] rhi::TArdaRHIResult<rhi::FArdaRHIBufferRef> ImportExternalBuffer(
        const char* ProviderName, uint64_t Id);
}
