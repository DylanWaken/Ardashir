/** Internal authoring primitives for operation implementations .
 * This header is private to the graph module; applications record through FArdaGraphEdit.
 */
#pragma once

#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Resources/ArdaGraphResourceIdentity.h"

namespace arda
{
	class FArdaGraphAccess final
	{
	public:
		static FArdaDependencyResourceHandle FindPhysicalResource(FArdaDependencyGraph& Graph,
		    const void* Identity,
		    EArdaGraphExternalResourceKind Kind)
		{
			if (Identity)
			{
				for (uint32_t Index = 0; Index < Graph.mImpl->mResources.size(); ++Index)
				{
					const auto& Resource = Graph.mImpl->mResources[Index];
					if (Resource.mExternalKind == Kind && GetArdaGraphPhysicalIdentity(Resource) == Identity)
					{
						return {Graph.mImpl->mIdentity, Index, Graph.mImpl->mResourceGenerations[Index]};
					}
				}
			}
			return {};
		}

		static eastl::string ResolveResourceName(FArdaGraphEdit& Edit, const char* Prefix, eastl::string Name)
		{
			return Name.empty() ? Edit.MakeName(Prefix) : Edit.mNamePrefix + Name;
		}

		static FArdaDependencyGraph& GetGraph(FArdaGraphEdit& Edit)
		{
			return Edit.GetRecordingGraph();
		}

		static auto CreateResource(FArdaDependencyGraph& Graph, FArdaDependencyResourceDesc Desc)
		{
			return Graph.CreateResource(eastl::move(Desc));
		}

		static auto ImportBuffer(FArdaDependencyGraph& Graph, eastl::string Name, FArdaRHIBufferRef Buffer)
		{
			return Graph.ImportBuffer(eastl::move(Name), eastl::move(Buffer));
		}

		static auto ImportTexture(FArdaDependencyGraph& Graph, eastl::string Name, FArdaRHITextureRef Texture)
		{
			return Graph.ImportTexture(eastl::move(Name), eastl::move(Texture));
		}

		static auto ImportAccelerationStructure(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    FArdaRHIAccelStructRef Structure)
		{
			return Graph.ImportAccelerationStructure(eastl::move(Name), eastl::move(Structure));
		}
	};
}
