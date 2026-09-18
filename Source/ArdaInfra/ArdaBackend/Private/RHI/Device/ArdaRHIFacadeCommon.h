/** Shared facade status and persistent shader identity helpers. */
#pragma once

#include "RHI/Providers/ArdaRHIProvider.h"
#include "ArdaHash.h"
#include <EASTL/algorithm.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cmath>
#include <optional>
#include <thread>
#include <type_traits>
#include <unordered_map>

namespace arda::detail
{
	inline FArdaRHIStatus Invalid(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
	}

	inline FArdaRHIStatus Unsupported(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
	}

	inline FArdaRHIStatus WrongDevice()
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
		    "Resource belongs to another RHI device or implementation.");
	}

	template <typename T>
	TArdaRHIResult<T> Failure(FArdaRHIStatus Status)
	{
		return {{}, eastl::move(Status)};
	}

	template <typename T>
	TArdaRHIResult<T> UnsupportedResult(const char* Message)
	{
		return Failure<T>(Unsupported(Message));
	}

	inline uint64_t PersistentShaderHash(const FArdaRHIShaderDesc& Desc) noexcept
	{
		uint64_t Hash = arda::ArdaFnv1a64OffsetBasis;
		const auto Append = [&Hash](const void* Data, size_t Size)
		{
			AppendArdaFnv1a64(Hash, Data, Size);
		};
		const auto AppendUnsigned = [&Hash](uint64_t Value)
		{
			AppendArdaFnv1a64LittleEndian(Hash, Value);
		};
		AppendUnsigned(static_cast<uint64_t>(Desc.mStage));
		AppendUnsigned(Desc.mEntryPoint.size());
		Append(Desc.mEntryPoint.data(), Desc.mEntryPoint.size());
		AppendUnsigned(Desc.mBytecodeSize);
		if (Desc.mBytecode && Desc.mBytecodeSize)
		{
			Append(Desc.mBytecode, Desc.mBytecodeSize);
		}
		return FinishArdaPersistentHash(Hash);
	}
}
