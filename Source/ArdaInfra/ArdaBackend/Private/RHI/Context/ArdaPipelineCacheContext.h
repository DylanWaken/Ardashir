#pragma once

#include "RHI/Pipelines/ArdaPipelineStateCache.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <condition_variable>
#include <mutex>

namespace arda
{
	struct FArdaPipelineStateCache::FArdaImpl
	{
		template <typename Desc, typename Pipeline>
		struct TArdaEntry
		{
			Desc mDesc;
			Pipeline mPipeline;
			uint64_t mLastUse = 0;
			bool mbInFlight = false;
		};

		using FArdaComputeEntry = TArdaEntry<arda::FArdaRHIComputePipelineDesc, arda::FArdaRHIComputePipelineRef>;
		using FArdaGraphicsEntry = TArdaEntry<arda::FArdaRHIGraphicsPipelineDesc, arda::FArdaRHIGraphicsPipelineRef>;
		using FArdaMeshletEntry = TArdaEntry<arda::FArdaRHIMeshletPipelineDesc, arda::FArdaRHIMeshletPipelineRef>;
		using FArdaRayTracingEntry =
		    TArdaEntry<arda::FArdaRHIRayTracingPipelineDesc, arda::FArdaRHIRayTracingPipelineRef>;
		using FArdaWorkGraphEntry = TArdaEntry<arda::FArdaRHIWorkGraphPipelineDesc, arda::FArdaRHIWorkGraphPipelineRef>;

		explicit FArdaImpl(arda::FArdaRHIDeviceRef InDevice, FArdaPipelineStateCacheConfiguration InConfiguration)
		    : mDevice(eastl::move(InDevice)),
		      mConfiguration(InConfiguration)
		{
		}

		template <typename Entry>
		static void EvictTo(eastl::vector<Entry>& Entries, size_t Capacity);

		void AddDiagnostic(EArdaPipelineStateKind Kind,
		    const arda::FArdaRHIStatus& Status,
		    size_t Hash,
		    const eastl::string& DebugName);

		arda::FArdaRHIStatus CheckDevice(const arda::IArdaRHIDevice* RequestingDevice,
		    EArdaPipelineStateKind Kind,
		    size_t Hash,
		    const eastl::string& DebugName);

		template <typename Entry, typename Desc, typename Pipeline>
		arda::FArdaRHIStatus GetOrCreate(eastl::vector<Entry>& Entries,
		    const Desc& CanonicalDesc,
		    const arda::FArdaRHIStatus& PreparationStatus,
		    EArdaPipelineStateKind Kind,
		    size_t Capacity,
		    Pipeline& OutPipeline,
		    const arda::IArdaRHIDevice* RequestingDevice);

		arda::FArdaRHIDeviceRef mDevice;
		FArdaPipelineStateCacheConfiguration mConfiguration;
		mutable std::mutex mMutex;
		std::condition_variable mChanged;
		eastl::vector<FArdaComputeEntry> mCompute;
		eastl::vector<FArdaGraphicsEntry> mGraphics;
		eastl::vector<FArdaMeshletEntry> mMeshlet;
		eastl::vector<FArdaRayTracingEntry> mRayTracing;
		eastl::vector<FArdaWorkGraphEntry> mWorkGraph;
		eastl::vector<FArdaPipelineStateDiagnostic> mDiagnostics;
		uint64_t mUseSerial = 0;
		uint64_t mHits = 0;
		uint64_t mMisses = 0;
		uint64_t mWaits = 0;
		uint64_t mCreateFailures = 0;
		size_t mInFlight = 0;
	};
}
