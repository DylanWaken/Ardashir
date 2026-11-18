#include "ArdaDependencyGraph.h"
#include "Inductor/Runtime/ArdaInductorInstancing.h"

#include <gtest/gtest.h>
#include <limits>

namespace arda
{
	namespace
	{
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaInstancingEmptyShaderParameters)
		ARDA_END_SHADER_PARAMETER_STRUCT()

		class FArdaInstancingTestShader final : public IArdaRHIShader
		{
		public:
			explicit FArdaInstancingTestShader(EArdaRHIShaderStage Stage,
			    const FArdaShaderParameterMetadata* Metadata =
			        &FArdaInstancingEmptyShaderParameters::GetStaticMetadata())
			    : mStage(Stage),
			      mMetadata(Metadata)
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
				return "Automatic instancing fixture";
			}

			EArdaRHIShaderStage GetStage() const noexcept override
			{
				return mStage;
			}

			uint64_t GetPersistentCacheHash() const noexcept override
			{
				return uint64_t(mStage) + 25000;
			}

			const FArdaShaderParameterMetadata* GetParameterMetadata() const noexcept override
			{
				return mMetadata;
			}

		private:
			EArdaRHIShaderStage mStage;
			const FArdaShaderParameterMetadata* mMetadata;
			uint32_t mReferences = 0;
		};

		FArdaDependencyNodeDesc DescribeStage(EArdaRHIShaderStage Stage)
		{
			static const FArdaRHIShaderRef Vertex(new FArdaInstancingTestShader(EArdaRHIShaderStage::Vertex));
			static const FArdaRHIShaderRef Pixel(new FArdaInstancingTestShader(EArdaRHIShaderStage::Pixel));
			FArdaDependencyNodeDesc Desc;
			FArdaInductorPipelineContribution Contribution;
			Contribution.mShader = Stage == EArdaRHIShaderStage::Vertex ? Vertex : Pixel;
			Desc.mPipelineStages.push_back(eastl::move(Contribution));
			Desc.mbSideEffect = Stage == EArdaRHIShaderStage::Pixel;
			return Desc;
		}

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInstancingVertexParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mStartInstance, 0)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaInstancingVertexNode
		    : TArdaNodeDefinition<FArdaInstancingVertexNode, FArdaInstancingVertexParameters, EArdaNodeType::Vertex>
		{
			static constexpr EArdaGraphicsInstancing mGraphicsInstancing = EArdaGraphicsInstancing::ContiguousInstances;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.instancing.vertex", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				return DescribeStage(EArdaRHIShaderStage::Vertex);
			}

			static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
			    const FArdaParameters& Parameters,
			    const FArdaState&,
			    FArdaInstanceState&,
			    FArdaGraphicsWork& Work)
			{
				Work.mDraw.mVertexCount = 3;
				Work.mDraw.mStartInstance = Parameters.mStartInstance;
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaInstancingVertexNode, InstancingVertexTest);
		ARDA_DEFINE_GRAPH_NODE(FArdaInstancingVertexNode, InstancingVertexTest);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInstancingPixelParameters)
			ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
			ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mResource)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWrite, false)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaInstancingPixelNode
		    : TArdaNodeDefinition<FArdaInstancingPixelNode, FArdaInstancingPixelParameters, EArdaNodeType::Pixel>
		{
			static constexpr EArdaGraphicsInstancing mGraphicsInstancing = EArdaGraphicsInstancing::ContiguousInstances;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.instancing.pixel", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
			{
				auto Desc = DescribeStage(EArdaRHIShaderStage::Pixel);
				if (Parameters.mResource)
				{
					Desc.mAccesses.push_back({Parameters.mResource,
					    Parameters.mbWrite ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read,
					    Parameters.mbWrite ? EArdaRHIResourceState::UnorderedAccess
					                       : EArdaRHIResourceState::ShaderResource});
				}
				return Desc;
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaInstancingPixelNode, InstancingPixelTest);
		ARDA_DEFINE_GRAPH_NODE(FArdaInstancingPixelNode, InstancingPixelTest);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUnspecifiedInstancingPixelParameters)
			ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaUnspecifiedInstancingPixelNode : TArdaNodeDefinition<FArdaUnspecifiedInstancingPixelNode,
		                                                 FArdaUnspecifiedInstancingPixelParameters,
		                                                 EArdaNodeType::Pixel>
		{
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.instancing.unspecified.pixel", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				return DescribeStage(EArdaRHIShaderStage::Pixel);
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaUnspecifiedInstancingPixelNode, UnspecifiedInstancingPixelTest);
		ARDA_DEFINE_GRAPH_NODE(FArdaUnspecifiedInstancingPixelNode, UnspecifiedInstancingPixelTest);

		FArdaGraphFragments AddRaster(FArdaGraphEdit& Edit, uint32_t Instance)
		{
			FArdaRasterConfig Config;
			Config.mViewport = {0.f, 16.f, 0.f, 16.f, 0.f, 1.f};
			return Rasterize(Edit, InstancingVertexTest(Edit, Instance), Config);
		}

		FArdaGraphicsWork MakeWork(bool bIndexed = false, uint32_t Start = 0, uint32_t Count = 1)
		{
			FArdaGraphicsWork Work;
			Work.mbIndexed = bIndexed;
			Work.mDraw.mVertexCount = 3;
			Work.mDraw.mStartInstance = Start;
			Work.mDraw.mInstanceCount = Count;
			Work.mState.mViewports.push_back({0.f, 16.f, 0.f, 16.f, 0.f, 1.f});
			Work.mState.mScissors.push_back({0, 16, 0, 16});
			Work.mState.mVertexBuffers.push_back({{}, 0, 0});
			return Work;
		}
	}

	TEST(ArdaInductorInstancing, CompilerGroupsPureTraditionalDraws)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = InstancingPixelTest(Edit, AddRaster(Edit, 0));
		const auto Second = InstancingPixelTest(Edit, AddRaster(Edit, 1));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Groups = Graph.GetCompileResult().mInstancedDraws;
		ASSERT_EQ(Groups.size(), 1u);
		ASSERT_EQ(Groups.front().size(), 2u);
		EXPECT_EQ(Groups.front()[0], First.GetProducer());
		EXPECT_EQ(Groups.front()[1], Second.GetProducer());
	}

	TEST(ArdaInductorInstancing, EveryStageMustExplicitlyPermitMerge)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		UnspecifiedInstancingPixelTest(Edit, AddRaster(Edit, 0));
		UnspecifiedInstancingPixelTest(Edit, AddRaster(Edit, 1));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetCompileResult().mInstancedDraws.empty());
	}

	TEST(ArdaInductorInstancing, DifferentLogicalResourcesPreventMerge)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		const auto First = Upload(Edit, AllocateBuffer(Edit, Desc), eastl::vector<uint8_t>(16, 0));
		const auto Second = Upload(Edit, AllocateBuffer(Edit, Desc), eastl::vector<uint8_t>(16, 1));
		InstancingPixelTest(Edit, AddRaster(Edit, 0), First.GetHandle());
		InstancingPixelTest(Edit, AddRaster(Edit, 1), Second.GetHandle());
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetCompileResult().mInstancedDraws.empty());
	}

	TEST(ArdaInductorInstancing, StorageWritesPreventMerge)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		const auto Buffer = AllocateBuffer(Edit, Desc);
		InstancingPixelTest(Edit, AddRaster(Edit, 0), Buffer.GetHandle(), true);
		InstancingPixelTest(Edit, AddRaster(Edit, 1), Buffer.GetHandle(), true);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetCompileResult().mInstancedDraws.empty());
	}

	TEST(ArdaInductorInstancing, NativeCheckMergesIndexedAndNonindexedRanges)
	{
		for (bool bIndexed : {false, true})
		{
			auto Work = MakeWork(bIndexed, 10, 2);
			EXPECT_TRUE(TryMergeArdaGraphicsInstances(Work, MakeWork(bIndexed, 12, 4)));
			EXPECT_EQ(Work.mDraw.mStartInstance, 10u);
			EXPECT_EQ(Work.mDraw.mInstanceCount, 6u);
			EXPECT_EQ(Work.mDraw.mVertexCount, 3u);
			EXPECT_EQ(Work.mbIndexed, bIndexed);
		}
	}

	TEST(ArdaInductorInstancing, NativeCheckPreservesResetAndNoncontiguousInstanceIds)
	{
		for (uint32_t Start : {0u, 2u, UINT32_MAX})
		{
			auto Work = MakeWork();
			EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, MakeWork(false, Start)));
			EXPECT_EQ(Work.mDraw.mInstanceCount, 1u);
		}
	}

	TEST(ArdaInductorInstancing, NativeCheckRejectsGeometryAndRasterStateChanges)
	{
		const auto Check = [](const FArdaGraphicsWork& Next)
		{
			auto Work = MakeWork();
			EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, Next));
			EXPECT_EQ(Work.mDraw.mInstanceCount, 1u);
		};
		auto Next = MakeWork(false, 1);
		Next.mbIndexed = true;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mDraw.mVertexCount = 6;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mDraw.mStartIndex = 3;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mDraw.mStartVertex = 3;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mVertexBuffers.front().mOffset = 16;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mVertexBuffers.front().mSlot = 1;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mIndexOffset = 4;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mIndexFormat = EArdaRHIFormat::R32UInt;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mViewports.front().mMaxX = 32.f;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mScissors.front().mMaxX = 8;
		Check(Next);
		Next = MakeWork(false, 1);
		Next.mState.mViewports.front().mMinZ = std::numeric_limits<float>::quiet_NaN();
		Check(Next);
	}

	TEST(ArdaInductorInstancing, NativeCheckRejectsEmptyDrawsAndOverflow)
	{
		auto Work = MakeWork(false, 0, UINT32_MAX);
		EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, MakeWork(false, UINT32_MAX, 1)));
		EXPECT_EQ(Work.mDraw.mInstanceCount, UINT32_MAX);
		Work = MakeWork(false, UINT32_MAX - 1, 1);
		EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, MakeWork(false, UINT32_MAX, 2)));
		EXPECT_TRUE(TryMergeArdaGraphicsInstances(Work, MakeWork(false, UINT32_MAX, 1)));
		Work = MakeWork();
		EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, MakeWork(false, 1, 0)));
		Work.mDraw.mVertexCount = 0;
		auto Next = MakeWork(false, 1);
		Next.mDraw.mVertexCount = 0;
		EXPECT_FALSE(TryMergeArdaGraphicsInstances(Work, Next));
	}
}
