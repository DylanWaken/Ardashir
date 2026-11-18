/** Portable kernel definitions registered through the same execution contract as class-authored nodes. */
#pragma once

#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "RHI/CUDA/ArdaCudaModule.h"
#include <EASTL/array.h>

namespace arda
{
	/** One named parameter in an exported kernel ABI. */
	struct FArdaRuntimeNodeArgumentDesc
	{
		/** Stable parameter name, unique within this definition. */
		eastl::string mName;
		/** Exact packed CUDA argument location, size, alignment and kind. */
		FArdaCudaModuleArgument mAbi;
		/** Buffer access used by both dependency analysis and CUDA submission. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
	};

	/** Complete immutable implementation; registration copies the descriptor and embedded device code. */
	struct FArdaRuntimeNodeDesc
	{
		/** Registry identity. Reusing a name requires byte-identical implementation metadata. */
		eastl::string mName;
		/** Positive implementation version. */
		uint32_t mVersion = 1;
		/** Embedded PTX or cubin and explicit target compatibility. */
		FArdaCudaModuleDesc mModule;
		/** Kernel entry point in the embedded module. */
		eastl::string mEntryPoint;
		/** Exact packed argument byte count. */
		uint32_t mParameterSize = 0;
		/** Packed argument alignment. */
		uint32_t mParameterAlignment = 1;
		/** ABI-ordered parameter schema. No native pointers or external calls are accepted. */
		eastl::vector<FArdaRuntimeNodeArgumentDesc> mArguments;
	};

	/** One invocation value: a graph buffer range or owned scalar representation, according to the schema. */
	struct FArdaRuntimeNodeArgument
	{
		/** Logical storage for a buffer argument; empty for scalar values. */
		FArdaGraphBuffer mBuffer;
		/** Accessible byte range; its offset is incorporated into the kernel address. */
		FArdaRHIBufferRange mRange;
		/** Exact little-endian scalar bytes; empty for buffer arguments. */
		eastl::vector<uint8_t> mValue;
	};

	/** Frozen per-invocation parameters for any runtime definition. */
	struct FArdaRuntimeNodeParameters
	{
		/** Arguments in the definition's declared ABI order. */
		eastl::vector<FArdaRuntimeNodeArgument> mArguments;
		/** Number of CUDA blocks. */
		eastl::array<uint32_t, 3> mGrid = {1, 1, 1};
		/** Threads in each block. */
		eastl::array<uint32_t, 3> mBlock = {1, 1, 1};
		/** Dynamic shared-memory bytes per block. */
		uint32_t mSharedMemoryBytes = 0;
	};

	/** Data-authored counterpart of TArdaNodeDefinition, using the common registry and graph executor. */
	class FArdaRuntimeNodeDefinition final
	{
	public:
		/** Validate and idempotently register a complete definition without touching a GPU. */
		[[nodiscard]] static FArdaRHIStatus Register(const FArdaRuntimeNodeDesc& Description);
		/** Validate and publish a library atomically; a conflicting definition publishes none of the library. */
		[[nodiscard]] static FArdaRHIStatus Register(const eastl::vector<FArdaRuntimeNodeDesc>& Descriptions);
		/** Record through the ordinary edit invocation path, retaining an immutable parameter snapshot. */
		[[nodiscard]] static FArdaGraphOperation Invoke(FArdaGraphEdit& Edit,
		    const eastl::string& Definition,
		    FArdaRuntimeNodeParameters Parameters,
		    eastl::string Name = {});
		/** Encode a definition independently of any graph or GPU. */
		[[nodiscard]] static TArdaRHIResult<eastl::string> Serialize(const FArdaRuntimeNodeDesc& Description);
		/** Decode and validate a portable definition without registering it. */
		[[nodiscard]] static TArdaRHIResult<FArdaRuntimeNodeDesc> Deserialize(const eastl::string& Bytes);

	private:
		friend class FArdaGraphSerialization;
		/** Construct the common immutable contract; archives publish all definitions only after validation. */
		static TArdaRHIResult<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> Create(
		    const FArdaRuntimeNodeDesc& Description);
	};
}
