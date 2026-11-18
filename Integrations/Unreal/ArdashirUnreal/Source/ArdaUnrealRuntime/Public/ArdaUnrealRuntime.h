#pragma once

#include "CoreMinimal.h"

#include "RHI/Device/ArdaRHIDevice.h"

namespace arda
{
	/** Initializes Arda with Unreal's existing RHI device. Call on the render thread, after RHI startup. */
	[[nodiscard]] ARDAUNREALRUNTIME_API bool InitializeUnrealBackend();

	/** Releases Arda's host-device wrapper. First retire all work and release application resource references. */
	ARDAUNREALRUNTIME_API void ShutdownUnrealBackend() noexcept;

	/** Returns the shared Arda device owned by the runtime module, or an empty reference before initialization. */
	[[nodiscard]] ARDAUNREALRUNTIME_API FArdaRHIDeviceRef GetUnrealBackendDevice() noexcept;

	/** Returns the most recent Arda initialization or backend error as an Unreal string. */
	[[nodiscard]] ARDAUNREALRUNTIME_API FString GetUnrealBackendError();
}
