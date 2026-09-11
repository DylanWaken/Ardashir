/** @file ArdaPipelineStateCache.h
 *  @brief Declares the thread-safe renderer-facing pipeline state cache.
 */
#pragma once

#include "PipelineStateCache/ArdaPipelineStateInitializer.h"
#include "RHI/ArdaRHIDevice.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arda
{
	/** Identifies the kind of cached pipeline state. */
	enum class EArdaPipelineStateKind : uint8_t
	{
		Compute,
		Graphics,
		Meshlet,
		RayTracing,
		WorkGraph
	};

	/** Configures pipeline cache capacities and diagnostic retention. */
	struct FArdaPipelineStateCacheConfiguration
	{
		/** Maximum retained compute pipeline entries. */
		size_t mMaxComputeEntries = 128;
		/** Maximum retained graphics pipeline entries. */
		size_t mMaxGraphicsEntries = 128;
		/** Maximum retained creation diagnostics. */
		size_t mMaxDiagnostics = 64;
		/** Maximum retained meshlet pipeline entries. */
		size_t mMaxMeshletEntries = 128;
		/** Maximum retained ray-tracing pipeline entries. */
		size_t mMaxRayTracingEntries = 64;
		/** Maximum retained work-graph pipeline entries. */
		size_t mMaxWorkGraphEntries = 64;
	};

	/** Snapshot of pipeline cache activity and occupancy. */
	struct FArdaPipelineStateCacheStats
	{
		/** Number of requests served from cached entries. */
		uint64_t mHits = 0;
		/** Number of requests requiring pipeline creation. */
		uint64_t mMisses = 0;
		/** Number of requests that waited for in-flight creation. */
		uint64_t mWaits = 0;
		/** Number of pipeline creation failures. */
		uint64_t mCreateFailures = 0;
		/** Number of creations currently in flight. */
		size_t mInFlight = 0;
		/** Number of retained compute entries. */
		size_t mComputeEntries = 0;
		/** Number of retained graphics entries. */
		size_t mGraphicsEntries = 0;
		/** Number of retained meshlet entries. */
		size_t mMeshletEntries = 0;
		/** Number of retained ray-tracing entries. */
		size_t mRayTracingEntries = 0;
		/** Number of retained work-graph entries. */
		size_t mWorkGraphEntries = 0;
	};

	/** Describes one failed pipeline state creation. */
	struct FArdaPipelineStateDiagnostic
	{
		/** Kind of pipeline whose creation failed. */
		EArdaPipelineStateKind mKind = EArdaPipelineStateKind::Compute;
		/** RHI result returned by pipeline creation. */
		arda::EArdaRHIResult mCode = arda::EArdaRHIResult::Success;
		/** Hash of the normalized pipeline descriptor. */
		size_t mDescriptorHash = 0;
		/** Optional pipeline debug name. */
		eastl::string mDebugName;
		/** Human-readable failure message. */
		eastl::string mMessage;
	};

	/**
     * Thread-safe renderer-facing PSO cache bound permanently to one RHI device.
     * The device may itself perform lower-level descriptor caching.
     */
	class FArdaPipelineStateCache final
	{
	public:
		/**
         * Creates a cache permanently bound to one RHI device.
         * @param Device Device used to create pipeline states.
         * @param Configuration Cache capacities and diagnostic retention.
         */
		explicit FArdaPipelineStateCache(arda::FArdaRHIDeviceRef Device,
		    FArdaPipelineStateCacheConfiguration Configuration = {});

		/** Releases the cache and waits for internal operations to finish. */
		~FArdaPipelineStateCache();

		/** Pipeline state caches cannot be copied. */
		FArdaPipelineStateCache(const FArdaPipelineStateCache&) = delete;

		/** Pipeline state caches cannot be copy-assigned. */
		FArdaPipelineStateCache& operator=(const FArdaPipelineStateCache&) = delete;

		/** Pipeline state caches cannot be moved. */
		FArdaPipelineStateCache(FArdaPipelineStateCache&&) = delete;

		/** Pipeline state caches cannot be move-assigned. */
		FArdaPipelineStateCache& operator=(FArdaPipelineStateCache&&) = delete;

		/**
         * Resolves or creates a compute pipeline state.
         * @param Initializer Compute pipeline description.
         * @param OutPipeline Receives the resolved pipeline.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus GetOrCreateCompute(const FArdaComputePipelineStateInitializer& Initializer,
		    arda::FArdaRHIComputePipelineRef& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Resolves or creates a framebuffer-compatible graphics pipeline state.
         * @param Initializer Graphics pipeline description.
         * @param Framebuffer Framebuffer used to complete dynamic formats.
         * @param OutPipeline Receives the resolved pipeline.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus GetOrCreateGraphics(const FArdaGraphicsPipelineStateInitializer& Initializer,
		    const arda::FArdaRHIFramebufferRef& Framebuffer,
		    arda::FArdaRHIGraphicsPipelineRef& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Resolves or creates a framebuffer-compatible meshlet pipeline state.
         * @param Initializer Meshlet pipeline description.
         * @param Framebuffer Framebuffer used to complete dynamic formats.
         * @param OutPipeline Receives the resolved pipeline.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus GetOrCreateMeshlet(const FArdaMeshletPipelineStateInitializer& Initializer,
		    const arda::FArdaRHIFramebufferRef& Framebuffer,
		    arda::FArdaRHIMeshletPipelineRef& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/** Resolves or creates a ray-tracing pipeline state. */
		[[nodiscard]] arda::FArdaRHIStatus GetOrCreateRayTracing(
		    const FArdaRayTracingPipelineStateInitializer& Initializer,
		    arda::FArdaRHIRayTracingPipelineRef& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/** Resolves or creates a work-graph pipeline state. */
		[[nodiscard]] arda::FArdaRHIStatus GetOrCreateWorkGraph(
		    const FArdaWorkGraphPipelineStateInitializer& Initializer,
		    arda::FArdaRHIWorkGraphPipelineRef& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Creates and caches a compute pipeline without returning it.
         * @param Initializer Compute pipeline description.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus PrecacheCompute(const FArdaComputePipelineStateInitializer& Initializer,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Creates and caches a framebuffer-compatible graphics pipeline.
         * @param Initializer Graphics pipeline description.
         * @param Framebuffer Framebuffer used to complete dynamic formats.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus PrecacheGraphics(const FArdaGraphicsPipelineStateInitializer& Initializer,
		    const arda::FArdaRHIFramebufferRef& Framebuffer,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Creates and caches a framebuffer-compatible meshlet pipeline.
         * @param Initializer Meshlet pipeline description.
         * @param Framebuffer Framebuffer used to complete dynamic formats.
         * @param RequestingDevice Optional device used to validate ownership.
         * @return RHI status for lookup or creation.
         */
		[[nodiscard]] arda::FArdaRHIStatus PrecacheMeshlet(const FArdaMeshletPipelineStateInitializer& Initializer,
		    const arda::FArdaRHIFramebufferRef& Framebuffer,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/** Creates and caches a ray-tracing pipeline without returning it. */
		[[nodiscard]] arda::FArdaRHIStatus PrecacheRayTracing(
		    const FArdaRayTracingPipelineStateInitializer& Initializer,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/** Creates and caches a work-graph pipeline without returning it. */
		[[nodiscard]] arda::FArdaRHIStatus PrecacheWorkGraph(const FArdaWorkGraphPipelineStateInitializer& Initializer,
		    const arda::IArdaRHIDevice* RequestingDevice = nullptr);

		/**
         * Resolves and binds a compute pipeline state.
         * @param CommandList Command list receiving the state.
         * @param Initializer Compute pipeline description.
         * @param State Additional compute state to bind.
         * @return RHI status for resolution and binding.
         */
		[[nodiscard]] arda::FArdaRHIStatus SetComputePipelineState(arda::IArdaRHICommandList& CommandList,
		    const FArdaComputePipelineStateInitializer& Initializer,
		    arda::FArdaRHIComputeState State);

		/**
         * Resolves and binds a graphics pipeline state.
         * @param CommandList Command list receiving the state.
         * @param Initializer Graphics pipeline description.
         * @param State Additional graphics state, including the framebuffer.
         * @return RHI status for resolution and binding.
         */
		[[nodiscard]] arda::FArdaRHIStatus SetGraphicsPipelineState(arda::IArdaRHICommandList& CommandList,
		    const FArdaGraphicsPipelineStateInitializer& Initializer,
		    arda::FArdaRHIGraphicsState State);

		/**
         * Resolves and binds a meshlet pipeline state.
         * @param CommandList Command list receiving the state.
         * @param Initializer Meshlet pipeline description.
         * @param State Additional meshlet state, including the framebuffer.
         * @return RHI status for resolution and binding.
         */
		[[nodiscard]] arda::FArdaRHIStatus SetMeshletPipelineState(arda::IArdaRHICommandList& CommandList,
		    const FArdaMeshletPipelineStateInitializer& Initializer,
		    arda::FArdaRHIMeshletState State);

		/**
         * Evicts least-recently-used entries until capacities are satisfied.
         * @param MaxComputeEntries Maximum retained compute entries.
         * @param MaxGraphicsEntries Maximum retained graphics entries.
         */
		void Trim(size_t MaxComputeEntries, size_t MaxGraphicsEntries);

		/**
         * Evicts least-recently-used entries until all capacities are satisfied.
         * @param MaxComputeEntries Maximum retained compute entries.
         * @param MaxGraphicsEntries Maximum retained graphics entries.
         * @param MaxMeshletEntries Maximum retained meshlet entries.
         */
		void Trim(size_t MaxComputeEntries, size_t MaxGraphicsEntries, size_t MaxMeshletEntries);

		/** Evicts all five pipeline kinds to the supplied capacities. */
		void Trim(size_t MaxComputeEntries,
		    size_t MaxGraphicsEntries,
		    size_t MaxMeshletEntries,
		    size_t MaxRayTracingEntries,
		    size_t MaxWorkGraphEntries);

		/** Removes all completed cached pipeline entries. */
		void Clear();

		/** @return A snapshot of cache activity and occupancy. */
		[[nodiscard]] FArdaPipelineStateCacheStats GetStats() const;

		/** @return Retained pipeline creation diagnostics. */
		[[nodiscard]] eastl::vector<FArdaPipelineStateDiagnostic> GetDiagnostics() const;

		/** @return The RHI device permanently bound to this cache. */
		[[nodiscard]] const arda::IArdaRHIDevice* GetDevice() const noexcept;

	private:
		/** Opaque cache implementation. */
		struct FImpl;
		/** Owned cache implementation. */
		std::unique_ptr<FImpl> mImpl;
	};
}
