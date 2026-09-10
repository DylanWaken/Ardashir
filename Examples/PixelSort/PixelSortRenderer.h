#pragma once
#include "PixelSortOperand.h"
#include "ArdaSwapChain.h"
#include <filesystem>
#include <vector>

namespace arda
{
    class FPixelSortRenderer
    {
    public:
        explicit FPixelSortRenderer(FArdaRHIDeviceRef Device) : mDevice(Device), mOperand(Device) {}
        ~FPixelSortRenderer();
        void Initialize(EArdaRHIFormat Format, const std::filesystem::path& Directory);
        void Render(IArdaSwapChain&, float Time, uint32_t Channel, uint32_t Threshold,
            bool Original, bool Verify, const std::filesystem::path& Capture);
    private:
        void Resize(uint32_t Width, uint32_t Height);
        std::vector<uint32_t> ReadPixels(const FArdaRHIStagingTextureRef&, uint32_t Width, uint32_t Height);
        FArdaRHIDeviceRef mDevice;
        FPixelSortOperand mOperand;
        FArdaRHIComputePipelineRef mNoisePipeline;
        FArdaRHIGraphicsPipelineRef mPresentPipeline;
        FArdaRHIBindingLayoutRef mNoiseLayout, mPresentLayout;
        FArdaRHIBindingSetRef mNoiseBindings, mPresentBindings;
        FArdaRHIBufferRef mConstants;
        FArdaRHITextureRef mNoise, mSorted;
        uint32_t mWidth = 0, mHeight = 0;
    };
}
