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

	/** Authored requirements plus compiler-populated pipeline metadata.
	 * Describe may supply prepared shader contributions, fixed state and dynamic requirements.
	 * Stage identities and logical connections are owned by TArdaNodeDefinition and parameter metadata;
	 * returning them from Describe is rejected.
	 */
	struct FArdaDependencyNodeDesc
	{
		/** Compiler-owned graphics stage selected by EArdaNodeType; Describe must leave None. */
		EArdaGraphicsStage mGraphicsStage = EArdaGraphicsStage::None;
		/** Compiler-owned logical connection from ARDA_NODE_STAGE_INPUT; Describe must leave empty. */
		FArdaGraphStageValue mGraphicsInput;
		/** Optional vertex input layout; raster fixed state is supplied independently by the raster node. */
		FArdaRHIInputLayoutRef mGraphicsInputLayout;
		/** Reusable fixed-function nodes may let the compiler select traditional versus meshlet rasterization. */
		bool mbAutomaticRasterFamily = false;
		/** Compiler-owned ray stage selected by EArdaNodeType; Describe must leave None. */
		EArdaRHIShaderStage mRayStage = EArdaRHIShaderStage::None;
		/** Compiler-owned providers from ARDA_NODE_STAGE_INPUTS; Describe must leave empty. */
		eastl::vector<FArdaGraphStageValue> mRayInputs;
		eastl::vector<FArdaDependencyAccess> mAccesses;
		/** Created after physical allocation and pipeline resolution, separately per frame slot. */
		eastl::vector<FArdaDependencyShaderBindings> mShaderBindings;
		eastl::vector<FArdaDependencyBindlessTable> mBindlessTables;
		eastl::vector<FArdaDependencyShaderTable> mShaderTables;
		/** Local prepared shaders/settings; resource and explicit ordering edges never import contributions. */
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
		/** Compiler-owned scheduling classification for typed pipeline stages.
		 * Describe must leave false; choose a stage EArdaNodeType to author a stage operation.
		 */
		bool mbPipelineStageOnly = false;
		/** Graph-populated admission facts from GetRequirements plus recognized descriptor features.
		 * Authors declare explicit requirements through the node/definition hook, not this output field.
		 */
		FArdaDependencyNodeRequirements mRequirements;
		/** Loaded semantic effects are complete even when shader schemas await target-device preparation. */
		bool mbArchivedShaderAccesses = false;

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
