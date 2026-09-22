#include "Inductor/Rules/ArdaInductorShaderContracts.h"
#include "ArdaDependencyGraph.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include "Graph/Node/ArdaNodeParameters.h"
#include <gtest/gtest.h>

namespace arda::shader_contract_tests
{
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaComputeContract)
		ARDA_SHADER_BUFFER_UAV_ACCESS(mData, 0, 0, EArdaRHIShaderStage::Compute, EArdaShaderResourceAccess::ReadWrite)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaRayLocalContract)
		ARDA_SHADER_BUFFER_UAV_ACCESS(mData,
		    0,
		    0,
		    EArdaRHIShaderStage::RayGeneration,
		    EArdaShaderResourceAccess::ReadWrite)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaNarrowComputeContract)
		ARDA_SHADER_BUFFER_UAV(mData, 0, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaVertexContract)
		ARDA_SHADER_BUFFER_SRV(mVertices, 0, 0, EArdaRHIShaderStage::Vertex)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaPixelContract)
		ARDA_SHADER_TEXTURE_SRV(mAlbedo, 0, 0, EArdaRHIShaderStage::Pixel)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	template <class Interface, EArdaRHIResourceType Kind>
	class TArdaContractResource : public Interface
	{
	public:
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
			return "Shader contract test";
		}

	private:
		uint32_t mReferences = 0;
	};

	class FArdaContractShader final : public TArdaContractResource<IArdaRHIShader, EArdaRHIResourceType::Shader>
	{
	public:
		FArdaContractShader(EArdaRHIShaderStage Stage, const FArdaShaderParameterMetadata* Metadata)
		    : mStage(Stage),
		      mMetadata(Metadata)
		{
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return static_cast<uint64_t>(mStage);
		}

		const FArdaShaderParameterMetadata* GetParameterMetadata() const noexcept override
		{
			return mMetadata;
		}

	private:
		EArdaRHIShaderStage mStage;
		const FArdaShaderParameterMetadata* mMetadata;
	};

	class FArdaContractLayout final
	    : public TArdaContractResource<IArdaRHIBindingLayout, EArdaRHIResourceType::BindingLayout>
	{
	public:
		explicit FArdaContractLayout(FArdaRHIBindingLayoutDesc Desc)
		    : mDesc(eastl::move(Desc))
		{
		}

		const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

	private:
		FArdaRHIBindingLayoutDesc mDesc;
	};

	FArdaRHIBindingLayoutRef Layout(const FArdaShaderParameterMetadata& Metadata)
	{
		eastl::vector<FArdaRHIBindingLayoutDesc> Descs;
		EXPECT_TRUE(Metadata.BuildBindingLayoutDescs(Descs));
		return FArdaRHIBindingLayoutRef(new FArdaContractLayout(Descs.front()));
	}

	FArdaInductorPipelineConfiguration Compute()
	{
		FArdaInductorPipelineConfiguration C;
		C.mKind = EArdaPipelineStateKind::Compute;
		C.mCompute.mDesc.mComputeShader = FArdaRHIShaderRef(
		    new FArdaContractShader(EArdaRHIShaderStage::Compute, &FArdaComputeContract::GetStaticMetadata()));
		C.mCompute.mDesc.mBindingLayouts = {Layout(FArdaComputeContract::GetStaticMetadata())};
		return C;
	}

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMissingVertexArguments)
		ARDA_NODE_PARAMETER(uint32_t, mVertexCount)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaMissingVertexArgumentsNode final : public TArdaNodeDefinition<FArdaMissingVertexArgumentsNode,
	                                                  FArdaMissingVertexArguments,
	                                                  EArdaNodeType::Vertex>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.contract.missing-vertex-arguments", 1};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			FArdaInductorPipelineContribution Stage;
			Stage.mShader = FArdaRHIShaderRef(
			    new FArdaContractShader(EArdaRHIShaderStage::Vertex, &FArdaVertexContract::GetStaticMetadata()));
			Stage.mBindingLayouts = {Layout(FArdaVertexContract::GetStaticMetadata())};
			D.mPipelineStages.push_back(Stage);
			return D;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMissingVertexArgumentsNode, MissingContractVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaMissingVertexArgumentsNode, MissingContractVertices);

	struct FArdaLegacyContractPixelParameters
	{
		FArdaGraphFragments mInput;
	};

	class FArdaLegacyContractPixelNode final : public TArdaNodeDefinition<FArdaLegacyContractPixelNode,
	                                               FArdaLegacyContractPixelParameters,
	                                               EArdaNodeType::Pixel>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.contract.legacy-pixel", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder().Stage(P.mInput).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
			FArdaInductorPipelineContribution Stage;
			Stage.mShader = FArdaRHIShaderRef(new FArdaContractShader(EArdaRHIShaderStage::Pixel, nullptr));
			D.mPipelineStages.push_back(Stage);
			return D;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaLegacyContractPixelNode, LegacyContractPixels);
	ARDA_DEFINE_GRAPH_NODE(FArdaLegacyContractPixelNode, LegacyContractPixels);

	TEST(ArdaShaderContract, DeclarativeVertexContractSurvivesLegacyPixelAssembly)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertices = MissingContractVertices(Edit, 3u);
		ASSERT_TRUE(Vertices);
		FArdaRasterConfig Raster;
		Raster.mViewport = {0.f, 8.f, 0.f, 8.f, 0.f, 1.f};
		const auto Fragments = Rasterize(Edit, Vertices, Raster);
		ASSERT_TRUE(Fragments);
		ASSERT_TRUE(LegacyContractPixels(Edit, Fragments));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("no node argument schema"), eastl::string::npos) << Status.mMessage.c_str();
	}

	TEST(ArdaShaderContract, PreparedComputeContractRequiresExactLayout)
	{
		auto C = Compute();
		EXPECT_TRUE(ValidateArdaPipelineShaderContract(C));
		const auto Original = C.mCompute.mDesc.mBindingLayouts.front()->GetDesc();
		C.mCompute.mDesc.mBindingLayouts.clear();
		EXPECT_FALSE(ValidateArdaPipelineShaderContract(C));
		for (uint32_t Case = 0; Case < 4; ++Case)
		{
			auto Changed = Original;
			if (Case == 0)
			{
				Changed.mItems.front().mType = EArdaRHIBindingType::StructuredBufferSRV;
			}
			if (Case == 1)
			{
				Changed.mItems.front().mArraySize = 2;
			}
			if (Case == 2)
			{
				Changed.mVisibility = EArdaRHIShaderStage::Pixel;
			}
			if (Case == 3)
			{
				Changed.mItems.front().mSlot = 1;
			}
			C.mCompute.mDesc.mBindingLayouts = {FArdaRHIBindingLayoutRef(new FArdaContractLayout(Changed))};
			EXPECT_FALSE(ValidateArdaPipelineShaderContract(C)) << Case;
		}
	}

	TEST(ArdaShaderContract, DisjointExtraSchemaCannotMasqueradeAsShaderRequirement)
	{
		auto C = Compute();
		auto Extra = C.mCompute.mDesc.mBindingLayouts.front()->GetDesc();
		Extra.mRegisterSpace = 9;
		C.mCompute.mDesc.mBindingLayouts.push_back(FArdaRHIBindingLayoutRef(new FArdaContractLayout(Extra)));
		EXPECT_FALSE(ValidateArdaPipelineShaderContract(C));
	}

	TEST(ArdaShaderContract, NativeRawShaderKeepsExplicitLayoutContract)
	{
		auto C = Compute();
		C.mCompute.mDesc.mComputeShader =
		    FArdaRHIShaderRef(new FArdaContractShader(EArdaRHIShaderStage::Compute, nullptr));
		EXPECT_TRUE(ValidateArdaPipelineShaderContract(C));
	}

	TEST(ArdaShaderContract, RegisteredParameterlessShaderRejectsResourceSchema)
	{
		const FArdaShaderParameterMetadata Empty("Empty", 1, 1, {});
		auto C = Compute();
		C.mCompute.mDesc.mComputeShader =
		    FArdaRHIShaderRef(new FArdaContractShader(EArdaRHIShaderStage::Compute, &Empty));
		EXPECT_FALSE(ValidateArdaPipelineShaderContract(C));
		C.mCompute.mDesc.mBindingLayouts.clear();
		EXPECT_TRUE(ValidateArdaPipelineShaderContract(C));
	}

	TEST(ArdaShaderContract, IndependentVertexAndPixelContractsShareRegisterSpace)
	{
		FArdaInductorPipelineConfiguration C;
		C.mKind = EArdaPipelineStateKind::Graphics;
		C.mGraphics.mDesc.mVertexShader = FArdaRHIShaderRef(
		    new FArdaContractShader(EArdaRHIShaderStage::Vertex, &FArdaVertexContract::GetStaticMetadata()));
		C.mGraphics.mDesc.mPixelShader = FArdaRHIShaderRef(
		    new FArdaContractShader(EArdaRHIShaderStage::Pixel, &FArdaPixelContract::GetStaticMetadata()));
		C.mGraphics.mDesc.mBindingLayouts = {Layout(FArdaVertexContract::GetStaticMetadata()),
		    Layout(FArdaPixelContract::GetStaticMetadata())};
		EXPECT_TRUE(ValidateArdaPipelineShaderContract(C));
		C.mGraphics.mDesc.mBindingLayouts.pop_back();
		EXPECT_FALSE(ValidateArdaPipelineShaderContract(C));
	}

	TEST(ArdaShaderContract, DeclarativeNodeCannotOmitAllShaderParameters)
	{
		FArdaDependencyNodeDesc D;
		D.mbDeclarativeParameters = true;
		EXPECT_FALSE(ValidateArdaNodeShaderContract(D, Compute(), "default"));
		D.BindShader<FArdaComputeContract>({{"mData", {}}});
		EXPECT_TRUE(ValidateArdaNodeShaderContract(D, Compute(), "default"));
		D.mShaderBindings.front().mResources.clear();
		EXPECT_FALSE(ValidateArdaNodeShaderContract(D, Compute(), "default"));
	}

	TEST(ArdaShaderContract, PreparedShaderAccessCannotBeNarrowedByLookalikeSchema)
	{
		FArdaDependencyNodeDesc D;
		D.mbDeclarativeParameters = true;
		D.BindShader<FArdaNarrowComputeContract>({{"mData", {}}});
		EXPECT_FALSE(ValidateArdaNodeShaderContract(D, Compute(), "default"));
		D.mShaderBindings.front().mResources.front().mAccess = EArdaDependencyAccess::ReadWrite;
		EXPECT_TRUE(ValidateArdaNodeShaderContract(D, Compute(), "default"));
	}

	TEST(ArdaShaderContract, MetadataVisibilityMustIncludePreparedStage)
	{
		auto C = Compute();
		C.mCompute.mDesc.mComputeShader = FArdaRHIShaderRef(
		    new FArdaContractShader(EArdaRHIShaderStage::Compute, &FArdaPixelContract::GetStaticMetadata()));
		C.mCompute.mDesc.mBindingLayouts = {Layout(FArdaPixelContract::GetStaticMetadata())};
		EXPECT_FALSE(ValidateArdaPipelineShaderContract(C));
	}

	TEST(ArdaShaderContract, RayLocalRecordUsesItsPreparedShaderAccessContract)
	{
		FArdaInductorPipelineConfiguration C;
		C.mKind = EArdaPipelineStateKind::RayTracing;
		C.mRayTracing.mDesc.mShaders.push_back({"RayGen",
		    FArdaRHIShaderRef(new FArdaContractShader(EArdaRHIShaderStage::RayGeneration,
		        &FArdaRayLocalContract::GetStaticMetadata())),
		    Layout(FArdaRayLocalContract::GetStaticMetadata())});
		EXPECT_TRUE(ValidateArdaPipelineShaderContract(C));
		FArdaDependencyNodeDesc D;
		D.mbDeclarativeParameters = true;
		EXPECT_FALSE(ValidateArdaNodeShaderContract(D, C, "default"));
		FArdaDependencyShaderTable Table;
		FArdaDependencyShaderTableRecord Record;
		Record.mExportName = "RayGen";
		Record.mLocalBindings.mMetadata = &FArdaRayLocalContract::GetStaticMetadata();
		Record.mLocalBindings.mResources.push_back({"mData", {}});
		Table.mRecords.push_back(Record);
		D.mShaderTables.push_back(Table);
		EXPECT_TRUE(ValidateArdaNodeShaderContract(D, C, "default"));
		D.mShaderTables.front().mRecords.front().mLocalBindings.mResources.front().mAccess =
		    EArdaDependencyAccess::Write;
		EXPECT_FALSE(ValidateArdaNodeShaderContract(D, C, "default"));
	}
}
