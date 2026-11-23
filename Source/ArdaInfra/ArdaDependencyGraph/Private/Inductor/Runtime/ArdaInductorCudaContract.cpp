#include "Inductor/Runtime/ArdaInductorCudaContract.h"

#include "NativeCompute/ArdaNativeCompute.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"
#include <EASTL/algorithm.h>

namespace arda
{
	namespace
	{
		bool Allows(EArdaDependencyAccess Access, bool bWrite)
		{
			return Access == EArdaDependencyAccess::ReadWrite ||
			    Access == (bWrite ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read);
		}

		bool AllowsState(EArdaRHIResourceState State,
		    EArdaRHIResourceState Required,
		    EArdaInductorNativeBindingDomain Domain,
		    bool bWrite)
		{
			if (Required != EArdaRHIResourceState::Unknown)
			{
				if (bWrite || IsWriteState(State) || IsWriteState(Required) || Required == EArdaRHIResourceState::Common)
				{
					return State == Required;
				}
				return (State & Required) == Required;
			}
			if (State == EArdaRHIResourceState::UnorderedAccess)
			{
				return true;
			}
			switch (Domain)
			{
			case EArdaInductorNativeBindingDomain::Any:
				return true;
			case EArdaInductorNativeBindingDomain::Cuda:
				return false;
			case EArdaInductorNativeBindingDomain::ComputeShader:
				return !bWrite &&
				    HasAnyFlags(State, EArdaRHIResourceState::NonPixelShaderResource | EArdaRHIResourceState::ConstantBuffer);
			case EArdaInductorNativeBindingDomain::GraphicsShader:
				return State == EArdaRHIResourceState::RenderTarget || State == EArdaRHIResourceState::DepthWrite ||
				    (!bWrite &&
				        HasAnyFlags(State,
				            EArdaRHIResourceState::ShaderResource | EArdaRHIResourceState::ConstantBuffer |
				                EArdaRHIResourceState::DepthRead));
			}
			return false;
		}

		template <class GetRange>
		bool Covers(const FArdaNativeResourceView& Binding,
		    const eastl::vector<FArdaInductorNativeResourceAccess>& Declared,
		    EArdaInductorNativeBindingDomain Domain,
		    EArdaRHIResourceState Required,
		    uint64_t Begin,
		    uint64_t End,
		    bool bWrite,
		    GetRange&& Range)
		{
			while (Begin < End)
			{
				uint64_t CoveredEnd = Begin;
				for (const auto& Access : Declared)
				{
					if (Access.mResource != Binding.mResource ||
					    !AllowsState(Access.mAccess.mState, Required, Domain, bWrite) ||
					    !Allows(Access.mAccess.mAccess, bWrite))
					{
						continue;
					}
					uint64_t RangeBegin = 0, RangeEnd = 0;
					if (Range(Access.mAccess, RangeBegin, RangeEnd) && RangeBegin <= Begin)
					{
						CoveredEnd = eastl::max(CoveredEnd, RangeEnd);
					}
				}
				if (CoveredEnd == Begin)
				{
					return false;
				}
				Begin = CoveredEnd;
			}
			return true;
		}

		FArdaRHIStatus ContractError(const FArdaNativeResourceView& Binding,
		    EArdaInductorNativeBindingDomain Domain,
		    const char* Reason)
		{
			eastl::string Message = Domain == EArdaInductorNativeBindingDomain::Cuda ? "CUDA" : "Native compute";
			Message += " operand resource '";
			const char* Name = Binding.mResource ? Binding.mResource->GetDebugName() : nullptr;
			Message += Name ? Name : "<unnamed>";
			Message += "' ";
			Message += Reason;
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Message.c_str());
		}
		FArdaRHIStatus ValidateBinding(const FArdaNativeResourceView& Binding,
		    EArdaComputeAccess Mode,
		    const eastl::vector<FArdaInductorNativeResourceAccess>& Declared,
		    EArdaInductorNativeBindingDomain Domain,
		    EArdaRHIResourceState Required = EArdaRHIResourceState::Unknown)
		{
			if (!Binding.mResource ||
			    (Mode != EArdaComputeAccess::Read && Mode != EArdaComputeAccess::Write && Mode != EArdaComputeAccess::ReadWrite))
			{
				return ContractError(Binding, Domain, "has an invalid resource or access requirement.");
			}
			for (const bool bWrite : {false, true})
			{
				if ((bWrite && Mode == EArdaComputeAccess::Read) ||
				    (!bWrite && Mode == EArdaComputeAccess::Write))
				{
					continue;
				}
				bool bCovered = false;
				if (const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(Binding.mResource.Get()))
				{
					const auto& Desc = Buffer->GetDesc();
					const auto& Requested = Binding.mBufferRange;
					if (Requested.mByteOffset >= Desc.mByteSize || !Requested.mByteSize ||
					    (Requested.mByteSize != ArdaRHIWholeBuffer &&
					        Requested.mByteSize > Desc.mByteSize - Requested.mByteOffset))
					{
						return ContractError(Binding, Domain, "has a byte range outside its allocation.");
					}
					const auto Range = Requested.Resolve(Desc);
					bCovered = Covers(Binding,
					    Declared,
					    Domain,
					    Required,
					    Range.mByteOffset,
					    Range.mByteOffset + Range.mByteSize,
					    bWrite,
					    [&](const FArdaDependencyAccess& Access, uint64_t& Begin, uint64_t& End)
					    {
						    const auto Allowed = Access.mBufferRange.Resolve(Desc);
						    Begin = Allowed.mByteOffset;
						    if (Begin > Desc.mByteSize || Allowed.mByteSize > Desc.mByteSize - Begin)
						    {
							    return false;
						    }
						    End = Begin + Allowed.mByteSize;
						    return true;
					    });
				}
				else if (const auto* Texture = dynamic_cast<const IArdaRHITexture*>(Binding.mResource.Get()))
				{
					const auto& Desc = Texture->GetDesc();
					if (Binding.mMipLevel >= Desc.mMipLevels)
					{
						return ContractError(Binding, Domain, "has a mip outside its allocation.");
					}
					// Native texture arguments expose all array layers of the selected mip.
					bCovered = Covers(Binding,
					    Declared,
					    Domain,
					    Required,
					    0,
					    Desc.mArraySize,
					    bWrite,
					    [&](const FArdaDependencyAccess& Access, uint64_t& Begin, uint64_t& End)
					    {
						    const auto Allowed = Access.mTextureRange.Resolve(Desc);
						    Begin = Allowed.mBaseArraySlice;
						    End = Begin + Allowed.mArraySliceCount;
						    return Allowed.mBaseMipLevel <= Binding.mMipLevel && Allowed.mMipLevelCount &&
						        Binding.mMipLevel - Allowed.mBaseMipLevel < Allowed.mMipLevelCount &&
						        Allowed.mBasePlane == 0 && Allowed.mPlaneCount && End <= Desc.mArraySize;
					    });
				}
				if (!bCovered)
				{
					return ContractError(Binding,
					    Domain,
					    bWrite ? "writes outside the current node's compatible declared accesses."
					           : "reads outside the current node's compatible declared accesses.");
				}
			}
			return {};
		}
	}

	FArdaRHIStatus ValidateArdaInductorCudaBindings(const eastl::vector<FArdaCudaBinding>& Bindings,
	    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared)
	{
		for (const auto& Binding : Bindings)
		{
			if (auto Status = ValidateBinding({Binding.mResource, Binding.mBufferRange, Binding.mMipLevel},
			        Binding.mAccess,
			        Declared,
			        EArdaInductorNativeBindingDomain::Cuda);
			    !Status)
			{
				return Status;
			}
		}
		return {};
	}

	EArdaInductorNativeBindingDomain GetArdaInductorNativeBindingDomain(EArdaNativeComputeRoute Route)
	{
		switch (Route)
		{
		case EArdaNativeComputeRoute::ComputeShader:
			return EArdaInductorNativeBindingDomain::ComputeShader;
		case EArdaNativeComputeRoute::GraphicsShader:
			return EArdaInductorNativeBindingDomain::GraphicsShader;
		case EArdaNativeComputeRoute::Native:
			return EArdaInductorNativeBindingDomain::Any;
		}
		return EArdaInductorNativeBindingDomain::Any;
	}

	bool AllowsArdaInductorNativeResourceState(EArdaNativeComputeRoute Route,
	    EArdaComputeAccess Access,
	    EArdaRHIResourceState Declared,
	    EArdaRHIResourceState Required)
	{
		const auto Domain = GetArdaInductorNativeBindingDomain(Route);
		return (Access == EArdaComputeAccess::Write || AllowsState(Declared, Required, Domain, false)) &&
		    (Access == EArdaComputeAccess::Read || AllowsState(Declared, Required, Domain, true));
	}

	FArdaRHIStatus ValidateArdaInductorNativeBindings(const FArdaNativeOperand& Operand,
	    const FArdaNativeInvocation& Invocation,
	    const eastl::vector<FArdaInductorNativeResourceAccess>& Declared,
	    EArdaInductorNativeBindingDomain Domain,
	    const eastl::vector<EArdaRHIResourceState>& RequiredStates)
	{
		if (Operand.mResources.size() != Invocation.mResources.size() ||
		    (!RequiredStates.empty() && RequiredStates.size() != Invocation.mResources.size()))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Native operand argument count does not match its graph resource contract.");
		}
		for (size_t Index = 0; Index < Invocation.mResources.size(); ++Index)
		{
			const auto& View = Invocation.mResources[Index];
			const auto& Slot = Operand.mResources[Index];
			const auto Required = RequiredStates.empty() ? EArdaRHIResourceState::Unknown : RequiredStates[Index];
			if (auto Status = ValidateBinding(View, Slot.mAccess, Declared, Domain, Required); !Status)
			{
				return Status;
			}
		}
		return {};
	}
}
