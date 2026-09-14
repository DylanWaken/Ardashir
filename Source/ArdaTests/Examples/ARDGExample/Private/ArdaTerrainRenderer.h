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
		/** Enable the one-time CPU geometry and gradient readback only for explicit verification runs. */
		bool Initialize(arda::FArdaRHIDeviceRef device,
		    arda::EArdaRHIFormat swapChainFormat,
		    bool bVerifyTerrain = false);
		void UpdateCamera(float forward, float right, float lookX, float lookY, float deltaSeconds);
		bool RenderFrame(arda::IArdaSwapChain& swapChain);

		[[nodiscard]] const eastl::string& GetError() const
		{
			return mError;
		}

	private:
		struct FArdaFrameGraph;
		bool CreateFrameGraph(const FArdaRHITextureRef& Color, uint32_t Width, uint32_t Height);

		arda::FArdaRHIDeviceRef mDevice;

		float mCameraPosition[3] = {-0.96875f, -0.96875f, 0.8125f};
		float mCameraYaw = 0.78539816f;
		float mCameraPitch = -0.67453292f;
		float mElapsedSeconds = 0.0f;
		bool mbVerifyTerrain = false;
		bool mbTerrainReadbackValidated = false;

		std::vector<std::unique_ptr<FArdaFrameGraph>> mFrameGraphs;
		eastl::string mError;
	};
}
