#include "Graph/Node/ArdaDependencyNode.h"

#include <gtest/gtest.h>
#include <EASTL/unique_ptr.h>
#include <type_traits>

namespace arda::callable_tests
{
	using namespace arda;

	struct FArdaCallableValueParameters
	{
		uint32_t mValue = 7;
		uint16_t mScale = 3;
	};

	class FArdaCallableValueNode final
	    : public TArdaSynchronizationDependencyNode<FArdaCallableValueNode, FArdaCallableValueParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.callable-value", 1};
		}

		static FArdaRHIStatus Validate(const FArdaParameters& Parameters)
		{
			return Parameters.mValue ? FArdaRHIStatus{}
			                         : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Value must be nonzero.");
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Value(Parameters.mValue).Value(Parameters.mScale).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCallableValueNode, CallableValue);
	ARDA_DEFINE_GRAPH_NODE(FArdaCallableValueNode, CallableValue);

	struct FArdaCallableEmptyParameters
	{
	};

	class FArdaCallableEmptyNode final
	    : public TArdaSynchronizationDependencyNode<FArdaCallableEmptyNode, FArdaCallableEmptyParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.callable-empty", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return {};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCallableEmptyNode, CallableEmpty);
	ARDA_DEFINE_GRAPH_NODE(FArdaCallableEmptyNode, CallableEmpty);

	struct FArdaCallableOwnedValue
	{
		uint32_t mValue;
		eastl::shared_ptr<uint32_t> mDestructions;

		~FArdaCallableOwnedValue()
		{
			++*mDestructions;
		}
	};

	struct FArdaCallableOwnedParameters
	{
		eastl::shared_ptr<FArdaCallableOwnedValue> mValue;
		uint32_t mTag = 11;
	};

	class FArdaCallableOwnedNode final
	    : public TArdaSynchronizationDependencyNode<FArdaCallableOwnedNode, FArdaCallableOwnedParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.callable-owned", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}
			    .Value(Parameters.mValue ? Parameters.mValue->mValue : 0)
			    .Value(Parameters.mTag)
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCallableOwnedNode, CallableOwned);
	ARDA_DEFINE_GRAPH_NODE(FArdaCallableOwnedNode, CallableOwned);

	static_assert(
	    std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&, const FArdaCallableValueParameters&>);
	static_assert(std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&, uint32_t, uint16_t>);
	static_assert(std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&>);
	static_assert(std::is_invocable_v<const FArdaCallableEmptyNode&, FArdaGraphEdit&>);
	static_assert(!std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&, const char*>);
	static_assert(!std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&, double>);
	static_assert(!std::is_invocable_v<const FArdaCallableValueNode&, FArdaGraphEdit&, uint32_t, uint32_t>);
	static_assert(!std::is_invocable_v<const FArdaCallableEmptyNode&, FArdaGraphEdit&, uint32_t>);
	static_assert(std::
	        is_invocable_v<const FArdaCallableOwnedNode&, FArdaGraphEdit&, eastl::unique_ptr<FArdaCallableOwnedValue>>);
	static_assert(!std::is_invocable_v<const FArdaCallableOwnedNode&,
	    FArdaGraphEdit&,
	    eastl::unique_ptr<FArdaCallableOwnedValue>&>);

	TEST(ArdaGraphCallable, StructuredAndPositionalCallsFreezeEquivalentParameters)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaCallableValueParameters Parameters{17, 5};
		const auto Structured = CallableValue(Edit, Parameters);
		const auto Positional = CallableValue(Edit, uint32_t{17}, uint16_t{5});
		const auto Inline = CallableValue(Edit, {17, 5});
		const auto DefaultTail = CallableValue(Edit, uint32_t{17});
		ASSERT_TRUE(Structured);
		ASSERT_TRUE(Positional);
		ASSERT_TRUE(Inline);
		ASSERT_TRUE(DefaultTail);
		Parameters.mValue = 99;
		const auto Expected = FArdaCallableValueNode::GetCanonicalKey({17, 5});
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Structured.GetHandle())->mPayload.mCanonicalKey, Expected);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Positional.GetHandle())->mPayload.mCanonicalKey, Expected);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Inline.GetHandle())->mPayload.mCanonicalKey, Expected);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(DefaultTail.GetHandle())->mPayload.mCanonicalKey,
		    FArdaCallableValueNode::GetCanonicalKey({17, 3}));
		ASSERT_EQ(Graph.GetTopology().GetNodes().size(), 4u);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 4u);
	}

	TEST(ArdaGraphCallable, NoArgumentsConstructEmptyOrDefaultParameters)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Default = CallableValue(Edit);
		const auto NoFields = CallableEmpty(Edit);
		ASSERT_TRUE(Default);
		ASSERT_TRUE(NoFields);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Default.GetHandle())->mPayload.mCanonicalKey,
		    FArdaCallableValueNode::GetCanonicalKey({7, 3}));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphCallable, MoveOnlyArgumentsBecomeRetainedCopyableParameterFields)
	{
		auto Destructions = eastl::make_shared<uint32_t>(0);
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			eastl::unique_ptr<FArdaCallableOwnedValue> Value(new FArdaCallableOwnedValue{47, Destructions});
			const auto Operation = CallableOwned(Edit, eastl::move(Value));
			ASSERT_TRUE(Operation);
			EXPECT_FALSE(Value);
			EXPECT_EQ(*Destructions, 0u);
			EXPECT_EQ(Graph.GetTopology().TryGetNode(Operation.GetHandle())->mPayload.mCanonicalKey,
			    FArdaDependencyKeyBuilder{}.Value(uint32_t{47}).Value(uint32_t{11}).Build());
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(*Destructions, 0u);
		}
		EXPECT_EQ(*Destructions, 1u);
	}

	TEST(ArdaGraphCallable, FailedCallsRetainTheFirstErrorAndStopFurtherRecording)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(CallableValue(Edit, uint32_t{0}));
		const auto Failure = Edit.GetStatus();
		EXPECT_EQ(Failure.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_FALSE(CallableValue(Edit));
		EXPECT_FALSE(CallableValue(Edit, FArdaCallableValueParameters{17, 5}));
		auto Destructions = eastl::make_shared<uint32_t>(0);
		eastl::unique_ptr<FArdaCallableOwnedValue> Pending(new FArdaCallableOwnedValue{53, Destructions});
		EXPECT_FALSE(CallableOwned(Edit, eastl::move(Pending)));
		EXPECT_TRUE(Pending);
		EXPECT_EQ(*Destructions, 0u);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		const auto Commit = Edit.Commit();
		EXPECT_EQ(Commit.mCode, Failure.mCode);
		EXPECT_EQ(Commit.mMessage, Failure.mMessage);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
		ASSERT_TRUE(Edit.Cancel());
	}
}
