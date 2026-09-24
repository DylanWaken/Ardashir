/** Transactional resource declarations made while attaching a graph node. */
#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include "Graph/Resources/ArdaGraphValue.h"
#include <EASTL/vector.h>

namespace arda
{
	class FArdaDependencyGraph;
	class IArdaRHIDevice;

	/** One named output exposed by a node's resource declaration. */
	struct FArdaDependencyNodeOutput
	{
		eastl::string mName;
		FArdaDependencyResourceHandle mResource;
		/** Explicit requirements retained until compilation, including for supplied AutoDeduction outputs. */
		EArdaRHIBufferUsage mRequiredBufferUsage = EArdaRHIBufferUsage::None;
		EArdaRHITextureUsage mRequiredTextureUsage = EArdaRHITextureUsage::None;
	};

	/** Attachment-time resource declarations. Empty outputs become graph-owned transients;
	 * supplied outputs are validated against the required description. No GPU allocation occurs here.
	 * Outputs are transactional and can be retrieved using Graph.FindOutput(Node, Name).
	 */
	class FArdaDependencyResourceContext
	{
	public:
		/** Borrowed device for read-only capability and execution-route decisions during DeclareResources.
		 * Null denotes device-independent analysis. The graph owns the device; do not retain this pointer
		 * beyond the declaration callback or use it to allocate, submit, wait, or mutate native state.
		 */
		[[nodiscard]] const IArdaRHIDevice* GetDevice() const;
		FArdaRHIStatus Buffer(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHIBufferDesc Desc);
		FArdaRHIStatus Texture(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHITextureDesc Desc);
		/** Preserve the typed facade while declaring or validating a named node output. */
		FArdaRHIStatus Buffer(FArdaGraphBuffer& Output, eastl::string Name, FArdaRHIBufferDesc Desc);
		FArdaRHIStatus Texture(FArdaGraphTexture& Output, eastl::string Name, FArdaRHITextureDesc Desc);
		const FArdaDependencyResourceDesc* Find(FArdaDependencyResourceHandle Input) const;

	private:
		friend class FArdaDependencyGraph;
		FArdaDependencyResourceContext(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    eastl::vector<FArdaDependencyNodeOutput> Existing);
		FArdaRHIStatus Declare(FArdaDependencyResourceHandle& Output,
		    eastl::string Name,
		    FArdaDependencyResourceDesc Desc);
		FArdaDependencyGraph& mGraph;
		eastl::string mName;
		eastl::vector<FArdaDependencyNodeOutput> mExisting, mOutputs;
	};
}
