#pragma once
#include "Nodes/ArdaPixelSortNodes.h"
#include <memory>
#include "ArdaSwapChain.h"
#include <filesystem>
#include <vector>

namespace arda
{
	/** Builds persistent graphs from the PixelSort node library; owns presentation and CPU diagnostics. */
	class FPixelSortRenderer
	{
	public:
		explicit FPixelSortRenderer(FArdaRHIDeviceRef Device);
		void ReleaseFrameGraphs();
		~FPixelSortRenderer();

		// Records and submits one frame. Verify/Capture opt into CPU readback;
		// ordinary animation keeps pixels on the GPU. Size follows the swap chain.
		void Render(IArdaSwapChain&,
		    float Time,
		    uint32_t Channel,
		    uint32_t Threshold,
		    bool Original,
		    bool Verify,
		    const std::filesystem::path& Capture);

	private:
		struct FArdaFrameGraph;
		FArdaFrameGraph& FindOrCreateFrameGraph(const FArdaRHITextureRef& Color,
		    uint32_t Width,
		    uint32_t Height,
		    bool bVerify,
		    bool bCapture);
		FArdaRHIDeviceRef mDevice;
		std::vector<std::unique_ptr<FArdaFrameGraph>> mFrames;
	};
}
