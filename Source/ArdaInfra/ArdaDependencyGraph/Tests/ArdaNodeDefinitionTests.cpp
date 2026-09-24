#include "Graph/Node/ArdaNodeDefinition.h"
#include <gtest/gtest.h>

namespace arda::node_definition_tests
{
	template <EArdaNodeType Type>
	struct TArdaDefinitionParameters
	{
		FArdaGraphStageValue mInput;
		eastl::vector<FArdaGraphRayShader> mShaders;
	};

	template <EArdaNodeType Type>
	class TArdaDefinitionNode final
	    : public TArdaNodeDefinition<TArdaDefinitionNode<Type>, TArdaDefinitionParameters<Type>, Type>
	{
	public:
		using FArdaParameters = TArdaDefinitionParameters<Type>;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {eastl::string("test.node-definition.") + eastl::to_string(uint32_t(Type)), 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return {};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
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

	template <EArdaNodeType Type>
	void RegisterDefinition()
	{
		using FArdaNode = TArdaDefinitionNode<Type>;
		static_assert(TArdaDependencyNodeContract<FArdaNode>::value);
		static_assert(
		    eastl::is_same_v<decltype(FArdaNode::CheckRequiredFeatures(static_cast<const IArdaRHIDevice*>(nullptr),
		                         typename FArdaNode::FArdaParameters{})),
		        FArdaRHIStatus>);
		const auto Status = FArdaNode::Register();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto Registered = FArdaNodeRegistry::Get().Find(FArdaNode::GetMetadata().mName);
		ASSERT_TRUE(Registered);
		if constexpr (Type == EArdaNodeType::Cuda)
		{
			EXPECT_TRUE(bool(Registered->mPrepareCuda));
			EXPECT_FALSE(bool(Registered->mRecord));
		}
		else
		{
			EXPECT_TRUE(bool(Registered->mRecord));
			EXPECT_FALSE(bool(Registered->mPrepareCuda));
		}
	}

	TEST(ArdaNodeDefinition, OneTemplateRegistersEveryExecutionDomainAndPipelineStage)
	{
		RegisterDefinition<EArdaNodeType::Compute>();
		RegisterDefinition<EArdaNodeType::Cuda>();
		RegisterDefinition<EArdaNodeType::Copy>();
		RegisterDefinition<EArdaNodeType::Synchronization>();
		RegisterDefinition<EArdaNodeType::Graphics>();
		RegisterDefinition<EArdaNodeType::Meshlet>();
		RegisterDefinition<EArdaNodeType::RayTracing>();
		RegisterDefinition<EArdaNodeType::WorkGraph>();
		RegisterDefinition<EArdaNodeType::Vertex>();
		RegisterDefinition<EArdaNodeType::Hull>();
		RegisterDefinition<EArdaNodeType::Domain>();
		RegisterDefinition<EArdaNodeType::Geometry>();
		RegisterDefinition<EArdaNodeType::Raster>();
		RegisterDefinition<EArdaNodeType::Pixel>();
		RegisterDefinition<EArdaNodeType::Amplification>();
		RegisterDefinition<EArdaNodeType::Mesh>();
		RegisterDefinition<EArdaNodeType::RayGeneration>();
		RegisterDefinition<EArdaNodeType::RayMiss>();
		RegisterDefinition<EArdaNodeType::RayClosestHit>();
		RegisterDefinition<EArdaNodeType::RayAnyHit>();
		RegisterDefinition<EArdaNodeType::RayIntersection>();
		RegisterDefinition<EArdaNodeType::RayCallable>();
	}

	template <EArdaNodeType Type>
	using TArdaInvocationResult = decltype(TArdaDefinitionNode<Type>{}(eastl::declval<FArdaGraphEdit&>()));
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Compute>, FArdaGraphOperation>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Cuda>, FArdaGraphOperation>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Vertex>, FArdaGraphVertices>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Hull>, FArdaGraphControlPoints>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Domain>, FArdaGraphVertices>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Geometry>, FArdaGraphVertices>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Raster>, FArdaGraphFragments>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Pixel>, FArdaGraphPixels>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Amplification>, FArdaGraphMeshPayload>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::Mesh>, FArdaGraphVertices>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::RayMiss>, FArdaGraphRayShader>);
	static_assert(eastl::is_same_v<TArdaInvocationResult<EArdaNodeType::RayGeneration>, FArdaGraphOperation>);
}
