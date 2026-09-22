/** Stage-specific contracts for graph-assembled ray-tracing pipelines. */
#pragma once
#include "Graph/Node/ArdaDependencyNode.h"
#include "Graph/Resources/ArdaRayTracingState.h"

namespace arda
{
	/** Providers return logical shader values; a ray-generation terminal consumes mShaders.
	 * Describe contributes this stage's shader and bindings. Exports and hit groups retain their names.
	 * Override GetRayInputs for another parameter layout. Only ray generation authors Record.
	 */
	template <class Derived, class Parameters, EArdaRHIShaderStage Stage>
	class TArdaRayTracingStageNode : public TArdaComputeDependencyNode<Derived, Parameters>
	{
		static_assert(Stage == EArdaRHIShaderStage::RayGeneration || Stage == EArdaRHIShaderStage::Miss ||
		        Stage == EArdaRHIShaderStage::ClosestHit || Stage == EArdaRHIShaderStage::AnyHit ||
		        Stage == EArdaRHIShaderStage::Intersection || Stage == EArdaRHIShaderStage::Callable,
		    "A ray-tracing stage template requires a single ray shader stage.");
		using FArdaBase = TArdaComputeDependencyNode<Derived, Parameters>;

	public:
		static constexpr EArdaRHIShaderStage mRayStage = Stage;
		using FArdaStageResult =
		    eastl::conditional_t<Stage == EArdaRHIShaderStage::RayGeneration, FArdaGraphOperation, FArdaGraphRayShader>;

		static eastl::vector<FArdaGraphStageValue> GetRayInputs(const Parameters& P)
		{
			eastl::vector<FArdaGraphStageValue> Result;
			if constexpr (TArdaNodeParameterTraits<Parameters>::value)
			{
				return Parameters::GetNodeParameterMetadata().GetStageInputs(P);
			}
			else if constexpr (Stage == EArdaRHIShaderStage::RayGeneration)
			{
				Result.reserve(P.mShaders.size());
				for (const auto& Shader : P.mShaders)
				{
					Result.push_back(Shader);
				}
			}
			return Result;
		}

		template <class Node = Derived,
		    eastl::enable_if_t<Node::mRayStage != EArdaRHIShaderStage::RayGeneration, int> = 0>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const Parameters&,
		    const typename Node::FArdaState&,
		    typename Node::FArdaInstanceState&)
		{
			return {};
		}

		static FArdaStageResult Invoke(FArdaGraphEdit& Edit, Parameters P, eastl::string Name = {})
		{
			return Result(FArdaBase::Invoke(Edit, eastl::move(P), eastl::move(Name)));
		}

		template <class Initializer, eastl::enable_if_t<eastl::is_invocable_v<Initializer, Parameters&>, int> = 0>
		static FArdaStageResult Invoke(FArdaGraphEdit& Edit, Initializer&& Initialize, eastl::string Name = {})
		{
			return Result(FArdaBase::Invoke(Edit, eastl::forward<Initializer>(Initialize), eastl::move(Name)));
		}

		template <class... Arguments,
		    eastl::enable_if_t<TArdaGraphPositionalArguments<Parameters, Arguments...>::value, int> = 0>
		static auto Invoke(FArdaGraphEdit& Edit, Arguments&&... Values)
		    -> decltype(Parameters{eastl::forward<Arguments>(Values)...}, FArdaStageResult{})
		{
			return Result(FArdaBase::Invoke(Edit, eastl::forward<Arguments>(Values)...));
		}

		FArdaStageResult operator()(FArdaGraphEdit& Edit, Parameters P, eastl::string Name = {}) const
		{
			return Invoke(Edit, eastl::move(P), eastl::move(Name));
		}

		template <class... Arguments>
		auto operator()(FArdaGraphEdit& Edit, Arguments&&... Values) const
		    -> decltype(Invoke(Edit, eastl::forward<Arguments>(Values)...))
		{
			return Invoke(Edit, eastl::forward<Arguments>(Values)...);
		}

	private:
		static FArdaStageResult Result(const FArdaGraphOperation& Operation)
		{
			if constexpr (Stage == EArdaRHIShaderStage::RayGeneration)
			{
				return Operation;
			}
			else
			{
				return Operation.GetRayShader();
			}
		}
	};

	template <class Node, class Parameters>
	using TArdaRayGenerationStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::RayGeneration>;
	template <class Node, class Parameters>
	using TArdaRayMissStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::Miss>;
	template <class Node, class Parameters>
	using TArdaRayClosestHitStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::ClosestHit>;
	template <class Node, class Parameters>
	using TArdaRayAnyHitStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::AnyHit>;
	template <class Node, class Parameters>
	using TArdaRayIntersectionStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::Intersection>;
	template <class Node, class Parameters>
	using TArdaRayCallableStageNode = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::Callable>;
}
