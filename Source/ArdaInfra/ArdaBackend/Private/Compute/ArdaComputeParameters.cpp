#include "Compute/ArdaComputeParameters.h"
#include <cstring>
#include <cstdio>

namespace arda
{
	FArdaComputeParameterMetadata::FArdaComputeParameterMetadata(const char* Name,
	    size_t Size,
	    size_t Alignment,
	    eastl::vector<FArdaComputeParameterMember> Members)
	    : mName(Name),
	      mSize(Size),
	      mAlignment(Alignment),
	      mMembers(eastl::move(Members))
	{
		const auto Invalid = [this](const char* Message)
		{
			mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		};
		if (!Name || !*Name || !Size || !Alignment || (Alignment & (Alignment - 1)) || Size % Alignment)
		{
			Invalid("Invalid compute parameter struct layout.");
			return;
		}
		for (size_t I = 0; I < mMembers.size(); ++I)
		{
			const auto& M = mMembers[I];
			if (!M.mName || !*M.mName || !M.mCppType || !*M.mCppType ||
			    M.mKind > EArdaComputeParameterKind::NestedStruct || M.mAccess > EArdaComputeAccess::ReadWrite ||
			    !M.mAlignment || (M.mAlignment & (M.mAlignment - 1)) || M.mAlignment > Alignment ||
			    M.mOffset % M.mAlignment || !M.mElementCount || !M.mElementStride || M.mElementStride % M.mAlignment ||
			    M.mElementCount > SIZE_MAX / M.mElementStride || M.mSize != M.mElementCount * M.mElementStride ||
			    M.mOffset > Size || M.mSize > Size - M.mOffset)
			{
				Invalid("Invalid compute parameter member layout.");
				return;
			}
			for (size_t J = 0; J < I; ++J)
			{
				const auto& Previous = mMembers[J];
				if (!std::strcmp(Previous.mName, M.mName) ||
				    (M.mOffset < Previous.mOffset + Previous.mSize && Previous.mOffset < M.mOffset + M.mSize))
				{
					Invalid("Compute parameter members overlap or duplicate a name.");
					return;
				}
			}
			size_t ExpectedSize = M.mElementStride, ExpectedAlignment = M.mAlignment;
			if (M.mKind == EArdaComputeParameterKind::NestedStruct)
			{
				if (!M.mNestedMetadata || !M.mNestedMetadata->GetStatus())
				{
					Invalid("Invalid nested compute parameter metadata.");
					return;
				}
				ExpectedSize = M.mNestedMetadata->GetSize();
				ExpectedAlignment = M.mNestedMetadata->GetAlignment();
			}
			else
			{
				if (M.mNestedMetadata)
				{
					Invalid("Leaf compute parameters cannot carry nested metadata.");
					return;
				}
				if (M.mKind == EArdaComputeParameterKind::Buffer)
				{
					ExpectedSize = sizeof(FArdaComputeBufferParameter);
					ExpectedAlignment = alignof(FArdaComputeBufferParameter);
				}
				if (M.mKind == EArdaComputeParameterKind::Texture)
				{
					ExpectedSize = sizeof(FArdaComputeTextureParameter);
					ExpectedAlignment = alignof(FArdaComputeTextureParameter);
				}
			}
			if (M.mElementStride != ExpectedSize || M.mAlignment != ExpectedAlignment)
			{
				Invalid("Compute parameter semantic kind does not match its storage layout.");
				return;
			}
		}
	}

	const FArdaComputeParameterMember* FArdaComputeParameterMetadata::FindMember(const char* Name) const noexcept
	{
		if (Name)
		{
			for (const auto& Member : mMembers)
			{
				if (Member.mName && !std::strcmp(Member.mName, Name))
				{
					return &Member;
				}
			}
		}
		return nullptr;
	}

	void FArdaComputeParameterMetadata::EnumerateInternal(const uint8_t* Root,
	    size_t Offset,
	    const eastl::string& Prefix,
	    const FArdaComputeParameterVisitor& Visitor) const
	{
		for (const auto& Member : mMembers)
		{
			for (size_t Index = 0; Index < Member.mElementCount; ++Index)
			{
				eastl::string Path = Prefix.empty() ? Member.mName : Prefix + "." + Member.mName;
				if (Member.mElementCount > 1)
				{
					char Suffix[32];
					std::snprintf(Suffix, sizeof(Suffix), "[%zu]", Index);
					Path += Suffix;
				}
				const size_t Absolute = Offset + Member.mOffset + Index * Member.mElementStride;
				if (Member.mKind == EArdaComputeParameterKind::NestedStruct)
				{
					Member.mNestedMetadata->EnumerateInternal(Root, Absolute, Path, Visitor);
				}
				else
				{
					Visitor({&Member, Root + Absolute, eastl::move(Path), Absolute});
				}
			}
		}
	}

	FArdaRHIStatus FArdaComputeParameterMetadata::Enumerate(const void* Parameters,
	    const FArdaComputeParameterVisitor& Visitor) const
	{
		if (!mStatus)
		{
			return mStatus;
		}
		if (!Parameters || reinterpret_cast<uintptr_t>(Parameters) % mAlignment || !Visitor)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Compute parameter instance or visitor is invalid.");
		}
		EnumerateInternal(static_cast<const uint8_t*>(Parameters), 0, {}, Visitor);
		return {};
	}

	FArdaRHIStatus FArdaComputeParameterMetadata::GetResourceAccesses(const void* Parameters,
	    eastl::vector<FArdaComputeResourceAccess>& OutAccesses) const
	{
		eastl::vector<FArdaComputeResourceAccess> Accesses;
		auto Status = Enumerate(Parameters,
		    [&](const FArdaComputeParameter& Parameter)
		    {
			    const auto Kind = Parameter.mMember->mKind;
			    if (Kind != EArdaComputeParameterKind::Buffer && Kind != EArdaComputeParameterKind::Texture)
			    {
				    return;
			    }
			    FArdaComputeResourceAccess Access;
			    Access.mPath = Parameter.mPath;
			    Access.mKind = Kind;
			    Access.mAccess = Parameter.mMember->mAccess;
			    if (Kind == EArdaComputeParameterKind::Buffer)
			    {
				    const auto& Value = *static_cast<const FArdaComputeBufferParameter*>(Parameter.mValue);
				    Access.mResource = Value.mBuffer;
				    Access.mBufferRange = Value.mRange;
			    }
			    else
			    {
				    const auto& Value = *static_cast<const FArdaComputeTextureParameter*>(Parameter.mValue);
				    Access.mResource = Value.mTexture;
				    Access.mTextureRange = Value.mRange;
			    }
			    Accesses.push_back(eastl::move(Access));
		    });
		if (Status)
		{
			OutAccesses = eastl::move(Accesses);
		}
		return Status;
	}
}
