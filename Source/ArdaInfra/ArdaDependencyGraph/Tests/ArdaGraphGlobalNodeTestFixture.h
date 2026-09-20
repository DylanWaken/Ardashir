#pragma once

#include "Graph/Node/ArdaDependencyNode.h"

namespace arda::global_node_tests
{
	struct FArdaGlobalNodeTestParameters
	{
		uint32_t mValue = 7;
		FArdaDependencyResourceHandle mOutput;
	};

	class FArdaGlobalNodeTestNode final
	    : public TArdaCopyDependencyNode<FArdaGlobalNodeTestNode, FArdaGlobalNodeTestParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef Device,
		    const FArdaParameters& Parameters,
		    const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaGlobalNodeTestNode, EmitTestValue);

	struct FArdaGlobalNodeTestStateCounts
	{
		uint32_t mPreparedStates = 0;
		uint32_t mOperationInstances = 0;
	};

	const FArdaGlobalNodeTestNode* GetArdaGlobalNodeTestDefinitionAddress();
	FArdaGlobalNodeTestStateCounts GetArdaGlobalNodeTestStateCounts();
}
