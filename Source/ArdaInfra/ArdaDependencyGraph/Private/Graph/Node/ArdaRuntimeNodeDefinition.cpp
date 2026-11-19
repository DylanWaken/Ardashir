#include "Graph/Node/ArdaRuntimeNodeDefinition.h"

#include "Graph/Node/ArdaDependencyKey.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "CUDA/ArdaNativeCudaProvider.h"
#include "RHI/Providers/ArdaBackendProvider.h"
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

		FArdaCudaModuleKernelDesc GetKernelDesc(const FArdaRuntimeNodeDesc& Desc)
		{
			FArdaCudaModuleKernelDesc Kernel;
			Kernel.mEntryPoint = Desc.mEntryPoint;
			Kernel.mParameterSize = Desc.mParameterSize;
			Kernel.mParameterAlignment = Desc.mParameterAlignment;
			for (const auto& Argument : Desc.mArguments)
			{
				Kernel.mArguments.push_back(Argument.mAbi);
			}
			return Kernel;
		}

		TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>> ValidateRuntimeDefinition(
		    const FArdaRuntimeNodeDesc& Desc)
		{
			if (Desc.mName.empty() || !Desc.mVersion)
			{
				return {{}, RuntimeInvalid("A runtime node requires a name and a positive version.")};
			}
			eastl::unordered_set<eastl::string> Names;
			for (const auto& Argument : Desc.mArguments)
			{
				if (Argument.mName.empty() || !Names.insert(Argument.mName).second ||
				    Argument.mAccess > EArdaComputeAccess::ReadWrite ||
				    Argument.mAbi.mKind > EArdaCudaModuleArgumentKind::Buffer || Argument.mAbi.mOffset > UINT32_MAX ||
				    Argument.mAbi.mSize > UINT32_MAX || Argument.mAbi.mAlignment > UINT32_MAX)
				{
					return {{},
					    RuntimeInvalid("Runtime parameters require unique names and a supported portable ABI.")};
				}
			}
			auto Module = CreateArdaCudaModule(Desc.mModule);
			return Module ? CreateArdaCudaModuleKernel(Module.mValue, GetKernelDesc(Desc))
			              : TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>>{{}, Module.mStatus};
		}

		FArdaRHIStatus ValidateRuntimeParameters(const FArdaRuntimeNodeDesc& Desc,
		    const FArdaRuntimeNodeParameters& Parameters)
		{
			if (Parameters.mArguments.size() != Desc.mArguments.size())
			{
				return RuntimeInvalid("Runtime argument count does not match its definition.");
			}
			uint64_t Threads = 1;
			for (size_t Axis = 0; Axis < 3; ++Axis)
			{
				if (!Parameters.mGrid[Axis] || !Parameters.mBlock[Axis] || Parameters.mBlock[Axis] > 1024)
				{
					return RuntimeInvalid("Runtime launch dimensions must be nonzero and fit CUDA block limits.");
				}
				Threads *= Parameters.mBlock[Axis];
			}
			if (Threads > 1024)
			{
				return RuntimeInvalid("Runtime launch exceeds the CUDA thread block limit.");
			}
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				const auto& Argument = Parameters.mArguments[Index];
				const auto& Schema = Desc.mArguments[Index];
				if (Schema.mAbi.mKind == EArdaCudaModuleArgumentKind::Buffer)
				{
					if (!Argument.mBuffer || !Argument.mValue.empty() || !Argument.mRange.mByteSize)
					{
						return RuntimeInvalid("A runtime buffer parameter needs a nonempty declared buffer range.");
					}
				}
				else if (Argument.mBuffer || Argument.mValue.size() != Schema.mAbi.mSize ||
				    Argument.mRange.mByteOffset || Argument.mRange.mByteSize != ArdaRHIWholeBuffer)
				{
					return RuntimeInvalid("A runtime scalar needs exact ABI bytes and no resource reference.");
				}
			}
			return {};
		}

		void WriteRuntimeParameters(FArdaGraphArchiveWriter& Archive, const FArdaRuntimeNodeParameters& Parameters)
		{
			Archive.Fields(Parameters.mGrid, Parameters.mBlock, Parameters.mSharedMemoryBytes);
			Archive.WriteInteger(Parameters.mArguments.size(), 8);
			for (const auto& Argument : Parameters.mArguments)
			{
				Archive.Fields(Argument.mBuffer, Argument.mRange, Argument.mValue);
			}
		}

		void ReadRuntimeParameters(FArdaGraphArchiveReader& Archive, FArdaRuntimeNodeParameters& Parameters)
		{
			Archive.Fields(Parameters.mGrid, Parameters.mBlock, Parameters.mSharedMemoryBytes);
			const auto Count = Archive.ReadCount();
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				FArdaRuntimeNodeArgument Argument;
				Archive.Fields(Argument.mBuffer, Argument.mRange, Argument.mValue);
				Parameters.mArguments.push_back(eastl::move(Argument));
			}
		}
	}

	TArdaRHIResult<eastl::string> FArdaRuntimeNodeDefinition::Serialize(const FArdaRuntimeNodeDesc& Desc)
	{
		if (const auto Validated = ValidateRuntimeDefinition(Desc); !Validated)
		{
			return {{}, Validated.mStatus};
		}
		FArdaGraphArchiveWriter Archive;
		Archive.Fields(uint32_t{1},
		    Desc.mName,
		    Desc.mVersion,
		    Desc.mModule.mFormat,
		    Desc.mModule.mImage,
		    Desc.mModule.mBuild.mName,
		    Desc.mModule.mBuild.mIdentity,
		    Desc.mModule.mBuild.mbFastMath);
		Archive.WriteInteger(Desc.mModule.mBuild.mArchitectures.size(), 8);
		for (const auto& Architecture : Desc.mModule.mBuild.mArchitectures)
		{
			Archive.Fields(Architecture.mComputeCapability, Architecture.mbExact);
		}
		Archive.Fields(Desc.mEntryPoint, Desc.mParameterSize, Desc.mParameterAlignment);
		Archive.WriteInteger(Desc.mArguments.size(), 8);
		for (const auto& Argument : Desc.mArguments)
		{
			Archive.Fields(Argument.mName,
			    Argument.mAbi.mKind,
			    Argument.mAccess,
			    uint32_t(Argument.mAbi.mOffset),
			    uint32_t(Argument.mAbi.mSize),
			    uint32_t(Argument.mAbi.mAlignment));
		}
		return {Archive.GetData(), Archive.GetStatus()};
	}

	TArdaRHIResult<FArdaRuntimeNodeDesc> FArdaRuntimeNodeDefinition::Deserialize(const eastl::string& Bytes)
	{
		FArdaGraphArchiveReader Archive(Bytes);
		FArdaRuntimeNodeDesc Desc;
		uint32_t Version = 0;
		Archive.Fields(Version,
		    Desc.mName,
		    Desc.mVersion,
		    Desc.mModule.mFormat,
		    Desc.mModule.mImage,
		    Desc.mModule.mBuild.mName,
		    Desc.mModule.mBuild.mIdentity,
		    Desc.mModule.mBuild.mbFastMath);
		if (Version != 1)
		{
			return {{}, RuntimeInvalid("Unsupported portable runtime definition version.")};
		}
		const auto ArchitectureCount = Archive.ReadCount();
		for (size_t Index = 0; Index < ArchitectureCount && Archive.GetStatus(); ++Index)
		{
			FArdaCudaArchitecture Architecture;
			Archive.Fields(Architecture.mComputeCapability, Architecture.mbExact);
			Desc.mModule.mBuild.mArchitectures.push_back(Architecture);
		}
		Archive.Fields(Desc.mEntryPoint, Desc.mParameterSize, Desc.mParameterAlignment);
		const auto ArgumentCount = Archive.ReadCount();
		for (size_t Index = 0; Index < ArgumentCount && Archive.GetStatus(); ++Index)
		{
			FArdaRuntimeNodeArgumentDesc Argument;
			uint32_t Offset = 0;
			uint32_t Size = 0;
			uint32_t Alignment = 0;
			Archive.Fields(Argument.mName, Argument.mAbi.mKind, Argument.mAccess, Offset, Size, Alignment);
			Argument.mAbi.mOffset = Offset;
			Argument.mAbi.mSize = Size;
			Argument.mAbi.mAlignment = Alignment;
			Desc.mArguments.push_back(eastl::move(Argument));
		}
		if (auto Status = Archive.Finish(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		if (const auto Validated = ValidateRuntimeDefinition(Desc); !Validated)
		{
			return {{}, Validated.mStatus};
		}
		return {eastl::move(Desc), {}};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> FArdaRuntimeNodeDefinition::Create(
	    const FArdaRuntimeNodeDesc& Description)
	{
		const auto Entry = ValidateRuntimeDefinition(Description);
		if (!Entry)
		{
			return {{}, Entry.mStatus};
		}
		const auto Portable = Serialize(Description);
		if (!Portable)
		{
			return {{}, Portable.mStatus};
		}
		auto InvocationSchema = Description;
		// The module entry retains the interned artifact, and the portable definition owns its archive.
		// Invocation hooks only require the compact ABI, not another copy of the device image.
		InvocationSchema.mModule = {};
		const auto Desc = eastl::make_shared<const FArdaRuntimeNodeDesc>(eastl::move(InvocationSchema));
		auto Registry = eastl::make_shared<FArdaNativeComputeRegistry>();
		// Runtime archives can run on custom graphics modules registered by the application.
		// The CUDA adapter still qualifies the actual device and queue before preparing a kernel.
		eastl::vector<eastl::string> GraphicsApis;
		for (auto& Backend : EnumerateBackendModules())
		{
			GraphicsApis.push_back(eastl::move(Backend.mName));
		}
		if (auto Status = Registry->RegisterProvider(CreateArdaNativeCudaProvider(eastl::move(GraphicsApis))); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		if (auto Status = Registry->RegisterProvider(CreateArdaNativeCudaInGraphicsProvider()); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		FArdaNativeOperandDesc Operand;
		Operand.mId = Desc->mName;
		Operand.mParameterSize = Desc->mParameterSize;
		FArdaNativeCudaSourceDesc Source;
		Source.mEntry = Entry.mValue;
		for (const auto& Argument : Desc->mArguments)
		{
			if (Argument.mAbi.mKind == EArdaCudaModuleArgumentKind::Buffer)
			{
				Source.mPatches.push_back({static_cast<uint32_t>(Operand.mResources.size()), Argument.mAbi.mOffset, 1});
				Operand.mResources.push_back({Argument.mName, EArdaRHIResourceType::Buffer, Argument.mAccess});
			}
		}
		auto NativeSource = CreateArdaNativeCudaSource(eastl::move(Source));
		if (!NativeSource)
		{
			return {{}, eastl::move(NativeSource.mStatus)};
		}
		AddArdaNativeCudaVariants(Operand, {Desc->mEntryPoint, eastl::move(NativeSource.mValue), {}});
		if (auto Status = Registry->RegisterOperand(eastl::move(Operand)); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		auto Definition = eastl::shared_ptr<FArdaDependencyNodeExecutable>(new FArdaDependencyNodeExecutable());
		Definition->mName = Desc->mName;
		Definition->mVersion = Desc->mVersion;
		Definition->mPortableDefinition = Portable.mValue;
		Definition->mKind = EArdaDependencyNodeKind::Cuda;
		Definition->mParameterType = &ArdaDependencyParameterType<FArdaRuntimeNodeParameters>;
		Definition->mPreparedParameterType = Definition->mParameterType;
		Definition->mSerializeParameters = [](const void* Value, FArdaGraphArchiveWriter& Archive)
		{
			WriteRuntimeParameters(Archive, *static_cast<const FArdaRuntimeNodeParameters*>(Value));
			return Archive.GetStatus();
		};
		Definition->mDeserializeParameters =
		    [Desc](FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const void>>
		{
			auto Parameters = eastl::make_shared<FArdaRuntimeNodeParameters>();
			ReadRuntimeParameters(Archive, *Parameters);
			auto Status = Archive.GetStatus() ? ValidateRuntimeParameters(*Desc, *Parameters) : Archive.GetStatus();
			return {eastl::move(Parameters), eastl::move(Status)};
		};
		Definition->mGetRequirements = [Desc](const void* Value)
		{
			const auto& Parameters = *static_cast<const FArdaRuntimeNodeParameters*>(Value);
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mbRequireCuda = true;
			Requirements.mMinCudaThreadsPerBlock = Parameters.mBlock[0] * Parameters.mBlock[1] * Parameters.mBlock[2];
			Requirements.mMinCudaSharedMemoryBytes = Parameters.mSharedMemoryBytes;
			return Requirements;
		};
		Definition->mAddIntrinsicRequirements = [](FArdaDependencyNodeRequirements& Requirements)
		{
			Requirements.mbRequireCuda = true;
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
			// The common attachment/revision contract separately retains and compares the immutable
			// executable identity. Store invocation semantics here, as class-authored nodes do;
			// duplicating an entire module into every key makes large compiled libraries quadratic.
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
					    EArdaRHIResourceState::UnorderedAccess,
					    Argument.mRange,
					    {}});
				}
			}
			return {eastl::move(Result), {}};
		};
		Definition->mPrepareCuda = [Desc, Registry](FArdaDependencyExecutionContext& Context,
		                               const void* Value,
		                               FArdaCudaSequence& Sequence)
		{
			const auto& Parameters = *static_cast<const FArdaRuntimeNodeParameters*>(Value);
			FArdaNativeInvocation Invocation;
			Invocation.mParameters.resize(Desc->mParameterSize, 0);
			Invocation.mLaunch.mSharedMemoryBytes = Parameters.mSharedMemoryBytes;
			for (size_t Axis = 0; Axis < 3; ++Axis)
			{
				Invocation.mLaunch.mGridSize[Axis] = Parameters.mGrid[Axis];
				Invocation.mLaunch.mBlockSize[Axis] = Parameters.mBlock[Axis];
			}
			for (size_t Index = 0; Index < Parameters.mArguments.size(); ++Index)
			{
				const auto& Argument = Parameters.mArguments[Index];
				const auto& Schema = Desc->mArguments[Index];
				if (Schema.mAbi.mKind == EArdaCudaModuleArgumentKind::Buffer)
				{
					const auto Buffer = Context.GetBuffer(Argument.mBuffer.GetHandle());
					if (!Buffer)
					{
						return RuntimeInvalid("Runtime kernel cannot resolve a declared buffer.");
					}
					Invocation.mResources.push_back({Buffer, Argument.mRange});
				}
				else
				{
					std::memcpy(Invocation.mParameters.data() + Schema.mAbi.mOffset,
					    Argument.mValue.data(),
					    Argument.mValue.size());
				}
			}
			return Context.AppendNativeCudaCompute(Sequence, *Registry, Desc->mName.c_str(), Invocation);
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
