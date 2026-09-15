#pragma once

#include "ArdaDependencyNode.h"

namespace arda
{
	/** Owned upload data for a typed, single-sample, uncompressed color texture. */
	struct FArdaMemoryUploadTextureParameters
	{
		/** Required destination texture, created or imported by this graph. */
		FArdaDependencyResourceHandle mDestination;
		/** Exact tightly packed texel bytes, ordered by row then depth slice. Frozen at attachment. */
		eastl::vector<uint8_t> mBytes;
		/** Destination mip, array layer, and region; omitted extents select its remainder. */
		FArdaRHITextureSlice mSlice;
		/** Validated copy footprint, replaced during attachment; caller values are ignored. */
		FArdaRHITextureBufferFootprint mFootprint;
		/** Derived during attachment; partial writes require previously initialized texels. */
		bool mbWholeSubresource = false;
	};

	/** Readback of one typed, single-sample, uncompressed color texture region. */
	struct FArdaMemoryReadbackTextureParameters
	{
		/** Required source texture. */
		FArdaDependencyResourceHandle mSource;
		/** Required retained vector, published as tightly packed rows after Execute/Wait succeeds.
		 * Serialize executions sharing it; do not access while completion may publish or clear it.
		 */
		eastl::shared_ptr<eastl::vector<uint8_t>> mDestination;
		/** Source mip, array layer, and region; omitted extents select its remainder. */
		FArdaRHITextureSlice mSlice;
		/** Validated transient GPU transfer capacity; replaced during attachment for memory budgeting. */
		uint64_t mWorkspaceBytes = 0;
	};

	/** Copy between matching typed, single-sample color texture subresources, including BC formats. */
	struct FArdaMemoryCopyTextureParameters
	{
		/** Required source texture. */
		FArdaDependencyResourceHandle mSource;
		/** Required destination; must differ from the source subresource. */
		FArdaDependencyResourceHandle mDestination;
		/** Source region; explicit extents must fit without clipping. */
		FArdaRHITextureSlice mSourceSlice;
		/** Destination origin/subresource; explicit extents must equal the source copy extent. */
		FArdaRHITextureSlice mDestinationSlice;
		/** Replaced during attachment; partial writes preserve previously initialized destination texels. */
		bool mbWholeSubresource = false;
	};

	/** Floating-point or normalized color clear; integer, depth/stencil and typeless formats are rejected. */
	struct FArdaMemoryClearTextureParameters
	{
		/** Required texture with RenderTarget usage. */
		FArdaDependencyResourceHandle mDestination;
		/** Color to write to every texel in the selected subresources. */
		FArdaRHIColor mColor;
		/** Nonempty mip/layer range, normalized during attachment; plane zero only. */
		FArdaRHITextureSubresourceRange mRange;
	};

	/** Uploads tightly packed host texels into one texture region. */
	class FArdaMemoryUploadTextureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryUploadTextureNode, FArdaMemoryUploadTextureParameters>
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

	/** Publishes tightly packed texture-region bytes after successful graph completion. */
	class FArdaMemoryReadbackTextureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryReadbackTextureNode, FArdaMemoryReadbackTextureParameters>
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

	/** Copies one texture region between distinct subresources. */
	class FArdaMemoryCopyTextureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryCopyTextureNode, FArdaMemoryCopyTextureParameters>
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

	/** Clears selected color render-target subresources. */
	class FArdaMemoryClearTextureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryClearTextureNode, FArdaMemoryClearTextureParameters>
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

	/** Registers the texture Memory nodes; typed attachment also registers each node on demand. */
	FArdaRHIStatus RegisterArdaMemoryTextureNodes();
}
