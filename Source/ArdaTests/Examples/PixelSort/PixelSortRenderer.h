#pragma once
#include "PixelSortOperand.h"
#include "ArdaSwapChain.h"
#include <filesystem>
#include <vector>

namespace arda
{
	// Backend-only frame owner. Read Initialize -> Resize -> Render in the .cpp
	// to follow pipeline setup, shared allocation, and the graphics/CUDA handoff.
	// Call on the application's render thread, while the device/swap chain live.
	class FPixelSortRenderer
	{
	public:
		explicit FPixelSortRenderer(FArdaRHIDeviceRef Device)
		    : mDevice(Device),
		      mOperand(Device)
		{
		}

		~FPixelSortRenderer();

		// Format comes from the actual swap chain. Directory is the executable's
		// directory, where CMake deploys HLSL and DXC; it is not the process cwd.
		void Initialize(EArdaRHIFormat Format, const std::filesystem::path& Directory);

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
		void Resize(uint32_t Width, uint32_t Height);
		std::vector<uint32_t> ReadPixels(const FArdaRHIStagingTextureRef&, uint32_t Width, uint32_t Height);

		FArdaRHIDeviceRef mDevice;

		// Keep the operand alive across frames so its immutable registry is reused.
		FPixelSortOperand mOperand;
		FArdaRHIComputePipelineRef mNoisePipeline;
		FArdaRHIGraphicsPipelineRef mPresentPipeline;
		FArdaRHIBindingLayoutRef mNoiseLayout, mPresentLayout;

		// Layouts/pipelines survive resize; binding sets reference the current
		// size-dependent textures and must be rebuilt when those allocations change.
		FArdaRHIBindingSetRef mNoiseBindings, mPresentBindings;
		FArdaRHIBufferRef mConstants;
		FArdaRHITextureRef mNoise, mSorted;
		uint32_t mWidth = 0, mHeight = 0;
	};
}
