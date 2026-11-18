#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	/** Built-in node parameters; uploaded bytes are owned immutable snapshots. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphUploadParameters)
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mDestination)
		ARDA_NODE_PARAMETER(eastl::vector<uint8_t>, mBytes)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mOffset, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphCopyParameters)
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mSource)
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mDestination)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mByteSize, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mSourceOffset, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mDestinationOffset, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphClearParameters)
		ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphReadbackParameters)
		ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<eastl::vector<uint8_t>>, mDestination)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** A dependency join operation. Connect predecessors and successors with Edit.Order. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphSyncParameters)

	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Typed attachment for the built-in upload operation; no caller registration or device setup. */
	class FArdaGraphUploadNode final
	    : public TArdaNodeDefinition<FArdaGraphUploadNode, FArdaGraphUploadParameters, EArdaNodeType::Copy>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphUploadNode, UploadNode);

	/** Typed attachment for the built-in copy-buffer operation; no caller registration or device setup. */
	class FArdaGraphCopyNode final
	    : public TArdaNodeDefinition<FArdaGraphCopyNode, FArdaGraphCopyParameters, EArdaNodeType::Copy>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphCopyNode, CopyNode);

	/** Typed attachment for the built-in clear-buffer operation; no caller registration or device setup. */
	class FArdaGraphClearNode final
	    : public TArdaNodeDefinition<FArdaGraphClearNode, FArdaGraphClearParameters, EArdaNodeType::Compute>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphClearNode, ClearNode);

	/** Typed attachment for the built-in readback operation; no caller registration or device setup. */
	class FArdaGraphReadbackNode final
	    : public TArdaNodeDefinition<FArdaGraphReadbackNode, FArdaGraphReadbackParameters, EArdaNodeType::Copy>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphReadbackNode, ReadbackNode);

	/** Typed attachment for the built-in sync operation; no caller registration or device setup. */
	class FArdaGraphSyncNode final
	    : public TArdaNodeDefinition<FArdaGraphSyncNode, FArdaGraphSyncParameters, EArdaNodeType::Synchronization>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphSyncNode, SyncNode);

	/** Registers the default upload/copy/clear/readback/synchronization node library once. */
	FArdaRHIStatus RegisterArdaBuiltinNodes();

	/** Parameters owned by a texture-to-buffer node; attachment validates the copy region and row layout. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphTextureToBufferParameters)
		/** Texture resource containing the source texels. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mTexture)
		/** Texture subresource and texel region to copy. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mSlice)
		/** Buffer resource receiving the copied texel rows. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mBuffer)
		/** Byte offset and row pitch in the destination buffer. */
		ARDA_NODE_PARAMETER(FArdaRHITextureBufferLayout, mLayout)
		/** Validated bytes per texel row; DeclareResources replaces caller values. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mRowBytes, 0)
		/** Validated number of copied rows; DeclareResources replaces caller values. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mRowCount, 0)
		/** Whether the copy covers the selected subresource; filled by DeclareResources. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWholeSubresource, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Parameters owned by a buffer-to-texture node; attachment validates the copy region and row layout. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphBufferToTextureParameters)
		/** Texture resource receiving the copied texels. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mTexture)
		/** Texture subresource and texel region to update. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mSlice)
		/** Buffer resource containing the source texel rows. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mBuffer)
		/** Byte offset and row pitch in the source buffer. */
		ARDA_NODE_PARAMETER(FArdaRHITextureBufferLayout, mLayout)
		/** Validated bytes per texel row; DeclareResources replaces caller values. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mRowBytes, 0)
		/** Validated number of copied rows; DeclareResources replaces caller values. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mRowCount, 0)
		/** Filled by DeclareResources; partial copies preserve the subresource's other texels. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWholeSubresource, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Copies texel rows into a buffer without writing row padding. Runs on the graphics queue. */
	class FArdaGraphTextureToBufferNode final : public TArdaNodeDefinition<FArdaGraphTextureToBufferNode,
	                                                FArdaGraphTextureToBufferParameters,
	                                                EArdaNodeType::Graphics>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphTextureToBufferNode, TextureToBuffer);

	/** Copies buffer texel rows into a texture without reading row padding. Runs on the graphics queue. */
	class FArdaGraphBufferToTextureNode final : public TArdaNodeDefinition<FArdaGraphBufferToTextureNode,
	                                                FArdaGraphBufferToTextureParameters,
	                                                EArdaNodeType::Graphics>
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

	ARDA_DECLARE_GRAPH_NODE(FArdaGraphBufferToTextureNode, BufferToTexture);
}
