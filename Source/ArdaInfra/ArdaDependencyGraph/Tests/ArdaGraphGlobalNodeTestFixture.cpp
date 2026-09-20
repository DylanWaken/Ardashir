#include "ArdaGraphGlobalNodeTestFixture.h"

namespace arda::global_node_tests
{
	ARDA_DEFINE_GRAPH_NODE(FArdaGlobalNodeTestNode, EmitTestValue);

	namespace
	{
		FArdaGlobalNodeTestStateCounts StateCounts;
	}

	FArdaDependencyNodeMetadata FArdaGlobalNodeTestNode::GetMetadata()
	{
		return {"test.global-node-callable", 1};
	}

	FArdaRHIStatus FArdaGlobalNodeTestNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = sizeof(uint32_t);
		return Context.Buffer(Parameters.mOutput, "Result", Desc);
	}

	eastl::string FArdaGlobalNodeTestNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder{}.Value(Parameters.mValue).Resource(Parameters.mOutput).Build();
	}

	FArdaDependencyNodeDesc FArdaGlobalNodeTestNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mAccesses.push_back({Parameters.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
		return Desc;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaGlobalNodeTestNode::FArdaState>> FArdaGlobalNodeTestNode::Prepare(
	    FArdaRHIDeviceRef)
	{
		++StateCounts.mPreparedStates;
		return {eastl::make_shared<const FArdaState>(), {}};
	}

	TArdaRHIResult<eastl::shared_ptr<FArdaGlobalNodeTestNode::FArdaInstanceState>> FArdaGlobalNodeTestNode::
	    CreateInstance(FArdaRHIDeviceRef, const FArdaParameters&, const FArdaState&)
	{
		++StateCounts.mOperationInstances;
		return {eastl::make_shared<FArdaInstanceState>(), {}};
	}

	FArdaRHIStatus FArdaGlobalNodeTestNode::Record(FArdaDependencyExecutionContext&,
	    const FArdaParameters&,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return {};
	}

	const FArdaGlobalNodeTestNode* GetArdaGlobalNodeTestDefinitionAddress()
	{
		return &EmitTestValue;
	}

	FArdaGlobalNodeTestStateCounts GetArdaGlobalNodeTestStateCounts()
	{
		return StateCounts;
	}
}
