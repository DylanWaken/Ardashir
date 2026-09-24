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
	    EArdaRHIShaderStage RayStage) noexcept
	{
		auto& Features = Requirements.mFeatures;
		Requirements.mbRequireCuda |= Kind == EArdaDependencyNodeKind::Cuda;
		Features.mbRequireGraphicsQueue |=
		    Kind == EArdaDependencyNodeKind::Graphics || GraphicsStage != EArdaGraphicsStage::None;
		Features.mbRequireGeometryShaders |= GraphicsStage == EArdaGraphicsStage::Geometry;
		Features.mbRequireTessellationShaders |=
		    GraphicsStage == EArdaGraphicsStage::Hull || GraphicsStage == EArdaGraphicsStage::Domain;
		Features.mbRequireMeshShaders |=
		    GraphicsStage == EArdaGraphicsStage::Mesh || GraphicsStage == EArdaGraphicsStage::Amplification;
		if (GraphicsStage == EArdaGraphicsStage::Amplification)
		{
			Features.mMinMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
		}
		Features.mbRequireRayTracingPipelines |= RayStage != EArdaRHIShaderStage::None;
		Features.mbRequirePersistentShaderTables |= RayStage == EArdaRHIShaderStage::RayGeneration;
	}

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
