#pragma once

#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	/** Owned upload data for a typed, single-sample, uncompressed color texture. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryUploadTextureParameters)
		/** Required destination texture, created or imported by this graph. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mDestination)
		/** Exact tightly packed texel bytes, ordered by row then depth slice. Frozen at attachment. */
		ARDA_NODE_PARAMETER(eastl::vector<uint8_t>, mBytes)
		/** Destination mip, array layer, and region; omitted extents select its remainder. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mSlice)
		/** Validated copy footprint, replaced during attachment; caller values are ignored. */
		ARDA_NODE_PARAMETER(FArdaRHITextureBufferFootprint, mFootprint)
		/** Derived during attachment; partial writes require previously initialized texels. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWholeSubresource, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Readback of one typed, single-sample, uncompressed color texture region. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryReadbackTextureParameters)
		/** Required source texture. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mSource)
		/** Required retained vector, published as tightly packed rows after Execute/Wait succeeds.
		 * Serialize executions sharing it; do not access while completion may publish or clear it.
		 */
		ARDA_NODE_PARAMETER(eastl::shared_ptr<eastl::vector<uint8_t>>, mDestination)
		/** Source mip, array layer, and region; omitted extents select its remainder. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mSlice)
		/** Validated transient GPU transfer capacity; replaced during attachment for memory budgeting. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Copy between matching typed, single-sample color texture subresources, including BC formats. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryCopyTextureParameters)
		/** Required source texture. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mSource)
		/** Required destination; must differ from the source subresource. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mDestination)
		/** Source region; explicit extents must fit without clipping. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mSourceSlice)
		/** Destination origin/subresource; explicit extents must equal the source copy extent. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSlice, mDestinationSlice)
		/** Replaced during attachment; partial writes preserve previously initialized destination texels. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWholeSubresource, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Floating-point or normalized color clear; integer, depth/stencil and typeless formats are rejected. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryClearTextureParameters)
		/** Required texture with RenderTarget usage. */
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mDestination)
		/** Color to write to every texel in the selected subresources. */
		ARDA_NODE_PARAMETER(FArdaRHIColor, mColor)
		/** Nonempty mip/layer range, normalized during attachment; plane zero only. */
		ARDA_NODE_PARAMETER(FArdaRHITextureSubresourceRange, mRange)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Uploads tightly packed host texels into one texture region. */
	class FArdaMemoryUploadTextureNode final : public TArdaNodeDefinition<FArdaMemoryUploadTextureNode,
	                                               FArdaMemoryUploadTextureParameters,
	                                               EArdaNodeType::Graphics>
	{
	public:
		/** Returns this operation's stable registry name and revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates resource identities and normalizes the selected region during attachment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes the complete resource identities, region, and operation inputs. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the affected subresources and required resource states. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records native work and returns failures without submitting the command list. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryUploadTextureNode, UploadTexture);

	/** Publishes tightly packed texture-region bytes after successful graph completion. */
	class FArdaMemoryReadbackTextureNode final : public TArdaNodeDefinition<FArdaMemoryReadbackTextureNode,
	                                                 FArdaMemoryReadbackTextureParameters,
	                                                 EArdaNodeType::Graphics>
	{
	public:
		/** Returns this operation's stable registry name and revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates resource identities and normalizes the selected region during attachment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes the complete resource identities, region, and operation inputs. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the affected subresources and required resource states. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records native work and returns failures without submitting the command list. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryReadbackTextureNode, ReadbackTexture);

	/** Copies one texture region between distinct subresources. */
	class FArdaMemoryCopyTextureNode final : public TArdaNodeDefinition<FArdaMemoryCopyTextureNode,
	                                             FArdaMemoryCopyTextureParameters,
	                                             EArdaNodeType::Graphics>
	{
	public:
		/** Returns this operation's stable registry name and revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates resource identities and normalizes the selected region during attachment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes the complete resource identities, region, and operation inputs. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the affected subresources and required resource states. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records native work and returns failures without submitting the command list. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryCopyTextureNode, CopyTexture);

	/** Clears selected color render-target subresources. */
	class FArdaMemoryClearTextureNode final : public TArdaNodeDefinition<FArdaMemoryClearTextureNode,
	                                              FArdaMemoryClearTextureParameters,
	                                              EArdaNodeType::Graphics>
	{
	public:
		/** Returns this operation's stable registry name and revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Validates resource identities and normalizes the selected region during attachment. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes the complete resource identities, region, and operation inputs. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Declares the affected subresources and required resource states. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Records native work and returns failures without submitting the command list. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryClearTextureNode, ClearTexture);

	/** Registers the texture Memory nodes; typed attachment also registers each node on demand. */
	FArdaRHIStatus RegisterArdaMemoryTextureNodes();
}
