/** D3D12 native operands that use NVIDIA HLSL shader extension intrinsics. */
#pragma once

#include "ArdaNativeCompute.h"

namespace arda
{
	/** Registry ID for native D3D12 NVAPI shader extensions. */
	inline constexpr char ArdaNativeNvapiProviderId[] = "nvapi";

	/** Compile HLSL with DXC and supply the resulting compute shader container. */
	struct FArdaNativeNvapiSourceDesc
	{
		/** Owned DXIL compiled with the NVIDIA nvHLSLExtns.h shader header. */
		eastl::vector<uint8_t> mDxil;
		/** Required NV_EXTN_OP_* numbers from nvShaderExtnEnums.h. */
		eastl::vector<uint32_t> mRequiredOpcodes;
		/** The u register declared by NV_SHADER_EXTN_SLOT in HLSL. */
		uint32_t mExtensionUavSlot = 0;
		/** The nonzero space declared by NV_SHADER_EXTN_REGISTER_SPACE in HLSL. Space zero holds operand resources. */
		uint32_t mExtensionRegisterSpace = 1;
	};

	/**
	 * Retain an NVAPI shader and its driver requirements. In space zero, operand slot i maps
	 * to ti for reads or ui for writes and read/write access. Buffers use ByteAddressBuffer
	 * views. Parameters are 32-bit root constants at b0. The compiled shader fixes numthreads.
	 */
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>>
	CreateArdaNativeNvapiSource(FArdaNativeNvapiSourceDesc Desc);

	/** Register the "nvapi" provider for "native-d3d12". Opcode support is checked on the selected device. */
	[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeNvapiProvider();
}
