#include "ArdaNvrhiPch.h"

#include "ArdaNvrhiBackend.h"

#include "ArdaNvrhiBackendDevice.h"
#include "ArdaBackendProvider.h"

namespace arda::backend
{
    namespace
    {
        class FArdaNvrhiD3D12BackendModule final : public IArdaBackendModule
        {
        public:
            FArdaNvrhiD3D12BackendModule()
            {
                mDescriptor.mName = "nvrhi-d3d12";
                mDescriptor.mDisplayName = "NVRHI D3D12";
                mDescriptor.mBackendType = EArdaBackendType::D3D12;
                mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::Dxil;
                mDescriptor.mShaderArtifactExtension = ".dxil";
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
                    : CreateD3D12BackendDevice();
            }

            rhi::FArdaRHIStatus ConfigureShaderCompileInvocation(
                FArdaBackendShaderCompileInvocation&) const override
            {
                return rhi::FArdaRHIStatus::Success();
            }

        private:
            FArdaBackendModuleDescriptor mDescriptor;
        };

        FArdaNvrhiD3D12BackendModule& GetModule()
        {
            static FArdaNvrhiD3D12BackendModule Module;
            return Module;
        }
    }

    bool RegisterArdaNvrhiD3D12Backend()
    {
        return RegisterBackendModule(GetModule());
    }
}
