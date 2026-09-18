/** Backend selection, feature requirements and shader/cache policy. */
#pragma once

#include "RHI/Config/ArdaBackendDiagnostics.h"
#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Config/ArdaCudaConfig.h"
#include "RHI/Device/ArdaAdapter.h"
#include <EASTL/vector.h>
#include <filesystem>

namespace arda
{
	/**
     * Selects when registered shaders are compiled and loaded.
     *
     * The selected policy is fixed by ConfigureBackend before initialization
     * and applies to the active graphics backend only.
     */
	enum class EArdaShaderCompilationMode
	{
		/** Never invokes the compiler; initialized maps load deployed artifacts eagerly. */
		LoadOnly,
		/** Ensures all selected artifacts during backend startup and loads maps eagerly. */
		Startup,
		/** Defers compilation and RHI shader creation until a shader is first requested. */
		OnDemand
	};

	/** Selects whether Arda creates the native device or wraps one supplied externally. */
	enum class EArdaDeviceSource
	{
		/** Arda creates and owns the native graphics device and queues. */
		ArdaCreated,
		/** A registered external provider supplies non-owning native device handles. */
		ExternalProvider
	};

	/** Describes the outcome of backend initialization. */
	enum class EArdaInitializeResult
	{
		/** Initialization completed successfully. */
		Success,
		/** The requested backend is unavailable on this system. */
		Unavailable,
		/** Initialization failed for another reason. */
		Failure,
		/** Validation was required, but its native layer could not be enabled. */
		ValidationUnavailable
	};

	/** Desktop-GPU admission profile applied during backend initialization. */
	enum class EArdaRHIDeviceProfile : uint8_t
	{
		None,
		RayTracingInfrastructure,
		RealtimeRayTracing,
		RealtimeRayTracingAndML
	};

	/** Returns the feature requirements implied by a standard device profile. */
	[[nodiscard]] inline arda::FArdaRHIFeatureRequirements GetArdaRHIProfileRequirements(
	    EArdaRHIDeviceProfile Profile) noexcept
	{
		arda::FArdaRHIFeatureRequirements Result;
		if (Profile >= EArdaRHIDeviceProfile::RayTracingInfrastructure)
		{
			Result.mbRequireRayTracingInfrastructure = true;
			Result.mbRequireAccelerationStructures = true;
		}
		if (Profile >= EArdaRHIDeviceProfile::RealtimeRayTracing)
		{
			Result.mbRequireHardwareRayTracing = true;
			Result.mbRequireRayTracingPipelines = true;
			Result.mbRequireLocalShaderTableArguments = true;
			Result.mbRequireUnboundedDescriptors = true;
			Result.mbRequireGpuQueueWaits = true;
		}
		if (Profile >= EArdaRHIDeviceProfile::RealtimeRayTracingAndML)
		{
			Result.mbRequireNativeFloat16 = true;
			Result.mbRequireDedicatedComputeQueue = true;
		}
		return Result;
	}

	/** Configures backend selection, devices, shader policy, validation, and diagnostics. */
	struct FArdaBackendConfiguration
	{
		/**
         * Authoritative backend module name. Empty selects the highest-priority
         * registered module. Accepted configurations are normalized to a
         * non-empty exact module name.
         */
		eastl::string mBackendName;
		/** The source from which the native graphics device is obtained. */
		EArdaDeviceSource mDeviceSource = EArdaDeviceSource::ArdaCreated;
		/**
		 * Adapters to create, in device-index order. Empty selects one adapter automatically.
		 * Every entry must come from this backend's EnumerateAdapters result. Repeated IDs
		 * create separate RHI devices and queues on the same adapter. ExternalProvider requires
		 * an empty list because the host supplies its already-selected device.
		 */
		eastl::vector<FArdaAdapterId> mAdapters;
		/** Initial default device and presentation device index; zero for an empty adapter list. */
		uint32_t mDefaultDeviceIndex = 0;
		/** CUDA scheduling policy; fixed for the lifetime of this device and its mappings. */
		EArdaCudaExecutionMode mCudaExecutionMode = EArdaCudaExecutionMode::Automatic;
		/** Whether graphics API validation layers are enabled. */
		bool mbEnableValidation = true;
		/** Optional RT/ML-oriented desktop-GPU admission profile. */
		EArdaRHIDeviceProfile mRequiredDeviceProfile = EArdaRHIDeviceProfile::None;
		/** Additional module-specific abilities required during initialization. */
		arda::FArdaRHIFeatureRequirements mRequiredFeatures;
		/** Timing policy for registered shader compilation on the active backend. */
		EArdaShaderCompilationMode mShaderCompilationMode = EArdaShaderCompilationMode::OnDemand;

		/**
         * Persistent registered-shader artifact cache.
         *
         * Relative paths are resolved to stable absolute paths when the
         * configuration is accepted. The directory is created only if
         * compilation needs to publish an artifact.
         */
		std::filesystem::path mShaderCacheDirectory = std::filesystem::path(".arda-cache") / "shaders";

		/**
         * Persistent backend-native compiled pipeline cache.
         *
         * Blobs are backend, adapter, and driver specific. Relative paths are
         * resolved to absolute paths by ConfigureBackend. An empty path
         * explicitly disables pipeline-cache disk I/O.
         */
		std::filesystem::path mPipelineCacheDirectory = std::filesystem::path(".arda-cache") / "pipelines";
		/** Receives backend diagnostic messages, or null to use the default callback. */
		IArdaDiagnosticCallback* mMessageCallback = nullptr;
	};
}
