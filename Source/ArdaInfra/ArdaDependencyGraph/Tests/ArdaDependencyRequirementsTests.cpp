#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "Inductor/Runtime/ArdaDependencyGraphCuda.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "../../ArdaBackend/Private/RHI/Device/ArdaRHIDevicePrivate.h"
#include <gtest/gtest.h>
#include <string>
#include <type_traits>

namespace arda::requirements_tests
{
	using namespace arda;

	/** Admission uses a real facade and capability snapshots; every native operation is counted and rejected. */
	class FArdaAdmissionProvider final : public IArdaRHIProviderDevice
	{
	public:
		FArdaRHICapabilities mCapabilities;
		FArdaCudaCapabilities mCuda;
		uint32_t mNativeAttempts = 0, mWaits = 0;

		static FArdaRHIStatus Unexpected()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Admission must not perform native GPU work.");
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mCapabilities;
		}

		FArdaCudaCapabilities GetCudaCapabilities() const override
		{
			return mCuda;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return {};
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return {};
		}

#define ARDA_ADMISSION_NO_OBJECT(Method, Parameter)                                                                    \
	FArdaProviderObjectResult Method(const Parameter&) override                                                        \
	{                                                                                                                  \
		++mNativeAttempts;                                                                                             \
		return {{}, Unexpected()};                                                                                     \
	}
		ARDA_ADMISSION_NO_OBJECT(CreateTexture, FArdaRHITextureDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateBuffer, FArdaRHIBufferDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateHeap, FArdaRHIHeapDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateStagingTexture, FArdaRHIStagingTextureDesc)
		ARDA_ADMISSION_NO_OBJECT(ImportTexture, FArdaRHINativeTextureImportDesc)
		ARDA_ADMISSION_NO_OBJECT(ImportBuffer, FArdaRHINativeBufferImportDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateSampler, FArdaRHISamplerDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateShader, FArdaRHIShaderDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateBindingLayout, FArdaRHIBindingLayoutDesc)
		ARDA_ADMISSION_NO_OBJECT(CreateFramebuffer, FArdaProviderFramebufferCreateInfo)
		ARDA_ADMISSION_NO_OBJECT(CreateGraphicsPipeline, FArdaProviderGraphicsPipelineCreateInfo)
		ARDA_ADMISSION_NO_OBJECT(CreateComputePipeline, FArdaProviderComputePipelineCreateInfo)
#undef ARDA_ADMISSION_NO_OBJECT

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    const FArdaProviderObjectRef&,
		    uint64_t) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&,
		    const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderBinding>&) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureSlice&,
		    EArdaRHICpuAccess) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef&) override
		{
			++mNativeAttempts;
			return Unexpected();
		}

		TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef&, uint64_t, size_t) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		void UnmapBuffer(const FArdaProviderObjectRef&) noexcept override
		{
			++mNativeAttempts;
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			++mNativeAttempts;
			return {{}, Unexpected()};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			++mWaits;
			return {};
		}

		FArdaRHIStatus WaitForSubmission(uint64_t) override
		{
			++mWaits;
			return Unexpected();
		}

		void RunGarbageCollection() override
		{
		}

		void FlushPipelineCache() noexcept override
		{
		}
	};

	struct FArdaAdmissionParameters
	{
		FArdaDependencyResourceHandle mOutput;
		bool mbMesh = true;
		bool mbEnvironment = false;
	};

	struct FArdaAdmissionNode : TArdaComputeDependencyNode<FArdaAdmissionNode, FArdaAdmissionParameters>
	{
		inline static uint32_t mChecks = 0, mDeclarations = 0, mPreparations = 0, mInstances = 0, mDescriptions = 0;
		inline static bool mbEnvironmentReady = true;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.typed", 1};
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& P)
		{
			++mChecks;
			FArdaDependencyNodeRequirements R;
			R.mFeatures.mbRequireMeshShaders = P.mbMesh;
			if (P.mbEnvironment)
			{
				R.mEnvironment.push_back({"example-library",
				    [](const IArdaRHIDevice&)
				    {
					    return mbEnvironmentReady
					        ? FArdaRHIStatus{}
					        : FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Library runtime unavailable.");
				    }});
			}
			return R;
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
		{
			++mDeclarations;
			FArdaRHIBufferDesc D;
			D.mByteSize = 64;
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			return C.Buffer(P.mOutput, "Output", D);
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).Value(P.mbMesh).Value(P.mbEnvironment).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPreparations;
			return {eastl::make_shared<const FArdaState>(), {}};
		}

		static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FArdaParameters&,
		    const FArdaState&)
		{
			++mInstances;
			return {eastl::make_shared<FArdaInstanceState>(), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			++mDescriptions;
			FArdaDependencyNodeDesc D;
			D.mAccesses = {{P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
			D.mbSideEffect = true;
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAdmissionNode, Admission);
	ARDA_DEFINE_GRAPH_NODE(FArdaAdmissionNode, Admission);

	class FArdaDependencyRequirementsTest : public testing::Test
	{
	protected:
		eastl::shared_ptr<FArdaAdmissionProvider> mProvider;
		FArdaRHIDeviceRef mDevice;
		eastl::vector<eastl::string> mDefinitions;

		void SetUp() override
		{
			mProvider = eastl::make_shared<FArdaAdmissionProvider>();
			mDevice = CreateArdaRHIDevice(mProvider);
			FArdaAdmissionNode::mChecks = FArdaAdmissionNode::mDeclarations = FArdaAdmissionNode::mPreparations = 0;
			FArdaAdmissionNode::mInstances = FArdaAdmissionNode::mDescriptions = 0;
			FArdaAdmissionNode::mbEnvironmentReady = true;
			mDefinitions.push_back(FArdaAdmissionNode::GetMetadata().mName);
			for (const auto& Name : mDefinitions)
			{
				(void)FArdaNodeRegistry::Get().Unregister(Name);
			}
		}

		void TearDown() override
		{
			EXPECT_EQ(mProvider->mNativeAttempts, 0u);
			EXPECT_EQ(mProvider->mWaits, 0u);
			for (const auto& Name : mDefinitions)
			{
				(void)FArdaNodeRegistry::Get().Unregister(Name);
			}
		}

		void ExpectNoSetup()
		{
			EXPECT_EQ(FArdaAdmissionNode::mDeclarations, 0u);
			EXPECT_EQ(FArdaAdmissionNode::mPreparations, 0u);
			EXPECT_EQ(FArdaAdmissionNode::mInstances, 0u);
			EXPECT_EQ(FArdaAdmissionNode::mDescriptions, 0u);
		}
	};

	template <class Operation>
	FArdaRHIStatus RejectAdmission(FArdaDependencyGraph& Graph, Operation&& Record)
	{
		FArdaGraphEdit Rejected(Graph);
		EXPECT_FALSE(Record(Rejected));
		const auto Status = Rejected.GetStatus();
		EXPECT_FALSE(Status);
		EXPECT_FALSE(Rejected.Commit());
		return Status;
	}

	TEST_F(FArdaDependencyRequirementsTest, TypedAndNamedPreflightRejectBeforeOutputOrPreparation)
	{

		FArdaDependencyGraph Graph(mDevice);
		const auto Typed = RejectAdmission(Graph,
		    [](FArdaGraphEdit& Edit)
		    {
			    return Admission(Edit, {}, "typed");
		    });
		EXPECT_EQ(Typed.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Typed.mMessage.find("mesh shaders"), eastl::string::npos);
		EXPECT_NE(Typed.mMessage.find("typed"), eastl::string::npos);
		const auto Named = RejectAdmission(Graph,
		    [](FArdaGraphEdit& Edit)
		    {
			    return Edit.Invoke(FArdaAdmissionNode::GetMetadata().mName, FArdaAdmissionParameters{}, "named");
		    });
		EXPECT_EQ(Named.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(FArdaAdmissionNode::mChecks, 2u);
		ExpectNoSetup();
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		EXPECT_FALSE(Graph.FindNode("typed"));
		EXPECT_FALSE(Graph.FindNode("named"));
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Probe;
		Probe.mByteSize = 4;
		const auto First = AllocateBuffer(Edit, Probe, "first resource");
		ASSERT_TRUE(First);
		EXPECT_EQ(First.GetHandle().mIndex, 0u);
	}

	TEST_F(FArdaDependencyRequirementsTest,
	    SuccessfulAdmissionRetainsRequirementsAndUsesPublicParametersForEveryOperation)
	{

		mProvider->mCapabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		const FArdaAdmissionParameters Input{{}, true, true};
		const auto First = Admission(Edit, Input, "supported");
		ASSERT_TRUE(First) << Edit.GetStatus().mMessage.c_str();
		ASSERT_TRUE(First.GetBuffer("Output"));
		const auto& Payload = Graph.GetTopology().TryGetNode(First.GetHandle())->mPayload;
		EXPECT_NE(Payload.mDefinition->mParameterType, Payload.mDefinition->mPreparedParameterType);
		EXPECT_TRUE(Payload.mDesc.mRequirements.mFeatures.mbRequireMeshShaders);
		EXPECT_EQ(Payload.mDesc.mRequirements.mEnvironment.size(), 1u);
		const auto Again = Edit.Invoke(FArdaAdmissionNode::GetMetadata().mName, Input, "supported again");
		ASSERT_TRUE(Again);
		EXPECT_NE(First.GetHandle(), Again.GetHandle());
		EXPECT_EQ(FArdaAdmissionNode::mChecks, 2u);
		EXPECT_EQ(FArdaAdmissionNode::mPreparations, 1u);
		EXPECT_EQ(FArdaAdmissionNode::mInstances, 2u);
		ASSERT_TRUE(Edit.Commit());

		// Admission is checked for every operation, even when its parameters match earlier calls.
		FArdaAdmissionNode::mbEnvironmentReady = false;
		const auto Failed = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Rejected)
		    {
			    return Admission(Rejected, Input, "environment changed");
		    });
		EXPECT_EQ(Failed.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Failed.mMessage.find("example-library"), eastl::string::npos);
		EXPECT_EQ(FArdaAdmissionNode::mChecks, 3u);
		EXPECT_EQ(FArdaAdmissionNode::mDeclarations, 2u);
		EXPECT_EQ(FArdaAdmissionNode::mPreparations, 1u);
		EXPECT_EQ(Graph.FindNode("supported"), First.GetHandle());
	}

	TEST_F(FArdaDependencyRequirementsTest, ParameterSelectionAndDeviceIdentityCannotReuseAnAdmissionVerdict)
	{

		mProvider->mCapabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		FArdaDependencyGraph First(mDevice);
		FArdaGraphEdit FirstEdit(First);
		ASSERT_TRUE(Admission(FirstEdit, {}, "mesh"));
		auto OtherProvider = eastl::make_shared<FArdaAdmissionProvider>();
		FArdaDependencyGraph Other(CreateArdaRHIDevice(OtherProvider));
		const auto Rejected = RejectAdmission(Other,
		    [](FArdaGraphEdit& Edit)
		    {
			    return Admission(Edit, {}, "mesh");
		    });
		EXPECT_EQ(Rejected.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(FArdaAdmissionNode::mPreparations, 1u);
		FArdaGraphEdit OtherEdit(Other);
		ASSERT_TRUE(Admission(OtherEdit, {{}, false, false}, "plain"));
		EXPECT_EQ(FArdaAdmissionNode::mPreparations, 2u);
		EXPECT_EQ(OtherProvider->mNativeAttempts, 0u);
		EXPECT_EQ(OtherProvider->mWaits, 0u);
		EXPECT_FALSE(OtherProvider->mCapabilities.mQueues.mbDedicatedComputeFamily);
		EXPECT_FALSE(OtherProvider->mCapabilities.mQueues.mbDedicatedCopyFamily);
	}

	struct FArdaNamedAdmissionNode : TArdaComputeDependencyNode<FArdaNamedAdmissionNode, uint32_t>
	{
		inline static uint32_t mChecks = 0;
		inline static uint32_t mSetup = 0;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.named", 1};
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& P)
		{
			++mChecks;
			EXPECT_EQ(P, 41u);
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mFeatures.mbRequireMeshShaders = true;
			Requirements.mFeatures.mbRequireWorkGraphs = true;
			return Requirements;
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext&, FArdaParameters&)
		{
			++mSetup;
			return {};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mSetup;
			return {eastl::make_shared<const FArdaState>(), {}};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaNamedAdmissionNode, NamedAdmission);
	ARDA_DEFINE_GRAPH_NODE(FArdaNamedAdmissionNode, NamedAdmission);

	TEST_F(FArdaDependencyRequirementsTest, NamedClassDiscoveryCannotBypassPreflight)
	{
		// Discover the class by registry name and prove admission still precedes every setup hook.
		FArdaNamedAdmissionNode::mChecks = FArdaNamedAdmissionNode::mSetup = 0;
		mDefinitions.push_back(FArdaNamedAdmissionNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaNamedAdmissionNode::Register());
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		const auto Node = Edit.Invoke(mDefinitions.back(), uint32_t(41), "named");

		// Preserve every missing feature in the diagnostic without inserting a partially prepared node.
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Edit.GetStatus().mMessage.find("mesh shaders"), eastl::string::npos);
		EXPECT_NE(Edit.GetStatus().mMessage.find("work graphs"), eastl::string::npos);
		EXPECT_EQ(FArdaNamedAdmissionNode::mChecks, 1u);
		EXPECT_EQ(FArdaNamedAdmissionNode::mSetup, 0u);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
	}

	struct FArdaPreparedAdmissionNode : TArdaComputeDependencyNode<FArdaPreparedAdmissionNode, uint32_t>
	{
		using FArdaState = eastl::string;
		inline static uint32_t mChecks = 0;
		inline static uint32_t mPrepared = 0;
		inline static uint32_t mDescribed = 0;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.prepared-schema", 1};
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& P)
		{
			++mChecks;
			EXPECT_EQ(P, 17u);
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mFeatures.mbRequireNativeFloat16 = true;
			return Requirements;
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPrepared;
			return {eastl::make_shared<const FArdaState>("prepared state"), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State)
		{
			++mDescribed;
			EXPECT_EQ(P, 17u);
			EXPECT_EQ(State, "prepared state");
			return {};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaPreparedAdmissionNode, PreparedAdmission);
	ARDA_DEFINE_GRAPH_NODE(FArdaPreparedAdmissionNode, PreparedAdmission);

	TEST_F(FArdaDependencyRequirementsTest, DistinctPreparedSchemaNeverReachesRequirementCallback)
	{

		FArdaPreparedAdmissionNode::mChecks = FArdaPreparedAdmissionNode::mPrepared =
		    FArdaPreparedAdmissionNode::mDescribed = 0;
		mDefinitions.push_back(FArdaPreparedAdmissionNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaPreparedAdmissionNode::Register());
		FArdaDependencyGraph Graph(mDevice);
		const auto Failed = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Edit)
		    {
			    return Edit.Invoke(mDefinitions.back(), uint32_t(17), "wrapped");
		    });
		EXPECT_EQ(Failed.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(FArdaPreparedAdmissionNode::mPrepared, 0u);
		mProvider->mCapabilities.mMachineLearning.mbNativeFloat16 = true;
		FArdaGraphEdit Edit(Graph);
		const auto Node = PreparedAdmission(Edit, uint32_t(17));
		ASSERT_TRUE(Node);
		const auto Again = Edit.Invoke(mDefinitions.back(), uint32_t(17));
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Node.GetHandle());
		EXPECT_EQ(FArdaPreparedAdmissionNode::mChecks, 3u);
		EXPECT_EQ(FArdaPreparedAdmissionNode::mPrepared, 1u);
		EXPECT_EQ(FArdaPreparedAdmissionNode::mDescribed, 2u);
	}

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaAdmissionWorkBindings)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::WorkGraph)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaWorkAdmissionNode
	    : TArdaDependencyNode<FArdaWorkAdmissionNode, FArdaDependencyResourceHandle, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaDependencyResourceHandle;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.inferred-work", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaDependencyResourceHandle& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelines = {{"default", 0, EArdaPipelineStateKind::WorkGraph}};
			Desc.BindShader<FArdaAdmissionWorkBindings>({{"mOutput", P}});
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaDependencyResourceHandle&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaWorkAdmissionNode, WorkAdmission);
	ARDA_DEFINE_GRAPH_NODE(FArdaWorkAdmissionNode, WorkAdmission);

	TEST_F(FArdaDependencyRequirementsTest, InferredPipelineRequirementsRejectBeforeNativeBindingLayoutCreation)
	{
		mDefinitions.push_back(FArdaWorkAdmissionNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaWorkAdmissionNode::Register());
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 64;
		Buffer.mStructureStride = 4;
		Buffer.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Structured;
		const auto Resource = AllocateBuffer(Edit, Buffer, "output");
		ASSERT_TRUE(Resource);
		const auto Node = WorkAdmission(Edit, Resource.GetHandle(), "inferred");
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Edit.GetStatus().mMessage.find("work graphs"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		EXPECT_EQ(mProvider->mNativeAttempts, 0u);
	}

	struct FArdaOfflineAdmissionNode
	    : TArdaDependencyNode<FArdaOfflineAdmissionNode, uint32_t, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = uint32_t;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.offline-pipeline", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(uint32_t, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mPipelines = {{"default", 0, EArdaPipelineStateKind::WorkGraph}};
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&, uint32_t, const FArdaState&, FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaOfflineAdmissionNode, OfflineAdmission);
	ARDA_DEFINE_GRAPH_NODE(FArdaOfflineAdmissionNode, OfflineAdmission);

	TEST_F(FArdaDependencyRequirementsTest, EmptyRequirementsPermitOfflineAnalysisButExplicitFeaturesNeedADevice)
	{

		FArdaDependencyNodeRequirements Empty;
		EXPECT_TRUE(Empty.Check(nullptr));
		FArdaDependencyGraph Graph;
		const auto Missing = RejectAdmission(Graph,
		    [](FArdaGraphEdit& Edit)
		    {
			    return Admission(Edit, {}, "needs device");
		    });
		EXPECT_EQ(Missing.mCode, EArdaRHIResult::Unsupported);
		ExpectNoSetup();
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Admission(Edit, {{}, false, false}, "plain analysis"));
		mDefinitions.push_back(FArdaOfflineAdmissionNode::GetMetadata().mName);
		const auto Node = OfflineAdmission(Edit, uint32_t(0), "offline work");
		ASSERT_TRUE(Node);
		EXPECT_TRUE(Graph.GetTopology()
		        .TryGetNode(Node.GetHandle())
		        ->mPayload.mDesc.mRequirements.mFeatures.mbRequireWorkGraphs);
	}

	struct FArdaCudaAdmissionNode : TArdaCudaDependencyNode<FArdaCudaAdmissionNode, uint32_t>
	{
		inline static uint32_t mPrepared = 0;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.cuda-kind", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPrepared;
			return {eastl::make_shared<const FArdaState>(), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
		}

		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCudaAdmissionNode, CudaAdmission);
	ARDA_DEFINE_GRAPH_NODE(FArdaCudaAdmissionNode, CudaAdmission);

	TEST_F(FArdaDependencyRequirementsTest, CudaKindRequiresAQualifiedDeviceBeforePreparation)
	{

		FArdaCudaAdmissionNode::mPrepared = 0;
		mDefinitions.push_back(FArdaCudaAdmissionNode::GetMetadata().mName);
		FArdaDependencyGraph Graph(mDevice);
		const auto Missing = RejectAdmission(Graph,
		    [](FArdaGraphEdit& Edit)
		    {
			    return CudaAdmission(Edit, uint32_t(0));
		    });
		EXPECT_EQ(Missing.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(FArdaCudaAdmissionNode::mPrepared, 0u);
		mProvider->mCuda.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(CudaAdmission(Edit, uint32_t(0)));
		EXPECT_EQ(FArdaCudaAdmissionNode::mPrepared, 1u);
	}

	TEST_F(FArdaDependencyRequirementsTest, CudaLimitsModesAndNamedEnvironmentFailuresAreReportedTogether)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireMeshShaders = true;
		R.mbRequireCudaLayeredSurfaces = true;
		R.mMinCudaComputeCapability = 120;
		R.mMinCudaThreadsPerBlock = 1024;
		R.mMinCudaSharedMemoryBytes = 49152;
		R.mAllowedCudaLaunchModes = {EArdaCudaLaunchMode::D3D12CiG};
		uint32_t PredicateCalls = 0;
		bool Ready = false;
		R.mEnvironment = {{"cu-example",
		    [&](const IArdaRHIDevice& Device)
		    {
			    ++PredicateCalls;
			    EXPECT_EQ(&Device, mDevice.Get());
			    return Ready
			        ? FArdaRHIStatus{}
			        : FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Required library version is unavailable.");
		    }}};
		auto Status = R.Check(mDevice.Get());
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		for (const char* Name : {"mesh shaders",
		         "CUDA launch support",
		         "CUDA layered surface access",
		         "minimum CUDA compute capability",
		         "CUDA threads-per-block limit",
		         "CUDA dynamic shared-memory limit",
		         "required CUDA launch mode",
		         "cu-example",
		         "Required library version is unavailable."})
		{
			EXPECT_NE(Status.mMessage.find(Name), eastl::string::npos) << Name;
		}
		EXPECT_EQ(PredicateCalls, 1u);
		mProvider->mCapabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		mProvider->mCuda.mLaunchMode = EArdaCudaLaunchMode::D3D12CiG;
		mProvider->mCuda.mbSurfaceAccess = mProvider->mCuda.mbLayeredSurfaceAccess = true;
		mProvider->mCuda.mComputeCapability = 120;
		mProvider->mCuda.mMaxThreadsPerBlock = 1024;
		mProvider->mCuda.mMaxSharedMemoryBytes = 49152;
		Ready = true;
		EXPECT_TRUE(R.Check(mDevice.Get()));
		EXPECT_EQ(PredicateCalls, 2u);
		mProvider->mCuda.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		Status = R.Check(mDevice.Get());
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Status.mMessage.find("required CUDA launch mode"), eastl::string::npos);
		EXPECT_EQ(PredicateCalls, 3u);

		// A borrowed environment cannot be queried without a device.
		Status = R.Check(nullptr);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Status.mMessage.find("cu-example (no device)"), eastl::string::npos);
		EXPECT_EQ(PredicateCalls, 3u);
	}

	/** Small immutable shader identity for feature inference without loading shader binaries. */
	class FArdaAdmissionShader final : public IArdaRHIShader
	{
	public:
		explicit FArdaAdmissionShader(EArdaRHIShaderStage Stage)
		    : mStage(Stage)
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
			return EArdaRHIResourceType::Shader;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Admission shader";
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return uint64_t(mStage) + 1;
		}

	private:
		EArdaRHIShaderStage mStage;
		uint32_t mReferences = 0;
	};

	struct FArdaInferenceProbeParameters
	{
		FArdaDependencyNodeDesc mDesc;
		FArdaDependencyResourceHandle mOutput;
		eastl::string mCase;
	};

	struct FArdaInferenceProbeNode
	    : TArdaDependencyNode<FArdaInferenceProbeNode, FArdaInferenceProbeParameters, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaInferenceProbeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.inference-probe", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaInferenceProbeParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).String(P.mCase).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaInferenceProbeParameters& P, const FArdaState&)
		{
			auto Desc = P.mDesc;
			Desc.mAccesses.push_back({P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			for (auto& Table : Desc.mBindlessTables)
			{
				for (auto& Entry : Table.mEntries)
				{
					Entry.mResource = P.mOutput;
				}
			}
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaInferenceProbeParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
		{
			// Let the graph own the probe output so a failed inferred requirement must roll it back.
			FArdaRHIBufferDesc Buffer;
			Buffer.mByteSize = 64;
			Buffer.mStructureStride = 4;
			Buffer.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess |
			    EArdaRHIBufferUsage::ShaderResource;
			return C.Buffer(P.mOutput, "Output", Buffer);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaInferenceProbeNode, InferenceProbe);
	ARDA_DEFINE_GRAPH_NODE(FArdaInferenceProbeNode, InferenceProbe);

	FArdaDependencyNodeDesc MakeBindlessInferenceProbe(uint32_t Maximum,
	    uint32_t Actual,
	    bool Variable,
	    uint32_t Banks,
	    uint32_t Populated)
	{
		FArdaDependencyNodeDesc D;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute}};
		FArdaDependencyBindlessTable Table;
		Table.mName = "bank";
		Table.mCapacity = Actual;
		Table.mLayout.mVisibility = EArdaRHIShaderStage::Compute;
		Table.mLayout.mMaxCapacity = Maximum;
		Table.mLayout.mbVariableDescriptorCount = Variable;
		for (uint32_t Bank = 0; Bank < Banks; ++Bank)
		{
			Table.mLayout.mRegisterSpaces.push_back({Bank, 1, EArdaRHIBindingType::StructuredBufferUAV});
			for (uint32_t Index = 0; Index < Populated; ++Index)
			{
				FArdaDependencyBindlessEntry Entry;
				Entry.mSlot = Bank;
				Entry.mArrayElement = Index;
				Entry.mType = EArdaRHIBindingType::StructuredBufferUAV;
				Entry.mAccess = EArdaDependencyAccess::Write;
				Table.mEntries.push_back(Entry);
			}
		}
		D.mBindlessTables.push_back(eastl::move(Table));
		return D;
	}

	TEST_F(FArdaDependencyRequirementsTest, GraphicsPipelineFamiliesRequireAGraphicsQueueAndRollbackOutputs)
	{
		mProvider->mCapabilities.mQueues.mbGraphics = false;
		mProvider->mCapabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		mDefinitions.push_back(FArdaInferenceProbeNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaInferenceProbeNode::Register());
		for (const auto Kind : {EArdaPipelineStateKind::Graphics, EArdaPipelineStateKind::Meshlet})
		{
			FArdaDependencyGraph Graph(mDevice);

			FArdaInferenceProbeParameters P;
			P.mDesc.mPipelines = {{"default", 0, Kind}};
			const auto Node = RejectAdmission(Graph,
			    [&](FArdaGraphEdit& Rejected)
			    {
				    return InferenceProbe(Rejected, P, "needs graphics");
			    });
			EXPECT_EQ(Node.mCode, EArdaRHIResult::Unsupported);
			EXPECT_NE(Node.mMessage.find("graphics queue"), eastl::string::npos);
			EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
			FArdaRHIBufferDesc Probe;
			Probe.mByteSize = 4;
			FArdaGraphEdit Edit(Graph);
			const auto AfterFailure = AllocateBuffer(Edit, Probe, "first");
			ASSERT_TRUE(AfterFailure);
			EXPECT_EQ(AfterFailure.GetHandle().mIndex, 0u);
		}
	}

	TEST_F(FArdaDependencyRequirementsTest, GeometryAndTessellationStagesAreInferredFromContributionsAndSeededGraphics)
	{
		mDefinitions.push_back(FArdaInferenceProbeNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaInferenceProbeNode::Register());
		for (const auto Stage : {EArdaRHIShaderStage::Geometry, EArdaRHIShaderStage::Hull, EArdaRHIShaderStage::Domain})
		{
			for (const bool Seeded : {false, true})
			{
				SCOPED_TRACE(std::to_string(uint32_t(Stage)) + (Seeded ? " seeded" : " contribution"));
				mProvider->mCapabilities.mbGeometryShaders = mProvider->mCapabilities.mbTessellationShaders = false;
				FArdaDependencyGraph Graph(mDevice);

				FArdaInferenceProbeParameters P;
				const FArdaRHIShaderRef Shader(new FArdaAdmissionShader(Stage));
				if (Seeded)
				{
					auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
					Config->mKind = EArdaPipelineStateKind::Graphics;
					auto& Graphics = Config->mGraphics.mDesc;
					if (Stage == EArdaRHIShaderStage::Geometry)
					{
						Graphics.mGeometryShader = Shader;
					}
					else if (Stage == EArdaRHIShaderStage::Hull)
					{
						Graphics.mHullShader = Shader;
					}
					else
					{
						Graphics.mDomainShader = Shader;
					}
					P.mDesc.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Config}};
				}
				else
				{
					P.mDesc.mPipelineStages = {{Shader}};
					P.mDesc.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics}};
				}
				const auto Unsupported = RejectAdmission(Graph,
				    [&](FArdaGraphEdit& Rejected)
				    {
					    return InferenceProbe(Rejected, P, "stage");
				    });
				EXPECT_EQ(Unsupported.mCode, EArdaRHIResult::Unsupported);
				EXPECT_NE(Unsupported.mMessage.find(
				              Stage == EArdaRHIShaderStage::Geometry ? "geometry shaders" : "tessellation shaders"),
				    eastl::string::npos);
				EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);

				mProvider->mCapabilities.mbGeometryShaders = Stage == EArdaRHIShaderStage::Geometry;
				mProvider->mCapabilities.mbTessellationShaders = Stage != EArdaRHIShaderStage::Geometry;
				FArdaGraphEdit Edit(Graph);
				const auto Supported = InferenceProbe(Edit, P, "stage");
				ASSERT_TRUE(Supported) << Edit.GetStatus().mMessage.c_str();
				const auto& R =
				    Graph.GetTopology().TryGetNode(Supported.GetHandle())->mPayload.mDesc.mRequirements.mFeatures;
				EXPECT_EQ(R.mbRequireGeometryShaders, Stage == EArdaRHIShaderStage::Geometry);
				EXPECT_EQ(R.mbRequireTessellationShaders, Stage != EArdaRHIShaderStage::Geometry);
				// Admission failure discarded the provisional logical output.
				EXPECT_EQ(Graph.FindOutput(Supported.GetHandle(), "Output").mIndex, 0u);
			}
		}
	}

	TEST_F(FArdaDependencyRequirementsTest, RayPipelineOpacityMicromapSettingsRequireTheFeatureBeforeInsertion)
	{
		auto& Ray = mProvider->mCapabilities.mRayTracing;
		Ray.mbPipelineShaders = Ray.mbPersistentShaderTables = true;
		Ray.mMaxRecursionDepth = 1;
		mDefinitions.push_back(FArdaInferenceProbeNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaInferenceProbeNode::Register());
		FArdaDependencyGraph Graph(mDevice);

		FArdaInferenceProbeParameters P;
		auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Config->mKind = EArdaPipelineStateKind::RayTracing;
		Config->mRayTracing.mDesc.mbAllowOpacityMicromaps = true;
		P.mDesc.mPipelines = {{"default", 0, EArdaPipelineStateKind::RayTracing, {}, Config}};
		const auto Unsupported = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Rejected)
		    {
			    return InferenceProbe(Rejected, P, "ray");
		    });
		EXPECT_EQ(Unsupported.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Unsupported.mMessage.find("opacity micromaps"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		Ray.mbOpacityMicromaps = true;
		FArdaGraphEdit Edit(Graph);
		const auto Supported = InferenceProbe(Edit, P, "ray");
		ASSERT_TRUE(Supported) << Edit.GetStatus().mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology()
		        .TryGetNode(Supported.GetHandle())
		        ->mPayload.mDesc.mRequirements.mFeatures.mbRequireOpacityMicromaps);
		EXPECT_EQ(Graph.FindOutput(Supported.GetHandle(), "Output").mIndex, 0u);
	}

	TEST_F(FArdaDependencyRequirementsTest, NativeBindlessBankHolesRejectBeforeLayoutAllocationAndRollbackResources)
	{
		auto& Descriptors = mProvider->mCapabilities.mDescriptors;
		Descriptors.mbBindless = Descriptors.mbVariableDescriptorCount = true;
		Descriptors.mMaxResourceDescriptors = 4096;
		mDefinitions.push_back(FArdaInferenceProbeNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaInferenceProbeNode::Register());
		for (const bool VariableMultipleBanks : {false, true})
		{
			FArdaDependencyGraph Graph(mDevice);

			FArdaInferenceProbeParameters P;
			P.mDesc = MakeBindlessInferenceProbe(1024, 1, VariableMultipleBanks, VariableMultipleBanks ? 2 : 1, 1);
			const auto Node = RejectAdmission(Graph,
			    [&](FArdaGraphEdit& Rejected)
			    {
				    return InferenceProbe(Rejected, P, "holes");
			    });
			EXPECT_EQ(Node.mCode, EArdaRHIResult::Unsupported);
			EXPECT_NE(Node.mMessage.find("partially bound descriptors"), eastl::string::npos);
			EXPECT_EQ(mProvider->mNativeAttempts, 0u);
			EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
			FArdaRHIBufferDesc Probe;
			Probe.mByteSize = 4;
			FArdaGraphEdit Edit(Graph);
			const auto AfterFailure = AllocateBuffer(Edit, Probe, "first");
			ASSERT_TRUE(AfterFailure);
			EXPECT_EQ(AfterFailure.GetHandle().mIndex, 0u);
		}
	}

	TEST_F(FArdaDependencyRequirementsTest, BindlessCoverageUsesNativeBankExtentAndRespectsVariableCountScope)
	{
		auto& Descriptors = mProvider->mCapabilities.mDescriptors;
		Descriptors.mbBindless = Descriptors.mbVariableDescriptorCount = true;
		Descriptors.mMaxResourceDescriptors = 4096;
		FArdaDependencyGraph::FArdaImpl Graph;
		Graph.mDevice = mDevice;

		struct FArdaCase
		{
			uint32_t mMaximum, mActual, mBanks, mPopulated;
			bool mbVariable, mbNeedsPartials;
		};

		for (const FArdaCase Case : {FArdaCase{1024, 1, 1, 1, false, true}, // Fixed bank remains 1024 descriptors.
		         FArdaCase{1024, 1, 1, 1, true, false}, // One variable bank can shrink to one populated descriptor.
		         FArdaCase{1024, 1, 2, 1, true, true},  // Other native banks remain at the maximum.
		         FArdaCase{1024, 1024, 1, 1024, false, false},
		         FArdaCase{1024, 1024, 2, 1024, true, false},
		         FArdaCase{1024, 4, 1, 3, true, true}}) // A hole in the actual variable extent still matters.
		{
			SCOPED_TRACE(std::to_string(Case.mActual) + "/" + std::to_string(Case.mBanks) +
			    (Case.mbVariable ? " variable" : " fixed"));
			auto Desc =
			    MakeBindlessInferenceProbe(Case.mMaximum, Case.mActual, Case.mbVariable, Case.mBanks, Case.mPopulated);
			InferArdaNodeRequirements(Graph, EArdaDependencyNodeKind::Compute, Desc);
			EXPECT_EQ(Desc.mRequirements.mFeatures.mbRequirePartiallyBoundDescriptors, Case.mbNeedsPartials);
			const auto Status = Desc.mRequirements.Check(mDevice.Get());
			EXPECT_EQ(bool(Status), !Case.mbNeedsPartials) << Status.mMessage.c_str();
		}
	}

	TEST_F(FArdaDependencyRequirementsTest, UnboundedBindlessCoverageUsesTheReportedLimitForEachDescriptorKind)
	{
		auto& Descriptors = mProvider->mCapabilities.mDescriptors;
		Descriptors.mbBindless = Descriptors.mbUnboundedArrays = Descriptors.mbRuntimeDescriptorArrays = true;
		Descriptors.mMaxResourceDescriptors = 4;
		Descriptors.mMaxSamplerDescriptors = 2;
		FArdaDependencyGraph::FArdaImpl Graph;
		Graph.mDevice = mDevice;
		for (const bool Sampler : {false, true})
		{
			for (const bool FullyPopulated : {false, true})
			{
				const uint32_t Count = FullyPopulated ? (Sampler ? 2u : 4u) : 1u;
				auto Desc = MakeBindlessInferenceProbe(0, Count, false, 1, Count);
				auto& Table = Desc.mBindlessTables.front();
				Table.mLayout.mbUnbounded = true;
				if (Sampler)
				{
					Table.mLayout.mRegisterSpaces.front().mType = EArdaRHIBindingType::Sampler;
					for (auto& Entry : Table.mEntries)
					{
						Entry.mType = EArdaRHIBindingType::Sampler;
					}
				}
				InferArdaNodeRequirements(Graph, EArdaDependencyNodeKind::Compute, Desc);
				EXPECT_EQ(Desc.mRequirements.mFeatures.mbRequirePartiallyBoundDescriptors, !FullyPopulated);
				EXPECT_EQ(bool(Desc.mRequirements.Check(mDevice.Get())), FullyPopulated);
			}
		}
	}

#define ARDA_ADMISSION_BUFFER_FIELDS(VALUE, BUFFER, SURFACE) BUFFER(uint32_t, mOutput, EArdaComputeAccess::Write)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaAdmissionCudaBufferParameters, ARDA_ADMISSION_BUFFER_FIELDS)
#undef ARDA_ADMISSION_BUFFER_FIELDS
#define ARDA_ADMISSION_SURFACE_FIELDS(VALUE, BUFFER, SURFACE)                                                          \
	SURFACE(uint32_t, mOutput, EArdaComputeAccess::Write, EArdaRHIFormat::R32UInt)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaAdmissionCudaSurfaceParameters, ARDA_ADMISSION_SURFACE_FIELDS)
#undef ARDA_ADMISSION_SURFACE_FIELDS

	/** Exercises the adapter's preflight independently of CUDA binaries or resource-dependent selection. */
	template <class Parameters>
	class TArdaAdmissionCudaOperand
	{
	public:
		using FArdaParameters = Parameters;
		FArdaRHIDeviceRef mDevice;
		FArdaRHIStatus mSupport;
		mutable uint32_t mSupportChecks = 0, mDispatchPreparations = 0;

		const FArdaRHIDeviceRef& GetDevice() const
		{
			return mDevice;
		}

		FArdaRHIStatus GetOperandSupport() const
		{
			++mSupportChecks;
			return mSupport;
		}

		TArdaRHIResult<eastl::shared_ptr<const FArdaCudaDispatchPlan>> PrepareDispatch(const FArdaParameters&,
		    EArdaRHIQueueType) const
		{
			++mDispatchPreparations;
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			        "Admission must not perform resource-dependent CUDA selection.")};
		}
	};

	struct FArdaAdmissionCudaBufferNode : TArdaCudaOperandNode<FArdaAdmissionCudaBufferNode,
	                                          TArdaAdmissionCudaOperand<FArdaAdmissionCudaBufferParameters>>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.cuda-operand-support", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAdmissionCudaBufferNode, AdmissionCudaBuffer);
	ARDA_DEFINE_GRAPH_NODE(FArdaAdmissionCudaBufferNode, AdmissionCudaBuffer);

	struct FArdaAdmissionCudaSurfaceNode : TArdaCudaOperandNode<FArdaAdmissionCudaSurfaceNode,
	                                           TArdaAdmissionCudaOperand<FArdaAdmissionCudaSurfaceParameters>>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.cuda-operand-surface", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAdmissionCudaSurfaceNode, AdmissionCudaSurface);
	ARDA_DEFINE_GRAPH_NODE(FArdaAdmissionCudaSurfaceNode, AdmissionCudaSurface);

	struct FArdaAlternateAdmissionCudaBufferNode : TArdaCudaOperandNode<FArdaAlternateAdmissionCudaBufferNode,
	                                                   TArdaAdmissionCudaOperand<FArdaAdmissionCudaBufferParameters>>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.requirements.cuda-operand-alternate", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaAlternateAdmissionCudaBufferNode, AlternateAdmissionCudaBuffer);
	ARDA_DEFINE_GRAPH_NODE(FArdaAlternateAdmissionCudaBufferNode, AlternateAdmissionCudaBuffer);

	static_assert(!std::is_same_v<FArdaAdmissionCudaBufferNode::FArdaParameters,
	                  FArdaAlternateAdmissionCudaBufferNode::FArdaParameters>,
	    "Distinct CUDA node wrappers must own distinct attachment parameter schemas.");
	static_assert(!std::is_convertible_v<FArdaAdmissionCudaBufferNode::FArdaParameters,
	                  FArdaAlternateAdmissionCudaBufferNode::FArdaParameters> &&
	        !std::is_convertible_v<FArdaAlternateAdmissionCudaBufferNode::FArdaParameters,
	            FArdaAdmissionCudaBufferNode::FArdaParameters>,
	    "Parameters for one CUDA node wrapper must not convert into another wrapper's parameters.");

	TEST_F(FArdaDependencyRequirementsTest, NodesSharingCudaOperandRejectEachOthersParameterSchema)
	{
		mDefinitions.push_back(FArdaAdmissionCudaBufferNode::GetMetadata().mName);
		mDefinitions.push_back(FArdaAlternateAdmissionCudaBufferNode::GetMetadata().mName);
		ASSERT_TRUE(FArdaAdmissionCudaBufferNode::Register());
		ASSERT_TRUE(FArdaAlternateAdmissionCudaBufferNode::Register());
		const auto First = FArdaNodeRegistry::Get().Find(FArdaAdmissionCudaBufferNode::GetMetadata().mName);
		const auto Second = FArdaNodeRegistry::Get().Find(FArdaAlternateAdmissionCudaBufferNode::GetMetadata().mName);
		ASSERT_TRUE(First);
		ASSERT_TRUE(Second);
		EXPECT_NE(First->mParameterType, Second->mParameterType);

		FArdaDependencyGraph Graph;

		const auto WrongFirst = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Edit)
		    {
			    return Edit.Invoke(First->mName, FArdaAlternateAdmissionCudaBufferNode::FArdaParameters{}, "first");
		    });
		const auto WrongSecond = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Edit)
		    {
			    return Edit.Invoke(Second->mName, FArdaAdmissionCudaBufferNode::FArdaParameters{}, "second");
		    });
		EXPECT_EQ(WrongFirst.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(WrongSecond.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		EXPECT_FALSE(Graph.IsEditing());
	}

	TEST_F(FArdaDependencyRequirementsTest, RegisteredCudaOperandChecksLibrarySupportBeforeSelectingOrMaterializing)
	{
		mProvider->mCuda.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		using FArdaOperand = TArdaAdmissionCudaOperand<FArdaAdmissionCudaBufferParameters>;
		auto Operation = eastl::make_shared<FArdaOperand>();
		Operation->mDevice = mDevice;
		Operation->mSupport = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
		    "No compatible native variant or external library entry.");
		mDefinitions.push_back("test.requirements.cuda-operand-support");
		ASSERT_TRUE(FArdaAdmissionCudaBufferNode::Register());
		EXPECT_EQ(Operation->mSupportChecks, 0u);
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Resources(Graph);
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 64;
		Buffer.mStructureStride = 4;
		Buffer.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
		Buffer.mbCudaInterop = true;
		const auto Output = AllocateBuffer(Resources, Buffer, "output");
		ASSERT_TRUE(Output);
		FArdaAdmissionCudaBufferNode::FArdaParameters P;
		P.mOperation = Operation;
		P.mArguments.mOutput.mResource = Output.GetHandle();
		ASSERT_TRUE(Resources.Commit());
		const auto Missing = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Rejected)
		    {
			    return AdmissionCudaBuffer(Rejected, P, "operand");
		    });
		EXPECT_EQ(Missing.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Missing.mMessage.find("registered CUDA operand"), eastl::string::npos);
		EXPECT_NE(Missing.mMessage.find("No compatible native variant"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		EXPECT_EQ(Operation->mSupportChecks, 1u);
		EXPECT_EQ(Operation->mDispatchPreparations, 0u);
		Operation->mSupport = {};
		FArdaGraphEdit Edit(Graph);
		const auto Supported = AdmissionCudaBuffer(Edit, P, "operand");
		ASSERT_TRUE(Supported) << Edit.GetStatus().mMessage.c_str();
		const auto& Requirements = Graph.GetTopology().TryGetNode(Supported.GetHandle())->mPayload.mDesc.mRequirements;
		EXPECT_TRUE(Requirements.mbRequireCuda);
		EXPECT_FALSE(Requirements.mbRequireCudaSurfaces);
		EXPECT_EQ(Requirements.mEnvironment.size(), 1u);
		EXPECT_EQ(Operation->mDispatchPreparations, 0u);
		Operation->mSupport = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Library is no longer available.");
		ASSERT_TRUE(Edit.Commit());
		const auto FailedAgain = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Rejected)
		    {
			    return AdmissionCudaBuffer(Rejected, P, "unsupported retry");
		    });
		EXPECT_EQ(FailedAgain.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Graph.FindNode("operand"), Supported.GetHandle());
		EXPECT_EQ(Operation->mDispatchPreparations, 0u);
	}

	TEST_F(FArdaDependencyRequirementsTest, RegisteredCudaSurfaceSchemaRequiresSurfacesAndRejectsAnotherDevice)
	{
		mProvider->mCuda.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		using FArdaOperand = TArdaAdmissionCudaOperand<FArdaAdmissionCudaSurfaceParameters>;
		auto Operation = eastl::make_shared<FArdaOperand>();
		Operation->mDevice = mDevice;
		mDefinitions.push_back("test.requirements.cuda-operand-surface");
		ASSERT_TRUE(FArdaAdmissionCudaSurfaceNode::Register());
		const auto Definition = FArdaNodeRegistry::Get().Find(mDefinitions.back());
		ASSERT_TRUE(Definition);
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Resources(Graph);
		FArdaRHITextureDesc Texture;
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		Texture.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		Texture.mbCudaInterop = true;
		const auto Output = AllocateTexture(Resources, Texture, "output");
		ASSERT_TRUE(Output);
		FArdaAdmissionCudaSurfaceNode::FArdaParameters P;
		P.mOperation = Operation;
		P.mArguments.mOutput.mResource = Output.GetHandle();
		ASSERT_TRUE(Resources.Commit());
		const auto Missing = RejectAdmission(Graph,
		    [&](FArdaGraphEdit& Rejected)
		    {
			    return AdmissionCudaSurface(Rejected, P, "surface");
		    });
		EXPECT_EQ(Missing.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Missing.mMessage.find("CUDA surface access"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		mProvider->mCuda.mbSurfaceAccess = true;
		FArdaGraphEdit Edit(Graph);
		const auto Supported = AdmissionCudaSurface(Edit, P, "surface");
		ASSERT_TRUE(Supported) << Edit.GetStatus().mMessage.c_str();
		const auto& Requirements = Graph.GetTopology().TryGetNode(Supported.GetHandle())->mPayload.mDesc.mRequirements;
		EXPECT_TRUE(Requirements.mbRequireCudaSurfaces);
		EXPECT_FALSE(Requirements.mbRequireCudaLayeredSurfaces);
		EXPECT_EQ(Operation->mDispatchPreparations, 0u);

		auto OtherProvider = eastl::make_shared<FArdaAdmissionProvider>();
		OtherProvider->mCuda = mProvider->mCuda;
		auto OtherDevice = CreateArdaRHIDevice(OtherProvider);
		FArdaDependencyGraph Other(OtherDevice);
		FArdaGraphEdit OtherEdit(Other);
		const auto OtherOutput = AllocateTexture(OtherEdit, Texture, "output");
		ASSERT_TRUE(OtherOutput);
		P.mArguments.mOutput.mResource = OtherOutput.GetHandle();
		const auto BeforeWrongDevice = Operation->mSupportChecks;
		const auto WrongDevice = AdmissionCudaSurface(OtherEdit, P, "wrong device");
		EXPECT_EQ(OtherEdit.GetStatus().mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(OtherEdit.GetStatus().mMessage.find("another device"), eastl::string::npos);
		EXPECT_EQ(Operation->mSupportChecks, BeforeWrongDevice);
		EXPECT_EQ(Operation->mDispatchPreparations, 0u);
		EXPECT_EQ(Other.GetTopology().GetNodeCount(), 0u);
		EXPECT_EQ(OtherProvider->mNativeAttempts, 0u);
		EXPECT_EQ(OtherProvider->mWaits, 0u);
	}

	TEST_F(FArdaDependencyRequirementsTest, MalformedEnvironmentAndLaunchModeDeclarationsAreInvalidArguments)
	{
		FArdaDependencyNodeRequirements R;
		R.mEnvironment = {{"",
		    [](const IArdaRHIDevice&)
		    {
			    return FArdaRHIStatus{};
		    }}};
		EXPECT_EQ(R.Check(mDevice.Get()).mCode, EArdaRHIResult::InvalidArgument);
		R.mEnvironment = {{"missing predicate", {}}};
		EXPECT_EQ(R.Check(mDevice.Get()).mCode, EArdaRHIResult::InvalidArgument);
		R.mEnvironment.clear();
		R.mAllowedCudaLaunchModes = {EArdaCudaLaunchMode::None};
		EXPECT_EQ(R.Check(mDevice.Get()).mCode, EArdaRHIResult::InvalidArgument);
		R.mAllowedCudaLaunchModes = {static_cast<EArdaCudaLaunchMode>(255)};
		EXPECT_EQ(R.Check(mDevice.Get()).mCode, EArdaRHIResult::InvalidArgument);
	}
}
