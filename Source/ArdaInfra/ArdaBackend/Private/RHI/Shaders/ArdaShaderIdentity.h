/** Stable shader-bytecode identity used by compiled pipeline caches. */
#pragma once

#include "RHI/Shaders/ArdaRHIShader.h"
#include "RHI/Resources/ArdaHash.h"

namespace arda::detail
{
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
