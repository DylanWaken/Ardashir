#include "ArdaDependencyGraph.h"
#include "ArdaDependencyGraphNodes.h"
#include <gtest/gtest.h>
#include <mutex>

namespace
{
	using namespace arda;

	struct FTestNodeParameters
	{
		FArdaDependencyResourceHandle mInput, mOutput;
		uint32_t mCost = 1, mValue = 0;
		bool mbSideEffect = false;
		uint64_t mWorkspaceBytes = 0;
		uint64_t mTransientWorkspaceBytes = 0;
	};

	void RegisterTestNodes()
	{
		static std::once_flag Once;
		std::call_once(Once,
		    []
		    {
			    for (auto Kind : {EArdaDependencyNodeKind::Compute,
			             EArdaDependencyNodeKind::Graphics,
			             EArdaDependencyNodeKind::Cuda,
			             EArdaDependencyNodeKind::Copy})
			    {
				    TArdaDependencyNodeDefinition<FTestNodeParameters> D;
				    D.mName = Kind == EArdaDependencyNodeKind::Compute ? "inductor.test.compute"
				        : Kind == EArdaDependencyNodeKind::Graphics    ? "inductor.test.graphics"
				        : Kind == EArdaDependencyNodeKind::Cuda        ? "inductor.test.cuda"
				                                                       : "inductor.test.copy";
				    D.mKind = Kind;
				    D.mCanonicalKey = [](const FTestNodeParameters& P)
				    {
					    eastl::string K;
					    for (uint64_t V : {P.mInput.mGraph,
					             uint64_t(P.mInput.mIndex),
					             P.mInput.mGeneration,
					             P.mOutput.mGraph,
					             uint64_t(P.mOutput.mIndex),
					             P.mOutput.mGeneration,
					             uint64_t(P.mCost),
					             uint64_t(P.mValue),
					             uint64_t(P.mbSideEffect),
					             P.mWorkspaceBytes,
					             P.mTransientWorkspaceBytes})
					    {
						    K.append(reinterpret_cast<const char*>(&V), sizeof(V));
					    }
					    return K;
				    };
				    D.mDescribe = [](const FTestNodeParameters& P)
				    {
					    FArdaDependencyNodeDesc D;
					    D.mEstimatedCost = P.mCost;
					    D.mbSideEffect = P.mbSideEffect;
					    D.mWorkspaceBytes = P.mWorkspaceBytes;
					    D.mTransientWorkspaceBytes = P.mTransientWorkspaceBytes;
					    if (P.mInput)
					    {
						    D.mAccesses.push_back(
						        {P.mInput, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess});
					    }
					    if (P.mOutput)
					    {
						    D.mAccesses.push_back(
						        {P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
					    }
					    return D;
				    };
				    if (Kind == EArdaDependencyNodeKind::Cuda)
				    {
					    D.mPrepareCuda = [](FArdaDependencyExecutionContext&, const auto&, FArdaCudaSequence&)
					    {
						    return FArdaRHIStatus{};
					    };
				    }
				    else
				    {
					    D.mRecord = [](FArdaDependencyExecutionContext&, const auto&)
					    {
						    return FArdaRHIStatus{};
					    };
				    }
				    ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(D)));
			    }
		    });
	}

	FArdaDependencyResourceHandle Buffer(FArdaDependencyGraph& G, const char* Name, uint64_t Bytes = 256)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = Bytes;
		D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		auto R = G.CreateBuffer(Name, D);
		EXPECT_TRUE(R);
		return R.mValue;
	}

	FArdaGraphNodeHandle Node(FArdaDependencyGraph& G,
	    const char* Name,
	    FTestNodeParameters P,
	    const char* Definition = "inductor.test.compute")
	{
		auto N = G.AttachOrFind(Name, Definition, P);
		EXPECT_TRUE(N) << N.mStatus.mMessage.c_str();
		return N.mValue;
	}

	TEST(ArdaInductor, ResourceDependenciesIgnoreAttachmentOrderAndDeduplicate)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c");
		auto Consumer = Node(G, "consumer", {B, C});
		auto Producer = Node(G, "producer", {{}, A});
		auto Middle = Node(G, "middle", {A, B});
		EXPECT_EQ(Node(G, "middle", {A, B}), Middle);
		EXPECT_FALSE(G.AttachOrFind("middle", "inductor.test.compute", FTestNodeParameters{A, B, 1, 99}));
		ASSERT_TRUE(G.MarkOutput(C));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_EQ(G.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer, Middle, Consumer}));
		EXPECT_TRUE(G.GetTopology().IsReachable(Producer, Consumer));
		EXPECT_EQ(G.GetCompileResult().mRevision, 1u);
		EXPECT_FALSE(G.CreateBuffer("outside", {}));
		EXPECT_FALSE(G.Execute().mStatus);
	}

	TEST(ArdaInductor, EditCancellationDoesNotReviveAbandonedResourceOrNodeHandles)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a");
		Node(G, "a", {{}, A});
		ASSERT_TRUE(G.MarkOutput(A));
		ASSERT_TRUE(G.EndGraphEdit());
		ASSERT_TRUE(G.BeginGraphEdit());
		auto Abandoned = Buffer(G, "abandoned");
		auto N = Node(G, "abandoned", {A, Abandoned});
		ASSERT_TRUE(G.CancelGraphEdit());
		ASSERT_TRUE(G.BeginGraphEdit());
		auto B = Buffer(G, "replacement");
		auto New = Node(G, "replacement", {A, B});
		EXPECT_NE(B, Abandoned);
		EXPECT_NE(N, New);
		EXPECT_FALSE(G.GetTopology().ContainsNode(N));
		EXPECT_EQ(G.FindResource(Abandoned), nullptr);
		EXPECT_FALSE(G.AttachOrFind("stale", "inductor.test.compute", FTestNodeParameters{Abandoned, B}));
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_EQ(G.GetCompileResult().mRevision, 1u);
	}

	TEST(ArdaInductor, RejectsMultipleWritersUnproducedReadsAndCycles)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b");
		Node(G, "one", {{}, A});
		auto Two = Node(G, "two", {{}, A});
		EXPECT_FALSE(G.EndGraphEdit());
		EXPECT_TRUE(G.IsEditing());
		ASSERT_TRUE(G.RemoveNode(Two));
		auto Missing = Node(G, "missing", {B, {}});
		EXPECT_FALSE(G.EndGraphEdit());
		ASSERT_TRUE(G.RemoveNode(Missing));
		auto First = G.FindNode("one"), Second = Node(G, "second", {A, B});
		ASSERT_TRUE(G.AddDependency(Second, First));
		EXPECT_FALSE(G.EndGraphEdit());
		ASSERT_TRUE(G.RemoveDependency(Second, First));
		ASSERT_TRUE(G.MarkOutput(B));
		ASSERT_TRUE(G.EndGraphEdit());
	}

	TEST(ArdaInductor, RemovalCascadesOnlyResourceConsumersAndCullsUnusedBranches)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), Dead = Buffer(G, "dead");
		auto P = Node(G, "producer", {{}, A});
		auto C = Node(G, "consumer", {A, B});
		auto D = Node(G, "dead", {{}, Dead});
		auto Sync = Node(G, "ordered-only", {{}, {}, 1, 0, true});
		ASSERT_TRUE(G.AddDependency(P, Sync));
		ASSERT_TRUE(G.MarkOutput(B));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_NE(eastl::find(G.GetCompileResult().mCulledNodes.begin(), G.GetCompileResult().mCulledNodes.end(), D),
		    G.GetCompileResult().mCulledNodes.end());
		ASSERT_TRUE(G.BeginGraphEdit());
		ASSERT_TRUE(G.RemoveNode(P));
		EXPECT_FALSE(G.GetTopology().ContainsNode(C));
		EXPECT_TRUE(G.GetTopology().ContainsNode(Sync));
		ASSERT_TRUE(G.EndGraphEdit());
	}

	TEST(ArdaInductor, CoalescesCudaWhilePreservingGraphicsDependencies)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c"), D = Buffer(G, "d");
		auto P = Node(G, "producer", {{}, A});
		auto First = Node(G, "cuda1", {A, B}, "inductor.test.cuda");
		auto Second = Node(G, "cuda2", {B, C}, "inductor.test.cuda");
		auto Graphics = Node(G, "graphics", {C, D}, "inductor.test.graphics");
		ASSERT_TRUE(G.MarkOutput(D));
		ASSERT_TRUE(G.EndGraphEdit());
		ASSERT_EQ(G.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(G.GetCompileResult().mCudaBatches[0], (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
		ASSERT_TRUE(G.BeginGraphEdit());
		ASSERT_TRUE(G.AddDependency(Graphics, First));
		EXPECT_FALSE(G.EndGraphEdit());
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_TRUE(G.GetTopology().IsReachable(P, Graphics));
	}

	TEST(ArdaInductor, ExhaustiveSearchEvaluatesAllOrdersAndCoalescesCuda)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		Options.mMaxSearchStates = 1; // Explicit exhaustive search ignores this limit.
		ASSERT_TRUE(Graph.SetOptions(Options));
		Node(Graph, "a-cuda", {{}, {}, 1, 0, true}, "inductor.test.cuda");
		Node(Graph, "b-graphics", {{}, {}, 3, 0, true}, "inductor.test.graphics");
		Node(Graph, "c-cuda", {{}, {}, 1, 0, true}, "inductor.test.cuda");
		Node(Graph, "d-graphics", {{}, {}, 3, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mbSearchComplete);
		EXPECT_FALSE(Result.mbSearchExhausted);
		EXPECT_EQ(Result.mSchedulesExamined, 26u); // 4! orders and two initial candidates.
		EXPECT_EQ(Result.mSearchStatesExamined, 65u);
		ASSERT_EQ(Result.mCudaBatches.size(), 1u);
		EXPECT_EQ(Result.mCudaBatches.front().size(), 2u);
		EXPECT_DOUBLE_EQ(Result.mEstimatedExecutionCost, 8.0 + Options.mCudaHandoffCost);
	}

	TEST(ArdaInductor, BoundedSearchKeepsFeasiblePlanWhenItsStateLimitIsReached)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mMaxSearchStates = 1;
		ASSERT_TRUE(Graph.SetOptions(Options));
		Node(Graph, "a", {{}, {}, 1, 0, true});
		Node(Graph, "b", {{}, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchExhausted);
		EXPECT_FALSE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 1u);
	}

	TEST(ArdaInductor, ExhaustiveSearchHasNoRecursiveDepthLimit)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaGraphNodeHandle Previous;
		for (uint32_t Index = 0; Index < 300; ++Index)
		{
			eastl::string Name = "node-";
			Name += std::to_string(Index).c_str();
			const auto Current = Node(Graph, Name.c_str(), {{}, {}, 1, 0, true});
			if (Previous)
			{
				ASSERT_TRUE(Graph.AddDependency(Previous, Current));
			}
			Previous = Current;
		}
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 301u);
		EXPECT_EQ(Graph.GetCompileResult().mSchedulesExamined, 3u);
	}

	TEST(ArdaInductor, TemporaryWorkspacesAreAliasedAndBudgetedForEveryFrameSlot)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mFramesInFlight = 3;
		ASSERT_TRUE(Graph.SetOptions(Options));
		const auto First = Node(Graph, "first", {{}, {}, 1, 0, true, 128, 4096});
		const auto Second = Node(Graph, "second", {{}, {}, 1, 0, true, 0, 4096});
		Node(Graph, "culled", {{}, {}, 1, 0, false, 0, 16384});
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_EQ(Result.mWorkspaceResourceIds.size(), 2u);
		EXPECT_NE(Result.mWorkspaceResourceIds.at(First.GetIndex()),
		    Result.mWorkspaceResourceIds.at(Second.GetIndex()));
		EXPECT_EQ(Result.mAllocatedBytes, 3u * 4096 + 128);
		EXPECT_EQ(Result.mAliasedBytes, 3u * 4096);
		EXPECT_FALSE(Result.mMemoryDependencies.empty());
	}

	TEST(ArdaInductor, TimingOptimizationWithoutSamplesPreservesTheCompiledGraph)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Node(Graph, "node", {{}, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_FALSE(Graph.OptimizeFromTimingProfile());
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
	}

	TEST(ArdaInductor, MemoryObjectiveReusesCompatibleDeadValuesAndKeepsOutputsLive)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		FArdaInductorOptions O;
		O.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(G.SetOptions(O));
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c"), D = Buffer(G, "d");
		Node(G, "a", {{}, A});
		Node(G, "b", {A, B});
		Node(G, "c", {B, C});
		Node(G, "d", {C, D});
		ASSERT_TRUE(G.MarkOutput(D));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_GT(G.GetCompileResult().mAliasedBytes, 0u);
		EXPECT_LT(G.GetCompileResult().mAllocatedBytes, 1024u);
		EXPECT_GE(G.GetCompileResult().mAllocatedBytes, 512u);
	}

	TEST(ArdaInductor, PureSynchronizationDoesNotSplitAnOrderedCudaBatch)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto A = Buffer(Graph, "a"), B = Buffer(Graph, "b");
		const auto First = Node(Graph, "first", {{}, A}, "inductor.test.cuda");
		const auto Second = Node(Graph, "second", {A, B}, "inductor.test.cuda");
		const auto Sync = Graph.AttachOrFind("sync", "arda.sync", FArdaGraphSyncParameters{});
		ASSERT_TRUE(Sync);
		ASSERT_TRUE(Graph.AddDependency(First, Sync.mValue));
		ASSERT_TRUE(Graph.AddDependency(Sync.mValue, Second));
		ASSERT_TRUE(Graph.MarkOutput(B));
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(), (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
	}

	TEST(ArdaInductor, EfficiencyObjectiveDoesNotSerializeIndependentLifetimesForReuse)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto A = Buffer(Graph, "a"), B = Buffer(Graph, "b");
		Node(Graph, "a-producer", {{}, A});
		Node(Graph, "a-consumer", {A, {}, 1, 0, true});
		Node(Graph, "b-producer", {{}, B});
		Node(Graph, "b-consumer", {B, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mMemoryDependencies.empty());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 512u);
	}

	struct FRangeNodeParameters
	{
		eastl::vector<FArdaDependencyAccess> mAccesses;
		eastl::vector<FArdaDependencyResourceHandle> mColorTargets;
		eastl::vector<FArdaInductorPipelineRequest> mPipelines;
		bool mbPipelineStageOnly = false;
	};

	void RegisterRangeTestNode()
	{
		static std::once_flag Once;
		std::call_once(Once,
		    []
		    {
			    TArdaDependencyNodeDefinition<FRangeNodeParameters> Definition;
			    Definition.mName = "inductor.test.ranges";
			    Definition.mKind = EArdaDependencyNodeKind::Graphics;
			    Definition.mCanonicalKey = [](const FRangeNodeParameters& P)
			    {
				    eastl::string Key;
				    const auto Append = [&](uint64_t Value)
				    {
					    Key.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
				    };
				    Append(P.mAccesses.size());
				    for (const auto& A : P.mAccesses)
				    {
					    for (const uint64_t Value : {A.mResource.mGraph,
					             uint64_t(A.mResource.mIndex),
					             A.mResource.mGeneration,
					             uint64_t(A.mAccess),
					             uint64_t(A.mState),
					             A.mBufferRange.mByteOffset,
					             A.mBufferRange.mByteSize,
					             uint64_t(A.mTextureRange.mBaseMipLevel),
					             uint64_t(A.mTextureRange.mMipLevelCount),
					             uint64_t(A.mTextureRange.mBaseArraySlice),
					             uint64_t(A.mTextureRange.mArraySliceCount),
					             uint64_t(A.mTextureRange.mBasePlane),
					             uint64_t(A.mTextureRange.mPlaneCount)})
					    {
						    Append(Value);
					    }
				    }
				    Append(P.mColorTargets.size());
				    for (const auto Target : P.mColorTargets)
				    {
					    Append(Target.mGraph);
					    Append(Target.mIndex);
					    Append(Target.mGeneration);
				    }
				    Append(P.mbPipelineStageOnly);
				    Append(P.mPipelines.size());
				    for (const auto& Request : P.mPipelines)
				    {
					    Append(uint64_t(Request.mKind));
					    Append(Request.mSlot.size());
					    Key.append(Request.mSlot);
					    Append(Request.mGroup.size());
					    Key.append(Request.mGroup);
				    }
				    return Key;
			    };
			    Definition.mDescribe = [](const FRangeNodeParameters& P)
			    {
				    FArdaDependencyNodeDesc Desc;
				    Desc.mAccesses = P.mAccesses;
				    Desc.mColorTargets = P.mColorTargets;
				    Desc.mPipelines = P.mPipelines;
				    Desc.mbPipelineStageOnly = P.mbPipelineStageOnly;
				    Desc.mbSideEffect = true;
				    return Desc;
			    };
			    Definition.mRecord = [](FArdaDependencyExecutionContext&, const FRangeNodeParameters&)
			    {
				    return FArdaRHIStatus{};
			    };
			    ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Definition)));
		    });
	}

	TEST(ArdaInductor, RejectsUnwrittenBufferBytesAndAcceptsMergedProducerRanges)
	{
		RegisterRangeTestNode();
		for (const bool Gap : {false, true})
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "range", 256);
			FRangeNodeParameters Writer;
			for (const FArdaRHIBufferRange Range :
			    {FArdaRHIBufferRange{128, 64}, FArdaRHIBufferRange{0, 64}, FArdaRHIBufferRange{48, Gap ? 64u : 80u}})
			{
				Writer.mAccesses.push_back(
				    {Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, Range});
			}
			FRangeNodeParameters Reader;
			Reader.mAccesses.push_back(
			    {Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess, {0, 192}});
			ASSERT_TRUE(Graph.AttachOrFind("reader", "inductor.test.ranges", Reader));
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			const auto Status = Graph.EndGraphEdit();
			EXPECT_EQ(bool(Status), !Gap) << Status.mMessage.c_str();
		}
	}

	FArdaRHITextureDesc RangeTexture()
	{
		FArdaRHITextureDesc Desc;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mWidth = Desc.mHeight = 16;
		Desc.mMipLevels = 3;
		Desc.mArraySize = 3;
		Desc.mFormat = EArdaRHIFormat::R32Float;
		Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		return Desc;
	}

	TEST(ArdaInductor, RejectsExplicitTextureRangesInsteadOfClampingThem)
	{
		RegisterRangeTestNode();
		const FArdaRHITextureSubresourceRange InvalidRanges[] = {{1, 3, 0, 1, 0, 1},
		    {0, 1, 2, 2, 0, 1},
		    {0, 1, 0, 1, 0, 2},
		    {3, ArdaRHIAllSubresources, 0, 1, 0, 1},
		    {0, 0, 0, 1, 0, 1}};
		for (const auto Range : InvalidRanges)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Texture = Graph.CreateTexture("texture", RangeTexture());
			ASSERT_TRUE(Texture);
			FRangeNodeParameters Writer;
			Writer.mAccesses.push_back(
			    {Texture.mValue, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {}, Range});
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			EXPECT_FALSE(Graph.EndGraphEdit());
		}
	}

	TEST(ArdaInductor, RequiresProducerCoverageForTextureMipAndArrayReads)
	{
		RegisterRangeTestNode();
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Texture = Graph.CreateTexture("texture", RangeTexture());
			ASSERT_TRUE(Texture);
			FRangeNodeParameters Writer, Reader;
			Writer.mAccesses.push_back({Texture.mValue,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {1, 2, 1, 2}});
			Reader.mAccesses.push_back({Texture.mValue,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {Case == 1 ? 0u : 1u, 1, Case == 2 ? 0u : 1u, 1}});
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			ASSERT_TRUE(Graph.AttachOrFind("reader", "inductor.test.ranges", Reader));
			EXPECT_EQ(bool(Graph.EndGraphEdit()), Case == 0);
		}
	}

	TEST(ArdaInductor, FramebufferTargetsRequireExplicitWritableAccess)
	{
		RegisterRangeTestNode();
		for (uint32_t Case = 0; Case < 4; ++Case)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			auto Desc = RangeTexture();
			Desc.mUsage |= EArdaRHITextureUsage::RenderTarget;
			const auto Texture = Graph.CreateTexture("target", Desc);
			ASSERT_TRUE(Texture);
			FRangeNodeParameters Parameters;
			Parameters.mColorTargets.push_back(Texture.mValue);
			if (Case)
			{
				Parameters.mAccesses.push_back({Texture.mValue,
				    EArdaDependencyAccess::Write,
				    Case >= 2 ? EArdaRHIResourceState::RenderTarget : EArdaRHIResourceState::UnorderedAccess});
				if (Case == 3)
				{
					Parameters.mAccesses.back().mTextureRange = {1, 1, 2, 1};
				}
			}
			ASSERT_TRUE(Graph.AttachOrFind("raster", "inductor.test.ranges", Parameters));
			EXPECT_EQ(bool(Graph.EndGraphEdit()), Case >= 2);
		}
	}

	TEST(ArdaInductor, AsyncChainOverlapsIndependentGraphicsScheduledBeforeItsFirstNode)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaGraphNodeHandle Previous;
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(Graph, Name.c_str());
			Previous = Node(Graph, Name.c_str(), {Input, Output});
			Chain.push_back(Previous);
			Input = Output;
		}
		const auto Graphics = Node(Graph, "independent-graphics", {{}, {}, 100, 0, true}, "inductor.test.graphics");
		const auto Join = Node(Graph, "join", {Input, {}, 1, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.AddDependency(Graphics, Join));
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		ASSERT_EQ(Result.mExecutionOrder.front(), Graphics);
		for (const auto Handle : Chain)
		{
			const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Handle);
			ASSERT_NE(Position, Result.mExecutionOrder.end());
			EXPECT_EQ(Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())], EArdaRHIQueueType::Compute);
		}
	}

	template <typename Interface, typename Descriptor, EArdaRHIResourceType Kind>
	class TInductorExternalResource final : public Interface
	{
	public:
		explicit TInductorExternalResource(Descriptor Desc, const void* Identity = nullptr)
		    : mDesc(eastl::move(Desc)),
		      mIdentity(Identity)
		{
		}

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (--mReferences == 0)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return Kind;
		}

		const char* GetDebugName() const noexcept override
		{
			return "inductor external test";
		}

		const Descriptor& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity ? mIdentity : this;
		}

	private:
		Descriptor mDesc;
		const void* mIdentity;
		uint32_t mReferences = 0;
	};

	FArdaRHIBufferRef ExternalTestBuffer(uint64_t Bytes)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = Bytes;
		Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
		using FBuffer = TInductorExternalResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		return FArdaRHIBufferRef(new FBuffer(Desc));
	}

	TEST(ArdaInductor, RejectsDistinctImportWrappersWithTheSamePhysicalIdentity)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const uint32_t PhysicalIdentity = 1;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		using FBuffer = TInductorExternalResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		const FArdaRHIBufferRef First(new FBuffer(Desc, &PhysicalIdentity));
		const FArdaRHIBufferRef Second(new FBuffer(Desc, &PhysicalIdentity));
		ASSERT_NE(First.Get(), Second.Get());
		ASSERT_TRUE(Graph.ImportBuffer("first", First));
		EXPECT_FALSE(Graph.ImportBuffer("second", Second));
	}

	TEST(ArdaInductor, RetainedImportsConsumeMemoryEvenWhenAllReadersAreCulled)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto ImportedBuffer = Graph.ImportBuffer("buffer", ExternalTestBuffer(1024));
		ASSERT_TRUE(ImportedBuffer);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 16;
		TextureDesc.mHeight = 8;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		using FTexture = TInductorExternalResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;
		const auto ImportedTexture = Graph.ImportTexture("texture", FArdaRHITextureRef(new FTexture(TextureDesc)));
		ASSERT_TRUE(ImportedTexture);
		const auto Reader = Node(Graph, "unused-reader", {ImportedBuffer.mValue, {}});
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mExecutionOrder.empty());
		EXPECT_EQ(Result.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Reader}));
		EXPECT_EQ(Result.mAllocatedBytes, 1536u);
		EXPECT_EQ(Result.mPeakLiveBytes, 1536u);
	}

	TEST(ArdaInductor, CulledAdaptersRetainTheirDeclaredWorkspaceUntilRemoved)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Adapter = Node(Graph, "adapter", {{}, {}, 1, 0, false, 8192});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 8192u);
		EXPECT_EQ(Graph.GetCompileResult().mPeakLiveBytes, 8192u);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Adapter));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 0u);
	}

	TEST(ArdaInductor, EmptyScheduleCannotHideOverflowInRetainedImportAccounting)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.ImportBuffer("maximum", ExternalTestBuffer(UINT64_MAX)));
		ASSERT_TRUE(Graph.ImportBuffer("one-more", ExternalTestBuffer(1)));
		const auto Status = Graph.EndGraphEdit();
		EXPECT_FALSE(Status);
		EXPECT_TRUE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 0u);
		EXPECT_NE(Status.mMessage.find("overflow"), eastl::string::npos);
	}

	TEST(ArdaInductor, MemorySerializationDoesNotAdvertiseFalseAsyncOverlap)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(Graph.SetOptions(Options));
		const auto GraphicsBuffer = Buffer(Graph, "graphics-storage");
		Node(Graph, "a-graphics-producer", {{}, GraphicsBuffer, 100}, "inductor.test.graphics");
		const auto GraphicsEnd =
		    Node(Graph, "b-graphics-consumer", {GraphicsBuffer, {}, 100, 0, true}, "inductor.test.graphics");
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(Graph, Name.c_str());
			Chain.push_back(Node(Graph, Name.c_str(), {Input, Output}));
			Input = Output;
		}
		const auto Join = Node(Graph, "join", {Input, {}, 1, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.AddDependency(GraphicsEnd, Join));
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_FALSE(Result.mMemoryDependencies.empty());
		for (const auto Handle : Chain)
		{
			const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Handle);
			ASSERT_NE(Position, Result.mExecutionOrder.end());
			EXPECT_EQ(Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())], EArdaRHIQueueType::Graphics);
		}
	}

	TEST(ArdaInductor, RejectsInvalidPipelineRequestsBeforePublishingADeviceIndependentEdit)
	{
		RegisterRangeTestNode();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FRangeNodeParameters Parameters;
		FArdaInductorPipelineRequest Request;
		Request.mKind = EArdaPipelineStateKind::Compute;
		Parameters.mPipelines.push_back(Request);
		ASSERT_TRUE(Graph.AttachOrFind("missing-shader", "inductor.test.ranges", Parameters));
		EXPECT_FALSE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Request.mSlot = "default";
		Parameters.mPipelines.push_back(Request);
		ASSERT_TRUE(Graph.AttachOrFind("duplicate-slots", "inductor.test.ranges", Parameters));
		const auto Status = Graph.EndGraphEdit();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("duplicate"), eastl::string::npos);
	}

	TEST(ArdaInductor, StageOnlyNodesCannotHidePhysicalAccessesOrPipelineRequests)
	{
		RegisterRangeTestNode();
		for (const bool Pipeline : {false, true})
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FRangeNodeParameters Parameters;
			Parameters.mbPipelineStageOnly = true;
			if (Pipeline)
			{
				Parameters.mPipelines.push_back({});
			}
			else
			{
				Parameters.mAccesses.push_back(
				    {Buffer(Graph, "storage"), EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			}
			ASSERT_TRUE(Graph.AttachOrFind("stage", "inductor.test.ranges", Parameters));
			const auto Status = Graph.EndGraphEdit();
			EXPECT_FALSE(Status);
			EXPECT_NE(Status.mMessage.find("Stage-only"), eastl::string::npos);
		}
	}

	TEST(ArdaInductor, SingletonRegistryIsTypedAndLibraryIsDiscoverable)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		auto Names = FArdaNodeRegistry::Get().GetNames();
		EXPECT_NE(eastl::find(Names.begin(), Names.end(), "arda.copy-buffer"), Names.end());
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		EXPECT_FALSE(G.AttachOrFind("wrong-type", "arda.upload", uint32_t(4)));
		EXPECT_FALSE(G.AttachOrFind("missing", "not-registered", uint32_t(4)));
		ASSERT_TRUE(G.CancelGraphEdit());
	}
}
