#pragma once
#include "Compute/ArdaComputeOperand.h"
#include "Compute/ArdaCudaModule.h"

namespace arda
{
    const char* GetArdaTestAddPtx();
    ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FArdaAddParameters)
        ARDA_COMPUTE_BUFFER(mInput, EArdaComputeAccess::Read)
        ARDA_COMPUTE_BUFFER(mOutput, EArdaComputeAccess::ReadWrite)
        ARDA_COMPUTE_PARAMETER(uint32_t, mCount)
        ARDA_COMPUTE_PARAMETER(eastl::vector<uint64_t>, mInputShape)
        ARDA_COMPUTE_PARAMETER(eastl::vector<uint64_t>, mOutputShape)
    ARDA_END_COMPUTE_PARAMETER_STRUCT()

    // Application-specific hook, not part of the operand base or a variant registry.
    using FArdaAddShaderDispatch = eastl::function<FArdaRHIStatus(IArdaRHICommandList&, const FArdaAddParameters&)>;

    // A compiled example: all host policy lives in its dispatch implementation.
    // The sample is caller-serialized because it retains diagnostic/tuning state.
    class FArdaAddOperand final : public TArdaComputeOperand<FArdaAddParameters>
    {
    public:
        explicit FArdaAddOperand(FArdaRHIDeviceRef Device, FArdaAddShaderDispatch Graphics = {},
            eastl::vector<eastl::shared_ptr<const FArdaCudaModule>> Modules = {});
        const char* GetName() const noexcept override { return "sample.add_twice"; }
        FArdaRHIStatus GetOperandSupport() const override;
        // This sample submits to its owned graphics queue and returns before GPU completion.
        // mLastSubmission identifies that queue submission; the caller can wait/read back later.
        FArdaRHIStatus Dispatch(const FArdaAddParameters& Parameters) override;
        FArdaRHIStatus DispatchDeferred(IArdaRHICommandList& Commands, const FArdaAddParameters& Parameters) override;
        bool mbPreferGraphics = false;
        uint32_t mLastBlockSize = 0;
        uint64_t mLastSubmission = 0;
        eastl::string mLastImplementation;
    private:
        FArdaRHIDeviceRef mDevice;
        FArdaAddShaderDispatch mGraphics;
        eastl::vector<eastl::shared_ptr<const FArdaCudaModule>> mModules;
    };
}
