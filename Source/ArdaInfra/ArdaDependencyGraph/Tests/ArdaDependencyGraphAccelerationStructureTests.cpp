#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "ArdaDependencyGraph.h"
#include "RHI/Memory/ArdaMemoryPlanner.h"

#include <gtest/gtest.h>

namespace arda::dependencygraphaccelerationstructure_tests
{
	using namespace arda;

	class FArdaRetainedASInput final : public IArdaRHIBuffer
	{
	public:
		FArdaRetainedASInput(const void* Identity, bool Known)
		    : mIdentity(Identity),
		      mbKnown(Known)
		{
			mDesc.mByteSize = 256;
			mDesc.mUsage = EArdaRHIBufferUsage::AccelStructBuildInput;
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
			return EArdaRHIResourceType::Buffer;
		}

		const char* GetDebugName() const noexcept override
		{
			return "retained AS input";
		}

		const FArdaRHIBufferDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity;
		}

		FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept override
		{
			return mbKnown ? FArdaRHIMemoryAllocationInfo{mIdentity, 65536, true} : FArdaRHIMemoryAllocationInfo{};
		}

	private:
		const void* mIdentity;
		bool mbKnown;
		FArdaRHIBufferDesc mDesc;
		uint32_t mReferences = 0;
	};

	class FArdaTestAccelerationStructure final : public IArdaRHIAccelStruct
	{
	public:
		explicit FArdaTestAccelerationStructure(const void* Identity, const FArdaRHIBufferRef& Input = {})
		    : mIdentity(Identity)
		{
			if (Input)
			{
				FArdaRHIRayTracingGeometryDesc Geometry;
				Geometry.mVertexOrAABBBuffer = Input;
				Geometry.mIndexBuffer = Input;
				mDesc.mBottomLevelGeometries = {Geometry};
			}
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
			return EArdaRHIResourceType::AccelStruct;
		}

		const char* GetDebugName() const noexcept override
		{
			return "persistent AS test";
		}

		const FArdaRHIAccelStructDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		bool IsCompacted() const noexcept override
		{
			return false;
		}

		uint64_t GetDeviceAddress() const noexcept override
		{
			return 256;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity;
		}

		FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept override
		{
			return {mIdentity, 131072, true};
		}

	private:
		FArdaRHIAccelStructDesc mDesc;
		const void* mIdentity;
		uint32_t mReferences = 0;
	};

	struct FArdaASParameters
	{
		FArdaDependencyResourceHandle mResource;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		EArdaRHIResourceState mState = EArdaRHIResourceState::AccelStructRead;
		FArdaRHIBufferRange mRange;
	};

	template <EArdaDependencyNodeKind Kind>
	struct TArdaAccelerationStructureTestNode
	    : TArdaDependencyNode<TArdaAccelerationStructureTestNode<Kind>, FArdaASParameters, Kind>
	{
		using FArdaParameters = FArdaASParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Kind == EArdaDependencyNodeKind::Compute    ? "test.as.compute"
			        : Kind == EArdaDependencyNodeKind::Graphics ? "test.as.graphics"
			                                                    : "test.as.copy",
			    1};
		}

		// Identify the semantic inputs for construction caching.
		static eastl::string GetCanonicalKey(const FArdaASParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mResource)
			    .Value(P.mAccess)
			    .Value(P.mState)
			    .BufferRange(P.mRange)
			    .Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaASParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			Desc.mEstimatedCost = 10;
			if (P.mResource)
			{
				Desc.mAccesses.push_back({P.mResource, P.mAccess, P.mState, P.mRange});
			}
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaASParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}
	};

	using FArdaASComputeNode = TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Compute>;
	ARDA_DECLARE_GRAPH_NODE(FArdaASComputeNode, ASCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaASComputeNode, ASCompute);
	using FArdaASGraphicsNode = TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Graphics>;
	ARDA_DECLARE_GRAPH_NODE(FArdaASGraphicsNode, ASGraphics);
	ARDA_DEFINE_GRAPH_NODE(FArdaASGraphicsNode, ASGraphics);
	using FArdaASCopyNode = TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Copy>;
	ARDA_DECLARE_GRAPH_NODE(FArdaASCopyNode, ASCopy);
	ARDA_DEFINE_GRAPH_NODE(FArdaASCopyNode, ASCopy);

	TEST(ArdaDependencyGraphAccelerationStructure, ImportsRetainStorageAndRejectDuplicatePhysicalHandles)
	{
		int Identity = 0;
		FArdaRHIAccelStructRef First(new FArdaTestAccelerationStructure(&Identity));
		FArdaRHIAccelStructRef Wrapper(new FArdaTestAccelerationStructure(&Identity));
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit NullEdit(Graph);
			EXPECT_FALSE(BindAccelerationStructure(NullEdit, {}, "null"));
			EXPECT_FALSE(NullEdit.Commit());
			ASSERT_TRUE(NullEdit.Cancel());
		}
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Imported = BindAccelerationStructure(Edit, First, "BLAS");
		ASSERT_TRUE(Imported);
		EXPECT_EQ(Graph.FindResource(Imported.GetHandle())->mExternalAccelerationStructure, First);
		ASSERT_TRUE(Edit.Commit());
		{
			FArdaGraphEdit AliasEdit(Graph);
			EXPECT_FALSE(BindAccelerationStructure(AliasEdit, Wrapper, "alias"));
			EXPECT_FALSE(AliasEdit.Commit());
			ASSERT_TRUE(AliasEdit.Cancel());
		}
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u);
	}

	TEST(ArdaDependencyGraphAccelerationStructure, BuildReaderOrderingAndGraphicsOwnershipAreAutomatic)
	{
		int Identity = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Resource = BindAccelerationStructure(Edit,
		    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)),
		    "BLAS");
		ASSERT_TRUE(Resource);
		FArdaInductorOptions Options;
		Options.mMinimumAsyncChain = 1;
		Options.mMinimumAsyncSlack = 1;
		ASSERT_TRUE(Edit.SetOptions(Options));
		auto Reader = ASCompute(Edit, FArdaASParameters{Resource.GetHandle()}, "reader");
		auto Writer = ASCompute(Edit,
		    FArdaASParameters{Resource.GetHandle(),
		        EArdaDependencyAccess::Write,
		        EArdaRHIResourceState::AccelStructWrite},
		    "build");
		ASSERT_TRUE(Reader);
		ASSERT_TRUE(Writer);
		ASSERT_TRUE(ASGraphics(Edit, FArdaASParameters{}, "independent graphics"));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Writer.GetHandle(), Reader.GetHandle()));
		for (auto Queue : Graph.GetCompileResult().mQueues)
		{
			EXPECT_EQ(Queue, EArdaRHIQueueType::Graphics);
		}
	}

	TEST(ArdaDependencyGraphAccelerationStructure, RepeatedBuildsPreserveInitialAndIntermediateWholeObjectReads)
	{
		for (const bool bReadWriteUpdate : {false, true})
		{
			SCOPED_TRACE(bReadWriteUpdate);
			int Identity = 0;
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.GetStatus());
			const auto Resource = BindAccelerationStructure(Edit,
			    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)),
			    "rebuilt BLAS");
			ASSERT_TRUE(Resource);
			const FArdaASParameters Read{Resource.GetHandle(),
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::AccelStructRead};
			const FArdaASParameters Write{Resource.GetHandle(),
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::AccelStructWrite};
			const FArdaASParameters Update{Resource.GetHandle(),
			    bReadWriteUpdate ? EArdaDependencyAccess::ReadWrite : EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::AccelStructWrite};
			const auto Initial = ASCompute(Edit, Read, "read imported contents");
			const auto First = ASCompute(Edit, Write, "first build");
			const auto Before = ASCompute(Edit, Read, "read first build");
			const auto Second = ASCompute(Edit, Update, "second build");
			const auto After = ASCompute(Edit, Read, "read second build");
			ASSERT_TRUE(Initial && First && Before && Second && After);
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{Initial.GetHandle(),
			        First.GetHandle(),
			        Before.GetHandle(),
			        Second.GetHandle(),
			        After.GetHandle()}));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Initial.GetHandle(), First.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(First.GetHandle(), Before.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Before.GetHandle(), Second.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Second.GetHandle(), After.GetHandle()));
			EXPECT_FALSE(Graph.GetTopology().FindEdge(First.GetHandle(), After.GetHandle()));
			const auto Rebuild = Graph.GetTopology().FindEdge(First.GetHandle(), Second.GetHandle());
			ASSERT_TRUE(Rebuild);
			EXPECT_TRUE(Graph.GetTopology().TryGetEdge(Rebuild)->mPayload.mbHazard);
			EXPECT_EQ(Graph.GetTopology().TryGetEdge(Rebuild)->mPayload.mbResource, bReadWriteUpdate);
			EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u);
			for (const auto Queue : Graph.GetCompileResult().mQueues)
			{
				EXPECT_EQ(Queue, EArdaRHIQueueType::Graphics);
			}
		}
	}

	TEST(ArdaDependencyGraphAccelerationStructure, RejectsBufferRangesNonASStatesAndCopyQueues)
	{
		int Identity = 0;
		for (uint32_t Case = 0; Case != 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.GetStatus());
			auto Resource = BindAccelerationStructure(Edit,
			    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)),
			    "BLAS");
			ASSERT_TRUE(Resource);
			FArdaASParameters P;
			P.mResource = Resource.GetHandle();
			if (Case == 0)
			{
				P.mRange.mByteSize = 256;
			}
			if (Case == 1)
			{
				P.mState = EArdaRHIResourceState::ShaderResource;
			}
			auto Attached = Case == 2 ? ASCopy(Edit, P, "invalid") : ASCompute(Edit, P, "invalid");
			ASSERT_TRUE(Attached);
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.Compile());
		}
	}

	TEST(ArdaDependencyGraphAccelerationStructure, MemoryPlanSharesImportedStorageAcrossFramePools)
	{
		int Identity = 0;
		FArdaMemoryRequest Request;
		Request.mKind = EArdaMemoryKind::AccelerationStructure;
		Request.mExternalAccelerationStructure = FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity));
		Request.mRequirements = {65536, 256, 1};
		FArdaMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mBudgetBytes = 131072;
		auto Plan = PlanArdaMemory({Request}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 131072u);
		EXPECT_EQ(Plan.mValue.mOwnedBytes, 0u);
		EXPECT_TRUE(Plan.mValue.mHeaps.empty());
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaMemory({Request}, Options));
	}

	TEST(ArdaDependencyGraphAccelerationStructure, RetainedGeometryAllocationCountsOnceWithOrWithoutExplicitImport)
	{
		int ASIdentity = 0, BufferIdentity = 0;
		FArdaRHIBufferRef Input(new FArdaRetainedASInput(&BufferIdentity, true));
		FArdaMemoryRequest AS;
		AS.mKind = EArdaMemoryKind::AccelerationStructure;
		AS.mExternalAccelerationStructure =
		    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&ASIdentity, Input));
		AS.mRequirements = {65536, 256, 1};
		FArdaMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mBudgetBytes = 196608;
		auto Plan = PlanArdaMemory({AS}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 196608u);
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaMemory({AS}, Options));
		++Options.mBudgetBytes;
		FArdaMemoryRequest Buffer;
		Buffer.mIdentifier = 1;
		Buffer.mExternalBuffer = Input;
		Buffer.mBufferDesc = Input->GetDesc();
		Buffer.mRequirements = {65536, 256, 1};
		Plan = PlanArdaMemory({AS, Buffer}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 196608u);
	}

	TEST(ArdaDependencyGraphAccelerationStructure, UnknownRetainedGeometryRejectsHardCapButAllowsUnboundedGraph)
	{
		int ASIdentity = 0, BufferIdentity = 0;
		FArdaRHIBufferRef Input(new FArdaRetainedASInput(&BufferIdentity, false));
		FArdaRHIAccelStructRef Resource(new FArdaTestAccelerationStructure(&ASIdentity, Input));
		FArdaMemoryRequest AS;
		AS.mKind = EArdaMemoryKind::AccelerationStructure;
		AS.mExternalAccelerationStructure = Resource;
		AS.mRequirements = {65536, 256, 1};
		FArdaMemoryOptions Options;
		Options.mBudgetBytes = 1ull << 30;
		auto Plan = PlanArdaMemory({AS}, Options);
		EXPECT_FALSE(Plan);
		EXPECT_EQ(Plan.mStatus.mCode, EArdaRHIResult::Unsupported);
		Options.mBudgetBytes = UINT64_MAX;
		EXPECT_TRUE(PlanArdaMemory({AS}, Options));
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		ASSERT_TRUE(BindAccelerationStructure(Edit, Resource, "AS"));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u + 256u);
	}
}
