#include "ArdaDependencyGraph.h"
#include "ArdaInductorMemory.h"

#include <gtest/gtest.h>
#include <mutex>

namespace
{
	using namespace arda;

	class FRetainedASInput final : public IArdaRHIBuffer
	{
	public:
		FRetainedASInput(const void* Identity, bool Known)
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

	class FTestAccelerationStructure final : public IArdaRHIAccelStruct
	{
	public:
		explicit FTestAccelerationStructure(const void* Identity, const FArdaRHIBufferRef& Input = {})
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

	struct FASParameters
	{
		FArdaDependencyResourceHandle mResource;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		EArdaRHIResourceState mState = EArdaRHIResourceState::AccelStructRead;
		FArdaRHIBufferRange mRange;
	};

	void RegisterASNodes()
	{
		static std::once_flag Once;
		std::call_once(Once,
		    []
		    {
			    for (const auto Kind : {EArdaDependencyNodeKind::Compute,
			             EArdaDependencyNodeKind::Graphics,
			             EArdaDependencyNodeKind::Copy})
			    {
				    TArdaDependencyNodeDefinition<FASParameters> D;
				    D.mName = Kind == EArdaDependencyNodeKind::Compute ? "test.as.compute"
				        : Kind == EArdaDependencyNodeKind::Graphics    ? "test.as.graphics"
				                                                       : "test.as.copy";
				    D.mKind = Kind;
				    D.mCanonicalKey = [](const FASParameters& P)
				    {
					    eastl::string Key;
					    for (uint64_t V : {P.mResource.mGraph,
					             uint64_t(P.mResource.mIndex),
					             P.mResource.mGeneration,
					             uint64_t(P.mAccess),
					             uint64_t(P.mState),
					             P.mRange.mByteOffset,
					             P.mRange.mByteSize})
					    {
						    Key.append(reinterpret_cast<const char*>(&V), sizeof(V));
					    }
					    return Key;
				    };
				    D.mDescribe = [](const FASParameters& P)
				    {
					    FArdaDependencyNodeDesc Desc;
					    Desc.mbSideEffect = true;
					    Desc.mEstimatedCost = 10;
					    if (P.mResource)
					    {
						    Desc.mAccesses.push_back({P.mResource, P.mAccess, P.mState, P.mRange});
					    }
					    return Desc;
				    };
				    D.mRecord = [](FArdaDependencyExecutionContext&, const FASParameters&)
				    {
					    return FArdaRHIStatus{};
				    };
				    ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(D)));
			    }
		    });
	}

	TEST(ArdaDependencyGraphAccelerationStructure, ImportsRetainStorageAndRejectDuplicatePhysicalHandles)
	{
		int Identity = 0;
		FArdaRHIAccelStructRef First(new FTestAccelerationStructure(&Identity));
		FArdaRHIAccelStructRef Wrapper(new FTestAccelerationStructure(&Identity));
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
		    FArdaRHIAccelStructRef(new FTestAccelerationStructure(&Identity)));
		ASSERT_TRUE(Resource);
		FArdaInductorOptions Options;
		Options.mMinimumAsyncChain = 1;
		Options.mMinimumAsyncSlack = 1;
		ASSERT_TRUE(Graph.SetOptions(Options));
		auto Reader = Graph.AttachOrFind("reader", "test.as.compute", FASParameters{Resource.mValue});
		auto Writer = Graph.AttachOrFind("build",
		    "test.as.compute",
		    FASParameters{Resource.mValue, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite});
		ASSERT_TRUE(Reader);
		ASSERT_TRUE(Writer);
		ASSERT_TRUE(Graph.AttachOrFind("independent graphics", "test.as.graphics", FASParameters{}));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Writer.mValue, Reader.mValue));
		for (auto Queue : Graph.GetCompileResult().mQueues)
		{
			EXPECT_EQ(Queue, EArdaRHIQueueType::Graphics);
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
			    FArdaRHIAccelStructRef(new FTestAccelerationStructure(&Identity)));
			ASSERT_TRUE(Resource);
			FASParameters P;
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
		FArdaInductorMemoryRequest Request;
		Request.mKind = EArdaInductorMemoryKind::AccelerationStructure;
		Request.mExternalAccelerationStructure = FArdaRHIAccelStructRef(new FTestAccelerationStructure(&Identity));
		Request.mRequirements = {65536, 256, 1};
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mBudgetBytes = 131072;
		auto Plan = PlanArdaInductorMemory({Request}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 131072u);
		EXPECT_EQ(Plan.mValue.mOwnedBytes, 0u);
		EXPECT_TRUE(Plan.mValue.mHeaps.empty());
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaInductorMemory({Request}, Options));
	}

	TEST(ArdaDependencyGraphAccelerationStructure, RetainedGeometryAllocationCountsOnceWithOrWithoutExplicitImport)
	{
		int ASIdentity = 0, BufferIdentity = 0;
		FArdaRHIBufferRef Input(new FRetainedASInput(&BufferIdentity, true));
		FArdaInductorMemoryRequest AS;
		AS.mKind = EArdaInductorMemoryKind::AccelerationStructure;
		AS.mExternalAccelerationStructure = FArdaRHIAccelStructRef(new FTestAccelerationStructure(&ASIdentity, Input));
		AS.mRequirements = {65536, 256, 1};
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mBudgetBytes = 196608;
		auto Plan = PlanArdaInductorMemory({AS}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 196608u);
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaInductorMemory({AS}, Options));
		++Options.mBudgetBytes;
		FArdaInductorMemoryRequest Buffer;
		Buffer.mIdentifier = 1;
		Buffer.mExternalBuffer = Input;
		Buffer.mBufferDesc = Input->GetDesc();
		Buffer.mRequirements = {65536, 256, 1};
		Plan = PlanArdaInductorMemory({AS, Buffer}, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, 196608u);
	}

	TEST(ArdaDependencyGraphAccelerationStructure, UnknownRetainedGeometryRejectsHardCapButAllowsUnboundedGraph)
	{
		int ASIdentity = 0, BufferIdentity = 0;
		FArdaRHIBufferRef Input(new FRetainedASInput(&BufferIdentity, false));
		FArdaRHIAccelStructRef Resource(new FTestAccelerationStructure(&ASIdentity, Input));
		FArdaInductorMemoryRequest AS;
		AS.mKind = EArdaInductorMemoryKind::AccelerationStructure;
		AS.mExternalAccelerationStructure = Resource;
		AS.mRequirements = {65536, 256, 1};
		FArdaInductorMemoryOptions Options;
		Options.mBudgetBytes = 1ull << 30;
		auto Plan = PlanArdaInductorMemory({AS}, Options);
		EXPECT_FALSE(Plan);
		EXPECT_EQ(Plan.mStatus.mCode, EArdaRHIResult::Unsupported);
		Options.mBudgetBytes = UINT64_MAX;
		EXPECT_TRUE(PlanArdaInductorMemory({AS}, Options));
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.ImportAccelerationStructure("AS", Resource));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 131072u + 256u);
	}
}
