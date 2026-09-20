/** Transactional resource declarations made while attaching a graph node. */
#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include <EASTL/vector.h>

namespace arda
{
	class FArdaDependencyGraph;

	/** One named output exposed by a node's resource declaration. */
	struct FArdaDependencyNodeOutput
	{
		eastl::string mName;
		FArdaDependencyResourceHandle mResource;
	};

	/** Attachment-time resource declarations. Empty outputs become graph-owned transients;
	 * supplied outputs are validated against the required description. No GPU allocation occurs here.
	 * Outputs are transactional and can be retrieved using Graph.FindOutput(Node, Name).
	 */
	class FArdaDependencyResourceContext
	{
	public:
		FArdaRHIStatus Buffer(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHIBufferDesc Desc);
		FArdaRHIStatus Texture(FArdaDependencyResourceHandle& Output, eastl::string Name, FArdaRHITextureDesc Desc);
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
