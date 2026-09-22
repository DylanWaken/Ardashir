#include "ArdaDependencyGraph.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include "RHI/CUDA/ArdaCudaParameters.h"
#include <gtest/gtest.h>

namespace arda::node_parameter_contract_tests
{
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaContractShaderParameters)
		ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_BUFFER_UAV_ACCESS(mUpdates,
		    1,
		    0,
		    EArdaRHIShaderStage::Compute,
		    EArdaShaderResourceAccess::ReadWrite)
		ARDA_SHADER_TEXTURE_UAV_ACCESS(mImageUpdates,
		    2,
		    0,
		    EArdaRHIShaderStage::Compute,
		    EArdaShaderResourceAccess::ReadWrite)
		ARDA_SHADER_TEXTURE_SRV(mTexture, 1, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_ACCELERATION_STRUCTURE(mScene, 2, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_SAMPLER(mSampler, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_CONSTANT_BUFFER(mConstants, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_PARAMETER_VALUE(uint32_t, mCount)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaBufferAsTextureParameters)
		ARDA_NODE_SHADER_BUFFER(mResource, FArdaContractShaderParameters, mTexture)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInheritedReadWriteParameters)
		ARDA_NODE_SHADER_BUFFER(mBuffer, FArdaContractShaderParameters, mUpdates)
		ARDA_NODE_SHADER_TEXTURE(mTexture, FArdaContractShaderParameters, mImageUpdates)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaNarrowedBufferAccessParameters)
		ARDA_NODE_SHADER_BUFFER_ACCESS(mBuffer, FArdaContractShaderParameters, mUpdates, EArdaDependencyAccess::Write)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaNarrowedTextureAccessParameters)
		ARDA_NODE_SHADER_TEXTURE_ACCESS(mTexture,
		    FArdaContractShaderParameters,
		    mImageUpdates,
		    EArdaDependencyAccess::Read)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTextureAsBufferParameters)
		ARDA_NODE_SHADER_TEXTURE(mResource, FArdaContractShaderParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaBufferAsSceneParameters)
		ARDA_NODE_SHADER_BUFFER(mResource, FArdaContractShaderParameters, mScene)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSceneAsBufferParameters)
		ARDA_NODE_SHADER_ACCELERATION_STRUCTURE(mResource, FArdaContractShaderParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaBufferAsSamplerParameters)
		ARDA_NODE_SHADER_BUFFER(mResource, FArdaContractShaderParameters, mSampler)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaBufferAsValueParameters)
		ARDA_NODE_SHADER_BUFFER(mResource, FArdaContractShaderParameters, mCount)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMissingShaderMemberParameters)
		ARDA_NODE_SHADER_BUFFER(mResource, FArdaContractShaderParameters, mMissing)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInvalidSrvAccessParameters)
		ARDA_NODE_SHADER_BUFFER_ACCESS(mInput, FArdaContractShaderParameters, mInput, EArdaDependencyAccess::Write)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaInvalidTextureSrvAccessParameters)
		ARDA_NODE_SHADER_TEXTURE_ACCESS(mTexture,
		    FArdaContractShaderParameters,
		    mTexture,
		    EArdaDependencyAccess::ReadWrite)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaValidShaderMappingParameters)
		ARDA_NODE_SHADER_BUFFER(mInput, FArdaContractShaderParameters, mInput)
		ARDA_NODE_SHADER_BUFFER_ACCESS(mOutput,
		    FArdaContractShaderParameters,
		    mOutput,
		    EArdaDependencyAccess::ReadWrite)
		ARDA_NODE_SHADER_TEXTURE(mTexture, FArdaContractShaderParameters, mTexture)
		ARDA_NODE_SHADER_ACCELERATION_STRUCTURE(mScene, FArdaContractShaderParameters, mScene)
		ARDA_NODE_SHADER_BUFFER(mConstants, FArdaContractShaderParameters, mConstants)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaContractArrayShaderParameters)
		ARDA_SHADER_BUFFER_SRV_ARRAY(mInputs, 0, 0, 2, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArrayOverflowParameters)
		ARDA_NODE_SHADER_BUFFER_ARRAY(mInputs,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    3,
		    EArdaDependencyAccess::Read)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArrayElementOverflowParameters)
		ARDA_NODE_SHADER_BUFFER_BINDING(mInput,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    "default",
		    EArdaDependencyAccess::Read,
		    2,
		    FArdaRHIViewDesc{})
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDuplicateShaderMappingParameters)
		ARDA_NODE_SHADER_BUFFER(mFirst, FArdaContractShaderParameters, mInput)
		ARDA_NODE_SHADER_BUFFER(mSecond, FArdaContractShaderParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaOverlappingArrayMappingParameters)
		ARDA_NODE_SHADER_BUFFER_ARRAY(mInputs,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    2,
		    EArdaDependencyAccess::Read)
		ARDA_NODE_SHADER_BUFFER_BINDING(mOverlap,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    "default",
		    EArdaDependencyAccess::Read,
		    1,
		    FArdaRHIViewDesc{})
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSeparatedArrayMappingParameters)
		ARDA_NODE_SHADER_BUFFER_BINDING(mSecond,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    "default",
		    EArdaDependencyAccess::Read,
		    1,
		    FArdaRHIViewDesc{})
		ARDA_NODE_SHADER_BUFFER_BINDING(mFirst,
		    FArdaContractArrayShaderParameters,
		    mInputs,
		    "default",
		    EArdaDependencyAccess::Read,
		    0,
		    FArdaRHIViewDesc{})
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDifferentSlotsParameters)
		ARDA_NODE_SHADER_BUFFER_BINDING(mFirst,
		    FArdaContractShaderParameters,
		    mInput,
		    "first",
		    EArdaDependencyAccess::Read,
		    0,
		    FArdaRHIViewDesc{})
		ARDA_NODE_SHADER_BUFFER_BINDING(mSecond,
		    FArdaContractShaderParameters,
		    mInput,
		    "second",
		    EArdaDependencyAccess::Read,
		    0,
		    FArdaRHIViewDesc{})
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaInvalidShaderMappingNode final
	    : public TArdaNodeDefinition<FArdaInvalidShaderMappingNode, FArdaBufferAsTextureParameters>
	{
	public:
		inline static uint32_t mPrepareCount = 0;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.invalid-shader-mapping", 1};
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPrepareCount;
			return {eastl::make_shared<FArdaState>(), {}};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaInvalidShaderMappingNode, InvalidShaderMapping);
	ARDA_DEFINE_GRAPH_NODE(FArdaInvalidShaderMappingNode, InvalidShaderMapping);

#define ARDA_TEST_CONTRACT_CUDA_FIELDS(VALUE, BUFFER, SURFACE)                                                         \
	VALUE(uint32_t, mCount)                                                                                            \
	BUFFER(float, mInput, EArdaComputeAccess::Read)                                                                    \
	BUFFER(float, mOutput, EArdaComputeAccess::Write)                                                                  \
	BUFFER(float, mAccumulation, EArdaComputeAccess::ReadWrite)                                                        \
	SURFACE(float, mImage, EArdaComputeAccess::Read, EArdaRHIFormat::R32Float)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaContractCudaParameters, ARDA_TEST_CONTRACT_CUDA_FIELDS)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaOtherContractCudaParameters, ARDA_TEST_CONTRACT_CUDA_FIELDS)
#undef ARDA_TEST_CONTRACT_CUDA_FIELDS

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaValidCudaMappingParameters)
		ARDA_NODE_CUDA_BUFFER(mSource, FArdaContractCudaParameters, mInput)
		ARDA_NODE_CUDA_BUFFER(mDestination, FArdaContractCudaParameters, mOutput)
		ARDA_NODE_CUDA_BUFFER(mAccumulator, FArdaContractCudaParameters, mAccumulation)
		ARDA_NODE_CUDA_TEXTURE(mTexture, FArdaContractCudaParameters, mImage)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCudaBufferAsTextureParameters)
		ARDA_NODE_CUDA_BUFFER(mResource, FArdaContractCudaParameters, mImage)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCudaTextureAsBufferParameters)
		ARDA_NODE_CUDA_TEXTURE(mResource, FArdaContractCudaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCudaBufferAsValueParameters)
		ARDA_NODE_CUDA_BUFFER(mResource, FArdaContractCudaParameters, mCount)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCudaMissingMemberParameters)
		ARDA_NODE_CUDA_BUFFER(mResource, FArdaContractCudaParameters, mMissing)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDuplicateCudaMappingParameters)
		ARDA_NODE_CUDA_BUFFER(mFirst, FArdaContractCudaParameters, mInput)
		ARDA_NODE_CUDA_BUFFER(mSecond, FArdaContractCudaParameters, mInput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaIncompleteCudaMappingParameters)
		ARDA_NODE_CUDA_BUFFER(mDestination, FArdaContractCudaParameters, mOutput)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaBorrowedCudaMetadataStorage
	{
		uint32_t mCount = 0;

		static const FArdaComputeParameterMetadata& GetStaticMetadata()
		{
			return FArdaContractCudaParameters::GetStaticMetadata();
		}
	};

	ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FArdaPlainComputeValues)
		ARDA_COMPUTE_PARAMETER(uint32_t, mCount)
	ARDA_END_COMPUTE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaScalarCudaNodeParameters)
		ARDA_NODE_PARAMETER(uint32_t, mCount)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCudaMappingsOnComputeNode final
	    : public TArdaNodeDefinition<FArdaCudaMappingsOnComputeNode, FArdaValidCudaMappingParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.cuda-mappings-on-compute", 1};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCudaMappingsOnComputeNode, InvalidCudaMappingsOnCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaCudaMappingsOnComputeNode, InvalidCudaMappingsOnCompute);

	class FArdaShaderMappingsOnCudaNode final : public TArdaNodeDefinition<FArdaShaderMappingsOnCudaNode,
	                                                FArdaValidShaderMappingParameters,
	                                                EArdaNodeType::Cuda>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.shader-mappings-on-cuda", 1};
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

	ARDA_DECLARE_GRAPH_NODE(FArdaShaderMappingsOnCudaNode, InvalidShaderMappingsOnCuda);
	ARDA_DEFINE_GRAPH_NODE(FArdaShaderMappingsOnCudaNode, InvalidShaderMappingsOnCuda);

	TEST(ArdaNodeParameterContracts, ResourceMappingsMustMatchShaderResourceKinds)
	{
		ASSERT_TRUE(FArdaContractShaderParameters::GetStaticMetadata().GetStatus());
		EXPECT_FALSE(FArdaBufferAsTextureParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaTextureAsBufferParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaBufferAsSceneParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaSceneAsBufferParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, ResourceMappingsRejectValuesSamplersAndUnknownMembers)
	{
		EXPECT_FALSE(FArdaBufferAsSamplerParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaBufferAsValueParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaMissingShaderMemberParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, ReadOnlyShaderBindingsRejectWriteOverrides)
	{
		EXPECT_FALSE(FArdaInvalidSrvAccessParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaInvalidTextureSrvAccessParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, DefaultAccessComesFromShaderSchemaAndUavAllowsReadWrite)
	{
		FArdaDependencyNodeDesc Description;
		const auto Status = FArdaValidShaderMappingParameters::GetNodeParameterMetadata().Apply({}, Description);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		EXPECT_EQ(Description.mShaderBindings.front().mMetadata, &FArdaContractShaderParameters::GetStaticMetadata());
		const auto& Resources = Description.mShaderBindings.front().mResources;
		ASSERT_EQ(Resources.size(), 5u);
		EXPECT_EQ(Resources[0].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(Resources[1].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_EQ(Resources[2].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(Resources[3].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(Resources[4].mAccess, EArdaDependencyAccess::Read);
	}

	TEST(ArdaNodeParameterContracts, ArrayMappingsMustFitShaderArrayBounds)
	{
		EXPECT_FALSE(FArdaArrayOverflowParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaArrayElementOverflowParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_TRUE(FArdaSeparatedArrayMappingParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, ReadWriteShaderRequirementsAreInheritedAndCannotBeNarrowed)
	{
		FArdaDependencyNodeDesc Description;
		const auto Status = FArdaInheritedReadWriteParameters::GetNodeParameterMetadata().Apply({}, Description);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		const auto& Resources = Description.mShaderBindings.front().mResources;
		ASSERT_EQ(Resources.size(), 2u);
		EXPECT_EQ(Resources[0].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_EQ(Resources[1].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_FALSE(FArdaNarrowedBufferAccessParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaNarrowedTextureAccessParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, DuplicateMappingsAreRejectedWithinTheSamePipelineSlot)
	{
		EXPECT_FALSE(FArdaDuplicateShaderMappingParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaOverlappingArrayMappingParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_TRUE(FArdaDifferentSlotsParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, RejectedSchemaLeavesCustomDescriptionUnchanged)
	{
		FArdaDependencyNodeDesc Description;
		Description.mbSideEffect = true;
		Description.mEstimatedCost = 37;
		Description.BindShader<FArdaContractShaderParameters>({}, "retained");
		EXPECT_FALSE(FArdaBufferAsTextureParameters::GetNodeParameterMetadata().Apply({}, Description));
		EXPECT_TRUE(Description.mbSideEffect);
		EXPECT_EQ(Description.mEstimatedCost, 37u);
		EXPECT_TRUE(Description.mAccesses.empty());
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		EXPECT_EQ(Description.mShaderBindings.front().mPipelineSlot, "retained");
		EXPECT_TRUE(Description.mShaderBindings.front().mResources.empty());
	}

	TEST(ArdaNodeParameterContracts, InvalidSchemaRejectsInvocationBeforeDevicePreparation)
	{
		FArdaInvalidShaderMappingNode::mPrepareCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(InvalidShaderMapping(Edit, FArdaGraphBuffer{}));
		EXPECT_FALSE(Edit.GetStatus());
		EXPECT_EQ(FArdaInvalidShaderMappingNode::mPrepareCount, 0u);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaNodeParameterContracts, MappingCannotDuplicateCustomDescribeBinding)
	{
		FArdaDependencyNodeDesc Description;
		Description.BindShader<FArdaContractShaderParameters>({{"mInput", FArdaDependencyResourceHandle{}}});
		const auto Status = FArdaValidShaderMappingParameters::GetNodeParameterMetadata().Apply({}, Description);
		EXPECT_FALSE(Status);
		EXPECT_TRUE(Description.mAccesses.empty());
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		ASSERT_EQ(Description.mShaderBindings.front().mResources.size(), 1u);
		EXPECT_EQ(Description.mShaderBindings.front().mResources.front().mMember, "mInput");
	}

	TEST(ArdaNodeParameterContracts, CudaAndShaderMappingsRequireTheirMatchingExecutionDomain)
	{
		EXPECT_FALSE(FArdaCudaMappingsOnComputeNode::Register());
		EXPECT_FALSE(FArdaShaderMappingsOnCudaNode::Register());
	}

	TEST(ArdaNodeParameterContracts, CudaMappingAccessComesFromTheKernelParameterSchema)
	{
		ASSERT_TRUE(FArdaContractCudaParameters::GetCudaMetadata().GetStatus());
		FArdaDependencyNodeDesc Description;
		const auto Status = FArdaValidCudaMappingParameters::GetNodeParameterMetadata().Apply({}, Description);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Description.mAccesses.size(), 4u);
		EXPECT_EQ(Description.mAccesses[0].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(Description.mAccesses[1].mAccess, EArdaDependencyAccess::Write);
		EXPECT_EQ(Description.mAccesses[2].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_EQ(Description.mAccesses[3].mAccess, EArdaDependencyAccess::Read);
		for (const auto& Access : Description.mAccesses)
		{
			EXPECT_EQ(Access.mState, EArdaRHIResourceState::UnorderedAccess);
		}
	}

	TEST(ArdaNodeParameterContracts, CudaMappingRequiresMatchingResourceKindsAndDeclaredMembers)
	{
		EXPECT_FALSE(FArdaCudaBufferAsTextureParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaCudaTextureAsBufferParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaCudaBufferAsValueParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaCudaMissingMemberParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_FALSE(FArdaDuplicateCudaMappingParameters::GetNodeParameterMetadata().GetStatus());
	}

	TEST(ArdaNodeParameterContracts, CudaHostResolutionRequiresCompleteMappingsFromTheExactSchema)
	{
		FArdaDependencyExecutionContext Context;
		FArdaContractCudaParameters Host;
		Host.mCount = 29;
		const auto Missing = ResolveNodeCudaParameters(Context, FArdaIncompleteCudaMappingParameters{}, Host);
		EXPECT_FALSE(Missing);
		EXPECT_NE(Missing.mMessage.find("complete"), eastl::string::npos);
		EXPECT_EQ(Host.mCount, 29u);

		FArdaOtherContractCudaParameters Other;
		Other.mCount = 41;
		const auto Different = ResolveNodeCudaParameters(Context, FArdaValidCudaMappingParameters{}, Other);
		EXPECT_FALSE(Different);
		EXPECT_NE(Different.mMessage.find("same schema"), eastl::string::npos);
		EXPECT_EQ(Other.mCount, 41u);
	}

	TEST(ArdaNodeParameterContracts, CudaHostResolutionRejectsDeviceStorageBeforeUsingHostOffsets)
	{
		FArdaDependencyExecutionContext Context;
		FArdaContractCudaParameters::FArdaCuda DeviceArguments;
		DeviceArguments.mCount = 53;
		const auto Status = ResolveNodeCudaParameters(Context, FArdaValidCudaMappingParameters{}, DeviceArguments);
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("retained host storage"), eastl::string::npos);
		EXPECT_EQ(DeviceArguments.mCount, 53u);
		EXPECT_EQ(DeviceArguments.mInput, nullptr);
		EXPECT_EQ(DeviceArguments.mOutput, nullptr);
	}

	TEST(ArdaNodeParameterContracts, CudaHostResolutionChecksTheActualDestinationLayout)
	{
		FArdaDependencyExecutionContext Context;
		FArdaBorrowedCudaMetadataStorage Other;
		Other.mCount = 67;
		const auto Status = ResolveNodeCudaParameters(Context, FArdaValidCudaMappingParameters{}, Other);
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("retained host storage"), eastl::string::npos);
		EXPECT_EQ(Other.mCount, 67u);
	}

	TEST(ArdaNodeParameterContracts, CudaHostResolutionAlsoSupportsPlainComputeSchemas)
	{
		FArdaDependencyExecutionContext Context;
		FArdaPlainComputeValues Host;
		Host.mCount = 79;
		const auto Status = ResolveNodeCudaParameters(Context, FArdaScalarCudaNodeParameters{}, Host);
		EXPECT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Host.mCount, 79u);
	}
}
