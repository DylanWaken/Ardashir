/** @file ArdaExternalInterop.h
 *  @brief Declares cross-platform external device and native-resource interoperation.
 */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"
#include "RHI/Interop/ArdaNativeObject.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace arda
{
	/** Names a backend-specific native object carried by an external device. */
	struct FArdaExternalNativeObject
	{
		/** Stable semantic name defined by the consuming backend module. */
		eastl::string mName;
		/** Opaque pointer or integer native handle. */
		FArdaNativeObject mObject;
	};

	/** One copied, module-defined external-device property. */
	struct FArdaExternalDeviceProperty
	{
		/** Stable property name documented by the consuming backend module. */
		eastl::string mName;
		/** String value copied by Arda before the provider call returns. */
		eastl::string mValue;
	};

	/** Describes one queue supplied with a generic external device. */
	struct FArdaExternalQueueDesc
	{
		/** Arda queue role represented by this native queue. */
		arda::EArdaRHIQueueType mType = arda::EArdaRHIQueueType::Graphics;
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
     * native-vulkan requires instance, adapter, device and a graphics queue, plus
     * vulkan.api-version=1.3 (or 1.4). Report each enabled feature as a repeated
     * vulkan.enabled-feature property using its Vulkan member name; dynamicRendering,
     * synchronization2 and timelineSemaphore are required. Report enabled device
     * extensions as repeated vulkan.device-extension properties. Validation requests
     * require vulkan.validation=enabled; the host owns validation setup and diagnostics.
     * The host must serialize access to borrowed queues with Arda submission/presentation
     * and retain all raw handles until every Arda child and submission has retired.
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
		/** Backend-specific named values; repeated names are permitted. */
		eastl::vector<FArdaExternalDeviceProperty> mProperties;
		/** Opaque immutable bytes interpreted only by the named backend module. */
		eastl::vector<uint8_t> mBackendData;
	};

	/**
     * Resolves and imports an external texture into the active RHI device.
     * @param ProviderName Stable registered provider name.
     * @param Id Provider-defined texture identifier.
     * @return Imported texture and status using normal RHI result conventions.
     */
	[[nodiscard]] arda::TArdaRHIResult<arda::FArdaRHITextureRef> ImportExternalTexture(const char* ProviderName,
	    uint64_t Id);

	/**
     * Resolves and imports an external buffer into the active RHI device.
     * @param ProviderName Stable registered provider name.
     * @param Id Provider-defined buffer identifier.
     * @return Imported buffer and status using normal RHI result conventions.
     */
	[[nodiscard]] arda::TArdaRHIResult<arda::FArdaRHIBufferRef> ImportExternalBuffer(const char* ProviderName,
	    uint64_t Id);
}
