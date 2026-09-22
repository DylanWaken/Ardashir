/** Execution-domain and resource requirements of one logical graph node. */
#pragma once

#include "Graph/Node/ArdaDependencyBindings.h"
#include "Graph/Node/ArdaDependencyRequirements.h"
#include "Inductor/Rules/ArdaInductorPipeline.h"
#include "Graph/Resources/ArdaGraphicsState.h"

namespace arda
{
	/** Execution domain; CUDA is kept on one framework stream per compiled batch. */
	enum class EArdaDependencyNodeKind : uint8_t
	{
		Graphics,
		Compute,
		Copy,
		Cuda,
		Synchronization
	};

	/** Compile-time declaration returned by a registered node's parameter visitor. */
	struct FArdaDependencyNodeDesc
	{
		/** Parameter-macro nodes require complete automatic bindings for their prepared shader contracts. */
		bool mbDeclarativeParameters = false;
		/** Filled by graphics stage templates; the preprocess assembler validates this typed connection. */
		EArdaGraphicsStage mGraphicsStage = EArdaGraphicsStage::None;
		FArdaGraphStageValue mGraphicsInput;
		/** Optional vertex input layout; raster fixed state is supplied independently by the raster node. */
		FArdaRHIInputLayoutRef mGraphicsInputLayout;
		/** Reusable fixed-function nodes may let the compiler select traditional versus meshlet rasterization. */
		bool mbAutomaticRasterFamily = false;
		/** Ray stage providers connect explicitly to a ray-generation terminal. */
		EArdaRHIShaderStage mRayStage = EArdaRHIShaderStage::None;
		eastl::vector<FArdaGraphStageValue> mRayInputs;
		eastl::vector<FArdaDependencyAccess> mAccesses;
		/** Created after physical allocation and pipeline resolution, separately per frame slot. */
		eastl::vector<FArdaDependencyShaderBindings> mShaderBindings;
		eastl::vector<FArdaDependencyBindlessTable> mBindlessTables;
		eastl::vector<FArdaDependencyShaderTable> mShaderTables;
		eastl::vector<FArdaInductorPipelineContribution> mPipelineStages;
		eastl::vector<FArdaInductorPipelineRequest> mPipelines;
		eastl::vector<FArdaDependencyResourceHandle> mColorTargets;
		FArdaDependencyResourceHandle mDepthTarget;
		/** Relative cost hint used for critical path and async overlap estimates. Must be positive. */
		uint32_t mEstimatedCost = 1;
		/** Extra device memory retained by an adapter outside declared graph resources. */
		uint64_t mWorkspaceBytes = 0;
		/** Graph-owned temporary buffer, valid only during this node's work and eligible for aliasing. */
		uint64_t mTransientWorkspaceBytes = 0;
		/** Keep observable operations even when none of their resource values is consumed. */
		bool mbSideEffect = false;
		/** Pure stage declarations contribute a shader without recording a separate GPU dispatch. */
		bool mbPipelineStageOnly = false;
		/** Graph-populated admission facts from GetRequirements plus recognized descriptor features.
		 * Authors declare explicit requirements through the node/definition hook, not this output field.
		 */
		FArdaDependencyNodeRequirements mRequirements;

		/** Declare shader arguments. Attachment derives hazards from metadata; compilation creates
		 * retained binding sets. Metadata has static lifetime. Declare non-shader accesses separately.
		 */
		template <class ShaderParameters>
		void BindShader(eastl::vector<FArdaDependencyShaderResource> Resources,
		    eastl::string Slot = "default",
		    eastl::vector<FArdaDependencyShaderValue> Values = {})
		{
			mShaderBindings.push_back({&ShaderParameters::GetStaticMetadata(),
			    eastl::move(Slot),
			    eastl::move(Resources),
			    eastl::move(Values)});
		}
	};
}
