/** Portable kernel definitions registered through the same execution contract as class-authored nodes. */
#pragma once

#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "NativeCompute/ArdaNativeKernelLibrary.h"
#include <EASTL/array.h>

namespace arda
{
	/** Graph schema for one argument in the kernel layout. */
	struct FArdaRuntimeNodeArgumentDesc
	{
		/** Stable parameter name, unique within this definition. */
		eastl::string mName;
		/** Resource access used by dependency analysis and execution. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		/** Resource state required by the operand's graph bindings. */
		EArdaRHIResourceState mState = EArdaRHIResourceState::UnorderedAccess;
		/** Override the graphics binding kind when an operand converts to a different kernel argument kind. */
		EArdaRHIResourceType mResourceType = EArdaRHIResourceType::Count;
		/** Required graphics texture format, or Unknown for an unconstrained view. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
	};

	/** Immutable reference to a deployed kernel and its graph binding schema. */
	struct FArdaRuntimeNodeDesc
	{
		/** Registry identity. Reusing a name requires byte-identical implementation metadata. */
		eastl::string mName;
		/** Positive implementation version. */
		uint32_t mVersion = 1;
		/** Provider, library, kernel, and signature used to resolve executable artifacts. */
		FArdaNativeKernelKey mKernel;
		/** Packed parameter ABI shared by the provider and graph. */
		FArdaNativeKernelLayout mLayout;
		/** Graph bindings in ABI order. */
		eastl::vector<FArdaRuntimeNodeArgumentDesc> mArguments;
		/** Native dispatch requires the matching operand's registered resource converters. */
		bool mbRequiresRegisteredConversions = false;
	};

	/** A graph buffer range, texture mip, or owned scalar representation, according to the schema. */
	struct FArdaRuntimeNodeArgument
	{
		/** Logical storage for a buffer argument; empty for scalar values. */
		FArdaGraphBuffer mBuffer;
		/** Accessible byte range; its offset is incorporated into the kernel address. */
		FArdaRHIBufferRange mRange;
		/** Exact little-endian scalar bytes; empty for buffer arguments. */
		eastl::vector<uint8_t> mValue;
		/** Logical texture storage for a surface argument. */
		FArdaGraphTexture mSurface;
		/** Mip exposed by a surface argument. */
		uint32_t mMipLevel = 0;
	};

	/** Frozen per-invocation parameters for any runtime definition. */
	struct FArdaRuntimeNodeParameters
	{
		/** Arguments in the definition's declared ABI order. */
		eastl::vector<FArdaRuntimeNodeArgument> mArguments;
		/** Work-group counts. */
		eastl::array<uint32_t, 3> mGrid = {1, 1, 1};
		/** Threads per work group. */
		eastl::array<uint32_t, 3> mBlock = {1, 1, 1};
		/** Dynamic shared-memory bytes per work group. */
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
