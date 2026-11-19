#pragma once

#include "ArdaNativeCompute.h"
#include "RHI/CUDA/ArdaCudaKernelVariants.h"
#include "RHI/CUDA/ArdaCudaModule.h"

namespace arda
{
	/** Provider identity used when constraining execution to a CUDA batch. */
	inline constexpr char ArdaNativeCudaProviderId[] = "cuda";
	/** Requires CUDA in Graphics for the active graphics API. */
	inline constexpr char ArdaNativeCudaInGraphicsProviderId[] = "cuda-in-graphics";

	/** Exactly one trusted linked entry, external call, module image, or CUDA C++ translation unit. */
	struct FArdaNativeCudaSourceDesc
	{
		/** Linked entry with build-profile metadata and architecture coverage. */
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
		/** External library call that runs on the framework's CUDA stream. */
		eastl::shared_ptr<const IArdaCudaExternalCall> mExternalCall;
		/** Owned PTX or cubin image and its target metadata. */
		FArdaCudaModuleDesc mModule;
		/** CUDA C++ translation unit to compile with NVRTC during preparation. */
		eastl::string mCudaCpp;
		/** ABI for a module image or NVRTC source. Use an extern "C" entry for a stable source symbol. */
		FArdaCudaModuleKernelDesc mModuleKernel;
		/** CUDA C++ native target. Zero compiles for the selected device. */
		uint32_t mComputeCapability = 0;
		/** Maps neutral resource slots to CUDA parameter locations. */
		eastl::vector<FArdaCudaParameterPatch> mPatches;
	};

	/**
	 * Construct a CUDA provider for the named graphics modules.
	 * @param GraphicsApis Exact registered backend names. An empty list selects native-d3d12 and native-vulkan.
	 * @return Provider for devices with CUDA capabilities and CUDA command recording.
	 */
	[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaProvider(
	    eastl::vector<eastl::string> GraphicsApis = {});

	/**
	 * Require D3D12 CiG on a graphics list or Vulkan CiG on a graphics/compute list.
	 * @return Provider for the native-d3d12 and native-vulkan modules. A context-switch device is ineligible.
	 */
	[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaInGraphicsProvider();

	/**
	 * Add the same CUDA kernel and launch policy for two execution routes, trying CiG first.
	 * @param Operand Operand that will own both variants.
	 * @param Variant CUDA source, unique name and optional launch policy. The CiG variant adds ".cig" to the name.
	 */
	void AddArdaNativeCudaVariants(FArdaNativeOperandDesc& Operand, FArdaNativeKernelVariant Variant);

	/**
	 * Validate one CUDA source descriptor and keep ownership of the source.
	 * @param Desc Exactly one linked entry, external call, module image, or CUDA C++ translation unit, plus its patches.
	 * @return Source for a native variant or failure status.
	 * @errors InvalidArgument for multiple or missing source choices, embedded null bytes, or malformed CUDA argument metadata.
	 */
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaSource(
	    FArdaNativeCudaSourceDesc Desc);

	/**
	 * Get the frozen dispatch plan for use by a CUDA sequence scheduler.
	 * @param Work Work from a prepared native compute plan.
	 * @return Shared CUDA dispatch plan, or an empty reference for other work.
	 */
	[[nodiscard]] eastl::shared_ptr<const FArdaCudaDispatchPlan> GetArdaNativeCudaWorkPlan(
	    const FArdaNativeWorkRef& Work);
}
