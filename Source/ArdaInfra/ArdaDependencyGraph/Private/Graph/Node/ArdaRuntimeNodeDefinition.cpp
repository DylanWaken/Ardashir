#include "Graph/Node/ArdaRuntimeNodeDefinition.h"

#include "NativeCompute/ArdaNativeCompute.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include "Graph/Node/ArdaLegacyRuntimeKernel.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Runtime/ArdaInductorNativeContract.h"
#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus RuntimeInvalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		bool IsIdentifier(const eastl::string& Value)
		{
			return !Value.empty() && Value.find('\0') == eastl::string::npos;
		}

		bool IsAlignment(size_t Value)
		{
			return Value && (Value & (Value - 1)) == 0;
		}

		EArdaNativeArgumentKind GetGraphicsBindingKind(const FArdaRuntimeNodeDesc& Desc, size_t Index)
		{
			switch (Desc.mArguments[Index].mResourceType)
			{
			case EArdaRHIResourceType::Buffer:
				return EArdaNativeArgumentKind::Buffer;
			case EArdaRHIResourceType::Texture:
				return EArdaNativeArgumentKind::Surface;
			default:
				return Desc.mLayout.mArguments[Index].mKind;
			}
		}

		bool RequiresRegisteredConversions(const FArdaRuntimeNodeDesc& Desc)
		{
			for (size_t Index = 0; Index < Desc.mArguments.size(); ++Index)
			{
				if (GetGraphicsBindingKind(Desc, Index) != Desc.mLayout.mArguments[Index].mKind)
				{
					return true;
				}
			}
			const auto Operand = GetArdaNativeComputeRegistry().FindOperand(Desc.mName.c_str());
			return Desc.mbRequiresRegisteredConversions ||
			    (Operand && Operand->mSignature == Desc.mKernel.mSignature &&
			        HasArdaNativeResourceConversions(*Operand));
		}

		FArdaRHIStatus ValidateRuntimeDefinition(const FArdaRuntimeNodeDesc& Desc)
		{
			const auto& Key = Desc.mKernel;
			if (!IsIdentifier(Desc.mName) || !Desc.mVersion || !IsValidArdaNativeKernelKey(Key))
			{
				return RuntimeInvalid("A runtime node requires a name, a positive version and a complete kernel key.");
			}
			const auto& Layout = Desc.mLayout;
			if (!IsAlignment(Layout.mParameterAlignment) || Layout.mArguments.size() != Desc.mArguments.size())
			{
				return RuntimeInvalid("Runtime argument layout and graph binding schema do not match.");
			}
			eastl::unordered_set<eastl::string> Names;
			size_t End = 0;
			for (size_t Index = 0; Index < Desc.mArguments.size(); ++Index)
			{
				const auto& Argument = Desc.mArguments[Index];
				const auto& Abi = Layout.mArguments[Index];
				if (!IsIdentifier(Argument.mName) || !Names.insert(Argument.mName).second ||
				    Argument.mAccess > EArdaComputeAccess::ReadWrite || Abi.mKind > EArdaNativeArgumentKind::Surface ||
				    (Argument.mResourceType != EArdaRHIResourceType::Count &&
				        (Abi.mKind == EArdaNativeArgumentKind::Value ||
				            (Argument.mResourceType != EArdaRHIResourceType::Buffer &&
				                Argument.mResourceType != EArdaRHIResourceType::Texture))) ||
				    (Argument.mFormat != EArdaRHIFormat::Unknown &&
				        (GetGraphicsBindingKind(Desc, Index) != EArdaNativeArgumentKind::Surface ||
				            !IsArdaRHIFormatKnown(Argument.mFormat))) ||
				    !Abi.mSize || !IsAlignment(Abi.mAlignment) || Abi.mAlignment > Layout.mParameterAlignment ||
				    Abi.mOffset % Abi.mAlignment || Abi.mOffset < End || Abi.mOffset > Layout.mParameterSize ||
				    Abi.mSize > Layout.mParameterSize - Abi.mOffset)
				{
					return RuntimeInvalid("Runtime parameters require unique names and a valid packed layout.");
				}
				End = Abi.mOffset + Abi.mSize;
			}
			return {};
		}

		FArdaRHIStatus ValidateRuntimeParameters(const FArdaRuntimeNodeDesc& Desc,
		    const FArdaRuntimeNodeParameters& Parameters)
		{
			if (Parameters.mArguments.size() != Desc.mArguments.size())
			{
				return RuntimeInvalid("Runtime argument count does not match its definition.");
			}
			for (size_t Axis = 0; Axis < 3; ++Axis)
			{
				if (!Parameters.mGrid[Axis] || !Parameters.mBlock[Axis])
				{
					return RuntimeInvalid("Runtime launch dimensions must be nonzero.");
				}
			}
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				const auto& Argument = Parameters.mArguments[Index];
				const auto& Abi = Desc.mLayout.mArguments[Index];
				const bool bHasBufferRange =
				    Argument.mRange.mByteOffset || Argument.mRange.mByteSize != ArdaRHIWholeBuffer;
				switch (GetGraphicsBindingKind(Desc, Index))
				{
				case EArdaNativeArgumentKind::Buffer:
					if (!Argument.mBuffer || Argument.mSurface || Argument.mMipLevel || !Argument.mValue.empty() ||
					    !Argument.mRange.mByteSize)
					{
						return RuntimeInvalid("A runtime buffer parameter needs a nonempty declared buffer range.");
					}
					break;
				case EArdaNativeArgumentKind::Surface:
					if (!Argument.mSurface || Argument.mBuffer || bHasBufferRange || !Argument.mValue.empty())
					{
						return RuntimeInvalid("A runtime surface parameter needs a declared texture mip.");
					}
					break;
				case EArdaNativeArgumentKind::Value:
					if (Argument.mBuffer || Argument.mSurface || Argument.mMipLevel ||
					    Argument.mValue.size() != Abi.mSize || bHasBufferRange)
					{
						return RuntimeInvalid("A runtime scalar needs exact ABI bytes and no resource reference.");
					}
					break;
				}
			}
			return {};
		}

		template <class ArchiveType, class ArgumentType>
		void RuntimeArgumentFields(ArchiveType& Archive, EArdaNativeArgumentKind Kind, ArgumentType& Argument)
		{
			if (Kind == EArdaNativeArgumentKind::Surface)
			{
				Archive.Fields(Argument.mSurface, Argument.mMipLevel);
			}
			else
			{
				Archive.Fields(Argument.mBuffer, Argument.mRange, Argument.mValue);
			}
		}

		void WriteRuntimeParameters(FArdaGraphArchiveWriter& Archive,
		    const FArdaRuntimeNodeDesc& Desc,
		    const FArdaRuntimeNodeParameters& Parameters)
		{
			Archive.Fields(Parameters.mGrid, Parameters.mBlock, Parameters.mSharedMemoryBytes);
			Archive.WriteInteger(Parameters.mArguments.size(), 8);
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				RuntimeArgumentFields(Archive, GetGraphicsBindingKind(Desc, Index), Parameters.mArguments[Index]);
			}
		}

		void ReadRuntimeParameters(FArdaGraphArchiveReader& Archive,
		    const FArdaRuntimeNodeDesc& Desc,
		    FArdaRuntimeNodeParameters& Parameters)
		{
			Archive.Fields(Parameters.mGrid, Parameters.mBlock, Parameters.mSharedMemoryBytes);
			const auto Count = Archive.ReadCount();
			if (Count != Desc.mArguments.size())
			{
				Archive.Fail("Runtime argument count does not match its definition.");
				return;
			}
			Parameters.mArguments.resize(Count);
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				RuntimeArgumentFields(Archive, GetGraphicsBindingKind(Desc, Index), Parameters.mArguments[Index]);
			}
		}

		FArdaRHIStatus ReadLegacyDefinition(FArdaGraphArchiveReader& Archive, FArdaRuntimeNodeDesc& Desc)
		{
			FArdaCudaModuleDesc Module;
			Archive.Fields(Module.mFormat,
			    Module.mImage,
			    Module.mBuild.mName,
			    Module.mBuild.mIdentity,
			    Module.mBuild.mbFastMath);
			const auto Count = Archive.ReadCount();
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				FArdaCudaArchitecture Architecture;
				Archive.Fields(Architecture.mComputeCapability, Architecture.mbExact);
				Module.mBuild.mArchitectures.push_back(Architecture);
			}
			eastl::string EntryPoint;
			Archive.Fields(EntryPoint, Desc.mLayout.mParameterSize, Desc.mLayout.mParameterAlignment);
			graph_detail::ReadLegacyKernelArguments(Archive, Desc);
			if (auto Status = Archive.Finish(); !Status)
			{
				return Status;
			}
			return graph_detail::ImportLegacyRuntimeKernel(Desc, Module, EntryPoint);
		}
	}

	TArdaRHIResult<eastl::string> FArdaRuntimeNodeDefinition::Serialize(const FArdaRuntimeNodeDesc& Desc)
	{
		if (const auto Status = ValidateRuntimeDefinition(Desc); !Status)
		{
			return {{}, Status};
		}
		FArdaGraphArchiveWriter Archive;
		const auto& Key = Desc.mKernel;
		const bool bHasStates = eastl::any_of(Desc.mArguments.begin(),
		    Desc.mArguments.end(),
		    [](const auto& Argument)
		    {
			    return Argument.mState != EArdaRHIResourceState::UnorderedAccess;
		    });
		const bool bRequiresConversions = RequiresRegisteredConversions(Desc);
		const bool bHasBindingOverrides = eastl::any_of(Desc.mArguments.begin(),
		    Desc.mArguments.end(),
		    [](const auto& Argument)
		    {
			    return Argument.mResourceType != EArdaRHIResourceType::Count ||
			        Argument.mFormat != EArdaRHIFormat::Unknown;
		    });
		const uint32_t Version = bRequiresConversions || bHasBindingOverrides ? 4 : bHasStates ? 3 : 2;
		Archive.Fields(Version,
		    Desc.mName,
		    Desc.mVersion,
		    Key.mLibrary,
		    Key.mProvider,
		    Key.mName,
		    Key.mSignature,
		    Desc.mLayout.mParameterSize,
		    Desc.mLayout.mParameterAlignment);
		if (Version >= 4)
		{
			Archive.Write(bRequiresConversions);
		}
		Archive.WriteInteger(Desc.mArguments.size(), 8);
		for (size_t Index = 0; Index < Desc.mArguments.size(); ++Index)
		{
			const auto& Argument = Desc.mArguments[Index];
			const auto& Abi = Desc.mLayout.mArguments[Index];
			Archive.Fields(Argument.mName,
			    Argument.mAccess,
			    Abi.mKind,
			    uint32_t(Abi.mOffset),
			    uint32_t(Abi.mSize),
			    uint32_t(Abi.mAlignment));
			if (Version >= 3)
			{
				Archive.Write(Argument.mState);
			}
			if (Version >= 4)
			{
				const uint8_t Binding = Argument.mResourceType == EArdaRHIResourceType::Buffer ? 1
				    : Argument.mResourceType == EArdaRHIResourceType::Texture                  ? 2
				                                                                               : 0;
				Archive.Fields(Binding, Argument.mFormat);
			}
		}
		return {Archive.GetData(), Archive.GetStatus()};
	}

	TArdaRHIResult<FArdaRuntimeNodeDesc> FArdaRuntimeNodeDefinition::Deserialize(const eastl::string& Bytes)
	{
		FArdaGraphArchiveReader Archive(Bytes);
		FArdaRuntimeNodeDesc Desc;
		uint32_t Version = 0;
		Archive.Fields(Version, Desc.mName, Desc.mVersion);
		if (Version == 1)
		{
			if (auto Status = ReadLegacyDefinition(Archive, Desc); !Status)
			{
				return {{}, eastl::move(Status)};
			}
		}
		else if (Version >= 2 && Version <= 4)
		{
			auto& Key = Desc.mKernel;
			Archive.Fields(Key.mLibrary,
			    Key.mProvider,
			    Key.mName,
			    Key.mSignature,
			    Desc.mLayout.mParameterSize,
			    Desc.mLayout.mParameterAlignment);
			if (Version >= 4)
			{
				Archive.Read(Desc.mbRequiresRegisteredConversions);
			}
			const auto Count = Archive.ReadCount();
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				FArdaRuntimeNodeArgumentDesc Argument;
				EArdaNativeArgumentKind Kind{};
				uint32_t Offset = 0, Size = 0, Alignment = 0;
				Archive.Fields(Argument.mName, Argument.mAccess, Kind, Offset, Size, Alignment);
				if (Version >= 3)
				{
					Archive.Read(Argument.mState);
				}
				if (Version >= 4)
				{
					uint8_t Binding = 0;
					Archive.Fields(Binding, Argument.mFormat);
					if (Binding > 2)
					{
						return {{}, RuntimeInvalid("Runtime argument has an unknown graphics binding kind.")};
					}
					Argument.mResourceType = Binding == 1 ? EArdaRHIResourceType::Buffer
					    : Binding == 2                    ? EArdaRHIResourceType::Texture
					                                      : EArdaRHIResourceType::Count;
				}
				Desc.mArguments.push_back(eastl::move(Argument));
				Desc.mLayout.mArguments.push_back({Offset, Size, Alignment, Kind});
			}
			if (auto Status = Archive.Finish(); !Status)
			{
				return {{}, eastl::move(Status)};
			}
		}
		else
		{
			return {{}, RuntimeInvalid("Unsupported portable runtime definition version.")};
		}
		const auto Status = ValidateRuntimeDefinition(Desc);
		return {eastl::move(Desc), Status};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> FArdaRuntimeNodeDefinition::Create(
	    const FArdaRuntimeNodeDesc& Description)
	{
		const auto Portable = Serialize(Description);
		if (!Portable)
		{
			return {{}, Portable.mStatus};
		}
		const auto Desc = eastl::make_shared<const FArdaRuntimeNodeDesc>(Description);
		auto Registry = eastl::make_shared<FArdaNativeComputeRegistry>(true);
		FArdaNativeOperand Operand;
		Operand.mId = Desc->mName;
		Operand.mParameterSize = Desc->mLayout.mParameterSize;
		Operand.mSignature = Desc->mKernel.mSignature;
		Operand.mbRequiresRegisteredConversions = RequiresRegisteredConversions(*Desc);
		if (!Operand.mbRequiresRegisteredConversions)
		{
			UseArdaNativeDirectResources(Operand);
		}
		FArdaNativeResourceSlotsCreator Slots;
		for (size_t Index = 0; Index < Desc->mArguments.size(); ++Index)
		{
			const auto Kind = GetGraphicsBindingKind(*Desc, Index);
			const auto& Argument = Desc->mArguments[Index];
			if (Kind == EArdaNativeArgumentKind::Buffer)
			{
				Slots.AddBuffer(Argument.mName, Argument.mAccess);
			}
			else if (Kind == EArdaNativeArgumentKind::Surface)
			{
				Slots.AddTexture(Argument.mName, Argument.mAccess, Argument.mFormat);
			}
		}
		Operand.mResources = Slots.Finalize();
		auto Packaged = MakeArdaPackagedNativeOperand(Desc->mKernel, Desc->mLayout, eastl::move(Operand));
		if (!Packaged)
		{
			return {{}, eastl::move(Packaged.mStatus)};
		}
		if (auto Status = Registry->RegisterOperand(eastl::move(Packaged.mValue)); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		auto Definition = eastl::shared_ptr<FArdaDependencyNodeExecutable>(new FArdaDependencyNodeExecutable());
		Definition->mName = Desc->mName;
		Definition->mVersion = Desc->mVersion;
		Definition->mPortableDefinition = Portable.mValue;
		Definition->mKind = EArdaDependencyNodeKind::Native;
		Definition->mNativeRegistry = Registry;
		Definition->mSelectNativeExecutionMode =
		    [Desc, Registry](const FArdaNativeComputeContext& Context, eastl::string& ProviderId)
		{
			return Registry->QueryExecutionMode(Desc->mName.c_str(),
			    Context,
			    &ProviderId,
			    [&](EArdaNativeComputeRoute Route,
			        const IArdaNativeKernelSource*,
			        const eastl::vector<EArdaRHIResourceState>& RequiredStates)
			    {
				    size_t ResourceIndex = 0;
				    for (size_t Index = 0; Index < Desc->mArguments.size(); ++Index)
				    {
					    if (Desc->mLayout.mArguments[Index].mKind == EArdaNativeArgumentKind::Value)
					    {
						    continue;
					    }
					    const auto& Argument = Desc->mArguments[Index];
					    const auto State =
					        RequiredStates.empty() ? EArdaRHIResourceState::Unknown : RequiredStates[ResourceIndex];
					    if (!AllowsArdaInductorNativeResourceState(Route, Argument.mAccess, Argument.mState, State))
					    {
						    return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
						        ("Runtime operand resource '" + Argument.mName +
						            "' has an incompatible state for this route.")
						            .c_str());
					    }
					    ++ResourceIndex;
				    }
				    return FArdaRHIStatus{};
			    });
		};
		Definition->mParameterType = &ArdaDependencyParameterType<FArdaRuntimeNodeParameters>;
		Definition->mPreparedParameterType = Definition->mParameterType;
		Definition->mSerializeParameters = [Desc](const void* Value, FArdaGraphArchiveWriter& Archive)
		{
			WriteRuntimeParameters(Archive, *Desc, *static_cast<const FArdaRuntimeNodeParameters*>(Value));
			return Archive.GetStatus();
		};
		Definition->mDeserializeParameters =
		    [Desc](FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const void>>
		{
			auto Parameters = eastl::make_shared<FArdaRuntimeNodeParameters>();
			ReadRuntimeParameters(Archive, *Desc, *Parameters);
			auto Status = Archive.GetStatus() ? ValidateRuntimeParameters(*Desc, *Parameters) : Archive.GetStatus();
			return {eastl::move(Parameters), eastl::move(Status)};
		};
		Definition->mGetRequirements = [](const void*)
		{
			return FArdaDependencyNodeRequirements{};
		};
		Definition->mAddIntrinsicRequirements = [](FArdaDependencyNodeRequirements&)
		{
		};
		Definition->mResolveResources =
		    [Desc](FArdaDependencyResourceContext& Context,
		        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
		{
			auto Parameters = eastl::make_shared<FArdaRuntimeNodeParameters>(
			    *eastl::static_pointer_cast<const FArdaRuntimeNodeParameters>(Value));
			if (auto Status = ValidateRuntimeParameters(*Desc, *Parameters); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			for (size_t Index = 0; Index < Parameters->mArguments.size(); ++Index)
			{
				auto& Argument = Parameters->mArguments[Index];
				if (Argument.mSurface)
				{
					const auto* Resource = Context.Find(Argument.mSurface.GetHandle());
					if (!Resource || !Resource->mbTexture || Argument.mMipLevel >= Resource->mTexture.mMipLevels)
					{
						return {{}, RuntimeInvalid("Runtime argument references an invalid graph texture mip.")};
					}
					if (Desc->mArguments[Index].mAccess != EArdaComputeAccess::Read)
					{
						if (auto Status =
						        Context.Texture(Argument.mSurface, Desc->mArguments[Index].mName, Resource->mTexture);
						    !Status)
						{
							return {{}, eastl::move(Status)};
						}
					}
					continue;
				}
				if (!Argument.mBuffer)
				{
					continue;
				}
				const auto* Resource = Context.Find(Argument.mBuffer.GetHandle());
				if (!Resource || Resource->mbTexture ||
				    Resource->mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure ||
				    Argument.mRange.mByteOffset >= Resource->mBuffer.mByteSize)
				{
					return {{}, RuntimeInvalid("Runtime argument references an invalid graph buffer range.")};
				}
				const auto Available = Resource->mBuffer.mByteSize - Argument.mRange.mByteOffset;
				if (Argument.mRange.mByteSize == ArdaRHIWholeBuffer)
				{
					Argument.mRange.mByteSize = Available;
				}
				if (Argument.mRange.mByteSize > Available)
				{
					return {{}, RuntimeInvalid("Runtime argument exceeds its backing buffer.")};
				}
				if (Desc->mArguments[Index].mAccess != EArdaComputeAccess::Read)
				{
					if (auto Status =
					        Context.Buffer(Argument.mBuffer, Desc->mArguments[Index].mName, Resource->mBuffer);
					    !Status)
					{
						return {{}, eastl::move(Status)};
					}
				}
			}
			return {eastl::move(Parameters), {}};
		};
		Definition->mCanonicalKey = [Name = Desc->mName, Version = Desc->mVersion](const void* Value)
		{
			const auto& Parameters = *static_cast<const FArdaRuntimeNodeParameters*>(Value);
			FArdaDependencyKeyBuilder Key;
			Key.String(Name)
			    .Value(Version)
			    .Value(uint64_t(Parameters.mArguments.size()))
			    .Value(Parameters.mSharedMemoryBytes);
			for (size_t Axis = 0; Axis < 3; ++Axis)
			{
				Key.Value(Parameters.mGrid[Axis]).Value(Parameters.mBlock[Axis]);
			}
			for (const auto& Argument : Parameters.mArguments)
			{
				Key.Resource(Argument.mBuffer.GetHandle())
				    .BufferRange(Argument.mRange)
				    .Bytes(Argument.mValue.data(), Argument.mValue.size());
				if (Argument.mSurface)
				{
					Key.Resource(Argument.mSurface.GetHandle()).Value(Argument.mMipLevel);
				}
			}
			return Key.Build();
		};
		Definition->mPrepareDeclaration =
		    [Desc](FArdaRHIDeviceRef,
		        eastl::shared_ptr<const void> Value) -> TArdaRHIResult<eastl::shared_ptr<const void>>
		{
			const auto Status =
			    ValidateRuntimeParameters(*Desc, *eastl::static_pointer_cast<const FArdaRuntimeNodeParameters>(Value));
			return {eastl::move(Value), Status};
		};
		Definition->mPrepare = Definition->mPrepareDeclaration;
		Definition->mDescribe = [Desc](const void* Value) -> TArdaRHIResult<FArdaDependencyNodeDesc>
		{
			const auto& Parameters = *static_cast<const FArdaRuntimeNodeParameters*>(Value);
			FArdaDependencyNodeDesc Result;
			if (auto Status = ValidateRuntimeParameters(*Desc, Parameters); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				const auto& Argument = Parameters.mArguments[Index];
				if (Argument.mBuffer)
				{
					Result.mAccesses.push_back({Argument.mBuffer.GetHandle(),
					    static_cast<EArdaDependencyAccess>(Desc->mArguments[Index].mAccess),
					    Desc->mArguments[Index].mState,
					    Argument.mRange,
					    {}});
				}
				else if (Argument.mSurface)
				{
					Result.mAccesses.push_back({Argument.mSurface.GetHandle(),
					    static_cast<EArdaDependencyAccess>(Desc->mArguments[Index].mAccess),
					    Desc->mArguments[Index].mState,
					    {},
					    {Argument.mMipLevel, 1}});
				}
			}
			return {eastl::move(Result), {}};
		};
		Definition->mPrepareNative = [Desc, Registry](FArdaDependencyExecutionContext& Context,
		                                 const void* Value) -> TArdaRHIResult<FArdaNativeComputePlan>
		{
			const auto& Parameters = *static_cast<const FArdaRuntimeNodeParameters*>(Value);
			FArdaNativeInvocation Invocation;
			Invocation.mParameters.resize(Desc->mLayout.mParameterSize, 0);
			Invocation.mLaunch.mSharedMemoryBytes = Parameters.mSharedMemoryBytes;
			for (size_t Axis = 0; Axis < 3; ++Axis)
			{
				Invocation.mLaunch.mGridSize[Axis] = Parameters.mGrid[Axis];
				Invocation.mLaunch.mBlockSize[Axis] = Parameters.mBlock[Axis];
			}
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				const auto& Argument = Parameters.mArguments[Index];
				const auto& Abi = Desc->mLayout.mArguments[Index];
				switch (GetGraphicsBindingKind(*Desc, Index))
				{
				case EArdaNativeArgumentKind::Buffer:
				{
					const auto Buffer = Context.GetBuffer(Argument.mBuffer.GetHandle());
					if (!Buffer)
					{
						return {{}, RuntimeInvalid("Runtime kernel cannot resolve a declared buffer.")};
					}
					Invocation.mResources.push_back({Buffer, Argument.mRange});
					break;
				}
				case EArdaNativeArgumentKind::Surface:
				{
					const auto Surface = Context.GetTexture(Argument.mSurface.GetHandle());
					if (!Surface)
					{
						return {{}, RuntimeInvalid("Runtime kernel cannot resolve a declared texture.")};
					}
					Invocation.mResources.push_back({Surface, {}, Argument.mMipLevel});
					break;
				}
				case EArdaNativeArgumentKind::Value:
					std::memcpy(Invocation.mParameters.data() + Abi.mOffset,
					    Argument.mValue.data(),
					    Argument.mValue.size());
					break;
				}
			}
			return Context.PrepareNativeCompute(*Registry,
			    Desc->mName.c_str(),
			    Invocation,
			    EArdaNativeComputePreference::PreferNative);
		};
		return {eastl::move(Definition), {}};
	}

	FArdaRHIStatus FArdaRuntimeNodeDefinition::Register(const FArdaRuntimeNodeDesc& Description)
	{
		const auto Definition = Create(Description);
		return Definition ? FArdaNodeRegistry::Get().RegisterPortableDefinitions({Definition.mValue})
		                  : Definition.mStatus;
	}

	FArdaRHIStatus FArdaRuntimeNodeDefinition::Register(const eastl::vector<FArdaRuntimeNodeDesc>& Descriptions)
	{
		eastl::vector<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> Definitions;
		for (const auto& Description : Descriptions)
		{
			auto Definition = Create(Description);
			if (!Definition)
			{
				return Definition.mStatus;
			}
			Definitions.push_back(eastl::move(Definition.mValue));
		}
		return FArdaNodeRegistry::Get().RegisterPortableDefinitions(Definitions);
	}

	FArdaGraphOperation FArdaRuntimeNodeDefinition::Invoke(FArdaGraphEdit& Edit,
	    const eastl::string& Definition,
	    FArdaRuntimeNodeParameters Parameters,
	    eastl::string Name)
	{
		return Edit.Invoke(Definition, eastl::move(Parameters), eastl::move(Name));
	}
}
