#pragma once

#include "Nodes/ArdaTerrainNodes.h"
#include "ArdaSwapChain.h"
#include "ArdaDependencyGraph.h"

#include <filesystem>
#include <EASTL/string.h>
#include <memory>
#include <vector>

namespace arda
{
	class FArdaTerrainRenderer
	{
	public:
		FArdaTerrainRenderer();
		~FArdaTerrainRenderer();
		void ReleaseFrameGraphs();
		bool Initialize(arda::FArdaRHIDeviceRef device, arda::EArdaRHIFormat swapChainFormat);
		void UpdateCamera(float forward, float right, float lookX, float lookY, float deltaSeconds);
		bool RenderFrame(arda::IArdaSwapChain& swapChain);

		[[nodiscard]] const eastl::string& GetError() const
		{
			return mError;
		}

	private:
		bool CreateSettingsUploadBuffer();
		bool CreateCameraResources();

		struct FFrameGraph;
		bool CreateFrameGraph(const FArdaRHITextureRef& Color, uint32_t Width, uint32_t Height);

		arda::FArdaRHIDeviceRef mDevice;
		arda::FArdaRHIBufferRef mSettingsUploadBuffer;
		arda::FArdaRHIBufferRef mCameraBuffer;

		float mCameraPosition[3] = {-0.96875f, -0.96875f, 0.8125f};
		float mCameraYaw = 0.78539816f;
		float mCameraPitch = -0.67453292f;
		float mElapsedSeconds = 0.0f;
		bool mbTerrainReadbackValidated = false;

		std::vector<std::unique_ptr<FFrameGraph>> mFrameGraphs;
		eastl::string mError;
	};
}
