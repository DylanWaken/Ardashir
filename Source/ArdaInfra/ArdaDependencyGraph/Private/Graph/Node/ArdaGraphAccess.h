/** Internal authoring primitives for operation implementations and compiler fixtures.
 * This header is private to the graph module; applications record through FArdaGraphEdit.
 */
#pragma once

#include "Graph/Node/ArdaGraphEdit.h"

namespace arda
{
	class FArdaGraphAccess final
	{
	public:
		static FArdaDependencyGraph& GetGraph(FArdaGraphEdit& Edit)
		{
			return Edit.mGraph;
		}

		static FArdaRHIStatus BeginGraphEdit(FArdaDependencyGraph& Graph, bool Extend = false)
		{
			return Graph.BeginGraphEdit(Extend);
		}

		static FArdaRHIStatus EndGraphEdit(FArdaDependencyGraph& Graph)
		{
			return Graph.EndGraphEdit();
		}

		static FArdaRHIStatus CancelGraphEdit(FArdaDependencyGraph& Graph)
		{
			return Graph.CancelGraphEdit();
		}

		static FArdaRHIStatus SetOptions(FArdaDependencyGraph& Graph, const FArdaInductorOptions& Options)
		{
			return Graph.SetOptions(Options);
		}

		static auto CreateResource(FArdaDependencyGraph& Graph, FArdaDependencyResourceDesc Desc)
		{
			return Graph.CreateResource(eastl::move(Desc));
		}

		static auto CreateBuffer(FArdaDependencyGraph& Graph, eastl::string Name, FArdaRHIBufferDesc Desc)
		{
			return Graph.CreateBuffer(eastl::move(Name), eastl::move(Desc));
		}

		static auto CreateTexture(FArdaDependencyGraph& Graph, eastl::string Name, FArdaRHITextureDesc Desc)
		{
			return Graph.CreateTexture(eastl::move(Name), eastl::move(Desc));
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

		static FArdaRHIStatus MarkOutput(FArdaDependencyGraph& Graph,
		    FArdaDependencyResourceHandle Resource,
		    bool Output = true)
		{
			return Graph.MarkOutput(Resource, Output);
		}

		static FArdaRHIStatus AddDependency(FArdaDependencyGraph& Graph,
		    FArdaGraphNodeHandle Producer,
		    FArdaGraphNodeHandle Consumer)
		{
			return Graph.AddDependency(Producer, Consumer);
		}

		static FArdaRHIStatus RemoveDependency(FArdaDependencyGraph& Graph,
		    FArdaGraphNodeHandle Producer,
		    FArdaGraphNodeHandle Consumer)
		{
			return Graph.RemoveDependency(Producer, Consumer);
		}

		static FArdaRHIStatus RemoveNode(FArdaDependencyGraph& Graph, FArdaGraphNodeHandle Node)
		{
			return Graph.RemoveNode(Node);
		}

		template <class Node>
		static auto AttachOrFind(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    typename Node::FArdaParameters Parameters)
		{
			return Graph.AttachOrFind<Node>(eastl::move(Name), eastl::move(Parameters));
		}

		template <class Node,
		    class Initializer,
		    eastl::enable_if_t<eastl::is_invocable_v<Initializer, typename Node::FArdaParameters&>, int> = 0>
		static auto AttachOrFind(FArdaDependencyGraph& Graph, eastl::string Name, Initializer&& Initialize)
		{
			return Graph.AttachOrFind<Node>(eastl::move(Name), eastl::forward<Initializer>(Initialize));
		}

		template <class Parameters>
		static auto AttachOrFind(FArdaDependencyGraph& Graph,
		    eastl::string Name,
		    const eastl::string& Definition,
		    Parameters Value)
		{
			return Graph.AttachOrFind(eastl::move(Name), Definition, eastl::move(Value));
		}

		template <class Node>
		static auto Attach(FArdaDependencyGraph& Graph, eastl::string Name, typename Node::FArdaParameters Parameters)
		{
			return Node::FArdaNodeBase::Attach(Graph, eastl::move(Name), eastl::move(Parameters));
		}

		template <class Node,
		    class Initializer,
		    eastl::enable_if_t<eastl::is_invocable_v<Initializer, typename Node::FArdaParameters&>, int> = 0>
		static auto Attach(FArdaDependencyGraph& Graph, eastl::string Name, Initializer&& Initialize)
		{
			return Node::FArdaNodeBase::Attach(Graph, eastl::move(Name), eastl::forward<Initializer>(Initialize));
		}
	};
}
