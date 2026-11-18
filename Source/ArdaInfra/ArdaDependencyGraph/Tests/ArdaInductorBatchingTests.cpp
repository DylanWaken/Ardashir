#include "ArdaDependencyGraph.h"

#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaBatchEmptyShaderParameters)
		ARDA_END_SHADER_PARAMETER_STRUCT()

		class FArdaBatchTestShader final : public IArdaRHIShader
		{
		public:
			FArdaBatchTestShader(EArdaRHIShaderStage Stage, uint64_t Key)
			    : mStage(Stage),
			      mKey(Key)
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
				return "Pipeline batching fixture";
			}

			EArdaRHIShaderStage GetStage() const noexcept override
			{
				return mStage;
			}

			uint64_t GetPersistentCacheHash() const noexcept override
			{
				return mKey;
			}

			const FArdaShaderParameterMetadata* GetParameterMetadata() const noexcept override
			{
				return &FArdaBatchEmptyShaderParameters::GetStaticMetadata();
			}

		private:
			EArdaRHIShaderStage mStage;
			uint64_t mKey;
			uint32_t mReferences = 0;
		};

#define ARDA_BATCH_TEST_PARAMETERS(Name)                                                                               \
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(Name)                                                                             \
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mResource)                                                  \
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaDependencyAccess, mAccess, EArdaDependencyAccess::Read)                  \
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaRHIResourceState, mState, EArdaRHIResourceState::ShaderResource)         \
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mShader, 1)                                                         \
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mFixedState, 0)                                                     \
	ARDA_END_NODE_PARAMETER_STRUCT()
		ARDA_BATCH_TEST_PARAMETERS(FArdaBatchComputeParameters)
		ARDA_BATCH_TEST_PARAMETERS(FArdaBatchGraphicsParameters)
		ARDA_BATCH_TEST_PARAMETERS(FArdaBatchMeshletParameters)
		ARDA_BATCH_TEST_PARAMETERS(FArdaBatchRayParameters)
		ARDA_BATCH_TEST_PARAMETERS(FArdaBatchWorkParameters)
#undef ARDA_BATCH_TEST_PARAMETERS

		template <class Parameters, EArdaPipelineStateKind Family, EArdaNodeType Type>
		struct TArdaBatchTestNode : TArdaNodeDefinition<TArdaBatchTestNode<Parameters, Family, Type>, Parameters, Type>
		{
			using FArdaState = FArdaEmptyDependencyNodeState;
			using FArdaInstanceState = FArdaEmptyDependencyNodeState;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {eastl::string("test.pipeline-batch.") + eastl::to_string(uint32_t(Family)), 1};
			}

			static FArdaDependencyNodeDesc Describe(const Parameters& P, const FArdaState&)
			{
				FArdaDependencyNodeDesc Desc;
				Desc.mbSideEffect = true;
				if (P.mResource)
				{
					Desc.mAccesses.push_back({P.mResource, P.mAccess, P.mState});
				}
				auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
				Configuration->mKind = Family;
				const auto Stage = [&](EArdaRHIShaderStage ShaderStage)
				{
					FArdaInductorPipelineContribution Contribution;
					Contribution.mShader = FArdaRHIShaderRef(new FArdaBatchTestShader(ShaderStage,
					    uint64_t(P.mShader) * 100000 + uint64_t(ShaderStage) + 700));
					if (ShaderStage == EArdaRHIShaderStage::RayGeneration)
					{
						Contribution.mExportName = "RayGen";
					}
					Desc.mPipelineStages.push_back(eastl::move(Contribution));
				};
				if constexpr (Family == EArdaPipelineStateKind::Compute)
				{
					Stage(EArdaRHIShaderStage::Compute);
				}
				else if constexpr (Family == EArdaPipelineStateKind::Graphics)
				{
					Stage(EArdaRHIShaderStage::Vertex);
					Stage(EArdaRHIShaderStage::Pixel);
					Configuration->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise = P.mFixedState != 0;
					Configuration->mGraphics.mDesc.mDepthStencilState = {false, false};
				}
				else if constexpr (Family == EArdaPipelineStateKind::Meshlet)
				{
					Stage(EArdaRHIShaderStage::Mesh);
					Stage(EArdaRHIShaderStage::Pixel);
					Configuration->mMeshlet.mDesc.mDepthStencilState = {false, false};
				}
				else if constexpr (Family == EArdaPipelineStateKind::RayTracing)
				{
					Stage(EArdaRHIShaderStage::RayGeneration);
				}
				else
				{
					Stage(EArdaRHIShaderStage::WorkGraph);
					Configuration->mWorkGraph.mDesc.mProgramName = "program";
					Configuration->mWorkGraph.mDesc.mEntryPoint = "entry";
				}
				Desc.mPipelines.push_back({"default", 0, Family, {}, Configuration});
				return Desc;
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const Parameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		using FArdaBatchComputeNode =
		    TArdaBatchTestNode<FArdaBatchComputeParameters, EArdaPipelineStateKind::Compute, EArdaNodeType::Compute>;
		using FArdaBatchGraphicsNode =
		    TArdaBatchTestNode<FArdaBatchGraphicsParameters, EArdaPipelineStateKind::Graphics, EArdaNodeType::Graphics>;
		using FArdaBatchMeshletNode =
		    TArdaBatchTestNode<FArdaBatchMeshletParameters, EArdaPipelineStateKind::Meshlet, EArdaNodeType::Meshlet>;
		using FArdaBatchRayNode =
		    TArdaBatchTestNode<FArdaBatchRayParameters, EArdaPipelineStateKind::RayTracing, EArdaNodeType::RayTracing>;
		using FArdaBatchWorkNode =
		    TArdaBatchTestNode<FArdaBatchWorkParameters, EArdaPipelineStateKind::WorkGraph, EArdaNodeType::WorkGraph>;

		template <class Node>
		void VerifyResourceBatching(bool bShared, bool bWrite)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mRules.mCompileRules.erase(eastl::remove_if(Options.mRules.mCompileRules.begin(),
			                                       Options.mRules.mCompileRules.end(),
			                                       [](const auto& Rule)
			                                       {
				                                       return dynamic_cast<const FArdaSearchOrderRule*>(Rule.get()) !=
				                                           nullptr;
			                                       }),
			    Options.mRules.mCompileRules.end());
			ASSERT_TRUE(Edit.SetOptions(Options));
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			const auto FirstResource = Upload(Edit, AllocateBuffer(Edit, Desc), eastl::vector<uint8_t>(16, 0));
			const auto SecondResource =
			    bShared ? FirstResource : Upload(Edit, AllocateBuffer(Edit, Desc), eastl::vector<uint8_t>(16, 1));
			typename Node::FArdaParameters Parameters;
			Parameters.mResource = FirstResource.GetHandle();
			Parameters.mAccess = bWrite ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read;
			Parameters.mState = bWrite ? EArdaRHIResourceState::UnorderedAccess : EArdaRHIResourceState::ShaderResource;
			const auto First = Edit.Invoke<Node>(Parameters);
			Parameters.mResource = SecondResource.GetHandle();
			const auto Second = Edit.Invoke<Node>(Parameters);
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			bool bGrouped = false;
			for (const auto& Dispatch : Graph.GetCompileResult().mDispatches)
			{
				bGrouped |=
				    Dispatch.mNodes == eastl::vector<FArdaGraphNodeHandle>{First.GetHandle(), Second.GetHandle()};
			}
			EXPECT_EQ(bGrouped, !(bShared && bWrite));
			EXPECT_TRUE(Graph.GetCompileResult().mInstancedDraws.empty());
		}

		class FArdaCorruptPipelineBatchRule final : public FArdaGraphCompileRule
		{
		public:
			const char* GetName() const noexcept override
			{
				return "CorruptPipelineBatch";
			}

			FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult& Result) const override
			{
				Result.mPipelineBatches.front().push_back(Result.mPipelineBatches.front().front());
				return {};
			}
		};

		class FArdaSplitPipelineBatchRule final : public FArdaGraphCompileRule
		{
		public:
			const char* GetName() const noexcept override
			{
				return "SplitPipelineBatch";
			}

			FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult& Result) const override
			{
				auto Previous = eastl::move(Result.mPipelineBatches);
				Result.mPipelineBatches.clear();
				for (const auto& Batch : Previous)
				{
					for (auto Handle : Batch)
					{
						Result.mPipelineBatches.push_back({Handle});
					}
				}
				Result.mInstancedDraws.clear();
				return {};
			}
		};

		class FArdaSeparateQueueBatchRule final : public FArdaInductorQueueRule
		{
		public:
			const char* GetName() const noexcept override
			{
				return "SeparateBatchQueues";
			}

			FArdaRHIStatus Assign(const FArdaInductorQueueContext&, FArdaInductorCompileResult& Result) const override
			{
				Result.mQueues.assign(Result.mExecutionOrder.size(), EArdaRHIQueueType::Graphics);
				if (Result.mQueues.size() > 1)
				{
					Result.mQueues.back() = EArdaRHIQueueType::Compute;
				}
				return {};
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaBatchCudaParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mIdentity, 0)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaBatchCudaNode
		    : TArdaNodeDefinition<FArdaBatchCudaNode, FArdaBatchCudaParameters, EArdaNodeType::Cuda>
		{
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.pipeline-batch.cuda", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				FArdaDependencyNodeDesc Desc;
				Desc.mbSideEffect = true;
				return Desc;
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
	}

	TEST(ArdaInductorBatching, EveryNativePipelineFamilyBatchesDifferentResources)
	{
		VerifyResourceBatching<FArdaBatchComputeNode>(false, false);
		VerifyResourceBatching<FArdaBatchGraphicsNode>(false, false);
		VerifyResourceBatching<FArdaBatchMeshletNode>(false, false);
		VerifyResourceBatching<FArdaBatchRayNode>(false, false);
		VerifyResourceBatching<FArdaBatchWorkNode>(false, false);
	}

	TEST(ArdaInductorBatching, SharedReadsBatchButOverlappingStorageWritesSplit)
	{
		VerifyResourceBatching<FArdaBatchComputeNode>(true, false);
		VerifyResourceBatching<FArdaBatchComputeNode>(true, true);
		VerifyResourceBatching<FArdaBatchComputeNode>(false, true);
	}

	TEST(ArdaInductorBatching, ShaderAndFixedFunctionDifferencesSplit)
	{
		for (bool bShader : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			FArdaBatchGraphicsParameters Parameters;
			Edit.Invoke<FArdaBatchGraphicsNode>(Parameters);
			if (bShader)
			{
				Parameters.mShader = 2;
			}
			else
			{
				Parameters.mFixedState = 1;
			}
			Edit.Invoke<FArdaBatchGraphicsNode>(Parameters);
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			ASSERT_EQ(Graph.GetCompileResult().mPipelineBatches.size(), 2u);
			EXPECT_EQ(Graph.GetCompileResult().mDispatches.size(), 2u);
		}
	}

	TEST(ArdaInductorBatching, StorageProducerConsumerKeepsBarrierBoundary)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		const auto Resource = AllocateBuffer(Edit, Desc);
		FArdaBatchComputeParameters Parameters;
		Parameters.mResource = Resource.GetHandle();
		Parameters.mAccess = EArdaDependencyAccess::Write;
		Parameters.mState = EArdaRHIResourceState::UnorderedAccess;
		const auto Producer = Edit.Invoke<FArdaBatchComputeNode>(Parameters);
		Parameters.mAccess = EArdaDependencyAccess::Read;
		const auto Consumer = Edit.Invoke<FArdaBatchComputeNode>(Parameters);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mDispatches.size(), 2u);
		EXPECT_EQ(Graph.GetCompileResult().mDispatches[0].mNodes.front(), Producer.GetHandle());
		EXPECT_EQ(Graph.GetCompileResult().mDispatches[1].mNodes.front(), Consumer.GetHandle());
		EXPECT_FALSE(Graph.GetCompileResult().mDispatchDependencies.empty());
	}

	TEST(ArdaInductorBatching, QueueChangesSplitCompatiblePipelines)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		for (auto& Rule : Options.mRules.mCompileRules)
		{
			if (dynamic_cast<const FArdaInductorQueueRule*>(Rule.get()))
			{
				Rule = MakeArdaGraphRule<FArdaSeparateQueueBatchRule>();
			}
		}
		ASSERT_TRUE(Edit.SetOptions(Options));
		Edit.Invoke<FArdaBatchComputeNode>(FArdaBatchComputeParameters{});
		Edit.Invoke<FArdaBatchComputeNode>(FArdaBatchComputeParameters{});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mPipelineBatches.size(), 2u);
		EXPECT_NE(Graph.GetCompileResult().mDispatches[0].mQueue, Graph.GetCompileResult().mDispatches[1].mQueue);
	}

	TEST(ArdaInductorBatching, CudaSequenceKeepsItsOrderedBatch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = Edit.Invoke<FArdaBatchCudaNode>(0u);
		const auto Second = Edit.Invoke<FArdaBatchCudaNode>(1u);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mPipelineBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mPipelineBatches.front(),
		    (eastl::vector<FArdaGraphNodeHandle>{First.GetHandle(), Second.GetHandle()}));
		EXPECT_EQ(Graph.GetCompileResult().mPipelineBatches, Graph.GetCompileResult().mCudaBatches);
		ASSERT_EQ(Graph.GetCompileResult().mDispatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mDispatches.front().mCudaStream, 0u);
	}

	TEST(ArdaInductorBatching, CustomRulesMayConservativelySplitCompatibleBatches)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		for (auto It = Options.mRules.mCompileRules.begin(); It != Options.mRules.mCompileRules.end(); ++It)
		{
			if (dynamic_cast<const FArdaFlattenDispatchesRule*>(It->get()))
			{
				Options.mRules.mCompileRules.insert(It, MakeArdaGraphRule<FArdaSplitPipelineBatchRule>());
				break;
			}
		}
		ASSERT_TRUE(Edit.SetOptions(Options));
		Edit.Invoke<FArdaBatchComputeNode>(FArdaBatchComputeParameters{});
		Edit.Invoke<FArdaBatchComputeNode>(FArdaBatchComputeParameters{});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mPipelineBatches.size(), 2u);
		EXPECT_EQ(Graph.GetCompileResult().mDispatches.size(), 2u);
	}

	TEST(ArdaInductorBatching, RejectsForgedCustomBatchMembership)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaCorruptPipelineBatchRule>());
		ASSERT_TRUE(Edit.SetOptions(Options));
		Edit.Invoke<FArdaBatchComputeNode>(FArdaBatchComputeParameters{});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("batch"), eastl::string::npos);
	}
}
