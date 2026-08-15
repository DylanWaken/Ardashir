#include "ShaderStructs/ArdaShaderParameters.h"

#include <EASTL/algorithm.h>
#include <EASTL/utility.h>

namespace arda::backend
{
    namespace
    {
        constexpr uint64_t FnvOffset = 1469598103934665603ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;

        void HashBytes(uint64_t& Hash, const void* Data, size_t Size)
        {
            const auto* Bytes = static_cast<const uint8_t*>(Data);
            for (size_t Index = 0; Index < Size; ++Index)
            {
                Hash ^= Bytes[Index];
                Hash *= FnvPrime;
            }
        }

        void HashString(uint64_t& Hash, const char* Text)
        {
            if (Text == nullptr)
                return;
            while (*Text != '\0')
            {
                HashBytes(Hash, Text, 1);
                ++Text;
            }
        }

        enum class ERegisterClass : uint8_t
        {
            None,
            ShaderResource,
            UnorderedAccess,
            Constant,
            Sampler,
            PushConstants
        };

        ERegisterClass GetRegisterClass(rhi::EArdaRHIBindingType Type)
        {
            using TypeEnum = rhi::EArdaRHIBindingType;
            switch (Type)
            {
            case TypeEnum::TextureSRV:
            case TypeEnum::TypedBufferSRV:
            case TypeEnum::StructuredBufferSRV:
            case TypeEnum::RawBufferSRV:
            case TypeEnum::RayTracingAccelStruct:
                return ERegisterClass::ShaderResource;
            case TypeEnum::TextureUAV:
            case TypeEnum::TypedBufferUAV:
            case TypeEnum::StructuredBufferUAV:
            case TypeEnum::RawBufferUAV:
            case TypeEnum::SamplerFeedbackTextureUAV:
                return ERegisterClass::UnorderedAccess;
            case TypeEnum::ConstantBuffer:
            case TypeEnum::VolatileConstantBuffer:
                return ERegisterClass::Constant;
            case TypeEnum::PushConstants:
                return ERegisterClass::PushConstants;
            case TypeEnum::Sampler:
                return ERegisterClass::Sampler;
            }
            return ERegisterClass::None;
        }

        bool IsBindingMember(EArdaShaderParameterKind Kind)
        {
            return Kind != EArdaShaderParameterKind::Value &&
                Kind != EArdaShaderParameterKind::NestedStruct;
        }

        void FlattenMembers(
            const FArdaShaderParameterMetadata& Metadata,
            size_t BaseOffset,
            const eastl::string& Prefix,
            eastl::vector<FArdaFlattenedShaderParameterMember>& Out)
        {
            for (const FArdaShaderParameterMember& Member : Metadata.GetMembers())
            {
                const eastl::string Path =
                    Prefix.empty() ? Member.mName : Prefix + "." + Member.mName;
                if (Member.mKind == EArdaShaderParameterKind::NestedStruct)
                {
                    FlattenMembers(
                        *Member.mNestedMetadata,
                        BaseOffset + Member.mOffset,
                        Path,
                        Out);
                }
                else
                {
                    Out.push_back({
                        &Member,
                        BaseOffset + Member.mOffset,
                        Path });
                }
            }
        }

        FArdaShaderStructStatus MakeError(
            EArdaShaderStructError Code,
            const eastl::string& Message)
        {
            return { Code, Message };
        }
    }

    FArdaShaderParameterMetadata::FArdaShaderParameterMetadata(
        const char* Name,
        size_t Size,
        size_t Alignment,
        eastl::vector<FArdaShaderParameterMember> Members)
        : mName(Name)
        , mSize(Size)
        , mAlignment(Alignment)
        , mMembers(eastl::move(Members))
    {
        ValidateAndHash();
    }

    const FArdaShaderParameterMember* FArdaShaderParameterMetadata::FindMember(
        const eastl::string& Name) const noexcept
    {
        for (const FArdaShaderParameterMember& Member : mMembers)
        {
            if (Name == Member.mName)
                return &Member;
        }
        return nullptr;
    }

    const FArdaShaderParameterMember*
    FArdaShaderParameterMetadata::FindFlattenedMember(
        const eastl::string& Path,
        size_t* AbsoluteOffset) const noexcept
    {
        eastl::vector<FArdaFlattenedShaderParameterMember> Flattened;
        GetFlattenedMembers(Flattened);
        for (const FArdaFlattenedShaderParameterMember& Member : Flattened)
        {
            if (Member.mPath == Path)
            {
                if (AbsoluteOffset != nullptr)
                    *AbsoluteOffset = Member.mAbsoluteOffset;
                return Member.mMember;
            }
        }
        return nullptr;
    }

    void FArdaShaderParameterMetadata::GetFlattenedMembers(
        eastl::vector<FArdaFlattenedShaderParameterMember>& OutMembers) const
    {
        OutMembers.clear();
        FlattenMembers(*this, 0, {}, OutMembers);
    }

    void FArdaShaderParameterMetadata::ValidateAndHash()
    {
        mLayoutHash = FnvOffset;
        if (mName == nullptr || *mName == '\0' || mSize == 0 || mAlignment == 0)
        {
            mStatus = MakeError(
                EArdaShaderStructError::InvalidStruct,
                "Shader parameter metadata has an invalid C++ layout.");
            return;
        }
        HashString(mLayoutHash, mName);
        HashBytes(mLayoutHash, &mSize, sizeof(mSize));
        HashBytes(mLayoutHash, &mAlignment, sizeof(mAlignment));

        eastl::vector<FArdaFlattenedShaderParameterMember> Flattened;
        GetFlattenedMembers(Flattened);
        for (const FArdaFlattenedShaderParameterMember& Resolved : Flattened)
        {
            const FArdaShaderParameterMember& Member = *Resolved.mMember;
            if (Member.mArrayCount == 0 ||
                uint64_t(Member.mSlot) + Member.mArrayCount >
                    uint64_t(UINT32_MAX) + 1)
            {
                mStatus = MakeError(
                    EArdaShaderStructError::MalformedArray,
                    eastl::string("Malformed shader parameter array: ") +
                        (Member.mName != nullptr ? Member.mName : "<unnamed>"));
                return;
            }
            if (Member.mName == nullptr || *Member.mName == '\0' ||
                Member.mElementStride == 0 ||
                Resolved.mAbsoluteOffset > mSize ||
                Member.mSize > mSize - Resolved.mAbsoluteOffset)
            {
                mStatus = MakeError(
                    EArdaShaderStructError::InvalidMember,
                    "Shader parameter member metadata exceeds its C++ struct.");
                return;
            }
            if (IsBindingMember(Member.mKind) &&
                Member.mVisibility == rhi::EArdaRHIShaderStage::None)
            {
                mStatus = MakeError(
                    EArdaShaderStructError::IncompatibleVisibility,
                    eastl::string("Shader resource has no stage visibility: ") + Member.mName);
                return;
            }
            if (Member.mKind == EArdaShaderParameterKind::PushConstants &&
                (Member.mArrayCount != 1 || Member.mSize == 0 ||
                 Member.mSize > 128 || (Member.mSize % 4) != 0))
            {
                mStatus = MakeError(
                    EArdaShaderStructError::MalformedPushConstants,
                    eastl::string("Malformed push-constant member: ") + Member.mName);
                return;
            }

            HashString(mLayoutHash, Resolved.mPath.c_str());
            HashBytes(mLayoutHash, &Member.mKind, sizeof(Member.mKind));
            HashBytes(mLayoutHash, &Member.mBindingType, sizeof(Member.mBindingType));
            HashBytes(mLayoutHash, &Member.mSlot, sizeof(Member.mSlot));
            HashBytes(mLayoutHash, &Member.mRegisterSpace, sizeof(Member.mRegisterSpace));
            HashBytes(mLayoutHash, &Member.mArrayCount, sizeof(Member.mArrayCount));
            HashBytes(mLayoutHash, &Resolved.mAbsoluteOffset, sizeof(Resolved.mAbsoluteOffset));
            HashBytes(mLayoutHash, &Member.mSize, sizeof(Member.mSize));
            HashBytes(mLayoutHash, &Member.mVisibility, sizeof(Member.mVisibility));
        }

        for (size_t LeftIndex = 0; LeftIndex < Flattened.size(); ++LeftIndex)
        {
            const auto& Left = *Flattened[LeftIndex].mMember;
            if (!IsBindingMember(Left.mKind))
                continue;
            for (size_t RightIndex = LeftIndex + 1; RightIndex < Flattened.size(); ++RightIndex)
            {
                const auto& Right = *Flattened[RightIndex].mMember;
                const bool bVisibilityOverlaps =
                    (static_cast<uint16_t>(Left.mVisibility) &
                     static_cast<uint16_t>(Right.mVisibility)) != 0;
                if (!IsBindingMember(Right.mKind) ||
                    Left.mRegisterSpace != Right.mRegisterSpace ||
                    GetRegisterClass(Left.mBindingType) != GetRegisterClass(Right.mBindingType) ||
                    !bVisibilityOverlaps)
                {
                    continue;
                }
                const uint64_t LeftEnd = uint64_t(Left.mSlot) + Left.mArrayCount;
                const uint64_t RightEnd = uint64_t(Right.mSlot) + Right.mArrayCount;
                if (Left.mSlot < RightEnd && Right.mSlot < LeftEnd)
                {
                    mStatus = MakeError(
                        EArdaShaderStructError::DuplicateRegister,
                        eastl::string("Overlapping shader registers: ") +
                            Left.mName + " and " + Right.mName);
                    return;
                }
            }
        }
        mStatus = {};
    }

    FArdaShaderStructStatus FArdaShaderParameterMetadata::BuildBindingLayoutDescs(
        eastl::vector<rhi::FArdaRHIBindingLayoutDesc>& OutDescs) const
    {
        OutDescs.clear();
        if (!mStatus)
            return mStatus;

        eastl::vector<FArdaFlattenedShaderParameterMember> Flattened;
        GetFlattenedMembers(Flattened);
        for (const FArdaFlattenedShaderParameterMember& Resolved : Flattened)
        {
            const FArdaShaderParameterMember& Member = *Resolved.mMember;
            if (!IsBindingMember(Member.mKind))
                continue;

            auto Existing = eastl::find_if(
                OutDescs.begin(),
                OutDescs.end(),
                [&Member](const rhi::FArdaRHIBindingLayoutDesc& Desc)
                {
                    return Desc.mRegisterSpace == Member.mRegisterSpace &&
                        Desc.mVisibility == Member.mVisibility;
                });
            if (Existing == OutDescs.end())
            {
                rhi::FArdaRHIBindingLayoutDesc Desc;
                Desc.mRegisterSpace = Member.mRegisterSpace;
                Desc.mVisibility = Member.mVisibility;
                Desc.mDebugName = mName;
                OutDescs.push_back(eastl::move(Desc));
                Existing = OutDescs.end() - 1;
            }
            Existing->mItems.push_back({
                Member.mSlot,
                Member.mArrayCount,
                Member.mBindingType });
        }
        for (const rhi::FArdaRHIBindingLayoutDesc& Desc : OutDescs)
        {
            size_t PushConstantCount = 0;
            for (const rhi::FArdaRHIBindingLayoutItem& Item : Desc.mItems)
            {
                if (Item.mType == rhi::EArdaRHIBindingType::PushConstants)
                    ++PushConstantCount;
            }
            if (PushConstantCount > 1)
            {
                OutDescs.clear();
                return MakeError(
                    EArdaShaderStructError::MalformedPushConstants,
                    "A generated binding layout contains multiple push-constant blocks.");
            }
        }
        return {};
    }

    FArdaShaderStructStatus FArdaShaderParameterMetadata::BuildBindingSetDesc(
        const void* Parameters,
        const rhi::FArdaRHIBindingLayoutRef& Layout,
        rhi::FArdaRHIBindingSetDesc& OutDesc) const
    {
        if (!mStatus)
            return mStatus;
        if (Parameters == nullptr || !Layout)
        {
            return MakeError(
                EArdaShaderStructError::BindingCreationFailed,
                "Direct shader bindings require parameters and a binding layout.");
        }

        OutDesc = {};
        OutDesc.mLayout = Layout;
        OutDesc.mDebugName = mName;
        const auto& LayoutDesc = Layout->GetDesc();
        eastl::vector<rhi::FArdaRHIBindingLayoutDesc> GeneratedLayouts;
        const FArdaShaderStructStatus LayoutStatus =
            BuildBindingLayoutDescs(GeneratedLayouts);
        if (!LayoutStatus)
            return LayoutStatus;
        const bool bGeneratedLayout = eastl::any_of(
            GeneratedLayouts.begin(),
            GeneratedLayouts.end(),
            [&LayoutDesc](const rhi::FArdaRHIBindingLayoutDesc& Generated)
            {
                return Generated == LayoutDesc;
            });
        if (!bGeneratedLayout)
        {
            return MakeError(
                EArdaShaderStructError::BindingCreationFailed,
                "The selected binding layout does not match generated shader metadata.");
        }

        const auto* Bytes = static_cast<const std::byte*>(Parameters);
        eastl::vector<uint32_t> Populated(LayoutDesc.mItems.size(), 0u);
        eastl::vector<FArdaFlattenedShaderParameterMember> Flattened;
        GetFlattenedMembers(Flattened);
        for (const FArdaFlattenedShaderParameterMember& Resolved : Flattened)
        {
            const FArdaShaderParameterMember& Member = *Resolved.mMember;
            if (!IsBindingMember(Member.mKind) ||
                Member.mRegisterSpace != LayoutDesc.mRegisterSpace ||
                Member.mVisibility != LayoutDesc.mVisibility)
            {
                continue;
            }
            size_t LayoutIndex = LayoutDesc.mItems.size();
            for (size_t Index = 0; Index < LayoutDesc.mItems.size(); ++Index)
            {
                const rhi::FArdaRHIBindingLayoutItem& Item =
                    LayoutDesc.mItems[Index];
                if (Item.mSlot == Member.mSlot &&
                    Item.mType == Member.mBindingType &&
                    Item.mArraySize == Member.mArrayCount)
                {
                    LayoutIndex = Index;
                    break;
                }
            }
            if (LayoutIndex == LayoutDesc.mItems.size())
            {
                return MakeError(
                    EArdaShaderStructError::BindingCreationFailed,
                    eastl::string("Generated layout member is missing: ") +
                        Resolved.mPath);
            }
            for (uint32_t Element = 0; Element < Member.mArrayCount; ++Element)
            {
                const void* Value =
                    Bytes + Resolved.mAbsoluteOffset + Element * Member.mElementStride;
                rhi::FArdaRHIBindingItem Item;
                Item.mSlot = Member.mSlot;
                Item.mArrayElement = Element;
                Item.mType = Member.mBindingType;
                switch (Member.mKind)
                {
                case EArdaShaderParameterKind::TextureSRV:
                case EArdaShaderParameterKind::TextureUAV:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        static_cast<const rhi::FArdaRHITextureRef*>(Value)->Get());
                    break;
                case EArdaShaderParameterKind::BufferSRV:
                case EArdaShaderParameterKind::BufferUAV:
                case EArdaShaderParameterKind::ConstantBuffer:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        static_cast<const rhi::FArdaRHIBufferRef*>(Value)->Get());
                    break;
                case EArdaShaderParameterKind::UniformBuffer:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        static_cast<const rhi::FArdaRHIUniformBufferRef*>(Value)->Get());
                    break;
                case EArdaShaderParameterKind::Sampler:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        static_cast<const rhi::FArdaRHISamplerRef*>(Value)->Get());
                    break;
                case EArdaShaderParameterKind::AccelerationStructure:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        static_cast<const rhi::FArdaRHIAccelStructRef*>(Value)->Get());
                    break;
                case EArdaShaderParameterKind::PushConstants:
                    Item.mView.mBufferRange.mByteSize = Member.mSize;
                    break;
                case EArdaShaderParameterKind::Value:
                case EArdaShaderParameterKind::NestedStruct:
                    continue;
                }
                if (Member.mKind != EArdaShaderParameterKind::PushConstants &&
                    !Item.mResource)
                {
                    return MakeError(
                        EArdaShaderStructError::BindingCreationFailed,
                        eastl::string("Shader resource is null: ") +
                            Resolved.mPath);
                }
                OutDesc.mItems.push_back(eastl::move(Item));
                ++Populated[LayoutIndex];
            }
        }
        for (size_t Index = 0; Index < LayoutDesc.mItems.size(); ++Index)
        {
            const rhi::FArdaRHIBindingLayoutItem& Item =
                LayoutDesc.mItems[Index];
            if (Populated[Index] != Item.mArraySize)
            {
                return MakeError(
                    EArdaShaderStructError::BindingCreationFailed,
                    "A selected binding layout element is not completely populated.");
            }
        }
        return {};
    }

    FArdaShaderStructStatus FArdaShaderParameterMetadata::CreateBindingSet(
        rhi::IArdaRHIDevice& Device,
        const void* Parameters,
        const rhi::FArdaRHIBindingLayoutRef& Layout,
        rhi::FArdaRHIBindingSetRef& OutBindingSet) const
    {
        rhi::FArdaRHIBindingSetDesc Desc;
        const FArdaShaderStructStatus Status =
            BuildBindingSetDesc(Parameters, Layout, Desc);
        if (!Status)
            return Status;
        auto Result = Device.CreateBindingSet(Desc);
        if (!Result)
        {
            return MakeError(
                EArdaShaderStructError::BindingCreationFailed,
                Result.mStatus.mMessage);
        }
        OutBindingSet = eastl::move(Result.mValue);
        return {};
    }

    FArdaShaderStructStatus FArdaShaderParameterMetadata::GetPushConstantData(
        const void* Parameters,
        const rhi::FArdaRHIBindingLayoutDesc& Layout,
        const void*& OutData,
        size_t& OutSize) const
    {
        OutData = nullptr;
        OutSize = 0;
        if (!mStatus)
            return mStatus;
        if (Parameters == nullptr)
        {
            return MakeError(
                EArdaShaderStructError::BindingCreationFailed,
                "Push constants require a concrete parameter instance.");
        }

        eastl::vector<rhi::FArdaRHIBindingLayoutDesc> GeneratedLayouts;
        const FArdaShaderStructStatus LayoutStatus =
            BuildBindingLayoutDescs(GeneratedLayouts);
        if (!LayoutStatus)
            return LayoutStatus;
        if (!eastl::any_of(
                GeneratedLayouts.begin(),
                GeneratedLayouts.end(),
                [&Layout](const rhi::FArdaRHIBindingLayoutDesc& Generated)
                {
                    return Generated == Layout;
                }))
        {
            return MakeError(
                EArdaShaderStructError::MalformedPushConstants,
                "Push constants require a generated binding layout.");
        }

        eastl::vector<FArdaFlattenedShaderParameterMember> Flattened;
        GetFlattenedMembers(Flattened);
        const FArdaFlattenedShaderParameterMember* PushBlock = nullptr;
        for (const FArdaFlattenedShaderParameterMember& Resolved : Flattened)
        {
            const FArdaShaderParameterMember& Member = *Resolved.mMember;
            if (Member.mKind != EArdaShaderParameterKind::PushConstants ||
                Member.mRegisterSpace != Layout.mRegisterSpace ||
                Member.mVisibility != Layout.mVisibility)
            {
                continue;
            }
            if (PushBlock != nullptr)
            {
                return MakeError(
                    EArdaShaderStructError::MalformedPushConstants,
                    "A binding layout has multiple compatible push-constant blocks.");
            }
            PushBlock = &Resolved;
        }
        if (PushBlock == nullptr)
            return {};

        const auto* Bytes = static_cast<const std::byte*>(Parameters);
        OutData = Bytes + PushBlock->mAbsoluteOffset;
        OutSize = PushBlock->mMember->mSize;
        return {};
    }

    FArdaShaderStructStatus FArdaShaderParameterMetadata::ApplyPushConstants(
        rhi::IArdaRHICommandList& CommandList,
        const void* Parameters,
        const rhi::FArdaRHIBindingLayoutDesc& Layout) const
    {
        const void* Data = nullptr;
        size_t Size = 0;
        const FArdaShaderStructStatus Status =
            GetPushConstantData(Parameters, Layout, Data, Size);
        if (!Status)
            return Status;
        if (Data != nullptr)
            CommandList.SetPushConstants(Data, Size);
        return {};
    }
}
