/** Stage-specific node contracts for automatic graphics pipeline assembly. */
#pragma once

#include "Graph/Node/ArdaDependencyNode.h"

namespace arda
{
	/** A shader/fixed-function operation returning a typed logical value.
	 * Describe declares this stage's shader, bindings and physical effects, without requesting a pipeline.
	 * Vertex owns the input layout; Raster owns fixed-function configuration; Pixel owns the attachments.
	 * Macro-authored parameters mark their logical input with ARDA_NODE_STAGE_INPUT; legacy schemas use mInput.
	 * Mesh accepts an empty payload when amplification is absent. Override GetStageInput for a custom layout.
	 * Optional ConfigureGraphics fills dynamic draw inputs; the compiler issues the single assembled draw.
	 * Optional FetchBuffer(Edit, const Parameters&, const FArdaStageBufferRequest&) registers a materializer.
	 */
	template <class Derived, class Parameters, EArdaGraphicsStage Stage>
	class TArdaGraphicsStageNode : public TArdaGraphicsDependencyNode<Derived, Parameters>
	{
		static_assert(Stage != EArdaGraphicsStage::None && Stage <= EArdaGraphicsStage::Mesh,
		    "A graphics stage template requires a recognized stage.");
		using FArdaBase = TArdaGraphicsDependencyNode<Derived, Parameters>;

	public:
		static constexpr EArdaGraphicsStage mGraphicsStage = Stage;
		using FArdaStageResult = TArdaGraphStageValue<GetArdaGraphicsStageOutput(Stage)>;

		static FArdaGraphStageValue GetStageInput(const Parameters& P)
		{
			if constexpr (TArdaNodeParameterTraits<Parameters>::value)
			{
				return Parameters::GetNodeParameterMetadata().GetStageInput(P);
			}
			else if constexpr (Stage == EArdaGraphicsStage::Vertex || Stage == EArdaGraphicsStage::Amplification)
			{
				return {};
			}
			else
			{
				return P.mInput;
			}
		}

		/** Shader-only stages need no recording hook. ConfigureGraphics is used by the assembled draw. */
		template <class Node = Derived>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const Parameters&,
		    const typename Node::FArdaState&,
		    typename Node::FArdaInstanceState&)
		{
			return {};
		}

		/** Override to bind vertex/index buffers, choose draw arguments, set viewport, or clear attachments. */
		template <class Node = Derived>
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const Parameters&,
		    const typename Node::FArdaState&,
		    typename Node::FArdaInstanceState&,
		    FArdaGraphicsWork&)
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
			if constexpr (GetArdaGraphicsStageOutput(Stage) == EArdaGraphStageValueKind::Vertices)
			{
				return Operation.GetVertices();
			}
			else if constexpr (Stage == EArdaGraphicsStage::Hull)
			{
				return Operation.GetControlPoints();
			}
			else if constexpr (Stage == EArdaGraphicsStage::Amplification)
			{
				return Operation.GetMeshPayload();
			}
			else if constexpr (Stage == EArdaGraphicsStage::Raster)
			{
				return Operation.GetFragments();
			}
			else
			{
				return Operation.GetPixels();
			}
		}
	};

	template <class Node, class Parameters>
	using TArdaVertexStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Vertex>;
	template <class Node, class Parameters>
	using TArdaRasterStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Raster>;
	template <class Node, class Parameters>
	using TArdaPixelStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Pixel>;
	template <class Node, class Parameters>
	using TArdaHullStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Hull>;
	template <class Node, class Parameters>
	using TArdaDomainStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Domain>;
	template <class Node, class Parameters>
	using TArdaGeometryStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Geometry>;
	template <class Node, class Parameters>
	using TArdaAmplificationStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Amplification>;
	template <class Node, class Parameters>
	using TArdaMeshStageNode = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Mesh>;
}
