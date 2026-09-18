#include <EASTL/string.h>
#include <EASTL/vector.h>
#include "RHI/Pipelines/ArdaRHIProviderPipelineCache.h"

#include "RHI/Resources/ArdaHash.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "FileOperations/ArdaFileOperations.h"

#include <cctype>
#include <fstream>

namespace arda
{
	namespace
	{
		constexpr uint64_t Magic = 0x45484341434F5350ull; // "PSOCACHE"
		constexpr uint32_t Schema = 4;

		struct FArdaBlobHeader
		{
			uint64_t mMagic = Magic;
			uint32_t mSchema = Schema;
			uint32_t mReserved = 0;
			uint64_t mBackendHash = 0;
			uint64_t mPayloadSize = 0;
		};

		static_assert(sizeof(FArdaBlobHeader) == 32);

		uint64_t StableNameHash(const eastl::string& Name) noexcept
		{
			uint64_t Hash = arda::ArdaFnv1a64OffsetBasis;
			AppendArdaFnv1a64(Hash, Name.data(), Name.size());
			return Hash;
		}

	}

	void LogArdaPipelineCacheMessage(arda::IArdaDiagnosticCallback* Callback,
	    arda::EArdaDiagnosticSeverity Severity,
	    const char* Text) noexcept
	{
		if (Callback)
		{
			Callback->Message(Severity, Text);
		}
	}

	std::filesystem::path MakeArdaPipelineCachePath(const std::filesystem::path& Directory,
	    const eastl::string& BackendName)
	{
		eastl::string Filename;
		Filename.reserve(BackendName.size() + 10);
		for (const unsigned char Character : BackendName)
		{
			Filename.push_back(
			    std::isalnum(Character) || Character == '-' || Character == '_' ? static_cast<char>(Character) : '_');
		}
		if (Filename.empty())
		{
			Filename = "unnamed-backend";
		}
		Filename += ".pso-cache";
		return Directory / Filename.c_str();
	}

	bool ReadArdaPipelineCacheBlob(const std::filesystem::path& Path,
	    const eastl::string& BackendName,
	    eastl::vector<uint8_t>& Payload)
	{
		Payload.clear();
		std::error_code Error;
		const uintmax_t FileSize = std::filesystem::file_size(Path, Error);
		if (Error || FileSize < sizeof(FArdaBlobHeader) ||
		    FileSize > sizeof(FArdaBlobHeader) + ArdaProviderPipelineCacheMaxPayloadSize)
		{
			return false;
		}

		std::ifstream Input(Path, std::ios::binary);
		FArdaBlobHeader Header;
		Input.read(reinterpret_cast<char*>(&Header), sizeof(Header));
		if (!Input || Header.mMagic != Magic || Header.mSchema != Schema || Header.mReserved != 0 ||
		    Header.mBackendHash != StableNameHash(BackendName) ||
		    Header.mPayloadSize > ArdaProviderPipelineCacheMaxPayloadSize ||
		    FileSize != sizeof(Header) + Header.mPayloadSize)
		{
			return false;
		}

		Payload.resize(static_cast<size_t>(Header.mPayloadSize));
		if (!Payload.empty())
		{
			Input.read(reinterpret_cast<char*>(Payload.data()), static_cast<std::streamsize>(Payload.size()));
			if (Input.gcount() != static_cast<std::streamsize>(Payload.size()))
			{
				Payload.clear();
				return false;
			}
		}
		return true;
	}

	bool WriteArdaPipelineCacheBlob(const std::filesystem::path& Path,
	    const eastl::string& BackendName,
	    const uint8_t* Payload, size_t PayloadSize)
	{
		if (PayloadSize > ArdaProviderPipelineCacheMaxPayloadSize || Path.empty() || (!Payload && PayloadSize))
		{
			return false;
		}

		std::error_code Error;
		std::filesystem::create_directories(Path.parent_path(), Error);
		if (Error)
		{
			return false;
		}

		const auto Temporary = fileops::TemporaryPath(Path, "pipeline-cache");
		const fileops::FArdaTemporaryFiles Cleanup{{Temporary}};
		{
			std::ofstream Output(Temporary, std::ios::binary | std::ios::trunc);
			FArdaBlobHeader Header;
			Header.mBackendHash = StableNameHash(BackendName);
			Header.mPayloadSize = PayloadSize;
			Output.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
			if (!(PayloadSize == 0))
			{
				Output.write(reinterpret_cast<const char*>(Payload),
				    static_cast<std::streamsize>(PayloadSize));
			}
			Output.close();
			if (!Output)
			{
				return false;
			}
		}

		return fileops::AtomicReplace(Temporary, Path);
	}
}
