/** Stage-specific contracts for graph-assembled ray-tracing pipelines. */
#pragma once
#include "Graph/Node/ArdaDependencyNode.h"
#include "Graph/Resources/ArdaRayTracingState.h"

namespace arda::node_detail
{
	/** Providers return logical shader values; a ray-generation terminal declares ARDA_NODE_STAGE_INPUTS.
	 * Describe contributes this stage's shader and bindings. Exports and hit groups retain their names.
	 * Only ray generation authors Record.
	 */
	template <class Derived, class Parameters, EArdaRHIShaderStage Stage>
	class TArdaRayStageDefinition : public TArdaExecutionNode<Derived, Parameters, EArdaDependencyNodeKind::Compute>
	{
		static_assert(Stage == EArdaRHIShaderStage::RayGeneration || Stage == EArdaRHIShaderStage::Miss ||
		        Stage == EArdaRHIShaderStage::ClosestHit || Stage == EArdaRHIShaderStage::AnyHit ||
		        Stage == EArdaRHIShaderStage::Intersection || Stage == EArdaRHIShaderStage::Callable,
		    "A ray-tracing stage template requires a single ray shader stage.");
		using FArdaBase = TArdaExecutionNode<Derived, Parameters, EArdaDependencyNodeKind::Compute>;

	public:
		static constexpr EArdaRHIShaderStage mRayStage = Stage;
		using FArdaStageResult =
		    eastl::conditional_t<Stage == EArdaRHIShaderStage::RayGeneration, FArdaGraphOperation, FArdaGraphRayShader>;

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

		/** Typed stage arrays feed the next stage directly, preserving each lane's producer identity. */
		template <class... Arguments, eastl::enable_if_t<ArdaHasNodeArray<Arguments...>, int> = 0>
		static auto Invoke(FArdaGraphEdit& Edit, Arguments&&... Values)
		    -> decltype(FArdaBase::Invoke(Edit, eastl::forward<Arguments>(Values)...),
		        TArdaNodeArray<FArdaStageResult>{})
		{
			return FArdaBase::Invoke(Edit, eastl::forward<Arguments>(Values)...)
			    .Map(
			        [](const FArdaGraphOperation& Operation)
			        {
				        return Result(Operation);
			        });
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

}
