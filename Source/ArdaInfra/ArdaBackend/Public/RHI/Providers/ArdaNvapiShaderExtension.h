/** NVAPI compute pipeline extension contract between native operands and D3D12. */
#pragma once

namespace arda
{
	/** Arguments are the extension UAV register, register space, then the required NV_EXTN_OP_* numbers. */
	inline constexpr char ArdaNvapiShaderExtensionId[] = "nvapi.d3d12.shader-extension.v1";
}
