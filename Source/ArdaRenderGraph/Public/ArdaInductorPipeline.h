/** @file ArdaInductorPipeline.h
 * Stage inference and cached native pipeline creation for persistent dependency graphs.
 */
#pragma once

#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include <EASTL/shared_ptr.h>

namespace arda
{
	/** Optional complete pipeline settings, supplied by a request or a stage-bearing node.
     * Only the initializer selected by mKind participates. Shader fields may seed a complete
     * pipeline; contributions add stages but cannot silently replace an existing shader.
     */
	struct FArdaInductorPipelineConfiguration
	{
		/** Selects the initializer whose settings and shaders are used. */
		EArdaPipelineStateKind mKind = EArdaPipelineStateKind::Graphics;
		/** Compute shader and root-layout settings. */
		FArdaComputePipelineStateInitializer mCompute;
		/** Traditional raster stages and fixed-function settings. */
		FArdaGraphicsPipelineStateInitializer mGraphics;
		/** Mesh/amplification raster stages and fixed-function settings. */
		FArdaMeshletPipelineStateInitializer mMeshlet;
		/** Ray exports, hit groups, local layouts and traversal limits. */
		FArdaRayTracingPipelineStateInitializer mRayTracing;
		/** Work-graph program, entry point and input-record limits. */
		FArdaWorkGraphPipelineStateInitializer mWorkGraph;
	};

	/** One shader-stage contribution; a node may contribute any number of these.
     * GetStage() identifies the stage. Empty groups are shared; named groups disambiguate
     * independent pipelines reachable from the same consumer. Ray exports require names,
     * while closest/any-hit/intersection shaders use mHitGroupName instead.
     */
	struct FArdaInductorPipelineContribution
	{
		/** Retained shader with one stage and a deterministic content identity. */
		FArdaRHIShaderRef mShader;
		/** Optional logical pipeline group; empty contributes to every selected group. */
		eastl::string mGroup;
		/** Named general ray export; ignored for ordinary graphics/compute stages. */
		eastl::string mExportName;
		/** Hit-group export shared by its closest/any-hit/intersection contributions. */
		eastl::string mHitGroupName;
		/** Optional local root/descriptor layout for this ray export or hit group. */
		FArdaRHIBindingLayoutRef mLocalBindingLayout;
		/** Global layouts; identical declarations deduplicate, disjoint stage visibility may share a space. */
		eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
		/** Optional fixed/program settings; can also seed complete shader stages. */
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	/** Topology projected from the persistent IR; intermediate nodes need no shader stages. */
	struct FArdaInductorPipelineNode
	{
		/** Unique ID within this topology projection. */
		uint64_t mNodeId = 0;
		/** IDs of nodes supplying data or stage ancestry. */
		eastl::vector<uint64_t> mDependencies;
		/** Zero or more shader stages/settings contributed by this node. */
		eastl::vector<FArdaInductorPipelineContribution> mContributions;
		/** Pipeline families requested by this consumer. For a downstream request in the same family,
		 * this node and its upstream ancestry are excluded from that branch of stage inference.
		 * The requesting terminal itself is never excluded; shared providers may have independent edges.
		 */
		eastl::vector<EArdaPipelineStateKind> mPipelineBoundaries;
	};

	/** One pipeline slot requested by a node; inference traverses its transitive dependencies,
     * stopping each branch before an upstream consumer requesting the same pipeline family.
     * A request with no group accepts a unique reachable group. Conflicting stages, settings,
     * exports, or layouts fail instead of depending on node insertion order.
     */
	struct FArdaInductorPipelineRequest
	{
		/** Consumer-local slot name used by the owning execution context. */
		eastl::string mSlot;
		/** Consumer whose ancestry is searched, including its own contributions. */
		uint64_t mTerminalNodeId = 0;
		/** Required pipeline family; other families may occur on data dependencies. */
		EArdaPipelineStateKind mKind = EArdaPipelineStateKind::Graphics;
		/** Selects one named branch when more than one group reaches the consumer. */
		eastl::string mGroup;
		/** Optional request-level pipeline settings compatible with stage settings. */
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
	};

	/** Immutable result of successful inference. The stable key excludes node IDs, group/slot
     * labels and debug names, and includes shader content identities and pipeline semantics.
     * Graphics/meshlet framebuffer-derived fields are completed by the native cache at resolve.
     */
	struct FArdaInductorPipelinePattern
	{
		/** Owned normalized settings and retained shader/layout references. */
		FArdaInductorPipelineConfiguration mConfiguration;
		/** Deterministic semantic identity, with unresolved framebuffer fields preserved. */
		uint64_t mStableKey = 0;
		/** Sorted participating node IDs for compiler diagnostics; excluded from the key. */
		eastl::vector<uint64_t> mContributingNodeIds;
	};

	/** Typed native result retained by a compiled graph's execution context. */
	struct FArdaInductorResolvedPipeline
	{
		/** Identifies the single populated native pipeline reference. */
		EArdaPipelineStateKind mKind = EArdaPipelineStateKind::Graphics;
		/** Concrete key, including framebuffer-derived formats and sample count after resolution. */
		uint64_t mStableKey = 0;
		/** Cached compute pipeline when mKind is Compute. */
		FArdaRHIComputePipelineRef mCompute;
		/** Cached traditional raster pipeline when mKind is Graphics. */
		FArdaRHIGraphicsPipelineRef mGraphics;
		/** Cached mesh pipeline when mKind is Meshlet. */
		FArdaRHIMeshletPipelineRef mMeshlet;
		/** Cached ray pipeline when mKind is RayTracing. */
		FArdaRHIRayTracingPipelineRef mRayTracing;
		/** Cached executable work graph when mKind is WorkGraph. */
		FArdaRHIWorkGraphPipelineRef mWorkGraph;
	};

	/** Infers one unique compatible pipeline without allocating a native PSO.
     * Missing dependencies, dependency cycles, ambiguous stages/groups, and shaders without
     * deterministic content hashes return errors. The input graph and resources are retained
     * only through shader/layout references copied into the returned pattern.
     */
	[[nodiscard]] TArdaRHIResult<FArdaInductorPipelinePattern> InferArdaInductorPipeline(
	    const eastl::vector<FArdaInductorPipelineNode>& Nodes,
	    const FArdaInductorPipelineRequest& Request);

	/** Creates or reuses the inferred pipeline through the existing device-bound PSO cache.
     * A framebuffer is required for incomplete graphics/meshlet attachment formats or samples.
     * Unsupported device/backend features propagate the cache's ordinary status.
     */
	[[nodiscard]] TArdaRHIResult<FArdaInductorResolvedPipeline> ResolveArdaInductorPipeline(
	    FArdaPipelineStateCache& Cache,
	    const FArdaInductorPipelinePattern& Pattern,
	    const FArdaRHIFramebufferRef& Framebuffer = {});
}
