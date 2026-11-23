/** Stable kernel identities shared by graph archives, provider modules and build-time libraries. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <filesystem>

namespace arda
{
	// ================================
	// Kernel identity and binary format
	// ================================

	struct FArdaNativeKernelKey
	{
		eastl::string mLibrary;
		eastl::string mProvider;
		eastl::string mName;
		eastl::string mSignature;
	};

	/** The provider interprets the format, entry point and attributes. The catalog owns the bytes. */
	struct FArdaNativeKernelBinary
	{
		eastl::string mFormat;
		eastl::string mEntryPoint;
		eastl::vector<uint32_t> mAttributes;
		eastl::vector<uint8_t> mBytes;
	};

	// ================================
	// Kernel argument layout
	// ================================

	enum class EArdaNativeArgumentKind : uint8_t { Value, Buffer, Surface };

	struct FArdaNativeArgumentLayout
	{
		size_t mOffset = 0;
		size_t mSize = 0;
		size_t mAlignment = 1;
		EArdaNativeArgumentKind mKind = EArdaNativeArgumentKind::Value;
	};

	struct FArdaNativeKernelLayout
	{
		uint32_t mParameterSize = 0;
		uint32_t mParameterAlignment = 1;
		eastl::vector<FArdaNativeArgumentLayout> mArguments;
	};

	// ================================
	// Kernel library storage and lookup
	// ================================

	[[nodiscard]] bool IsValidArdaNativeKernelKey(const FArdaNativeKernelKey&);
	/** Copy a deployed sidecar or persist in-memory kernels when saving a graph to a new directory.
	 * Unavailable deployment references are preserved by the graph and may resolve on another device. */
	[[nodiscard]] FArdaRHIStatus StageArdaNativeKernelLibrary(const eastl::string& Library, const std::filesystem::path& Directory);
	/** Persist generated kernels as an indexed library beside a saved graph. */
	[[nodiscard]] FArdaRHIStatus SaveArdaNativeKernelLibrary(const eastl::string& Library, const std::filesystem::path& Directory);
	/** Add a deployment directory searched for <library>.ardakernels on a catalog miss. */
	void AddArdaNativeKernelDirectory(std::filesystem::path Directory);
	/** Load and validate the complete index before publishing any entries. */
	[[nodiscard]] FArdaRHIStatus LoadArdaNativeKernelLibrary(const std::filesystem::path& File);
	/** Load a build-packaged library shipped inside a provider module. */
	[[nodiscard]] FArdaRHIStatus LoadArdaNativeKernelLibrary(const uint8_t* Bytes, size_t Size);
	/** Publish an in-memory artifact, for application-generated kernels and legacy package importers. */
	[[nodiscard]] FArdaRHIStatus RegisterArdaNativeKernelBinary(const FArdaNativeKernelKey&, FArdaNativeKernelBinary);
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const FArdaNativeKernelBinary>> FindArdaNativeKernelBinary(
	    const FArdaNativeKernelKey&);
}
