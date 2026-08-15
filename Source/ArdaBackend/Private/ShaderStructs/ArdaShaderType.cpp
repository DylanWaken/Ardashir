#include "ShaderStructs/ArdaShaderType.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <mutex>

namespace arda::backend
{
    namespace
    {
        struct FShaderRegistry
        {
            std::mutex mMutex;
            eastl::vector<FArdaShaderTypeRegistration*> mNodes;
            eastl::vector<const FArdaShaderType*> mCommitted;
        };

        FShaderRegistry& GetRegistry()
        {
            static FShaderRegistry Registry;
            return Registry;
        }

        uint64_t HashIdentity(const FArdaShaderType& Type)
        {
            uint64_t Hash = 1469598103934665603ull;
            const auto Append = [&Hash](const char* Text)
            {
                if (Text == nullptr)
                    return;
                while (*Text != '\0')
                {
                    Hash ^= static_cast<uint8_t>(*Text++);
                    Hash *= 1099511628211ull;
                }
                Hash ^= 0xffu;
                Hash *= 1099511628211ull;
            };
            Append(Type.GetSourceStem());
            Append(Type.GetOutputStem());
            Append(Type.GetEntryPoint());
            const auto Stage = Type.GetStage();
            const auto* Bytes = reinterpret_cast<const uint8_t*>(&Stage);
            for (size_t Index = 0; Index < sizeof(Stage); ++Index)
            {
                Hash ^= Bytes[Index];
                Hash *= 1099511628211ull;
            }
            return Hash;
        }

        FArdaShaderRegistrationStatus Error(
            EArdaShaderRegistrationError Code,
            const eastl::string& Message)
        {
            return { Code, Message };
        }
    }

    const FArdaShaderParameterMetadata* FArdaShaderType::GetParameterMetadata() const
    {
        return mParameterMetadataFunction != nullptr
            ? mParameterMetadataFunction()
            : nullptr;
    }

    FArdaShaderTypeRegistration::FArdaShaderTypeRegistration(
        const char* Name,
        const char* SourceStem,
        const char* OutputStem,
        const char* EntryPoint,
        rhi::EArdaRHIShaderStage Stage,
        FArdaShaderType::FParameterMetadataFunction ParameterMetadataFunction)
    {
        mType.mName = Name;
        mType.mSourceStem = SourceStem;
        mType.mOutputStem = OutputStem;
        mType.mEntryPoint = EntryPoint;
        mType.mStage = Stage;
        mType.mParameterMetadataFunction = ParameterMetadataFunction;
        mType.mIdentityHash = HashIdentity(mType);

        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mNodes.push_back(this);
    }

    FArdaShaderTypeRegistration::~FArdaShaderTypeRegistration()
    {
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mNodes.erase(
            eastl::remove(
                Registry.mNodes.begin(),
                Registry.mNodes.end(),
                this),
            Registry.mNodes.end());
        Registry.mCommitted.erase(
            eastl::remove(
                Registry.mCommitted.begin(),
                Registry.mCommitted.end(),
                &mType),
            Registry.mCommitted.end());
    }

    FArdaShaderRegistrationStatus FArdaShaderTypeRegistration::CommitAll()
    {
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);

        eastl::vector<const FArdaShaderType*> Candidates;
        Candidates.reserve(Registry.mNodes.size());
        for (const FArdaShaderTypeRegistration* Node : Registry.mNodes)
            Candidates.push_back(&Node->mType);
        eastl::sort(
            Candidates.begin(),
            Candidates.end(),
            [](const FArdaShaderType* Left, const FArdaShaderType* Right)
            {
                const eastl::string LeftName =
                    Left->GetName() != nullptr ? Left->GetName() : "";
                const eastl::string RightName =
                    Right->GetName() != nullptr ? Right->GetName() : "";
                if (LeftName != RightName)
                    return LeftName < RightName;
                return Left->GetIdentityHash() < Right->GetIdentityHash();
            });

        for (const FArdaShaderType* Type : Candidates)
        {
            if (Type->GetName() == nullptr || *Type->GetName() == '\0' ||
                Type->GetSourceStem() == nullptr || *Type->GetSourceStem() == '\0' ||
                Type->GetOutputStem() == nullptr || *Type->GetOutputStem() == '\0' ||
                Type->GetEntryPoint() == nullptr || *Type->GetEntryPoint() == '\0' ||
                Type->GetStage() == rhi::EArdaRHIShaderStage::None)
            {
                return Error(
                    EArdaShaderRegistrationError::InvalidType,
                    "A global shader type has an invalid identity.");
            }
            const FArdaShaderParameterMetadata* Metadata =
                Type->GetParameterMetadata();
            if (Metadata != nullptr && !Metadata->GetStatus())
            {
                return Error(
                    EArdaShaderRegistrationError::InvalidParameterMetadata,
                    eastl::string("Invalid parameter metadata for ") + Type->GetName() +
                        ": " + Metadata->GetStatus().mMessage);
            }
            if (Metadata != nullptr)
            {
                eastl::vector<rhi::FArdaRHIBindingLayoutDesc> Layouts;
                const FArdaShaderStructStatus LayoutStatus =
                    Metadata->BuildBindingLayoutDescs(Layouts);
                if (!LayoutStatus)
                {
                    return Error(
                        EArdaShaderRegistrationError::InvalidParameterMetadata,
                        LayoutStatus.mMessage);
                }
                for (const rhi::FArdaRHIBindingLayoutDesc& Layout : Layouts)
                {
                    if ((static_cast<uint16_t>(Layout.mVisibility) &
                         static_cast<uint16_t>(Type->GetStage())) == 0)
                    {
                        return Error(
                            EArdaShaderRegistrationError::InvalidParameterMetadata,
                            eastl::string("Parameter visibility does not include shader stage for ") +
                                Type->GetName());
                    }
                }
            }
        }

        for (size_t Left = 0; Left < Candidates.size(); ++Left)
        {
            for (size_t Right = Left + 1; Right < Candidates.size(); ++Right)
            {
                if (eastl::string(Candidates[Left]->GetName()) ==
                    Candidates[Right]->GetName())
                {
                    return Error(
                        EArdaShaderRegistrationError::DuplicateTypeName,
                        eastl::string("Duplicate global shader type name: ") +
                            Candidates[Left]->GetName());
                }
                if (Candidates[Left]->GetIdentityHash() ==
                    Candidates[Right]->GetIdentityHash())
                {
                    return Error(
                        EArdaShaderRegistrationError::DuplicateIdentity,
                        eastl::string("Duplicate global shader artifact identity: ") +
                            Candidates[Left]->GetName() + " and " +
                            Candidates[Right]->GetName());
                }
            }
        }

        Registry.mCommitted = eastl::move(Candidates);
        return {};
    }

    const FArdaShaderType* FArdaShaderTypeRegistration::Find(
        const eastl::string& Name)
    {
        const auto Status = CommitAll();
        if (!Status)
            return nullptr;
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const FArdaShaderType* Type : Registry.mCommitted)
        {
            if (Name == Type->GetName())
                return Type;
        }
        return nullptr;
    }

    eastl::vector<const FArdaShaderType*> FArdaShaderTypeRegistration::Enumerate()
    {
        const auto Status = CommitAll();
        if (!Status)
            return {};
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        return Registry.mCommitted;
    }

    void FArdaShaderTypeRegistration::ResetForTests()
    {
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mCommitted.clear();
    }
}
