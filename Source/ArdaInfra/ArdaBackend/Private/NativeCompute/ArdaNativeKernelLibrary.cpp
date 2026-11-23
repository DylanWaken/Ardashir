#include "NativeCompute/ArdaNativeKernelLibrary.h"

#include "FileOperations/ArdaFileOperations.h"
#include <algorithm>
#include <array>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace arda
{
	namespace
	{
		using FArdaIndexKey = std::array<std::string, 4>;
		using FArdaIndex = std::map<FArdaIndexKey, eastl::shared_ptr<const FArdaNativeKernelBinary>>;

		struct FArdaKernelCatalog
		{
			std::mutex mMutex;
			FArdaIndex mEntries;
			std::vector<std::filesystem::path> mDirectories;

			FArdaKernelCatalog()
			{
#if defined(_WIN32)
				std::wstring Path(32768, L'\0');
				const auto Length = GetModuleFileNameW(nullptr, Path.data(), static_cast<DWORD>(Path.size()));
				if (Length && Length < Path.size())
				{
					Path.resize(Length);
					mDirectories.push_back(std::filesystem::path(Path).parent_path());
				}
#elif defined(__linux__)
				std::error_code Error;
				const auto Path = std::filesystem::read_symlink("/proc/self/exe", Error);
				if (!Error) mDirectories.push_back(Path.parent_path());
#endif
				mDirectories.emplace_back(".");
			}
		};

		FArdaKernelCatalog& Catalog()
		{
			static FArdaKernelCatalog Instance;
			return Instance;
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}


		FArdaIndexKey IndexKey(const FArdaNativeKernelKey& Key)
		{
			return {Key.mLibrary.c_str(), Key.mProvider.c_str(), Key.mName.c_str(), Key.mSignature.c_str()};
		}

		FArdaRHIStatus Publish(const FArdaIndex& Entries)
		{
			auto& State = Catalog();
			std::lock_guard Lock(State.mMutex);
			for (const auto& [Key, Binary] : Entries)
			{
				const auto Existing = State.mEntries.find(Key);
				if (Existing == State.mEntries.end()) continue;
				const auto& Other = *Existing->second;
				if (Other.mFormat != Binary->mFormat || Other.mEntryPoint != Binary->mEntryPoint ||
				    Other.mAttributes != Binary->mAttributes || Other.mBytes != Binary->mBytes)
					return Invalid("A different kernel already owns this library/provider/name/signature.");
			}
			State.mEntries.insert(Entries.begin(), Entries.end());
			return {};
		}

		struct FArdaIndexReader
		{
			const uint8_t* mFile;
			size_t mPosition;
			size_t mEnd;
			bool mbValid = true;

			uint32_t Word()
			{
				if (mEnd - mPosition < 4) { mbValid = false; return 0; }
				uint32_t Value = 0;
				for (unsigned Shift = 0; Shift < 32; Shift += 8)
					Value |= uint32_t(mFile[mPosition++]) << Shift;
				return Value;
			}
			eastl::string String()
			{
				const auto Size = Word();
				if (!Size || Size > mEnd - mPosition) { mbValid = false; return {}; }
				const char* Begin = reinterpret_cast<const char*>(mFile + mPosition);
				mPosition += Size;
				if (std::find(Begin, Begin + Size, '\0') != Begin + Size) mbValid = false;
				return {Begin, Begin + Size};
			}
		};
	}

	bool IsValidArdaNativeKernelKey(const FArdaNativeKernelKey& Key)
	{
		return !Key.mLibrary.empty() && Key.mLibrary != "." && Key.mLibrary != ".." &&
		    Key.mLibrary.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") ==
		        eastl::string::npos &&
		    !Key.mProvider.empty() && !Key.mName.empty() && !Key.mSignature.empty() &&
		    Key.mProvider.find('\0') == eastl::string::npos && Key.mName.find('\0') == eastl::string::npos &&
		    Key.mSignature.find('\0') == eastl::string::npos;
	}

	void AddArdaNativeKernelDirectory(std::filesystem::path Directory)
	{
		auto& State = Catalog();
		std::lock_guard Lock(State.mMutex);
		if (std::find(State.mDirectories.begin(), State.mDirectories.end(), Directory) == State.mDirectories.end())
			State.mDirectories.push_back(std::move(Directory));
	}

	FArdaRHIStatus RegisterArdaNativeKernelBinary(const FArdaNativeKernelKey& Key, FArdaNativeKernelBinary Binary)
	{
		if (!IsValidArdaNativeKernelKey(Key) || Binary.mFormat.empty() || Binary.mFormat.find('\0') != eastl::string::npos ||
		    Binary.mEntryPoint.empty() || Binary.mEntryPoint.find('\0') != eastl::string::npos || Binary.mBytes.empty())
			return Invalid("A kernel needs a complete key, format, entry point and payload.");
		return Publish({{IndexKey(Key), eastl::make_shared<const FArdaNativeKernelBinary>(eastl::move(Binary))}});
	}

	static FArdaRHIStatus InstallKernelLibrary(const std::filesystem::path& File, const eastl::string& Data)
	{
		const auto Directory = File.parent_path();
		std::error_code Error;
		if (!Directory.empty()) std::filesystem::create_directories(Directory, Error);
		if (Error) return Invalid("Cannot create the native kernel directory.");
		// Atomic installation without replacing another writer's immutable library.
		const auto Temporary = fileops::TemporaryPath(File, "kernels");
		fileops::FArdaTemporaryFiles Cleanup{{Temporary}};
		if (!fileops::AtomicWrite(Temporary, Data)) return Invalid("Cannot stage native kernel library.");
#if defined(_WIN32)
		if (MoveFileW(Temporary.c_str(), File.c_str())) return {};
#else
		std::filesystem::create_hard_link(Temporary, File, Error);
		if (!Error) return {};
#endif
		eastl::string Existing;
		if (fileops::ReadBinaryFile(File, Existing, INT32_MAX) == fileops::EArdaFileReadResult::Success && Existing == Data)
			return {};
		return Invalid("Cannot install native kernel library or a conflicting file already exists.");
	}

	FArdaRHIStatus SaveArdaNativeKernelLibrary(const eastl::string& Library, const std::filesystem::path& Directory)
	{
		if (!IsValidArdaNativeKernelKey({Library, "provider", "kernel", "abi"})) return Invalid("Invalid native library name.");
		FArdaIndex Entries;
		{
			auto& State = Catalog();
			std::lock_guard Lock(State.mMutex);
			auto It = State.mEntries.lower_bound({Library.c_str(), {}, {}, {}});
			for (; It != State.mEntries.end() && It->first[0] == Library.c_str(); ++It) Entries.insert(*It);
		}
		if (Entries.empty()) return Invalid("No indexed artifacts found for the native kernel library.");

		uint64_t IndexSize = 20 + Library.size(), PayloadSize = 0;
		for (const auto& [Key, Binary] : Entries)
		{
			IndexSize += 32 + Key[1].size() + Key[2].size() + Key[3].size() +
			    Binary->mFormat.size() + Binary->mEntryPoint.size() + 4ull * Binary->mAttributes.size();
			PayloadSize += Binary->mBytes.size();
		}
		if (IndexSize + PayloadSize > INT32_MAX) return Invalid("Native kernel library exceeds the size limit.");
		eastl::string Data("ARDANK01");
		Data.reserve(static_cast<size_t>(IndexSize + PayloadSize));
		auto Word = [&](uint32_t Value)
		{
			for (unsigned Shift = 0; Shift < 32; Shift += 8) Data.push_back(static_cast<char>(Value >> Shift));
		};
		auto String = [&](const auto& Text)
		{
			Word(static_cast<uint32_t>(Text.size()));
			Data.append(Text.data(), Text.size());
		};
		uint32_t Offset = static_cast<uint32_t>(IndexSize);
		Word(Offset);
		Word(static_cast<uint32_t>(Entries.size()));
		String(Library);
		for (const auto& [Key, Binary] : Entries)
		{
			String(Key[1]); String(Key[2]); String(Key[3]);
			String(Binary->mFormat); String(Binary->mEntryPoint);
			Word(static_cast<uint32_t>(Binary->mAttributes.size()));
			for (auto Attribute : Binary->mAttributes) Word(Attribute);
			Word(Offset);
			Word(static_cast<uint32_t>(Binary->mBytes.size()));
			Offset += static_cast<uint32_t>(Binary->mBytes.size());
		}
		for (const auto& Entry : Entries)
		{
			const auto& Bytes = Entry.second->mBytes;
			Data.append(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
		}
		return InstallKernelLibrary(Directory / (std::string(Library.c_str()) + ".ardakernels"), Data);
	}

	static FArdaRHIStatus LoadIndexedLibrary(const uint8_t* Bytes, size_t Size, const eastl::string* ExpectedId)
	{
		if (!Bytes || Size < 16 || Size > INT32_MAX || std::memcmp(Bytes, "ARDANK01", 8) != 0)
			return Invalid("Unrecognized native kernel library.");
		FArdaIndexReader Reader{Bytes, 8, Size};
		const size_t DataStart = Reader.Word();
		const auto Count = Reader.Word();
		if (DataStart < 16 || DataStart > Size || !Count || Count > (DataStart - 16) / 32)
			return Invalid("Invalid native kernel index.");
		Reader.mEnd = DataStart;
		const auto Library = Reader.String();
		if (ExpectedId && *ExpectedId != Library) return Invalid("Native kernel library identity differs from its filename.");
		FArdaIndex Entries;
		size_t Next = DataStart;
		for (uint32_t I = 0; I < Count; ++I)
		{
			FArdaNativeKernelKey Key{Library, Reader.String(), Reader.String(), Reader.String()};
			FArdaNativeKernelBinary Binary;
			Binary.mFormat = Reader.String();
			Binary.mEntryPoint = Reader.String();
			const auto Attributes = Reader.Word();
			if (!IsValidArdaNativeKernelKey(Key) || Attributes > (Reader.mEnd - Reader.mPosition) / 4)
				return Invalid("Invalid native kernel index entry.");
			for (uint32_t J = 0; J < Attributes; ++J) Binary.mAttributes.push_back(Reader.Word());
			const size_t Offset = Reader.Word();
			const size_t PayloadSize = Reader.Word();
			if (!Reader.mbValid || Offset != Next || !PayloadSize || PayloadSize > Size - Next)
				return Invalid("Invalid native kernel payload range.");
			Next += PayloadSize;
			Binary.mBytes.assign(Bytes + Offset, Bytes + Next);
			if (!Entries.emplace(IndexKey(Key), eastl::make_shared<const FArdaNativeKernelBinary>(eastl::move(Binary))).second)
				return Invalid("Duplicate native kernel index entry.");
		}
		if (Reader.mPosition != DataStart || Next != Size) return Invalid("Unexpected kernel library data.");
		return Publish(Entries);
	}

	FArdaRHIStatus LoadArdaNativeKernelLibrary(const uint8_t* Bytes, size_t Size)
	{
		return LoadIndexedLibrary(Bytes, Size, nullptr);
	}

	static FArdaRHIStatus ReadKernelLibrary(const std::filesystem::path& File, const eastl::string* ExpectedId, eastl::string& Data)
	{
		if (fileops::ReadBinaryFile(File, Data, INT32_MAX) != fileops::EArdaFileReadResult::Success)
			return Invalid("Cannot read native kernel library.");
		return LoadIndexedLibrary(reinterpret_cast<const uint8_t*>(Data.data()), Data.size(), ExpectedId);
	}

	FArdaRHIStatus LoadArdaNativeKernelLibrary(const std::filesystem::path& File)
	{
		eastl::string Data;
		auto Status = ReadKernelLibrary(File, nullptr, Data);
		if (Status) AddArdaNativeKernelDirectory(std::filesystem::absolute(File).parent_path());
		return Status;
	}

	FArdaRHIStatus StageArdaNativeKernelLibrary(const eastl::string& Library, const std::filesystem::path& Directory)
	{
		if (!IsValidArdaNativeKernelKey({Library, "provider", "kernel", "abi"})) return Invalid("Invalid native library name.");
		auto& State = Catalog();
		std::vector<std::filesystem::path> Directories;
		auto EntryCount = [&]
		{
			std::lock_guard Lock(State.mMutex);
			auto It = State.mEntries.lower_bound({Library.c_str(), {}, {}, {}});
			size_t Count = 0;
			for (; It != State.mEntries.end() && It->first[0] == Library.c_str(); ++It) ++Count;
			return Count;
		};
		{
			std::lock_guard Lock(State.mMutex);
			Directories = State.mDirectories;
		}
		const auto Filename = std::string(Library.c_str()) + ".ardakernels";
		for (const auto& From : Directories)
		{
			std::error_code Error;
			const auto Source = From / Filename;
			if (!std::filesystem::is_regular_file(Source, Error)) continue;
			eastl::string Data;
			if (auto Status = ReadKernelLibrary(Source, &Library, Data); !Status) return Status;
			FArdaIndexReader Reader{reinterpret_cast<const uint8_t*>(Data.data()), 12, Data.size()};
			if (EntryCount() != Reader.Word()) break; // An application registered additional entries in memory.
			if (std::filesystem::equivalent(Source, Directory / Filename, Error)) return {};
			return InstallKernelLibrary(Directory / Filename, Data);
		}
		return EntryCount() ? SaveArdaNativeKernelLibrary(Library, Directory) : FArdaRHIStatus{};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaNativeKernelBinary>> FindArdaNativeKernelBinary(
	    const FArdaNativeKernelKey& Key)
	{
		if (!IsValidArdaNativeKernelKey(Key)) return {{}, Invalid("Invalid native kernel key.")};
		auto& State = Catalog();
		const auto Id = IndexKey(Key);
		std::vector<std::filesystem::path> Directories;
		{
			std::lock_guard Lock(State.mMutex);
			const auto Found = State.mEntries.find(Id);
			if (Found != State.mEntries.end()) return {Found->second, {}};
			Directories = State.mDirectories;
		}
		for (const auto& Directory : Directories)
		{
			const auto File = Directory / (Id[0] + ".ardakernels");
			std::error_code Error;
			if (!std::filesystem::is_regular_file(File, Error)) continue;
			eastl::string Data;
			if (auto Status = ReadKernelLibrary(File, &Key.mLibrary, Data); !Status) return {{}, Status};
			std::lock_guard Lock(State.mMutex);
			const auto Found = State.mEntries.find(Id);
			if (Found != State.mEntries.end()) return {Found->second, {}};
		}
		return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Native kernel or matching signature is unavailable.")};
	}
}
