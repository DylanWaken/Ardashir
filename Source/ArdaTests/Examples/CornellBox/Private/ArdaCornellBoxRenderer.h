#pragma once

#include "ArdaBackend.h"
#include "ArdaMath.h"
#include "RHI/Scheduling/ArdaSwapChain.h"
#include "ArdaDependencyGraph.h"
#include "Nodes/ArdaCornellBoxNodes.h"
#include "Graph/Node/ArdaGraphPackage.h"

#include <EASTL/string.h>
#include <memory>

namespace arda
{
	struct FArdaCornellBoxSettings
	{
		/** Independent path samples launched in parallel per pixel and dispatch. */
		uint32_t mSamplesPerDispatch = 8;
		uint32_t mMaxSamples = 1024;
		uint32_t mMaxBounces = 12;
		uint32_t mSeed = 1;
		float mExposure = 1.0f;
		bool mbCompactStaticBlas = true;
		/** Optional OIDN spatial or NPPD temporal CUDA model descriptor. */
		std::filesystem::path mDenoisingPackage;
		/** Checkpoint for the descriptor; defaults to its sibling .ardastate file. */
		std::filesystem::path mDenoisingStateDict;
		/** Optional linear HDR image pair written after the finite demonstration run. */
		std::filesystem::path mDenoisingDumpPrefix;
	};

	class FArdaCornellBoxRenderer final
	{
	public:
		bool Initialize(arda::FArdaRHIDeviceRef Device,
		    arda::EArdaRHIFormat SwapChainFormat,
		    const FArdaCornellBoxSettings& Settings);

		/** Updates the free-flight camera and resets progressive accumulation on motion. */
		void UpdateCamera(float Forward, float Right, float LookX, float LookY, float DeltaSeconds);

		/** Invalidates resolution-dependent progressive state after swap-chain resize. */
		void NotifyResize();

		bool RenderFrame(arda::IArdaSwapChain& SwapChain);
		bool SaveDenoisingImages();

		[[nodiscard]] const eastl::string& GetError() const noexcept
		{
			return mError;
		}

		[[nodiscard]] uint32_t GetAccumulatedSamples() const noexcept
		{
			return mAccumulatedSamples;
		}

	private:
		bool CreateSceneGeometryResources();
		bool BuildSceneAccelerationStructures();
		bool BuildUncompactedSceneAccelerationStructures();
		bool CompactSceneBlas(uint64_t CompactedSize);
		bool CreateFrameTlasResource();
		bool ExecuteGraph(arda::FArdaDependencyGraph& Graph, const char* Description);
		void ResetAccumulation();
		bool AppendDenoising(FArdaGraphEdit& Edit,
		    FArdaGraphTexture Accumulation,
		    FArdaGraphBuffer Constants,
		    uint32_t Width,
		    uint32_t Height,
		    FArdaGraphTexture& OutImage);
		bool AppendTemporalDenoising(FArdaGraphEdit& Edit,
		    FArdaGraphBuffer Radiance,
		    FArdaGraphBuffer Guides,
		    FArdaGraphBuffer Constants,
		    uint32_t Width,
		    uint32_t Height,
		    FArdaGraphTexture OutImage);

		arda::FArdaRHIDeviceRef mDevice;

		struct FArdaCachedFrame
		{
			FArdaRHITextureRef mBackBuffer;
			uint32_t mDispatchSamples = 0;
			uint32_t mHistorySlot = 0;
			bool mbResetHistory = false;
			eastl::shared_ptr<FArdaCornellFrameInput> mInput;
			std::unique_ptr<FArdaDependencyGraph> mGraph;
		};

		eastl::vector<eastl::shared_ptr<FArdaCachedFrame>> mFrames;

		arda::FArdaRHIBufferRef mVertexBuffer;
		arda::FArdaRHIBufferRef mIndexBuffer;
		arda::FArdaRHIBufferRef mMaterialBuffer;
		arda::FArdaRHIAccelStructRef mBlas;
		arda::FArdaRHIAccelStructRef mTlas;
		uint64_t mTlasWorkspaceBytes = 0;
		arda::FArdaRHITextureRef mAccumulationTexture;
		FArdaRHITextureRef mDenoisedTexture;
		eastl::shared_ptr<const FArdaGraphPackage> mDenoisingPackage;
		FArdaRHITensorDesc mDenoisingTensorDesc;
		eastl::shared_ptr<eastl::vector<uint8_t>> mNoisySnapshot;
		eastl::shared_ptr<eastl::vector<uint8_t>> mDenoisedSnapshot;
		FArdaRHIBufferRef mTemporalHistory[2];
		bool mbTemporalDenoising = false;

		struct FArdaTensorSnapshot
		{
			eastl::string mName;
			eastl::shared_ptr<eastl::vector<uint8_t>> mBytes;
		};

		eastl::vector<FArdaTensorSnapshot> mTensorSnapshots;

		FArdaCornellBoxSettings mSettings;
		FArdaVector3f mCameraPosition = {0.0f, -2.65f, 1.0f};
		float mCameraYaw = 1.57079632679f;
		float mCameraPitch = 0.0f;
		uint32_t mAccumulatedSamples = 0;
		uint32_t mFrameIndex = 0;
		bool mbSceneReady = false;
		eastl::string mError;
	};
}
