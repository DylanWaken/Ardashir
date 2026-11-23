/** Runtime CUDA artifacts and their explicit, serialized kernel argument ABI. */
#pragma once

#include "CUDA/ArdaRHICuda.h"

namespace arda
{
	enum class EArdaCudaModuleFormat : uint8_t
	{
		Cubin,
		Ptx
	};

	enum class EArdaCudaModuleArgumentKind : uint8_t
	{
		Value,
		Buffer,
		Surface
	};

	/** One native function parameter. Offsets describe the CUDA parameter ABI, not a host struct. */
	struct FArdaCudaModuleArgument
	{
		size_t mOffset = 0;
		size_t mSize = 0;
		size_t mAlignment = 1;
		EArdaCudaModuleArgumentKind mKind = EArdaCudaModuleArgumentKind::Value;
	};

	struct FArdaCudaModuleKernelDesc
	{
		eastl::string mEntryPoint;
		size_t mParameterSize = 0;
		size_t mParameterAlignment = 1;
		eastl::vector<FArdaCudaModuleArgument> mArguments;
	};

	struct FArdaCudaModuleDesc
	{
		EArdaCudaModuleFormat mFormat = EArdaCudaModuleFormat::Cubin;
		eastl::vector<uint8_t> mImage;
		/** Cubin targets describe native compatibility; PTX targets are minimum virtual architectures.
		 * Exact targets remain exact. The driver additionally validates PTX ISA compatibility. */
		FArdaCudaBuildInfo mBuild;
	};

	/** Immutable host artifact shared by entries. Native modules belong to each execution context. */
	class FArdaCudaModule
	{
	public:
		const FArdaCudaModuleDesc& GetDesc() const noexcept
		{
			return mDesc;
		}

	private:
		explicit FArdaCudaModule(FArdaCudaModuleDesc Desc)
		    : mDesc(eastl::move(Desc))
		{
		}

		const FArdaCudaModuleDesc mDesc;
		friend TArdaRHIResult<eastl::shared_ptr<const FArdaCudaModule>> CreateArdaCudaModule(FArdaCudaModuleDesc);
	};

	/** Validates and owns image bytes without loading CUDA or inspecting device resources.
	 * Images are executable trusted artifacts, not sandboxed data. PTX is terminated by this factory. */
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const FArdaCudaModule>> CreateArdaCudaModule(
	    FArdaCudaModuleDesc Desc);

	/** Validates the native parameter layout before an image has been compiled or loaded. */
	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaModuleKernelDesc(const FArdaCudaModuleKernelDesc& Desc);

	/** Registers a host-only entry. Native loading and ABI verification occur before recording work. */
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>> CreateArdaCudaModuleKernel(
	    eastl::shared_ptr<const FArdaCudaModule> Module,
	    FArdaCudaModuleKernelDesc Desc);

	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaModuleParameters(const FArdaCudaModuleKernelDesc& Desc,
	    const FArdaCudaKernel& Kernel);
}
