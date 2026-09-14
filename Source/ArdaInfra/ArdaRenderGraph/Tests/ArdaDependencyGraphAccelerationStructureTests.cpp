#include "ArdaDependencyNode.h"
#include "ArdaDependencyGraph.h"
#include "Allocator/ArdaMemoryPlanner.h"

#include <gtest/gtest.h>

namespace
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

		// Identify the semantic inputs for node deduplication.
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

	void RegisterASNodes()
	{
		ASSERT_TRUE(TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Compute>::Register());
		ASSERT_TRUE(TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Graphics>::Register());
		ASSERT_TRUE(TArdaAccelerationStructureTestNode<EArdaDependencyNodeKind::Copy>::Register());
	}

	TEST(ArdaDependencyGraphAccelerationStructure, ImportsRetainStorageAndRejectDuplicatePhysicalHandles)
	{
		int Identity = 0;
		FArdaRHIAccelStructRef First(new FArdaTestAccelerationStructure(&Identity));
		FArdaRHIAccelStructRef Wrapper(new FArdaTestAccelerationStructure(&Identity));
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		EXPECT_FALSE(Graph.ImportAccelerationStructure("null", {}));
		auto Imported = Graph.ImportAccelerationStructure("BLAS", First);
		ASSERT_TRUE(Imported);
		EXPECT_EQ(Graph.FindResource(Imported.mValue)->mExternalAccelerationStructure, First);
		EXPECT_FALSE(Graph.ImportAccelerationStructure("alias", Wrapper));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u);
	}

	TEST(ArdaDependencyGraphAccelerationStructure, BuildReaderOrderingAndGraphicsOwnershipAreAutomatic)
	{
		RegisterASNodes();
		int Identity = 0;
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Resource = Graph.ImportAccelerationStructure("BLAS",
		    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)));
		ASSERT_TRUE(Resource);
		FArdaInductorOptions Options;
		Options.mMinimumAsyncChain = 1;
		Options.mMinimumAsyncSlack = 1;
		ASSERT_TRUE(Graph.SetOptions(Options));
		auto Reader = Graph.AttachOrFind("reader", "test.as.compute", FArdaASParameters{Resource.mValue});
		auto Writer = Graph.AttachOrFind("build",
		    "test.as.compute",
		    FArdaASParameters{Resource.mValue, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite});
		ASSERT_TRUE(Reader);
		ASSERT_TRUE(Writer);
		ASSERT_TRUE(Graph.AttachOrFind("independent graphics", "test.as.graphics", FArdaASParameters{}));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Writer.mValue, Reader.mValue));
		for (auto Queue : Graph.GetCompileResult().mQueues)
		{
			EXPECT_EQ(Queue, EArdaRHIQueueType::Graphics);
		}
	}

	TEST(ArdaDependencyGraphAccelerationStructure, RepeatedBuildsPreserveInitialAndIntermediateWholeObjectReads)
	{
		RegisterASNodes();
		for (const bool bReadWriteUpdate : {false, true})
		{
			SCOPED_TRACE(bReadWriteUpdate);
			int Identity = 0;
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Graph.ImportAccelerationStructure("rebuilt BLAS",
			    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)));
			ASSERT_TRUE(Resource);
			const FArdaASParameters Read{Resource.mValue,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::AccelStructRead};
			const FArdaASParameters Write{Resource.mValue,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::AccelStructWrite};
			const FArdaASParameters Update{Resource.mValue,
			    bReadWriteUpdate ? EArdaDependencyAccess::ReadWrite : EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::AccelStructWrite};
			const auto Initial = Graph.AttachOrFind("read imported contents", "test.as.compute", Read);
			const auto First = Graph.AttachOrFind("first build", "test.as.compute", Write);
			const auto Before = Graph.AttachOrFind("read first build", "test.as.compute", Read);
			const auto Second = Graph.AttachOrFind("second build", "test.as.compute", Update);
			const auto After = Graph.AttachOrFind("read second build", "test.as.compute", Read);
			ASSERT_TRUE(Initial && First && Before && Second && After);
			const auto Status = Graph.EndGraphEdit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{Initial.mValue,
			        First.mValue,
			        Before.mValue,
			        Second.mValue,
			        After.mValue}));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Initial.mValue, First.mValue));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(First.mValue, Before.mValue));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Before.mValue, Second.mValue));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Second.mValue, After.mValue));
			EXPECT_FALSE(Graph.GetTopology().FindEdge(First.mValue, After.mValue));
			const auto Rebuild = Graph.GetTopology().FindEdge(First.mValue, Second.mValue);
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
		RegisterASNodes();
		int Identity = 0;
		for (uint32_t Case = 0; Case != 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			auto Resource = Graph.ImportAccelerationStructure("BLAS",
			    FArdaRHIAccelStructRef(new FArdaTestAccelerationStructure(&Identity)));
			ASSERT_TRUE(Resource);
			FArdaASParameters P;
			P.mResource = Resource.mValue;
			if (Case == 0)
			{
				P.mRange.mByteSize = 256;
			}
			if (Case == 1)
			{
				P.mState = EArdaRHIResourceState::ShaderResource;
			}
			auto Attached = Graph.AttachOrFind("invalid", Case == 2 ? "test.as.copy" : "test.as.compute", P);
			ASSERT_TRUE(Attached);
			EXPECT_FALSE(Graph.EndGraphEdit());
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.ImportAccelerationStructure("AS", Resource));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u + 256u);
	}
}
