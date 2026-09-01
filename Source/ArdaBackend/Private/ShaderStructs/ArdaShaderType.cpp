#include "ShaderStructs/ArdaShaderType.h"

#include "ArdaHash.h"
#include "ShaderStructs/ArdaShaderDirectories.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace arda::backend
{
    namespace
    {
        struct FShaderRegistry
        {
            std::mutex mMutex;
            eastl::vector<std::shared_ptr<FArdaShaderType>> mNodes;
            eastl::vector<std::shared_ptr<FArdaShaderType>> mCommitted;
            eastl::vector<std::shared_ptr<FArdaShaderType>> mRetired;
            uint64_t mGeneration = 0;
        };

        FShaderRegistry& GetRegistry()
        {
            static FShaderRegistry Registry;
            return Registry;
        }

        uint64_t HashIdentity(const FArdaShaderType& Type)
        {
            uint64_t Hash = private_api::ArdaFnv1a64OffsetBasis;
            const auto Append = [&Hash](const char* Text)
            {
                if (Text == nullptr)
                    return;
                while (*Text != '\0')
                {
                    private_api::AppendFnv1a64(Hash, Text, 1);
                    ++Text;
                }
                const uint8_t Delimiter = 0xffu;
                private_api::AppendFnv1a64(
                    Hash, &Delimiter, sizeof(Delimiter));
            };
            Append(Type.GetSourceStem());
            Append(Type.GetOutputStem());
            Append(Type.GetEntryPoint());
            const auto AppendUint32 = [&Hash](uint32_t Value)
            {
                private_api::AppendFnv1a64LittleEndian(
                    Hash, Value, sizeof(Value));
            };
            AppendUint32(static_cast<uint32_t>(Type.GetStage()));
            AppendUint32(Type.GetPermutationCount());
            return Hash;
        }

        FArdaShaderRegistrationStatus Error(
            EArdaShaderRegistrationError Code,
            const eastl::string& Message)
        {
            return { Code, Message };
        }

        bool DefaultShouldCompilePermutation(
            const FArdaShaderPermutationParameters&)
        {
            return true;
        }

        void DefaultModifyCompilationEnvironment(
            const FArdaShaderPermutationParameters&,
            FArdaShaderCompileEnvironment&)
        {
        }

        bool IsPortableArtifactStem(const char* Stem)
        {
            const auto IsAsciiAlphaNumeric = [](unsigned char Character)
            {
                return (Character >= 'A' && Character <= 'Z') ||
                    (Character >= 'a' && Character <= 'z') ||
                    (Character >= '0' && Character <= '9');
            };
            if (Stem == nullptr || Stem[0] == '\0' || Stem[0] == '.' ||
                !(IsAsciiAlphaNumeric(static_cast<unsigned char>(Stem[0])) ||
                  Stem[0] == '_'))
            {
                return false;
            }
            std::string Value(Stem);
            if (Value.find("..") != std::string::npos)
                return false;
            for (const unsigned char Character : Value)
            {
                if (!(IsAsciiAlphaNumeric(Character) || Character == '_' ||
                      Character == '.' || Character == '-'))
                {
                    return false;
                }
            }
            const std::filesystem::path Path(Value);
            return !Path.is_absolute() && !Path.has_parent_path() &&
                Path.filename() == Path;
        }

        std::string PortableFold(const eastl::string& Value)
        {
            std::string Result(Value.data(), Value.size());
            std::transform(
                Result.begin(), Result.end(), Result.begin(),
                [](unsigned char Character)
                {
                    return static_cast<char>(std::tolower(Character));
                });
            return Result;
        }
    }

    const FArdaShaderParameterMetadata* FArdaShaderType::GetParameterMetadata() const
    {
        return mParameterMetadataFunction != nullptr
            ? mParameterMetadataFunction()
            : nullptr;
    }

    bool FArdaShaderType::ShouldCompilePermutation(
        EArdaBackendType Backend,
        uint32_t PermutationId) const
    {
        FArdaShaderTarget Target;
        Target.mBackend = Backend;
        return ShouldCompilePermutation(Target, PermutationId);
    }

    bool FArdaShaderType::ShouldCompilePermutation(
        const FArdaShaderTarget& Target,
        uint32_t PermutationId) const
    {
        if (PermutationId >= mPermutationCount ||
            mShouldCompilePermutationFunction == nullptr)
        {
            return false;
        }
        FArdaShaderPermutationParameters Parameters;
        Parameters.mType = this;
        Parameters.mBackend = Target.mBackend;
        Parameters.mBackendName = Target.mBackendName;
        Parameters.mBinaryFormat = Target.mBinaryFormat;
        Parameters.mPermutationId = PermutationId;
        return mShouldCompilePermutationFunction(Parameters);
    }

    FArdaShaderCompileEnvironment FArdaShaderType::BuildCompilationEnvironment(
        EArdaBackendType Backend,
        uint32_t PermutationId) const
    {
        FArdaShaderTarget Target;
        Target.mBackend = Backend;
        return BuildCompilationEnvironment(Target, PermutationId);
    }

    FArdaShaderCompileEnvironment FArdaShaderType::BuildCompilationEnvironment(
        const FArdaShaderTarget& Target,
        uint32_t PermutationId) const
    {
        FArdaShaderCompileEnvironment Environment;
        if (PermutationId < mPermutationCount &&
            mModifyCompilationEnvironmentFunction != nullptr)
        {
            FArdaShaderPermutationParameters Parameters;
            Parameters.mType = this;
            Parameters.mBackend = Target.mBackend;
            Parameters.mBackendName = Target.mBackendName;
            Parameters.mBinaryFormat = Target.mBinaryFormat;
            Parameters.mPermutationId = PermutationId;
            mModifyCompilationEnvironmentFunction(Parameters, Environment);
        }
        return Environment;
    }

    eastl::string FArdaShaderType::GetPermutationArtifactStem(
        uint32_t PermutationId) const
    {
        if (mOutputStem.empty() || PermutationId >= mPermutationCount)
            return {};
        if (mPermutationCount == 1)
            return mOutputStem;
        const std::string Id = std::to_string(PermutationId);
        eastl::string Result = mOutputStem;
        Result += "_P";
        Result.append(Id.data(), Id.size());
        return Result;
    }

    FArdaShaderTypeRegistration::FArdaShaderTypeRegistration(
        const char* Name,
        const char* SourceStem,
        const char* OutputStem,
        const char* EntryPoint,
        rhi::EArdaRHIShaderStage Stage,
        FArdaShaderType::FParameterMetadataFunction ParameterMetadataFunction,
        uint32_t PermutationCount,
        FArdaShaderType::FShouldCompilePermutationFunction
            ShouldCompilePermutationFunction,
        FArdaShaderType::FModifyCompilationEnvironmentFunction
            ModifyCompilationEnvironmentFunction)
    {
        mType = std::make_shared<FArdaShaderType>();
        mType->mName = Name != nullptr ? Name : "";
        mType->mSourceStem = SourceStem != nullptr ? SourceStem : "";
        mType->mOutputStem = OutputStem != nullptr ? OutputStem : "";
        mType->mEntryPoint = EntryPoint != nullptr ? EntryPoint : "";
        mType->mStage = Stage;
        mType->mParameterMetadataFunction = ParameterMetadataFunction;
        mType->mPermutationCount = PermutationCount;
        mType->mbPermutationCallbacksValid =
            PermutationCount == 1 ||
            (ShouldCompilePermutationFunction != nullptr &&
             ModifyCompilationEnvironmentFunction != nullptr);
        mType->mShouldCompilePermutationFunction =
            ShouldCompilePermutationFunction != nullptr
                ? ShouldCompilePermutationFunction
                : &DefaultShouldCompilePermutation;
        mType->mModifyCompilationEnvironmentFunction =
            ModifyCompilationEnvironmentFunction != nullptr
                ? ModifyCompilationEnvironmentFunction
                : &DefaultModifyCompilationEnvironment;
        mType->mIdentityHash = HashIdentity(*mType);

        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mNodes.push_back(mType);
        ++Registry.mGeneration;
    }

    FArdaShaderTypeRegistration::~FArdaShaderTypeRegistration()
    {
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mNodes.erase(
            eastl::remove(
                Registry.mNodes.begin(),
                Registry.mNodes.end(),
                mType),
            Registry.mNodes.end());
        Registry.mCommitted.erase(
            eastl::remove(
                Registry.mCommitted.begin(),
                Registry.mCommitted.end(),
                mType),
            Registry.mCommitted.end());
        Registry.mRetired.push_back(mType);
        ++Registry.mGeneration;
    }

    FArdaShaderRegistrationStatus FArdaShaderTypeRegistration::CommitAll()
    {
        FShaderRegistry& Registry = GetRegistry();
        constexpr uint32_t MaxAttempts = 4;
        for (uint32_t Attempt = 0; Attempt < MaxAttempts; ++Attempt)
        {
            eastl::vector<std::shared_ptr<FArdaShaderType>> Candidates;
            uint64_t Generation = 0;
            {
                std::lock_guard<std::mutex> Lock(Registry.mMutex);
                Candidates = Registry.mNodes;
                Generation = Registry.mGeneration;
            }
            eastl::sort(
                Candidates.begin(), Candidates.end(),
                [](const auto& Left, const auto& Right)
                {
                    const eastl::string LeftName = Left->GetName();
                    const eastl::string RightName = Right->GetName();
                    if (LeftName != RightName)
                        return LeftName < RightName;
                    return Left->GetIdentityHash() < Right->GetIdentityHash();
                });

            for (size_t Left = 0; Left < Candidates.size(); ++Left)
            {
                for (size_t Right = Left + 1; Right < Candidates.size(); ++Right)
                {
                    if (eastl::string(Candidates[Left]->GetName()) ==
                        Candidates[Right]->GetName())
                        return Error(EArdaShaderRegistrationError::DuplicateTypeName,
                            eastl::string("Duplicate global shader type name: ") +
                                Candidates[Left]->GetName());
                    if (Candidates[Left]->GetIdentityHash() ==
                        Candidates[Right]->GetIdentityHash())
                        return Error(EArdaShaderRegistrationError::DuplicateIdentity,
                            eastl::string("Duplicate global shader artifact identity: ") +
                                Candidates[Left]->GetName() + " and " +
                                Candidates[Right]->GetName());
                }
            }
            std::map<std::string, eastl::string> ArtifactOwners;
            for (const auto& Candidate : Candidates)
            {
                const FArdaShaderType* Type = Candidate.get();
                if (*Type->GetName() == '\0' || *Type->GetSourceStem() == '\0' ||
                    *Type->GetEntryPoint() == '\0' ||
                    Type->GetStage() == rhi::EArdaRHIShaderStage::None ||
                    !IsPortableArtifactStem(Type->GetOutputStem()))
                {
                    return Error(
                        EArdaShaderRegistrationError::InvalidType,
                        eastl::string("Global shader type '") + Type->GetName() +
                            "' has an invalid identity or non-portable output stem.");
                }
                if (Type->GetPermutationCount() == 0 ||
                    Type->GetPermutationCount() > 65536 ||
                    !Type->mbPermutationCallbacksValid ||
                    Type->mShouldCompilePermutationFunction == nullptr ||
                    Type->mModifyCompilationEnvironmentFunction == nullptr)
                {
                    return Error(
                        EArdaShaderRegistrationError::InvalidPermutation,
                        eastl::string("Global shader type '") + Type->GetName() +
                            "' has an invalid permutation registration.");
                }
                if (Type->GetSourceStem()[0] == '/')
                {
                    std::filesystem::path PhysicalSource;
                    const FArdaShaderDirectoryStatus SourceStatus =
                        ResolveVirtualShaderSource(Type->GetSourceStem(), PhysicalSource);
                    if (!SourceStatus)
                    {
                        const EArdaShaderRegistrationError Code =
                            SourceStatus.mCode == EArdaShaderDirectoryError::MissingVirtualSource
                            ? EArdaShaderRegistrationError::MissingVirtualSource
                            : SourceStatus.mCode == EArdaShaderDirectoryError::NotFrozen
                            ? EArdaShaderRegistrationError::DirectoryRegistryNotFrozen
                            : EArdaShaderRegistrationError::DirectoryRegistryFailure;
                        return Error(
                            Code,
                            eastl::string("Global shader type '") + Type->GetName() +
                                "' has invalid virtual source '" + Type->GetSourceStem() +
                                "': " + SourceStatus.mMessage);
                    }
                }
                const FArdaShaderParameterMetadata* Metadata = Type->GetParameterMetadata();
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
                        return Error(EArdaShaderRegistrationError::InvalidParameterMetadata,
                            LayoutStatus.mMessage);
                    for (const auto& Layout : Layouts)
                    {
                        if ((static_cast<uint16_t>(Layout.mVisibility) &
                             static_cast<uint16_t>(Type->GetStage())) == 0)
                            return Error(
                                EArdaShaderRegistrationError::InvalidParameterMetadata,
                                eastl::string("Parameter visibility does not include shader stage for ") +
                                    Type->GetName());
                    }
                }
                for (uint32_t Id = 0; Id < Type->GetPermutationCount(); ++Id)
                {
                    const eastl::string Stem = Type->GetPermutationArtifactStem(Id);
                    const std::string Folded = PortableFold(Stem);
                    const auto Existing = ArtifactOwners.find(Folded);
                    if (Existing != ArtifactOwners.end())
                        return Error(
                            EArdaShaderRegistrationError::ArtifactStemCollision,
                            eastl::string("Global shader artifact stem collision between ") +
                                Existing->second + " and " + Type->GetName() + ": " + Stem);
                    ArtifactOwners.emplace(Folded, Type->GetName());
                }
            }
            {
                std::lock_guard<std::mutex> Lock(Registry.mMutex);
                if (Registry.mGeneration != Generation)
                    continue;
                Registry.mCommitted = eastl::move(Candidates);
                return {};
            }
        }
        return Error(
            EArdaShaderRegistrationError::RegistryMutation,
            "Shader registration changed repeatedly during validation; retry after registrations settle.");
    }

    const FArdaShaderType* FArdaShaderTypeRegistration::Find(
        const eastl::string& Name)
    {
        const auto Status = CommitAll();
        if (!Status)
            return nullptr;
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        for (const auto& Type : Registry.mCommitted)
        {
            if (Name == Type->GetName())
                return Type.get();
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
        eastl::vector<const FArdaShaderType*> Result;
        Result.reserve(Registry.mCommitted.size());
        for (const auto& Type : Registry.mCommitted)
            Result.push_back(Type.get());
        return Result;
    }

    eastl::vector<FArdaShaderType>
    FArdaShaderTypeRegistration::EnumerateSnapshots()
    {
        const auto Status = CommitAll();
        if (!Status)
            return {};
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        eastl::vector<FArdaShaderType> Result;
        Result.reserve(Registry.mCommitted.size());
        for (const auto& Type : Registry.mCommitted)
            Result.push_back(*Type);
        return Result;
    }

    void FArdaShaderTypeRegistration::ResetForTests()
    {
        FShaderRegistry& Registry = GetRegistry();
        std::lock_guard<std::mutex> Lock(Registry.mMutex);
        Registry.mCommitted.clear();
    }
}
