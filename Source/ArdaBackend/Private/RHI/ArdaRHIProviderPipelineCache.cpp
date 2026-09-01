#include "RHI/ArdaRHIProviderPipelineCache.h"

#include "ArdaHash.h"
#include "ArdaBackendProvider.h"

#include <atomic>
#include <cctype>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace arda::rhi::provider::pipeline_cache
{
    namespace
    {
        constexpr uint64_t Magic = 0x45484341434F5350ull; // "PSOCACHE"
        constexpr uint32_t Schema = 4;
        std::atomic_uint64_t TempCounter{ 0 };

        struct FBlobHeader
        {
            uint64_t mMagic = Magic;
            uint32_t mSchema = Schema;
            uint32_t mReserved = 0;
            uint64_t mBackendHash = 0;
            uint64_t mPayloadSize = 0;
        };
        static_assert(sizeof(FBlobHeader) == 32);

        uint64_t StableNameHash(const eastl::string& Name) noexcept
        {
            uint64_t Hash = private_api::ArdaFnv1a64OffsetBasis;
            private_api::AppendFnv1a64(Hash, Name.data(), Name.size());
            return Hash;
        }

        uint64_t ProcessId() noexcept
        {
#if defined(_WIN32)
            return static_cast<uint64_t>(_getpid());
#else
            return static_cast<uint64_t>(getpid());
#endif
        }
    }

    void Message(
        backend::IArdaDiagnosticCallback* Callback,
        backend::EArdaDiagnosticSeverity Severity,
        const char* Text) noexcept
    {
        if (Callback)
            Callback->Message(Severity, Text);
    }

    std::filesystem::path MakePath(
        const std::filesystem::path& Directory,
        const eastl::string& BackendName)
    {
        std::string Filename;
        Filename.reserve(BackendName.size() + 10);
        for (const unsigned char Character : BackendName)
        {
            Filename.push_back(
                std::isalnum(Character) || Character == '-' || Character == '_'
                    ? static_cast<char>(Character)
                    : '_');
        }
        if (Filename.empty())
            Filename = "unnamed-backend";
        Filename += ".pso-cache";
        return Directory / Filename;
    }

    bool ReadBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        std::vector<uint8_t>& Payload)
    {
        Payload.clear();
        std::error_code Error;
        const uintmax_t FileSize = std::filesystem::file_size(Path, Error);
        if (Error || FileSize < sizeof(FBlobHeader) ||
            FileSize > sizeof(FBlobHeader) + MaxPayloadSize)
            return false;

        std::ifstream Input(Path, std::ios::binary);
        FBlobHeader Header;
        Input.read(reinterpret_cast<char*>(&Header), sizeof(Header));
        if (!Input || Header.mMagic != Magic || Header.mSchema != Schema ||
            Header.mReserved != 0 ||
            Header.mBackendHash != StableNameHash(BackendName) ||
            Header.mPayloadSize > MaxPayloadSize ||
            FileSize != sizeof(Header) + Header.mPayloadSize)
            return false;

        Payload.resize(static_cast<size_t>(Header.mPayloadSize));
        if (!Payload.empty())
        {
            Input.read(reinterpret_cast<char*>(Payload.data()),
                static_cast<std::streamsize>(Payload.size()));
            if (Input.gcount() != static_cast<std::streamsize>(Payload.size()))
            {
                Payload.clear();
                return false;
            }
        }
        return true;
    }

    bool WriteBlob(
        const std::filesystem::path& Path,
        const eastl::string& BackendName,
        const std::vector<uint8_t>& Payload)
    {
        if (Payload.size() > MaxPayloadSize || Path.empty())
            return false;

        std::error_code Error;
        std::filesystem::create_directories(Path.parent_path(), Error);
        if (Error)
            return false;

        std::filesystem::path Temporary = Path;
        Temporary += ".tmp-" + std::to_string(ProcessId()) + "-" +
            std::to_string(TempCounter.fetch_add(1, std::memory_order_relaxed));
        {
            std::ofstream Output(Temporary, std::ios::binary | std::ios::trunc);
            FBlobHeader Header;
            Header.mBackendHash = StableNameHash(BackendName);
            Header.mPayloadSize = Payload.size();
            Output.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
            if (!Payload.empty())
                Output.write(reinterpret_cast<const char*>(Payload.data()),
                    static_cast<std::streamsize>(Payload.size()));
            Output.flush();
            if (!Output)
            {
                Output.close();
                std::filesystem::remove(Temporary, Error);
                return false;
            }
        }

#if defined(_WIN32)
        if (!MoveFileExW(Temporary.c_str(), Path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(Temporary, Error);
            return false;
        }
#else
        std::filesystem::rename(Temporary, Path, Error);
        if (Error)
        {
            std::filesystem::remove(Temporary, Error);
            return false;
        }
#endif
        return true;
    }
}
