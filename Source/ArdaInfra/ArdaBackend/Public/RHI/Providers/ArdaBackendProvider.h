/** @file ArdaBackendProvider.h
 *  @brief Declares the provider contract implemented by linkable backend modules.
 */
#pragma once

#include "RHI/Config/ArdaBackendConfiguration.h"
#include "RHI/Interop/ArdaNativeObject.h"
#include "RHI/Scheduling/ArdaSwapChain.h"
#include "RHI/Shaders/ArdaShaderTarget.h"
#include "RHI/Providers/ArdaRHIProviderDevice.h"

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <cstdint>
#include <filesystem>

namespace arda
{
	class IArdaExternalDeviceProvider;

	// ================================
	// Backend module identity
	// ================================

	/** C++ provider contract version required by this ArdaBackend build. */
	inline constexpr uint32_t ArdaBackendProviderInterfaceVersion = 17;

	/** Describes one backend implementation available to ArdaBackend. */
	struct FArdaBackendModuleDescriptor
	{
		/** Provider contract version used to compile the module. */
		uint32_t mInterfaceVersion = ArdaBackendProviderInterfaceVersion;
		/** Stable registry key, such as "native-vulkan" or "unreal-rhi". */
		eastl::string mName;
		/** Human-readable module name used by diagnostics and tools. */
		eastl::string mDisplayName;
		/** Shader bytecode family accepted by the module. */
		EArdaShaderBinaryFormat mShaderBinaryFormat = EArdaShaderBinaryFormat::BackendDefined;
		/** Artifact suffix, including the leading period. */
		eastl::string mShaderArtifactExtension;
		/** Stable cache identity for an in-process or engine-owned shader compiler. */
		eastl::string mShaderCompilerIdentity;
		/** Whether the module can create and own a native device. */
		bool mbSupportsOwnedDevice = false;
		/** Whether the module can adopt a device supplied by an external provider. */
		bool mbSupportsExternalDevice = false;
		/** Default-selection priority among all registered backend modules. */
		int32_t mPriority = 0;
	};

	// ================================
	// Backend runtime and presentation
	// ================================

	/** Result of creating presentation resources for an initialized backend device. */
	struct FArdaSwapChainCreateResult
	{
		/** Created swap chain, or empty on failure. */
		eastl::unique_ptr<IArdaSwapChain> mSwapChain;
		/** Failure diagnostic. Empty on success. */
		eastl::string mError;

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return mSwapChain != nullptr;
		}
	};

	/** Owns the module-private runtime state that accompanies an RHI device. */
	class IArdaBackendRuntime
	{
	public:
		/** Destroys the runtime after Arda has released its RHI and swap-chain references. */
		virtual ~IArdaBackendRuntime() = default;

		/**
         * Creates presentation resources for an initialized presentation device.
         * @param Width Initial width in pixels.
         * @param Height Initial height in pixels.
         * @param Device Core-owned RHI facade used to import presentation images.
         * @return Swap-chain result containing either the resource or its error.
         */
		[[nodiscard]] virtual FArdaSwapChainCreateResult CreateSwapChain(uint32_t Width,
		    uint32_t Height,
		    arda::FArdaRHIDeviceRef Device) = 0;
	};

	// ================================
	// Device creation result
	// ================================

	/** Result of atomically creating and initializing a backend device. */
	struct FArdaBackendDeviceCreateResult
	{
		/** Initialization outcome. */
		EArdaInitializeResult mResult = EArdaInitializeResult::Failure;
		/** Actual physical adapter used by this device; required on success. */
		FArdaAdapterInfo mAdapter;
		/** Module-private runtime and lifetime owner. Empty unless initialization succeeded. */
		eastl::unique_ptr<IArdaBackendRuntime> mBackendRuntime;
		/** Provider implementation wrapped by ArdaBackend's concrete RHI device. */
		eastl::shared_ptr<arda::IArdaRHIProviderDevice> mProviderDevice;
		/** Failure diagnostic. Empty on success. */
		eastl::string mError;

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return mResult == EArdaInitializeResult::Success && mBackendRuntime != nullptr &&
			    mProviderDevice != nullptr;
		}
	};

	// ================================
	// Backend module interface
	// ================================

	/** Contract implemented by every linkable Arda backend module. */
	class IArdaBackendModule
	{
	public:
		/** Destroys the module only after it has been unregistered and is no longer active. */
		virtual ~IArdaBackendModule() = default;

		/** @return Module-owned descriptor, immutable while the module is registered. */
		[[nodiscard]] virtual const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept = 0;

		/**
		 * Enumerates adapter identities without changing any live device's dispatch or state.
		 * External-only modules may return Unsupported. Owned-device modules must honor these
		 * identities during creation and report the actual adapter in the creation result.
		 */
		[[nodiscard]] virtual TArdaRHIResult<eastl::vector<FArdaAdapterInfo>> EnumerateAdapters() const = 0;

		/**
         * Creates and initializes a device as one operation.
         * @param Configuration Process configuration selected for this module.
		 * @param Adapter One requested adapter, or an empty identity for automatic/host selection.
         * @param WindowSurface Optional presentation surface.
         * @param ExternalProvider Provider selected for ExternalProvider mode, or null.
         * @return Initialized device result.
         */
		[[nodiscard]] virtual FArdaBackendDeviceCreateResult CreateDevice(
		    const FArdaBackendConfiguration& Configuration,
		    const FArdaAdapterId& Adapter,
		    IArdaWindowSurface* WindowSurface,
		    const IArdaExternalDeviceProvider* ExternalProvider) = 0;

		// ================================
		// Shader compilation
		// ================================

		/**
         * Allows a module to validate or replace the fallback compiler command.
         * BackendDefined shader formats must make the invocation usable by this
         * method and/or handle it through InvokeShaderCompiler. DXIL and SPIR-V
         * modules may retain the core's DXC-compatible command.
         * @param Invocation Mutable, self-contained compiler invocation.
         * @return Status describing whether the invocation can proceed.
         */
		[[nodiscard]] virtual arda::FArdaRHIStatus ConfigureShaderCompileInvocation(
		    FArdaBackendShaderCompileInvocation& Invocation) const = 0;

		/**
         * Optionally executes a compiler invocation inside the backend module.
         * This supports engine shader services and compilers whose process ABI
         * cannot be represented by the core's DXC-compatible fallback launcher.
         * @param Invocation Invocation whose output path is a temporary artifact.
         * @param OutDiagnostics Receives compiler output on failure.
         * @return Whether the module declined, succeeded, or failed.
         */
		[[nodiscard]] virtual EArdaBackendShaderCompileResult InvokeShaderCompiler(
		    const FArdaBackendShaderCompileInvocation& Invocation,
		    eastl::string& OutDiagnostics) const
		{
			static_cast<void>(Invocation);
			OutDiagnostics.clear();
			return EArdaBackendShaderCompileResult::NotHandled;
		}
	};
}
