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
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Typed attachment for the built-in copy-buffer operation; no caller registration or device setup. */
	class FArdaGraphCopyNode final : public TArdaCopyDependencyNode<FArdaGraphCopyNode, FArdaGraphCopyParameters>
	{
	public:
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Typed attachment for the built-in clear-buffer operation; no caller registration or device setup. */
	class FArdaGraphClearNode final : public TArdaComputeDependencyNode<FArdaGraphClearNode, FArdaGraphClearParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Typed attachment for the built-in readback operation; no caller registration or device setup. */
	class FArdaGraphReadbackNode final
	    : public TArdaCopyDependencyNode<FArdaGraphReadbackNode, FArdaGraphReadbackParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Typed attachment for the built-in sync operation; no caller registration or device setup. */
	class FArdaGraphSyncNode final
	    : public TArdaSynchronizationDependencyNode<FArdaGraphSyncNode, FArdaGraphSyncParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
	};

	/** Registers the default upload/copy/clear/readback/synchronization node library once. */
	FArdaRHIStatus RegisterArdaBuiltinNodes();

	/** Parameters owned by a texture-to-buffer node; attachment validates the copy region and row layout. */
	struct FArdaGraphTextureToBufferParameters
	{
		/** Texture resource containing the source texels. */
		FArdaDependencyResourceHandle mTexture;
		/** Texture subresource and texel region to copy. */
		FArdaRHITextureSlice mSlice;
		/** Buffer resource receiving the copied texel rows. */
		FArdaDependencyResourceHandle mBuffer;
		/** Byte offset and row pitch in the destination buffer. */
		FArdaRHITextureBufferLayout mLayout;
		/** Validated bytes per texel row; DeclareResources replaces caller values. */
		uint64_t mRowBytes = 0;
		/** Validated number of copied rows; DeclareResources replaces caller values. */
		uint64_t mRowCount = 0;
		/** Whether the copy covers the selected subresource; filled by DeclareResources. */
		bool mbWholeSubresource = false;
	};

	/** Parameters owned by a buffer-to-texture node; attachment validates the copy region and row layout. */
	struct FArdaGraphBufferToTextureParameters
	{
		/** Texture resource receiving the copied texels. */
		FArdaDependencyResourceHandle mTexture;
		/** Texture subresource and texel region to update. */
		FArdaRHITextureSlice mSlice;
		/** Buffer resource containing the source texel rows. */
		FArdaDependencyResourceHandle mBuffer;
		/** Byte offset and row pitch in the source buffer. */
		FArdaRHITextureBufferLayout mLayout;
		/** Validated bytes per texel row; DeclareResources replaces caller values. */
		uint64_t mRowBytes = 0;
		/** Validated number of copied rows; DeclareResources replaces caller values. */
		uint64_t mRowCount = 0;
		/** Filled by DeclareResources; partial copies preserve the subresource's other texels. */
		bool mbWholeSubresource = false;
	};

	/** Copies texel rows into a buffer without writing row padding. Runs on the graphics queue. */
	class FArdaGraphTextureToBufferNode final
	    : public TArdaGraphicsDependencyNode<FArdaGraphTextureToBufferNode, FArdaGraphTextureToBufferParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Copies buffer texel rows into a texture without reading row padding. Runs on the graphics queue. */
	class FArdaGraphBufferToTextureNode final
	    : public TArdaGraphicsDependencyNode<FArdaGraphBufferToTextureNode, FArdaGraphBufferToTextureParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
