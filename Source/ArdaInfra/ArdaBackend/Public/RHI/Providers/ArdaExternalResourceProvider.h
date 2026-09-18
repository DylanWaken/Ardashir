/** Host-owned resource resolution contract and named provider registry API. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Interop/ArdaRHINativeResourceImports.h"
#include <cstdint>

namespace arda
{
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

		/**
         * @return Exact registered backend module required by native handles.
         */
		[[nodiscard]] virtual const char* GetBackendName() const noexcept = 0;

		/**
         * Resolves a stable texture identifier.
         * @param Id Provider-defined stable identifier.
         * @param OutDesc Receives the complete native texture import descriptor.
         * @return Success or a provider-specific failure status.
         */
		[[nodiscard]] virtual arda::FArdaRHIStatus ResolveNativeTexture(uint64_t Id,
		    arda::FArdaRHINativeTextureImportDesc& OutDesc) = 0;

		/**
         * Resolves a stable buffer identifier.
         * @param Id Provider-defined stable identifier.
         * @param OutDesc Receives the complete native buffer import descriptor.
         * @return Success or a provider-specific failure status.
         */
		[[nodiscard]] virtual arda::FArdaRHIStatus ResolveNativeBuffer(uint64_t Id,
		    arda::FArdaRHINativeBufferImportDesc& OutDesc) = 0;
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
	[[nodiscard]] const IArdaExternalResourceProvider* GetExternalResourceProvider(const char* Name) noexcept;
}
