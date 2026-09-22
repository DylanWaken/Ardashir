#include "Graph/Node/ArdaNodeParameters.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"

namespace arda
{
	namespace
	{
		FArdaRHIStatus MappingError(const FArdaNodeParameterMember& Member, const char* Reason)
		{
			const eastl::string Message =
			    eastl::string("Node parameter '") + (Member.mName ? Member.mName : "?") + "': " + Reason;
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message.c_str());
		}
	}

	FArdaRHIStatus ValidateArdaNodeShaderParameter(const FArdaNodeParameterMember& Member)
	{
		if (!Member.mShaderMetadata || !Member.mShaderMetadata->GetStatus() || !Member.mShaderMember ||
		    !Member.mPipelineSlot || !*Member.mPipelineSlot)
		{
			return MappingError(Member, "shader mapping requires valid metadata, a member and a pipeline slot.");
		}
		const auto* Shader = Member.mShaderMetadata->FindFlattenedMember(Member.mShaderMember);
		if (!Shader)
		{
			return MappingError(Member, "shader member does not exist in the referenced parameter schema.");
		}
		EArdaGraphResourceKind Expected;
		bool UAV = false;
		switch (Shader->mBindingType)
		{
		case EArdaRHIBindingType::TextureUAV:
			UAV = true;
			[[fallthrough]];
		case EArdaRHIBindingType::TextureSRV:
			Expected = EArdaGraphResourceKind::Texture;
			break;
		case EArdaRHIBindingType::StructuredBufferUAV:
		case EArdaRHIBindingType::RawBufferUAV:
		case EArdaRHIBindingType::TypedBufferUAV:
			UAV = true;
			[[fallthrough]];
		case EArdaRHIBindingType::StructuredBufferSRV:
		case EArdaRHIBindingType::RawBufferSRV:
		case EArdaRHIBindingType::TypedBufferSRV:
		case EArdaRHIBindingType::ConstantBuffer:
		case EArdaRHIBindingType::VolatileConstantBuffer:
			Expected = EArdaGraphResourceKind::Buffer;
			break;
		case EArdaRHIBindingType::RayTracingAccelStruct:
			Expected = EArdaGraphResourceKind::AccelerationStructure;
			break;
		default:
			return MappingError(Member, "shader member is not a GPU resource binding.");
		}
		if (Shader->mKind == EArdaShaderParameterKind::Value ||
		    Shader->mKind == EArdaShaderParameterKind::NestedStruct ||
		    Shader->mKind == EArdaShaderParameterKind::Sampler ||
		    Shader->mKind == EArdaShaderParameterKind::PushConstants || Expected != Member.mResourceKind)
		{
			return MappingError(Member, "graph resource category does not match the shader member.");
		}
		if (!Member.mElementCount || Member.mArrayElement >= Shader->mArrayCount ||
		    Member.mElementCount > Shader->mArrayCount - Member.mArrayElement)
		{
			return MappingError(Member, "graph resource mapping exceeds the shader member's array bounds.");
		}
		if (Member.mAccess != EArdaDependencyAccess::AutoDeduction)
		{
			if (Member.mAccess > EArdaDependencyAccess::ReadWrite ||
			    (!UAV && Member.mAccess != EArdaDependencyAccess::Read))
			{
				return MappingError(Member, "shader access override is incompatible with its binding type.");
			}
			const auto Required = Shader->GetResourceAccess();
			const auto Access = Member.mAccess == EArdaDependencyAccess::Read ? EArdaShaderResourceAccess::Read
			    : Member.mAccess == EArdaDependencyAccess::Write              ? EArdaShaderResourceAccess::Write
			                                                                  : EArdaShaderResourceAccess::ReadWrite;
			if (Access != EArdaShaderResourceAccess::ReadWrite && Access != Required)
			{
				return MappingError(Member, "node access omits a read or write required by the shader schema.");
			}
		}
		return {};
	}

	FArdaRHIStatus ValidateArdaNodeCudaParameter(const FArdaNodeParameterMember& Member)
	{
		if (!Member.mComputeMetadata || !Member.mComputeMetadata->GetStatus() || !Member.mComputeMember)
		{
			return MappingError(Member, "CUDA mapping requires valid operand metadata and a member.");
		}
		const auto* Compute = Member.mComputeMetadata->FindMember(Member.mComputeMember);
		if (!Compute)
		{
			return MappingError(Member, "CUDA resource member does not exist in the operand schema.");
		}
		const auto Expected = Member.mResourceKind == EArdaGraphResourceKind::Texture
		    ? EArdaComputeParameterKind::Texture
		    : EArdaComputeParameterKind::Buffer;
		if (Member.mResourceKind == EArdaGraphResourceKind::AccelerationStructure || Compute->mKind != Expected)
		{
			return MappingError(Member, "graph resource category does not match the CUDA operand member.");
		}
		if (Member.mElementCount != 1 || Compute->mElementCount != 1 || Member.mArrayElement != 0)
		{
			return MappingError(Member, "scalar CUDA mappings require a scalar operand resource member.");
		}
		if (Compute->mAccess > EArdaComputeAccess::ReadWrite)
		{
			return MappingError(Member, "CUDA operand member has an invalid access direction.");
		}
		return {};
	}

	EArdaDependencyAccess GetArdaNodeShaderParameterAccess(const FArdaNodeParameterMember& Member)
	{
		if (Member.mAccess != EArdaDependencyAccess::AutoDeduction)
		{
			return Member.mAccess;
		}
		const auto Access = Member.mShaderMetadata->FindFlattenedMember(Member.mShaderMember)->GetResourceAccess();
		return Access == EArdaShaderResourceAccess::Read ? EArdaDependencyAccess::Read
		    : Access == EArdaShaderResourceAccess::Write ? EArdaDependencyAccess::Write
		                                                 : EArdaDependencyAccess::ReadWrite;
	}

	EArdaDependencyAccess GetArdaNodeCudaParameterAccess(const FArdaNodeParameterMember& Member)
	{
		const auto Access = Member.mComputeMetadata->FindMember(Member.mComputeMember)->mAccess;
		return Access == EArdaComputeAccess::Read ? EArdaDependencyAccess::Read
		    : Access == EArdaComputeAccess::Write ? EArdaDependencyAccess::Write
		                                          : EArdaDependencyAccess::ReadWrite;
	}

	FArdaRHIStatus ResolveArdaNodeCudaResource(FArdaDependencyExecutionContext& Context,
	    const FArdaNodeParameterMember& Member,
	    FArdaDependencyResourceHandle Resource,
	    void* HostParameters)
	{
		const auto* Compute = Member.mComputeMetadata->FindMember(Member.mComputeMember);
		auto* Destination = static_cast<uint8_t*>(HostParameters) + Compute->mOffset;
		if (Compute->mKind == EArdaComputeParameterKind::Buffer)
		{
			auto Buffer = Context.GetBuffer(Resource);
			if (!Buffer)
			{
				return MappingError(Member, "CUDA buffer could not be resolved from the declared graph resources.");
			}
			*reinterpret_cast<FArdaComputeBufferParameter*>(
			    Destination) = {eastl::move(Buffer), Member.mView.mBufferRange};
		}
		else
		{
			auto Texture = Context.GetTexture(Resource);
			if (!Texture)
			{
				return MappingError(Member, "CUDA texture could not be resolved from the declared graph resources.");
			}
			*reinterpret_cast<FArdaComputeTextureParameter*>(
			    Destination) = {eastl::move(Texture), Member.mView.mTextureRange};
		}
		return {};
	}
}
