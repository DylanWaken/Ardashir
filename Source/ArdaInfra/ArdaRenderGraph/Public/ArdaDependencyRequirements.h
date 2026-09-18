#pragma once
#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/functional.h>

namespace arda
{
	/** Named read-only admission predicate for library/driver constraints absent from portable capabilities.
	 * Must not allocate GPU resources, submit work, mutate external state or retain the borrowed device.
	 */
	struct FArdaDependencyEnvironmentRequirement
	{
		/** Nonempty library/driver constraint name included in admission diagnostics. */
		eastl::string mName;
		/** Required predicate on the current device. Success admits this condition; failure text is retained. */
		eastl::function<FArdaRHIStatus(const IArdaRHIDevice&)> mCheck;
	};

	/** Explicit node admission contract, evaluated against the current device on every attachment.
	 * Empty requirements permit device-independent graph analysis. Nonempty requirements must be
	 * satisfied even when no device is supplied; unavailable facts are never assumed supported.
	 * All fields are conjunctive. Requirement selection may depend on immutable public parameters.
	 */
	struct FArdaDependencyNodeRequirements
	{
		/** Structured portable D3D12/Vulkan features and limits. */
		FArdaRHIFeatureRequirements mFeatures;
		/** Requires a qualified CUDA execution mode on this device, independently of graphics compute. */
		bool mbRequireCuda = false;
		/** Requires ordinary CUDA surface access as well as a qualified CUDA execution mode. */
		bool mbRequireCudaSurfaces = false;
		/** Requires layered CUDA surfaces, ordinary surface support and a qualified execution mode. */
		bool mbRequireCudaLayeredSurfaces = false;
		/** Minimum major*10+minor architecture; zero imposes no architecture requirement. */
		uint32_t mMinCudaComputeCapability = 0;
		/** Per-block thread capacity needed by the node; zero imposes no requirement. */
		uint32_t mMinCudaThreadsPerBlock = 0;
		/** Dynamic shared-memory capacity needed per block in bytes; zero imposes no requirement. */
		uint32_t mMinCudaSharedMemoryBytes = 0;
		/** Empty accepts any qualified CUDA launch mode; a nonempty list also requires CUDA.
		 * None is not a supported launch mode and is invalid in this list.
		 */
		eastl::vector<EArdaCudaLaunchMode> mAllowedCudaLaunchModes;
		/** Additional named constraints, all of which must succeed; evaluated on every attachment. */
		eastl::vector<FArdaDependencyEnvironmentRequirement> mEnvironment;

		/** Read-only admission, without GPU work or waits.
		 * Returns Unsupported with all missing portable/CUDA abilities and named environment failures.
		 * Malformed predicates or launch-mode declarations return InvalidArgument.
		 * A null device is useful only for requirements which do not request environment capabilities.
		 */
		[[nodiscard]] FArdaRHIStatus Check(const IArdaRHIDevice* Device) const;
	};
}
