#include "ArdaDependencyRequirements.h"
#include <EASTL/algorithm.h>

namespace arda
{
	FArdaRHIStatus FArdaDependencyNodeRequirements::Check(const IArdaRHIDevice* Device) const
	{
		for (const auto Mode : mAllowedCudaLaunchModes)
		{
			if (Mode <= EArdaCudaLaunchMode::None || Mode > EArdaCudaLaunchMode::ContextSwitch)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "A CUDA launch-mode requirement must name a qualified execution mode.");
			}
		}
		for (const auto& Requirement : mEnvironment)
		{
			if (Requirement.mName.empty() || !Requirement.mCheck)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Environment requirements need a nonempty diagnostic name and a predicate.");
			}
		}
		FArdaRHICapabilities Unavailable;
		Unavailable.mQueues.mbGraphics = false;
		auto Report = (Device ? Device->GetCapabilities() : Unavailable).Evaluate(mFeatures);
		const bool NeedsCuda = mbRequireCuda || mbRequireCudaSurfaces || mbRequireCudaLayeredSurfaces ||
		    mMinCudaComputeCapability || mMinCudaThreadsPerBlock || mMinCudaSharedMemoryBytes ||
		    !mAllowedCudaLaunchModes.empty();
		if (NeedsCuda)
		{
			const auto Cuda = Device ? Device->GetCudaCapabilities() : FArdaCudaCapabilities{};
			const auto Need = [&](bool Required, bool Available, const char* Name)
			{
				if (Required && !Available)
				{
					Report.mMissingAbilities.push_back(Name);
				}
			};
			Need(true, bool(Cuda), "CUDA launch support");
			Need(mbRequireCudaSurfaces, bool(Cuda) && Cuda.mbSurfaceAccess, "CUDA surface access");
			Need(mbRequireCudaLayeredSurfaces,
			    bool(Cuda) && Cuda.mbSurfaceAccess && Cuda.mbLayeredSurfaceAccess,
			    "CUDA layered surface access");
			Need(mMinCudaComputeCapability != 0,
			    bool(Cuda) && Cuda.mComputeCapability >= mMinCudaComputeCapability,
			    "minimum CUDA compute capability");
			Need(mMinCudaThreadsPerBlock != 0,
			    bool(Cuda) && Cuda.mMaxThreadsPerBlock >= mMinCudaThreadsPerBlock,
			    "CUDA threads-per-block limit");
			Need(mMinCudaSharedMemoryBytes != 0,
			    bool(Cuda) && Cuda.mMaxSharedMemoryBytes >= mMinCudaSharedMemoryBytes,
			    "CUDA dynamic shared-memory limit");
			Need(!mAllowedCudaLaunchModes.empty(),
			    eastl::find(mAllowedCudaLaunchModes.begin(), mAllowedCudaLaunchModes.end(), Cuda.mLaunchMode) !=
			        mAllowedCudaLaunchModes.end(),
			    "required CUDA launch mode");
			if (!Cuda && !Cuda.mUnavailableReason.empty())
			{
				Report.mMissingAbilities.push_back(Cuda.mUnavailableReason);
			}
			if ((mbRequireCudaSurfaces || mbRequireCudaLayeredSurfaces) && !Cuda.mbSurfaceAccess &&
			    !Cuda.mSurfaceUnavailableReason.empty())
			{
				Report.mMissingAbilities.push_back(Cuda.mSurfaceUnavailableReason);
			}
		}
		for (const auto& Requirement : mEnvironment)
		{
			if (!Device)
			{
				Report.mMissingAbilities.push_back(Requirement.mName + " (no device)");
			}
			else if (const auto Status = Requirement.mCheck(*Device); !Status)
			{
				Report.mMissingAbilities.push_back(
				    Requirement.mName + (Status.mMessage.empty() ? eastl::string{} : ": " + Status.mMessage));
			}
		}
		return Report.ToStatus();
	}
}
