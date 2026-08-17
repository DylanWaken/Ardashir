#include "ArdaBackendPch.h"

#include "ArdaNvrhiBackend.h"

#include "ArdaBackendDevice.h"
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
                FArdaBackendShaderCompileInvocation&) const override
            {
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
