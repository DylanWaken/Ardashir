#pragma once
#include "ArdaDependencyNode.h"

namespace arda
{
	/** Built-in node parameters; uploaded bytes are owned immutable snapshots. */
	struct FArdaGraphUploadParameters
	{
		FArdaDependencyResourceHandle mDestination;
		eastl::vector<uint8_t> mBytes;
		uint64_t mOffset = 0;
	};

	struct FArdaGraphCopyParameters
	{
		FArdaDependencyResourceHandle mSource, mDestination;
		uint64_t mByteSize = 0, mSourceOffset = 0, mDestinationOffset = 0;
	};

	struct FArdaGraphClearParameters
	{
		FArdaDependencyResourceHandle mDestination;
		uint32_t mValue = 0;
	};

	struct FArdaGraphReadbackParameters
	{
		FArdaDependencyResourceHandle mSource;
		eastl::shared_ptr<eastl::vector<uint8_t>> mDestination;
	};

	/** A named dependency join. Connect predecessors and successors with AddDependency. */
	struct FArdaGraphSyncParameters
	{
	};

	/** Typed attachment for the built-in upload operation; no caller registration or device setup. */
	class FArdaGraphUploadNode final : public TArdaCopyDependencyNode<FArdaGraphUploadNode, FArdaGraphUploadParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};

	/** Typed attachment for the built-in copy-buffer operation; no caller registration or device setup. */
	class FArdaGraphCopyNode final : public TArdaCopyDependencyNode<FArdaGraphCopyNode, FArdaGraphCopyParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};

	/** Typed attachment for the built-in clear-buffer operation; no caller registration or device setup. */
	class FArdaGraphClearNode final : public TArdaComputeDependencyNode<FArdaGraphClearNode, FArdaGraphClearParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};

	/** Typed attachment for the built-in readback operation; no caller registration or device setup. */
	class FArdaGraphReadbackNode final
	    : public TArdaCopyDependencyNode<FArdaGraphReadbackNode, FArdaGraphReadbackParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FParameters& Parameters,
		    const FState& State,
		    FInstanceState& Instance);
	};

	/** Typed attachment for the built-in sync operation; no caller registration or device setup. */
	class FArdaGraphSyncNode final
	    : public TArdaSynchronizationDependencyNode<FArdaGraphSyncNode, FArdaGraphSyncParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FParameters& Parameters, const FState& State);
	};

	/** Registers the default upload/copy/clear/readback/synchronization node library once. */
	FArdaRHIStatus RegisterArdaBuiltinNodes();

	/** Attaches a texture-to-buffer node with validated copy layout and exact texel-row accesses.
	 * Row padding is not written. Transfers are explicitly authored and stay on the graphics queue.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaGraphNodeHandle> AttachArdaTextureToBuffer(FArdaDependencyGraph& Graph,
	    eastl::string Name,
	    FArdaDependencyResourceHandle Texture,
	    const FArdaRHITextureSlice& Slice,
	    FArdaDependencyResourceHandle Buffer,
	    const FArdaRHITextureBufferLayout& Layout);

	/** Attaches a buffer-to-texture node; reads only copied texels, without requiring row padding. */
	[[nodiscard]] TArdaRHIResult<FArdaGraphNodeHandle> AttachArdaBufferToTexture(FArdaDependencyGraph& Graph,
	    eastl::string Name,
	    FArdaDependencyResourceHandle Texture,
	    const FArdaRHITextureSlice& Slice,
	    FArdaDependencyResourceHandle Buffer,
	    const FArdaRHITextureBufferLayout& Layout);
}
