/** NVAPI public driver ABI from NVIDIA/nvapi commit 51e5aae4bc40c3ccd3fb7d4dfd94256e7bfc496e.
 * nvapi_interface.h supplies the IDs and nvapi.h defines the versioned PSO descriptors.
 * The matching NVIDIA MIT license is in ThirdParty/NVAPI/LICENSE.
 */
#pragma once

#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
#include "RHI/Providers/ArdaNvapiShaderExtension.h"
#include <cstdio>
#include <d3d12.h>
#include <windows.h>
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#endif

namespace arda
{
	class FArdaD3D12Nvapi
	{
	public:
		static const FArdaD3D12Nvapi& Get()
		{
			static const FArdaD3D12Nvapi Api;
			return Api;
		}

		bool IsAvailable() const noexcept
		{
			return mbReady;
		}

		FArdaRHIStatus Check(ID3D12Device* Device, const FArdaRHIComputePipelineExtension& Extension) const
		{
			if (Extension.mId != ArdaNvapiShaderExtensionId || !mbReady)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "The D3D12 driver does not expose this compute pipeline extension.");
			}
			if (Extension.mArguments.size() < 3 || Extension.mArguments[0] == UINT_MAX ||
			    Extension.mArguments[1] >= D3D12_DRIVER_RESERVED_REGISTER_SPACE_VALUES_START)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "NVAPI requires a valid extension UAV register, register space and at least one opcode.");
			}
			for (size_t Index = 2; Index < Extension.mArguments.size(); ++Index)
			{
				bool Supported = false;
				if (mCheckOpcode(Device, Extension.mArguments[Index], &Supported) != 0 || !Supported)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "The selected D3D12 device does not support a required NVAPI shader opcode.");
				}
			}
			return {};
		}

		FArdaRHIStatus Create(ID3D12Device* Device, const FArdaRHIComputePipelineExtension& Extension,
		    const D3D12_COMPUTE_PIPELINE_STATE_DESC& Desc, ID3D12PipelineState** Pipeline) const
		{
			// NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC_V1 extends the two-word PSO base.
			const FArdaNvapiShaderSlot Slot{{sizeof(FArdaNvapiPsoExtension) | (1u << 16), 5},
			    sizeof(FArdaNvapiShaderSlot) | (1u << 16), Extension.mArguments[0], Extension.mArguments[1]};
			const FArdaNvapiPsoExtension* Extensions[] = {&Slot.mBase};
			const int Status = mCreatePipeline(Device, &Desc, 1, Extensions, Pipeline);
			if (Status != 0 || !*Pipeline)
			{
				char Message[128]{};
				std::snprintf(Message, sizeof(Message), "NvAPI_D3D12_CreateComputePipelineState failed (NVAPI status %d).", Status);
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Message);
			}
			return {};
		}

	private:
		struct FArdaNvapiPsoExtension
		{
			uint32_t mVersion;
			uint32_t mType;
		};
		struct FArdaNvapiShaderSlot
		{
			FArdaNvapiPsoExtension mBase;
			uint32_t mVersion;
			uint32_t mUavSlot;
			uint32_t mSpace;
		};
		static_assert(sizeof(FArdaNvapiShaderSlot) == 20);
#if defined(NV_SET_SHADER_EXTENSION_SLOT_DESC_VER_1)
		static_assert(sizeof(FArdaNvapiPsoExtension) == sizeof(NVAPI_D3D12_PSO_EXTENSION_DESC_V1));
		static_assert(sizeof(FArdaNvapiShaderSlot) == sizeof(NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC_V1));
		static_assert(alignof(FArdaNvapiShaderSlot) == alignof(NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC_V1));
		static_assert(NV_PSO_SET_SHADER_EXTENSION_SLOT_AND_SPACE == 5);
		static_assert(NV_SET_SHADER_EXTENSION_SLOT_DESC_VER_1 == (sizeof(FArdaNvapiShaderSlot) | (1u << 16)));
#endif
		using FArdaNvapiInitialize = int(__cdecl*)();
		using FArdaNvapiCheckOpcode = int(__cdecl*)(ID3D12Device*, uint32_t, bool*);
		using FArdaNvapiCreatePipeline = int(__cdecl*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*,
		    uint32_t, const FArdaNvapiPsoExtension**, ID3D12PipelineState**);
		HMODULE mLibrary = nullptr;
		FArdaNvapiInitialize mUnload = nullptr;
		FArdaNvapiCheckOpcode mCheckOpcode = nullptr;
		FArdaNvapiCreatePipeline mCreatePipeline = nullptr;
		bool mbReady = false;

		FArdaD3D12Nvapi()
		{
			mLibrary = LoadLibraryExW(sizeof(void*) == 8 ? L"nvapi64.dll" : L"nvapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!mLibrary)
			{
				return;
			}
			using FArdaNvapiQuery = void*(__cdecl*)(uint32_t);
			const auto Query = reinterpret_cast<FArdaNvapiQuery>(GetProcAddress(mLibrary, "nvapi_QueryInterface"));
			if (!Query)
			{
				return;
			}
			const auto Initialize = reinterpret_cast<FArdaNvapiInitialize>(Query(0x0150e828));
			mUnload = reinterpret_cast<FArdaNvapiInitialize>(Query(0xd22bdd7e));
			mCheckOpcode = reinterpret_cast<FArdaNvapiCheckOpcode>(Query(0x3dfacec8));
			mCreatePipeline = reinterpret_cast<FArdaNvapiCreatePipeline>(Query(0x2762deac));
			mbReady = Initialize && mUnload && mCheckOpcode && mCreatePipeline && Initialize() == 0;
		}
		~FArdaD3D12Nvapi()
		{
			if (mLibrary && (!mbReady || mUnload() == 0))
			{
				FreeLibrary(mLibrary);
			}
		}
	};
}
