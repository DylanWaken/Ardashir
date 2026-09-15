#pragma once

#include "ArdaDependencyNode.h"
#include "NodeLibrary/ArdaMemoryTextureNodes.h"
#include "NodeLibrary/ArdaMemoryAccelerationStructureNodes.h"

#include <cstdint>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Host-to-device buffer upload. Attachment owns an immutable copy of the bytes. */
	struct FArdaMemoryUploadBufferParameters
	{
		/** GPU destination, or empty to infer an output named Destination with no shader usage flags.
		 * Supply a graph buffer with the desired descriptor for vertex, structured, or shader use.
		 */
		FArdaDependencyResourceHandle mDestination;
		/** Nonempty bytes copied into the node snapshot; later caller edits have no effect. */
		eastl::vector<uint8_t> mBytes;
		/** First destination byte. Bytes outside the uploaded range are preserved, not initialized. */
		uint64_t mDestinationOffset = 0;
	};

	/** Buffer-to-buffer copy; source and destination must be different resources. */
	struct FArdaMemoryCopyBufferParameters
	{
		/** Source buffer. CPU readback heaps cannot be copy sources. */
		FArdaDependencyResourceHandle mSource;
		/** Destination, or empty to infer a GPU output named Destination from the source shape/usage
		 * excluding Volatile and without inheriting CPU access or backing-allocation policy.
		 * CPU upload heaps cannot be copy destinations. Bytes outside the copy are preserved;
		 * an inferred output's unwritten bytes remain uninitialized.
		 */
		FArdaDependencyResourceHandle mDestination;
		/** Nonzero byte count, or ArdaRHIWholeBuffer for the source's remaining bytes. */
		uint64_t mByteSize = ArdaRHIWholeBuffer;
		/** First byte to read in the source. */
		uint64_t mSourceOffset = 0;
		/** First byte to write in the destination. */
		uint64_t mDestinationOffset = 0;
	};

	/** Device-to-host readback of a buffer range. */
	struct FArdaMemoryReadbackBufferParameters
	{
		/** Buffer whose bytes are read; CPU readback heaps cannot be copy sources. */
		FArdaDependencyResourceHandle mSource;
		/** Required retained output. Successful Execute/Wait replaces its bytes; failed frames clear
		 * registered outputs. Do not access it while graph completion can publish, including frame-slot
		 * recycling in Submit. Serialize executions sharing this destination and consume after Wait.
		 */
		eastl::shared_ptr<eastl::vector<uint8_t>> mDestination;
		/** First source byte; output bytes always start at index zero. */
		uint64_t mSourceOffset = 0;
		/** Nonzero byte count, or ArdaRHIWholeBuffer for the source's remaining bytes. */
		uint64_t mByteSize = ArdaRHIWholeBuffer;
	};

	/** Fill a complete GPU buffer with a repeated 32-bit unsigned value. */
	struct FArdaMemoryClearBufferParameters
	{
		/** GPU buffer with UnorderedAccess usage and a byte size divisible by four. */
		FArdaDependencyResourceHandle mDestination;
		/** Repeated 32-bit pattern; zero clears every byte. */
		uint32_t mValue = 0;
	};

	/** Uploads owned host bytes on the graph's copy queue without blocking submission. */
	class FArdaMemoryUploadBufferNode final
	    : public TArdaCopyDependencyNode<FArdaMemoryUploadBufferNode, FArdaMemoryUploadBufferParameters>
	{
	public:
		/** Returns the stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates the upload range and supplies or infers the Destination output. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes resource identity, offset, and owned bytes for attachment deduplication. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the precise destination byte range written by the upload. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records upload commands and returns any RHI failure. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Copies a byte range between distinct buffers on the graph's copy queue. */
	class FArdaMemoryCopyBufferNode final
	    : public TArdaCopyDependencyNode<FArdaMemoryCopyBufferNode, FArdaMemoryCopyBufferParameters>
	{
	public:
		/** Returns the stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Resolves the byte count, validates both ranges, and declares Destination. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes both resource identities and the resolved byte ranges. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the source read and destination write ranges. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records the copy and returns any RHI failure. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Publishes a retained CPU byte vector only after graph completion succeeds. */
	class FArdaMemoryReadbackBufferNode final
	    : public TArdaCopyDependencyNode<FArdaMemoryReadbackBufferNode, FArdaMemoryReadbackBufferParameters>
	{
	public:
		/** Returns the stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates the destination and resolves the source byte range during attachment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes source, byte range, and output vector identity. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the source range and a side effect so output is retained by graph culling. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Registers the asynchronous copy with the graph's frame completion handling. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Fills a complete unordered-access buffer with a 32-bit pattern on the compute queue. */
	class FArdaMemoryClearBufferNode final
	    : public TArdaComputeDependencyNode<FArdaMemoryClearBufferNode, FArdaMemoryClearBufferParameters>
	{
	public:
		/** Returns the stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates buffer identity, CPU access, usage, and four-byte size alignment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes the destination resource identity and fill value. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares a write of the complete destination buffer. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records the integer clear and returns any RHI failure. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Registers all Memory node types for registry enumeration; typed attachment registers on demand.
	 * Safe to repeat, including after a node definition is unregistered. Does not create a device.
	 */
	FArdaRHIStatus RegisterArdaMemoryNodes();
}
