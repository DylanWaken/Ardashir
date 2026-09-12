#pragma once

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "ArdaDependencyGraph.h"
#include "Nodes/ArdaCornellBoxNodes.h"

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

		arda::FArdaRHIDeviceRef mDevice;

		struct FCachedFrame
		{
			FArdaRHITextureRef mBackBuffer;
			uint32_t mDispatchSamples = 0;
			eastl::shared_ptr<FArdaCornellFrameInput> mInput;
			std::unique_ptr<FArdaDependencyGraph> mGraph;
		};

		eastl::vector<eastl::shared_ptr<FCachedFrame>> mFrames;

		arda::FArdaRHIBufferRef mVertexBuffer;
		arda::FArdaRHIBufferRef mIndexBuffer;
		arda::FArdaRHIBufferRef mMaterialBuffer;
		arda::FArdaRHIAccelStructRef mBlas;
		arda::FArdaRHIAccelStructRef mTlas;
		uint64_t mTlasWorkspaceBytes = 0;
		arda::FArdaRHITextureRef mAccumulationTexture;

		FArdaCornellBoxSettings mSettings;
		float mCameraPosition[3] = {0.0f, -2.65f, 1.0f};
		float mCameraYaw = 1.57079632679f;
		float mCameraPitch = 0.0f;
		uint32_t mAccumulatedSamples = 0;
		uint32_t mFrameIndex = 0;
		bool mbSceneReady = false;
		eastl::string mError;
	};
}
