/** @file ArdaRHIDiagnostics.h
 * Declares bounded, backend-neutral device and GPU fault snapshots.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Scheduling/ArdaRHIQueueTypes.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <cstdint>

namespace arda
{
	/** Maximum entries retained in each diagnostic list; excess native records are reported as truncated. */
	inline constexpr uint32_t ArdaRHIMaxDiagnosticEntries = 64;

	/** Queue progress observed without submitting or waiting for GPU work. */
	struct FArdaRHIQueueDiagnostic
	{
		/** Queue whose submission counters are reported. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		/** Most recently submitted native queue timeline/fence value, zero before any work. */
		uint64_t mLastSubmitted = 0;
		/** Last observed completed value; meaningful only when mbCompletedValueKnown is true. */
		uint64_t mLastCompleted = 0;
		/** False when native completion could not be queried, including a removed device. */
		bool mbCompletedValueKnown = false;
	};

	/** One native page-fault/address record; interpretation is described by the backend. */
	struct FArdaRHIFaultDiagnostic
	{
		/** Native GPU virtual address, or zero for a fault without an address. */
		uint64_t mAddress = 0;
		/** Native fault category, address precision or object description. */
		eastl::string mDescription;
	};

	/**
	 * Best-effort diagnostic evidence with bounded owned strings and lists.
	 * Capturing a snapshot performs no GPU submission or wait and does not recreate the device.
	 * Unsupported native crash facilities leave mbNativeFaultDataAvailable false; that is not a clean-health proof.
	 * Queue counters and markers are observations, not synchronization primitives or a coherent global GPU stop.
	 */
	struct FArdaRHIDiagnosticSnapshot
	{
		/** Stable provider name such as native-d3d12 or native-vulkan. */
		eastl::string mBackendName;
		/** Native adapter description. */
		eastl::string mAdapterName;
		/** Native driver version when available, otherwise empty. */
		eastl::string mDriverVersion;
		/** Best-effort device status; Success means no reported error at capture time. */
		EArdaRHIResult mDeviceStatus = EArdaRHIResult::Success;
		/** Provider-specific HRESULT or VkResult associated with the observed failure. */
		int64_t mNativeErrorCode = 0;
		/** Whether native DRED/device-fault data was obtained, independent of marker availability. */
		bool mbNativeFaultDataAvailable = false;
		/** True if an entry limit or bounded string length omitted diagnostic data. */
		bool mbTruncated = false;
		/** Native progress for supported queues. */
		eastl::vector<FArdaRHIQueueDiagnostic> mQueues;
		/** Recent submitted markers and available native crash breadcrumbs; descriptions identify their provenance. */
		eastl::vector<eastl::string> mBreadcrumbs;
		/** Native address faults when the driver exposes them. */
		eastl::vector<FArdaRHIFaultDiagnostic> mFaults;
		/** Native diagnostic details and unavailable-data explanations. */
		eastl::vector<eastl::string> mMessages;
	};
}
