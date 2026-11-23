#include "Graph/Node/ArdaDependencyRequirements.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/algorithm.h>
#include <exception>

namespace arda
{
	void AddArdaNodeIntrinsicRequirements(FArdaDependencyNodeRequirements& Requirements,
	    EArdaDependencyNodeKind Kind,
	    EArdaGraphicsStage GraphicsStage,
	    EArdaRHIShaderStage RayStage)
	{
		auto& Features = Requirements.mFeatures;
		Requirements.mbRequireCuda |= Kind == EArdaDependencyNodeKind::Cuda;
		if (Kind == EArdaDependencyNodeKind::Graphics || GraphicsStage != EArdaGraphicsStage::None)
		{
			Features.Require("queue.graphics");
		}
		if (GraphicsStage == EArdaGraphicsStage::Geometry)
		{
			Features.Require("shader.geometry");
		}
		if (GraphicsStage == EArdaGraphicsStage::Hull || GraphicsStage == EArdaGraphicsStage::Domain)
		{
			Features.Require("shader.tessellation");
		}
		if (GraphicsStage == EArdaGraphicsStage::Mesh || GraphicsStage == EArdaGraphicsStage::Amplification)
		{
			Features.Require("shader.mesh");
		}
		if (GraphicsStage == EArdaGraphicsStage::Amplification)
		{
			Features.Require("shader.mesh-tier", 2);
		}
		if (RayStage != EArdaRHIShaderStage::None)
		{
			Features.Require("ray-tracing.pipeline");
		}
		if (RayStage == EArdaRHIShaderStage::RayGeneration)
		{
			Features.Require("ray-tracing.persistent-shader-tables");
		}
	}

	FArdaRHIStatus FArdaDependencyNodeRequirements::Validate() const
	{
		for (const auto& Requirement : mFeatures.GetEntries())
		{
			if (!Requirement.IsValid())
			{
				const auto Message = eastl::string("Invalid graph feature requirement: ") + Requirement.mName;
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message.c_str());
			}
		}
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
		return {};
	}

	FArdaRHIStatus FArdaDependencyNodeRequirements::Check(const IArdaRHIDevice* Device) const
	{
		if (auto Status = Validate(); !Status)
		{
			return Status;
		}
		auto Report = Device ? Device->CheckFeatureSupport(mFeatures) : FArdaBackendFeatureList{}.Evaluate(mFeatures);
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
			else
			{
				FArdaRHIStatus Status;
				try
				{
					Status = Requirement.mCheck(*Device);
				}
				catch (const std::exception& Error)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    (Requirement.mName + ": " + Error.what()).c_str());
				}
				catch (...)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    (Requirement.mName + ": environment requirement threw an exception.").c_str());
				}
				if (!Status)
				{
					Report.mMissingAbilities.push_back(
					    Requirement.mName + (Status.mMessage.empty() ? eastl::string{} : ": " + Status.mMessage));
				}
			}
		}
		return Report.ToStatus();
	}
}
