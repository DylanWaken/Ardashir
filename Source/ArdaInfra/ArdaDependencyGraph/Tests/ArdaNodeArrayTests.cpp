#include "Graph/Node/ArdaNodeDefinition.h"
#include "Graph/DefaultNodes/ArdaGraphicsOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include <gtest/gtest.h>
#include <type_traits>

namespace arda::node_array_tests
{
	// clang-format off
	template <EArdaNodeType Type>
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(TArdaArrayParameters)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 1)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint16_t, mScale, 3)
	ARDA_END_NODE_PARAMETER_STRUCT()
    // clang-format on

    template <EArdaNodeType Type>
    class TArdaArrayNode final : public TArdaNodeDefinition<TArdaArrayNode<Type>, TArdaArrayParameters<Type>, Type>
{
public:
	using FArdaParameters = TArdaArrayParameters<Type>;
	using FArdaState = FArdaEmptyDependencyNodeState;
	using FArdaInstanceState = FArdaEmptyDependencyNodeState;

	static FArdaDependencyNodeMetadata GetMetadata()
	{
		return {eastl::string("test.node-array.") + eastl::to_string(uint32_t(Type)), 1};
	}

	static FArdaRHIStatus Validate(const FArdaParameters& P)
	{
		return P.mValue ? FArdaRHIStatus{}
		                : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Array value must be nonzero.");
	}

	static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
	{
		FArdaDependencyNodeDesc Desc;
		Desc.mbSideEffect = true;
		return Desc;
	}

	template <EArdaNodeType Kind = Type, eastl::enable_if_t<Kind != EArdaNodeType::Cuda, int> = 0>
	static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
	    const FArdaParameters&,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return {};
	}

	template <EArdaNodeType Kind = Type, eastl::enable_if_t<Kind == EArdaNodeType::Cuda, int> = 0>
	static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
	    const FArdaParameters&,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaCudaSequence&)
	{
		return {};
	}
};

using FArdaArrayNode = TArdaArrayNode<EArdaNodeType::Synchronization>;
using FArdaArrayParameters = FArdaArrayNode::FArdaParameters;
static_assert(std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, TArdaNodeArray<uint32_t>, uint16_t>);
static_assert(std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, uint32_t, TArdaNodeArray<uint16_t>>);
static_assert(std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, TArdaNodeArray<FArdaArrayParameters>>);
static_assert(!std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, TArdaNodeArray<double>>);
static_assert(!std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, uint32_t, TArdaNodeArray<uint32_t>>);
static_assert(!std::is_invocable_v<FArdaArrayNode, FArdaGraphEdit&, TArdaNodeArray<const char*>>);
static_assert(std::is_same_v<decltype(TArdaArrayNode<EArdaNodeType::Vertex>::Invoke(std::declval<FArdaGraphEdit&>(),
                                 std::declval<TArdaNodeArray<uint32_t>>())),
    TArdaNodeArray<FArdaGraphVertices>>);
static_assert(std::is_same_v<decltype(TArdaArrayNode<EArdaNodeType::RayMiss>::Invoke(std::declval<FArdaGraphEdit&>(),
                                 std::declval<TArdaNodeArray<uint32_t>>())),
    TArdaNodeArray<FArdaGraphRayShader>>);
static_assert(
    !std::is_invocable_v<FArdaRasterizeNode, FArdaGraphEdit&, TArdaNodeArray<FArdaGraphFragments>, FArdaRasterConfig>);

TEST(ArdaNodeArray, PositionalArraysZipAndBroadcastFrozenValues)
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	TArdaNodeArray<uint32_t> Values{7, 11, 19};
	TArdaNodeArray<uint16_t> Scales{2, 3, 5};
	const auto Zipped = FArdaArrayNode{}(Edit, Values, Scales);
	const auto BroadcastFirst = FArdaArrayNode::Invoke(Edit, uint32_t{23}, Scales);
	const auto BroadcastLast = Edit.Invoke<FArdaArrayNode>(Values, uint16_t{7});
	const auto Defaults = FArdaArrayNode{}(Edit, Values);
	ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
	ASSERT_EQ(Zipped.size(), 3u);
	ASSERT_EQ(BroadcastFirst.size(), 3u);
	ASSERT_EQ(BroadcastLast.size(), 3u);
	ASSERT_EQ(Defaults.size(), 3u);
	for (size_t Index = 0; Index < Values.size(); ++Index)
	{
		const auto& Node = Edit.GetGraph().GetTopology().TryGetNode(Zipped[Index].GetHandle())->mPayload;
		ASSERT_TRUE(Node.mArrayInvocation);
		EXPECT_EQ(Node.mArrayInvocation->mLaneCount, 3u);
		EXPECT_EQ(Node.mArrayLane, Index);
		EXPECT_EQ(Node.mArrayInvocation,
		    Edit.GetGraph().GetTopology().TryGetNode(Zipped[0].GetHandle())->mPayload.mArrayInvocation);
		EXPECT_EQ(Node.mCanonicalKey, FArdaArrayNode::GetCanonicalKey({Values[Index], Scales[Index]}));
		EXPECT_EQ(Edit.GetGraph().GetTopology().TryGetNode(BroadcastFirst[Index].GetHandle())->mPayload.mCanonicalKey,
		    FArdaArrayNode::GetCanonicalKey({23, Scales[Index]}));
		EXPECT_EQ(Edit.GetGraph().GetTopology().TryGetNode(BroadcastLast[Index].GetHandle())->mPayload.mCanonicalKey,
		    FArdaArrayNode::GetCanonicalKey({Values[Index], 7}));
		EXPECT_EQ(Edit.GetGraph().GetTopology().TryGetNode(Defaults[Index].GetHandle())->mPayload.mCanonicalKey,
		    FArdaArrayNode::GetCanonicalKey({Values[Index], 3}));
	}
	Values[0] = 91;
	Scales[0] = 13;
	EXPECT_EQ(Edit.GetGraph().GetTopology().TryGetNode(Zipped[0].GetHandle())->mPayload.mCanonicalKey,
	    FArdaArrayNode::GetCanonicalKey({7, 2}));
	ASSERT_TRUE(Edit.Commit());
	ASSERT_TRUE(Graph.CompileAndWait());
	EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 12u);
}

TEST(ArdaNodeArray, StructuredArraysRetainOwnedParameterSnapshots)
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	TArdaNodeArray<FArdaArrayParameters> Parameters{{7, 2}, {11, 5}};
	const auto Structured = FArdaArrayNode{}(Edit, Parameters);
	const auto Static = FArdaArrayNode::Invoke(Edit, Parameters, eastl::string("NamedArray"));
	const auto Direct = Edit.Invoke<FArdaArrayNode>(Parameters);
	const auto Registered = Edit.Invoke(FArdaArrayNode::GetMetadata().mName, Parameters);
	ASSERT_EQ(Structured.size(), 2u);
	ASSERT_EQ(Static.size(), 2u);
	ASSERT_EQ(Direct.size(), 2u);
	ASSERT_EQ(Registered.size(), 2u);
	EXPECT_EQ(Edit.GetGraph().FindNode("NamedArray[0]"), Static[0].GetHandle());
	EXPECT_EQ(Edit.GetGraph().FindNode("NamedArray[1]"), Static[1].GetHandle());
	Parameters[0].mValue = 99;
	EXPECT_EQ(Edit.GetGraph().GetTopology().TryGetNode(Structured[0].GetHandle())->mPayload.mCanonicalKey,
	    FArdaArrayNode::GetCanonicalKey({7, 2}));
	ASSERT_TRUE(Edit.Commit());
	ASSERT_TRUE(Graph.CompileAndWait());
	EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 8u);
}

TEST(ArdaNodeArray, EmptyArraysAreNoOpsAndMismatchesFailBeforeAttachment)
{
	for (bool EmptyMismatch : {false, true})
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_TRUE(FArdaArrayNode{}(Edit, TArdaNodeArray<uint32_t>{}, uint16_t{3}).empty());
		EXPECT_TRUE(FArdaArrayNode{}(Edit, TArdaNodeArray<FArdaArrayParameters>{}).empty());
		EXPECT_TRUE(FArdaArrayNode{}(Edit, TArdaNodeArray<uint32_t>{}, TArdaNodeArray<uint16_t>{}).empty());
		ASSERT_TRUE(Edit.GetStatus());
		TArdaNodeArray<uint32_t> Values = EmptyMismatch ? TArdaNodeArray<uint32_t>{} : TArdaNodeArray<uint32_t>{1, 2};
		EXPECT_TRUE(FArdaArrayNode{}(Edit, Values, TArdaNodeArray<uint16_t>{3}).empty());
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_TRUE(Edit.GetGraph().GetTopology().GetNodes().empty());
		EXPECT_FALSE(Edit.Commit());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
	}
}

TEST(ArdaNodeArray, FailedLaneCannotPublishPartialFamily)
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	EXPECT_TRUE(FArdaArrayNode{}(Edit, TArdaNodeArray<uint32_t>{1, 0, 3}).empty());
	EXPECT_FALSE(Edit.GetStatus());
	EXPECT_FALSE(Edit.Commit());
	EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
	ASSERT_TRUE(Edit.Cancel());
	EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
}

template <EArdaNodeType Type>
void CheckDomainArrays()
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	const auto Positional = TArdaArrayNode<Type>{}(Edit, TArdaNodeArray<uint32_t>{7, 11});
	const auto Structured =
	    TArdaArrayNode<Type>::Invoke(Edit, TArdaNodeArray<TArdaArrayParameters<Type>>{{13, 2}, {17, 3}});
	ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
	ASSERT_EQ(Positional.size(), 2u);
	ASSERT_EQ(Structured.size(), 2u);
	EXPECT_TRUE(Positional[0]);
	EXPECT_TRUE(Positional[1]);
	EXPECT_TRUE(Structured[0]);
	EXPECT_TRUE(Structured[1]);
}

TEST(ArdaNodeArray, OneFacadeCoversEveryExecutionDomainAndShaderStage)
{
	CheckDomainArrays<EArdaNodeType::Compute>();
	CheckDomainArrays<EArdaNodeType::Cuda>();
	CheckDomainArrays<EArdaNodeType::Copy>();
	CheckDomainArrays<EArdaNodeType::Synchronization>();
	CheckDomainArrays<EArdaNodeType::Graphics>();
	CheckDomainArrays<EArdaNodeType::Meshlet>();
	CheckDomainArrays<EArdaNodeType::RayTracing>();
	CheckDomainArrays<EArdaNodeType::WorkGraph>();
	CheckDomainArrays<EArdaNodeType::Vertex>();
	CheckDomainArrays<EArdaNodeType::Hull>();
	CheckDomainArrays<EArdaNodeType::Domain>();
	CheckDomainArrays<EArdaNodeType::Geometry>();
	CheckDomainArrays<EArdaNodeType::Raster>();
	CheckDomainArrays<EArdaNodeType::Pixel>();
	CheckDomainArrays<EArdaNodeType::Amplification>();
	CheckDomainArrays<EArdaNodeType::Mesh>();
	CheckDomainArrays<EArdaNodeType::RayGeneration>();
	CheckDomainArrays<EArdaNodeType::RayMiss>();
	CheckDomainArrays<EArdaNodeType::RayClosestHit>();
	CheckDomainArrays<EArdaNodeType::RayAnyHit>();
	CheckDomainArrays<EArdaNodeType::RayIntersection>();
	CheckDomainArrays<EArdaNodeType::RayCallable>();
}

TEST(ArdaNodeArray, VertexArraysFeedRasterConfigurationAndPositionalForms)
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	const auto Vertices = TArdaArrayNode<EArdaNodeType::Vertex>{}(Edit, TArdaNodeArray<uint32_t>{1, 2, 3});
	FArdaRasterConfig Config;
	Config.mViewport = {0, 64, 0, 32, 0, 1};
	const auto Configured = Rasterize(Edit, Vertices, Config);
	const auto Positional = Rasterize(Edit, Vertices, Config.mViewport);
	const auto BroadcastVertex = Rasterize(Edit, Vertices[0], TArdaNodeArray<FArdaRasterConfig>{Config, Config});
	ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
	ASSERT_EQ(Configured.size(), 3u);
	ASSERT_EQ(Positional.size(), 3u);
	ASSERT_EQ(BroadcastVertex.size(), 2u);
	for (size_t Index = 0; Index < Vertices.size(); ++Index)
	{
		const auto& A = Edit.GetGraph().GetTopology().TryGetNode(Configured[Index].GetProducer())->mPayload;
		const auto& B = Edit.GetGraph().GetTopology().TryGetNode(Positional[Index].GetProducer())->mPayload;
		EXPECT_EQ(A.mCanonicalKey, B.mCanonicalKey);
		EXPECT_EQ(Edit.GetGraph().GetNodeDescription(A.mHandle).mGraphicsInput.GetProducer(),
		    Vertices[Index].GetProducer());
	}
}

TEST(ArdaNodeArray, ResourceOutputArraysFeedLaterOperationsAndVectorsRemainScalar)
{
	static_assert(std::is_same_v<decltype(UploadBuffer(std::declval<FArdaGraphEdit&>(),
	                                 FArdaDependencyResourceHandle{},
	                                 eastl::vector<uint8_t>{})),
	    FArdaGraphOperation>);
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	TArdaNodeArray<eastl::vector<uint8_t>> Bytes{{1, 2, 3, 4}, {5, 6, 7, 8}};
	const auto Uploaded = UploadBuffer(Edit, FArdaDependencyResourceHandle{}, Bytes).GetBuffer();
	const auto Copied = CopyBuffer(Edit, Uploaded, FArdaDependencyResourceHandle{}).GetBuffer();
	ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
	ASSERT_EQ(Uploaded.size(), 2u);
	ASSERT_EQ(Copied.size(), 2u);
	EXPECT_NE(Uploaded[0].GetHandle(), Uploaded[1].GetHandle());
	for (size_t Index = 0; Index < Copied.size(); ++Index)
	{
		EXPECT_NE(Copied[Index].GetHandle(), Uploaded[Index].GetHandle());
		Edit.Keep(Copied[Index]);
	}
	ASSERT_TRUE(Edit.Commit());
	ASSERT_TRUE(Graph.CompileAndWait());
	EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 4u);
}

TEST(ArdaNodeArray, OneHundredThousandLanesRecordInOneFacadeCall)
{
	FArdaDependencyGraph Graph;
	FArdaGraphEdit Edit(Graph);
	TArdaNodeArray<uint32_t> Values;
	Values.reserve(100000);
	for (uint32_t Index = 0; Index < 100000; ++Index)
	{
		Values.push_back(Index + 1);
	}
	const auto Operations = FArdaArrayNode{}(Edit, Values);
	ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
	ASSERT_EQ(Operations.size(), 100000u);
	EXPECT_EQ(Edit.GetGraph().GetTopology().GetNodes().size(), 100000u);
	const auto& Last = Edit.GetGraph().GetTopology().TryGetNode(Operations[99999].GetHandle())->mPayload;
	ASSERT_TRUE(Last.mArrayInvocation);
	EXPECT_EQ(Last.mArrayInvocation->mLaneCount, 100000u);
	EXPECT_EQ(Last.mArrayLane, 99999u);
}
}
