#include "ArdaNvrhiPch.h"

#include "ArdaNvrhiBackend.h"

#include "ArdaNvrhiBackendDevice.h"
#include "ArdaBackendProvider.h"

namespace arda::backend
{
    namespace
    {
        class FArdaNvrhiVulkanBackendModule final : public IArdaBackendModule
        {
        public:
            FArdaNvrhiVulkanBackendModule()
            {
                mDescriptor.mName = "nvrhi-vulkan";
                mDescriptor.mDisplayName = "NVRHI Vulkan";
                mDescriptor.mBackendType = EArdaBackendType::Vulkan;
                mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::Spirv;
                mDescriptor.mShaderArtifactExtension = ".spv";
                mDescriptor.mbSupportsOwnedDevice = true;
                mDescriptor.mbSupportsExternalDevice = true;
                mDescriptor.mPriority = 100;
            }

            const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept override
            {
                return mDescriptor;
            }

            eastl::unique_ptr<IArdaBackendDevice> CreateDevice(
                EArdaDeviceSource Source) override
            {
                return Source == EArdaDeviceSource::ExternalProvider
                    ? CreateExternalBackendDevice()
                    : CreateVulkanBackendDevice();
            }

            rhi::FArdaRHIStatus ConfigureShaderCompileInvocation(
                FArdaBackendShaderCompileInvocation& Invocation) const override
            {
                Invocation.mArguments.push_back("-spirv");
                Invocation.mArguments.push_back("-fspv-target-env=vulkan1.3");
                using Stage = rhi::EArdaRHIShaderStage;
                if (Invocation.mStage == Stage::RayGeneration ||
                    Invocation.mStage == Stage::AnyHit ||
                    Invocation.mStage == Stage::ClosestHit ||
                    Invocation.mStage == Stage::Miss ||
                    Invocation.mStage == Stage::Intersection ||
                    Invocation.mStage == Stage::Callable)
                {
                    Invocation.mArguments.push_back(
                        "-fspv-extension=SPV_KHR_ray_tracing");
                }
                const auto AddShift = [&Invocation](const char* Flag, const char* Value)
                {
                    Invocation.mArguments.push_back(Flag);
                    Invocation.mArguments.push_back(Value);
                    Invocation.mArguments.push_back("0");
                };
                AddShift("-fvk-t-shift", "0");
                AddShift("-fvk-s-shift", "128");
                AddShift("-fvk-b-shift", "256");
                AddShift("-fvk-u-shift", "384");
                return rhi::FArdaRHIStatus::Success();
            }

        private:
            FArdaBackendModuleDescriptor mDescriptor;
        };

        FArdaNvrhiVulkanBackendModule& GetModule()
        {
            static FArdaNvrhiVulkanBackendModule Module;
            return Module;
        }
    }

    bool RegisterArdaNvrhiVulkanBackend()
    {
        return RegisterBackendModule(GetModule());
    }
}
