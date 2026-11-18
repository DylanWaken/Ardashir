/** Compile-time composition of a named node from ordinary graph edits. */
#pragma once

#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaNodeParameters.h"

namespace arda
{
	/** A subgraph's private content builder. Its edit records ordinary operations and transient resources.
	 * Inputs and outputs are declared by the parent node's parameter metadata and DeclareResources hook.
	 * Build runs during compilation and may invoke further subgraph nodes. Do not retain this context.
	 */
	class FArdaSubgraphContext final
	{
	public:
		/** Access the content edit for resource creation, operations, explicit ordering and nested nodes. */
		FArdaGraphEdit& GetEdit() noexcept;
		/** Request a buffer input using its parameter field name, such as "mSource". */
		FArdaGraphBuffer RequestBufferInput(const eastl::string& Name);
		/** Request a texture input using its parameter field name. */
		FArdaGraphTexture RequestTextureInput(const eastl::string& Name);
		/** Request an acceleration-structure input using its parameter field name. */
		FArdaGraphAccelerationStructure RequestAccelerationStructureInput(const eastl::string& Name);
		/** Get the parent output storage for children to write, using its DeclareResources slot name. */
		FArdaGraphBuffer GetBufferOutput(const eastl::string& Name = "Destination");
		/** Get the texture storage declared by the parent node. */
		FArdaGraphTexture GetTextureOutput(const eastl::string& Name = "Destination");
		/** Publish parent storage, a readable input or a compatible content transient without copying storage. */
		FArdaRHIStatus DeclareOutput(const eastl::string& Name, FArdaDependencyResourceHandle Resource);
		/** Return the first content authoring error. */
		const FArdaRHIStatus& GetStatus() const noexcept;

	private:
		friend class FArdaSubgraphPreprocessor;
		template <class, class, EArdaDependencyNodeKind>
		friend class node_detail::TArdaExecutionNode;
		FArdaSubgraphContext(FArdaGraphEdit& Edit, eastl::vector<FArdaDependencyNodeOutput> Outputs);

		template <class Parameters>
		void BindParameters(const Parameters& Value)
		{
			for (const auto& Member : Parameters::GetNodeParameterMetadata().GetMembers())
			{
				if (Member.mGetResource && Member.mAccess != EArdaDependencyAccess::Write)
				{
					mInputs.push_back({Member.mName, Member.mGetResource(Value)});
				}
			}
		}

		FArdaDependencyResourceHandle FindInput(const eastl::string& Name);
		FArdaDependencyResourceHandle FindOutput(const eastl::string& Name);
		FArdaRHIStatus ValidateOutputs();
		FArdaGraphEdit& mEdit;
		eastl::vector<FArdaDependencyNodeOutput> mInputs;
		eastl::vector<FArdaDependencyNodeOutput> mOutputs;
		eastl::vector<eastl::string> mDeclaredOutputs;
		eastl::vector<eastl::pair<FArdaDependencyResourceHandle, FArdaDependencyResourceHandle>> mOutputBindings;
	};
}
