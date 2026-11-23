#include "../Cuda/ArdaNativeCudaProviderPrivate.h"
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus CheckCudaInGraphicsMode(const char* Backend, EArdaCudaLaunchMode Mode)
		{
			const bool bD3D12 =
			    Mode == EArdaCudaLaunchMode::D3D12CiG && Backend && std::strcmp(Backend, "native-d3d12") == 0;
			const bool bVulkan =
			    Mode == EArdaCudaLaunchMode::VulkanCiG && Backend && std::strcmp(Backend, "native-vulkan") == 0;
			if (!bD3D12 && !bVulkan)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "CUDA in Graphics requires a qualified CiG context for this graphics API.");
			}
			return {};
		}
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaInGraphicsProvider()
	{
		return MakeArdaNativeCudaProvider(ArdaNativeCudaInGraphicsProviderId, {"native-d3d12", "native-vulkan"},
		    CheckCudaInGraphicsMode);
	}

	FArdaRHIStatus RegisterArdaCiGNativeComputeProvider(FArdaNativeComputeRegistry& Registry)
	{
		static const auto Provider = CreateArdaNativeCudaInGraphicsProvider();
		return Registry.RegisterProvider(Provider);
	}
}
