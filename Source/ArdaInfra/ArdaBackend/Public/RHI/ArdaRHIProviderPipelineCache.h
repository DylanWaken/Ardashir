/** @file RHI/ArdaRHIProviderPipelineCache.h
 * Shared provider-contract persistence helpers implemented by ArdaBackend.
 */
#pragma once

#include "ArdaBackend.h"

#include <EASTL/string.h>

#include <filesystem>
#include <vector>

namespace arda
{
	class IArdaDiagnosticCallback;

	/** Maximum admitted native pipeline-cache payload size, in bytes. */
	inline constexpr uint64_t ArdaProviderPipelineCacheMaxPayloadSize = 256ull * 1024ull * 1024ull;

	/** Sends a pipeline-cache diagnostic when a callback is present.
     * @return No value (void).
     * @ownership Callback and Text are borrowed for this call only.
     * @errors A null callback is a no-op. The callback must not throw.
     * @threading The callback runs synchronously on the calling thread.
     */
	void LogArdaPipelineCacheMessage(arda::IArdaDiagnosticCallback* Callback,
	    arda::EArdaDiagnosticSeverity Severity,
	    const char* Text) noexcept;

	/** Builds a backend-specific cache filename within Directory without filesystem I/O.
     * @return An owning path with a sanitized backend name and .pso-cache suffix.
     * @ownership Inputs are borrowed; the returned path owns its storage.
     * @errors Allocation and path-construction exceptions propagate; no status object is returned.
     * @threading Independent calls may run concurrently.
     */
	[[nodiscard]] std::filesystem::path MakeArdaPipelineCachePath(const std::filesystem::path& Directory,
	    const eastl::string& BackendName);

	/** Reads a native cache payload after validating its header, backend identity and size.
     * @return True only when the complete validated payload was read.
     * @ownership Payload is caller-owned and overwritten; disregard its contents on failure.
     * @errors Returns false for missing, malformed, oversized or unreadable files. Allocation exceptions propagate.
     * @threading Serialize access to Payload; each call owns its file stream.
     */
	[[nodiscard]] bool ReadArdaPipelineCacheBlob(const std::filesystem::path& Path,
	    const eastl::string& BackendName,
	    std::vector<uint8_t>& Payload);

	/** Writes and flushes a temporary cache file, then replaces the destination.
     * @return True only when the complete cache file replaced the destination.
     * @ownership All inputs are borrowed for this call; temporary-file cleanup is internal.
     * @errors Returns false for empty paths, oversized payloads or filesystem failures. Allocation exceptions propagate.
     * @threading Temporary paths are unique; concurrent writers to one destination use last-successful-replacement semantics.
     */
	[[nodiscard]] bool WriteArdaPipelineCacheBlob(const std::filesystem::path& Path,
	    const eastl::string& BackendName,
	    const std::vector<uint8_t>& Payload);
}
