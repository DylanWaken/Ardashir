#include "Graph/Node/ArdaGraphPackage.h"

#include "FileOperations/ArdaFileOperations.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Node/ArdaRuntimeNodeDefinition.h"
#include <EASTL/algorithm.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus PackageInvalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		struct FArdaPackageBuffer
		{
			eastl::string mName;
			uint64_t mByteSize = 0;
			eastl::vector<uint8_t> mInitializer;
		};

		struct FArdaPackageArgument
		{
			uint32_t mBuffer = UINT32_MAX;
			uint64_t mOffset = 0;
			uint64_t mSize = 0;
			eastl::vector<uint8_t> mValue;
		};

		struct FArdaPackageCommand
		{
			uint8_t mKind = 0;
			uint32_t mKernel = 0;
			FArdaRuntimeNodeParameters mParameters;
			eastl::vector<FArdaPackageArgument> mArguments;
			uint32_t mSource = 0;
			uint32_t mDestination = 0;
			uint64_t mSourceOffset = 0;
			uint64_t mDestinationOffset = 0;
			uint64_t mByteSize = 0;
			eastl::vector<uint8_t> mPattern;
		};

		FArdaRuntimeNodeDesc MakePackageFillDefinition()
		{
			constexpr char Source[] = R"ptx(.version 6.0
.target sm_50
.address_size 64
.visible .entry ArdaPackageFill(.param .u64 output, .param .u64 count, .param .u32 pattern)
{
 .reg .pred p;
 .reg .b32 a, b, c, shift, value;
 .reg .b64 index, size, pointer, address;
 mov.u32 a, %ctaid.x;
 mov.u32 b, %ntid.x;
 mov.u32 c, %tid.x;
 mul.wide.u32 index, a, b;
 cvt.u64.u32 address, c;
 add.u64 index, index, address;
 ld.param.u64 size, [count];
 setp.ge.u64 p, index, size;
 @p bra done;
 ld.param.u64 pointer, [output];
 add.u64 address, pointer, index;
 cvt.u32.u64 shift, index;
 and.b32 shift, shift, 3;
 shl.b32 shift, shift, 3;
 ld.param.u32 value, [pattern];
 shr.u32 value, value, shift;
 st.global.u8 [address], value;
done:
 ret;
}
)ptx";
			FArdaRuntimeNodeDesc Desc;
			Desc.mName = "arda.runtime.fill.v1";
			Desc.mModule.mFormat = EArdaCudaModuleFormat::Ptx;
			Desc.mModule.mBuild.mName = Desc.mName;
			Desc.mModule.mBuild.mIdentity = Desc.mName;
			Desc.mModule.mBuild.mArchitectures.push_back({50, false});
			Desc.mModule.mImage.assign(Source, Source + sizeof(Source) - 1);
			Desc.mEntryPoint = "ArdaPackageFill";
			Desc.mParameterSize = 24;
			Desc.mParameterAlignment = 8;
			Desc.mArguments = {
			    {"Destination", {0, 8, 8, EArdaCudaModuleArgumentKind::Buffer}, EArdaComputeAccess::Write},
			    {"Count", {8, 8, 8, EArdaCudaModuleArgumentKind::Value}, EArdaComputeAccess::Read},
			    {"Pattern", {16, 4, 4, EArdaCudaModuleArgumentKind::Value}, EArdaComputeAccess::Read}};
			return Desc;
		}

		eastl::vector<uint8_t> PackageScalar(uint64_t Value, size_t Size)
		{
			eastl::vector<uint8_t> Bytes(Size);
			for (size_t Index = 0; Index < Size; ++Index)
			{
				Bytes[Index] = static_cast<uint8_t>(Value >> (Index * 8));
			}
			return Bytes;
		}

		FArdaRuntimeNodeDesc MakePackageCopyDefinition()
		{
			constexpr char Source[] = R"ptx(.version 6.0
.target sm_50
.address_size 64
.visible .entry ArdaPackageCopy(.param .u64 source, .param .u64 destination, .param .u64 count)
{
 .reg .pred p;
 .reg .b32 a, b, c, value;
 .reg .b64 index, size, src, dst, temporary;
 mov.u32 a, %ctaid.x;
 mov.u32 b, %ntid.x;
 mov.u32 c, %tid.x;
 mul.wide.u32 index, a, b;
 cvt.u64.u32 temporary, c;
 add.u64 index, index, temporary;
 ld.param.u64 size, [count];
 setp.ge.u64 p, index, size;
 @p bra done;
 ld.param.u64 src, [source];
 ld.param.u64 dst, [destination];
 add.u64 src, src, index;
 add.u64 dst, dst, index;
 ld.global.u8 value, [src];
 st.global.u8 [dst], value;
done:
 ret;
}
)ptx";
			FArdaRuntimeNodeDesc Desc;
			Desc.mName = "arda.runtime.copy.v1";
			Desc.mModule.mFormat = EArdaCudaModuleFormat::Ptx;
			Desc.mModule.mBuild.mName = Desc.mName;
			Desc.mModule.mBuild.mIdentity = Desc.mName;
			Desc.mModule.mBuild.mArchitectures.push_back({50, false});
			Desc.mModule.mImage.assign(Source, Source + sizeof(Source) - 1);
			Desc.mEntryPoint = "ArdaPackageCopy";
			Desc.mParameterSize = 24;
			Desc.mParameterAlignment = 8;
			Desc.mArguments = {{"Source", {0, 8, 8, EArdaCudaModuleArgumentKind::Buffer}, EArdaComputeAccess::Read},
			    {"Destination", {8, 8, 8, EArdaCudaModuleArgumentKind::Buffer}, EArdaComputeAccess::Write},
			    {"Count", {16, 8, 8, EArdaCudaModuleArgumentKind::Value}, EArdaComputeAccess::Read}};
			return Desc;
		}
	}

	struct FArdaGraphPackage::FArdaImpl
	{
		eastl::string mName;
		eastl::vector<FArdaPackageBuffer> mBuffers;
		eastl::vector<FArdaGraphPackageTensor> mTensors;
		eastl::vector<FArdaGraphPackageStateSlot> mStateSlots;
		eastl::vector<FArdaRuntimeNodeDesc> mKernels;
		eastl::vector<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> mDefinitions;
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mFillDefinition;
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mCopyDefinition;
		eastl::vector<FArdaPackageCommand> mCommands;

		bool HasRange(uint32_t Buffer, uint64_t Offset, uint64_t Size) const
		{
			return Buffer < mBuffers.size() && Size && Offset <= mBuffers[Buffer].mByteSize &&
			    Size <= mBuffers[Buffer].mByteSize - Offset;
		}

		FArdaRHIStatus Validate() const
		{
			if (mName.empty() || mBuffers.empty() || mCommands.empty())
			{
				return PackageInvalid("A compiled package requires a name, storage and an execution program.");
			}
			eastl::unordered_set<eastl::string> Names;
			for (const auto& Buffer : mBuffers)
			{
				if (Buffer.mName.empty() || !Names.insert(Buffer.mName).second || !Buffer.mByteSize ||
				    Buffer.mInitializer.size() > Buffer.mByteSize)
				{
					return PackageInvalid("Package buffer names, capacities or initializers are invalid.");
				}
			}
			Names.clear();
			eastl::unordered_set<uint32_t> StateBuffers;
			for (const auto& Slot : mStateSlots)
			{
				if (Slot.mName.empty() || !Names.insert(Slot.mName).second || Slot.mBuffer >= mBuffers.size() ||
				    !StateBuffers.insert(Slot.mBuffer).second || !mBuffers[Slot.mBuffer].mInitializer.empty())
				{
					return PackageInvalid("Model state slots require unique names and separate uninitialized storage.");
				}
				const auto Contiguous = MakeArdaContiguousTensorDesc(Slot.mDesc.mDataType, Slot.mDesc.mDimensions);
				const auto Range = GetArdaRHITensorByteRange(Slot.mDesc);
				if (!Contiguous || Slot.mDesc != Contiguous.mValue || !Range || !Range.mValue.mByteSize ||
				    Range.mValue.mByteSize != mBuffers[Slot.mBuffer].mByteSize)
				{
					return PackageInvalid(
					    "Model state slots require dense contiguous tensors spanning their entire buffer.");
				}
			}
			const auto IsImmutable = [&](uint32_t Buffer)
			{
				return !mBuffers[Buffer].mInitializer.empty() || StateBuffers.find(Buffer) != StateBuffers.end();
			};
			Names.clear();
			for (const auto& Tensor : mTensors)
			{
				if (Tensor.mName.empty() || !Names.insert(Tensor.mName).second || Tensor.mRole > 3 ||
				    Tensor.mBuffer >= mBuffers.size())
				{
					return PackageInvalid("Package tensor interface is invalid.");
				}
				if (auto Status = ValidateArdaRHITensorDesc(Tensor.mDesc,
				        mBuffers[Tensor.mBuffer].mByteSize,
				        bool(Tensor.mRole & 2));
				    !Status)
				{
					return Status;
				}
				if (Tensor.mRole && IsImmutable(Tensor.mBuffer))
				{
					return PackageInvalid("Checkpoint storage cannot also be a mutable package interface.");
				}
			}
			for (const auto& Command : mCommands)
			{
				if (Command.mKind == 0)
				{
					if (Command.mKernel >= mKernels.size() ||
					    Command.mArguments.size() != mKernels[Command.mKernel].mArguments.size())
					{
						return PackageInvalid("Package dispatch references an invalid kernel or argument list.");
					}
					uint64_t Threads = 1;
					for (size_t Axis = 0; Axis < 3; ++Axis)
					{
						if (!Command.mParameters.mGrid[Axis] || !Command.mParameters.mBlock[Axis] ||
						    Command.mParameters.mBlock[Axis] > 1024)
						{
							return PackageInvalid("Package dispatch has invalid launch dimensions.");
						}
						Threads *= Command.mParameters.mBlock[Axis];
					}
					if (Threads > 1024)
					{
						return PackageInvalid("Package dispatch exceeds the CUDA thread block limit.");
					}
					for (size_t Index = 0; Index < Command.mArguments.size(); ++Index)
					{
						const auto& Argument = Command.mArguments[Index];
						const auto& Schema = mKernels[Command.mKernel].mArguments[Index];
						if (Schema.mAbi.mKind == EArdaCudaModuleArgumentKind::Buffer)
						{
							if (!HasRange(Argument.mBuffer, Argument.mOffset, Argument.mSize) ||
							    !Argument.mValue.empty())
							{
								return PackageInvalid("Package kernel buffer range is invalid.");
							}
							if (Schema.mAccess != EArdaComputeAccess::Read && IsImmutable(Argument.mBuffer))
							{
								return PackageInvalid("Package kernels cannot overwrite checkpoint storage.");
							}
						}
						else if (Argument.mBuffer != UINT32_MAX || Argument.mOffset || Argument.mSize ||
						    Argument.mValue.size() != Schema.mAbi.mSize)
						{
							return PackageInvalid("Package scalar argument has an incompatible ABI representation.");
						}
					}
				}
				else
				{
					if (!HasRange(Command.mDestination, Command.mDestinationOffset, Command.mByteSize) ||
					    IsImmutable(Command.mDestination) || Command.mByteSize > uint64_t(UINT32_MAX) * 256)
					{
						return PackageInvalid("Package transfer destination is invalid or immutable.");
					}
					if (Command.mKind == 1)
					{
						if (Command.mPattern.size() != 1 && Command.mPattern.size() != 2 &&
						    Command.mPattern.size() != 4)
						{
							return PackageInvalid("Package fill pattern or extent is unsupported.");
						}
					}
					else if (Command.mKind != 2 ||
					    !HasRange(Command.mSource, Command.mSourceOffset, Command.mByteSize) ||
					    (Command.mSource == Command.mDestination &&
					        Command.mSourceOffset < Command.mDestinationOffset + Command.mByteSize &&
					        Command.mDestinationOffset < Command.mSourceOffset + Command.mByteSize))
					{
						return PackageInvalid("Package copy requires nonoverlapping valid buffer ranges.");
					}
				}
			}
			return {};
		}
	};

	const FArdaGraphTensor* FArdaGraphPackageInvocation::Find(const eastl::string& Name) const
	{
		const auto Found = eastl::find_if(mBindings.begin(),
		    mBindings.end(),
		    [&](const auto& Binding)
		    {
			    return Binding.mName == Name;
		    });
		return Found == mBindings.end() ? nullptr : &Found->mTensor;
	}

	FArdaGraphPackage::FArdaGraphPackage(eastl::shared_ptr<const FArdaImpl> Impl)
	    : mImpl(eastl::move(Impl))
	{
	}

	const eastl::string& FArdaGraphPackage::GetName() const noexcept
	{
		return mImpl->mName;
	}

	const eastl::vector<FArdaGraphPackageTensor>& FArdaGraphPackage::GetTensors() const noexcept
	{
		return mImpl->mTensors;
	}

	const eastl::vector<FArdaGraphPackageStateSlot>& FArdaGraphPackage::GetStateSlots() const noexcept
	{
		return mImpl->mStateSlots;
	}

	bool FArdaGraphPackage::HasStateDict() const noexcept
	{
		return eastl::all_of(mImpl->mStateSlots.begin(),
		    mImpl->mStateSlots.end(),
		    [&](const auto& Slot)
		    {
			    return mImpl->mBuffers[Slot.mBuffer].mInitializer.size() == Slot.mStorageByteSize;
		    });
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> FArdaGraphPackage::Deserialize(
	    const eastl::string& Bytes)
	{
		FArdaGraphArchiveReader Archive(Bytes);
		eastl::string Magic;
		Archive.ReadBytes(Magic, 8);
		uint32_t Version = 0;
		Archive.Read(Version);
		if (Magic != eastl::string("ARDAGPU\0", 8) || (Version != 1 && Version != 2))
		{
			return {{}, PackageInvalid("Unsupported compiled graph package format.")};
		}
		auto Impl = eastl::make_shared<FArdaImpl>();
		Archive.Read(Impl->mName);
		eastl::vector<FArdaCudaModuleDesc> Modules;
		const auto ModuleCount = Archive.ReadCount();
		for (size_t Index = 0; Index < ModuleCount && Archive.GetStatus(); ++Index)
		{
			FArdaCudaModuleDesc Module;
			uint8_t Format = 0;
			uint32_t Target = 0;
			Archive.Fields(Format, Target, Module.mBuild.mIdentity, Module.mImage);
			if (Format > 1 || !Target)
			{
				return {{}, PackageInvalid("Package module format or target architecture is invalid.")};
			}
			Module.mFormat = Format == 0 ? EArdaCudaModuleFormat::Ptx : EArdaCudaModuleFormat::Cubin;
			Module.mBuild.mName = Impl->mName;
			Module.mBuild.mArchitectures.push_back({Target, Format == 1});
			Modules.push_back(eastl::move(Module));
		}
		const auto BufferCount = Archive.ReadCount();
		for (size_t Index = 0; Index < BufferCount && Archive.GetStatus(); ++Index)
		{
			FArdaPackageBuffer Buffer;
			Archive.Fields(Buffer.mName, Buffer.mByteSize, Buffer.mInitializer);
			Impl->mBuffers.push_back(eastl::move(Buffer));
		}
		const auto TensorCount = Archive.ReadCount();
		for (size_t Index = 0; Index < TensorCount && Archive.GetStatus(); ++Index)
		{
			FArdaGraphPackageTensor Tensor;
			Archive.Fields(Tensor.mName,
			    Tensor.mBuffer,
			    Tensor.mRole,
			    Tensor.mDesc.mDataType,
			    Tensor.mDesc.mDimensions,
			    Tensor.mDesc.mStrides,
			    Tensor.mDesc.mByteOffset);
			Impl->mTensors.push_back(eastl::move(Tensor));
		}
		const auto KernelCount = Archive.ReadCount();
		for (size_t Index = 0; Index < KernelCount && Archive.GetStatus(); ++Index)
		{
			FArdaRuntimeNodeDesc Kernel;
			uint32_t Module = 0;
			Archive.Fields(Kernel.mName, Module, Kernel.mEntryPoint, Kernel.mParameterSize, Kernel.mParameterAlignment);
			if (Module >= Modules.size())
			{
				return {{}, PackageInvalid("Package kernel references an unknown module.")};
			}
			Kernel.mModule = Modules[Module];
			const auto ArgumentCount = Archive.ReadCount();
			for (size_t ArgumentIndex = 0; ArgumentIndex < ArgumentCount && Archive.GetStatus(); ++ArgumentIndex)
			{
				FArdaRuntimeNodeArgumentDesc Argument;
				uint32_t Offset = 0;
				uint32_t Size = 0;
				uint32_t Alignment = 0;
				Archive.Fields(Argument.mName, Argument.mAbi.mKind, Argument.mAccess, Offset, Size, Alignment);
				Argument.mAbi.mOffset = Offset;
				Argument.mAbi.mSize = Size;
				Argument.mAbi.mAlignment = Alignment;
				Kernel.mArguments.push_back(eastl::move(Argument));
			}
			Impl->mKernels.push_back(eastl::move(Kernel));
		}
		const auto CommandCount = Archive.ReadCount();
		for (size_t Index = 0; Index < CommandCount && Archive.GetStatus(); ++Index)
		{
			FArdaPackageCommand Command;
			Archive.Read(Command.mKind);
			if (Command.mKind == 0)
			{
				Archive.Fields(Command.mKernel,
				    Command.mParameters.mGrid,
				    Command.mParameters.mBlock,
				    Command.mParameters.mSharedMemoryBytes);
				const auto ArgumentCount = Archive.ReadCount();
				for (size_t ArgumentIndex = 0; ArgumentIndex < ArgumentCount && Archive.GetStatus(); ++ArgumentIndex)
				{
					FArdaPackageArgument Argument;
					Archive.Fields(Argument.mBuffer, Argument.mOffset, Argument.mSize, Argument.mValue);
					Command.mArguments.push_back(eastl::move(Argument));
				}
			}
			else if (Command.mKind == 1)
			{
				Archive.Fields(Command.mDestination, Command.mDestinationOffset, Command.mByteSize, Command.mPattern);
			}
			else if (Command.mKind == 2)
			{
				Archive.Fields(Command.mSource,
				    Command.mSourceOffset,
				    Command.mDestination,
				    Command.mDestinationOffset,
				    Command.mByteSize);
			}
			else
			{
				return {{}, PackageInvalid("Package contains an unsupported host command.")};
			}
			Impl->mCommands.push_back(eastl::move(Command));
		}
		if (Version == 2)
		{
			const auto SlotCount = Archive.ReadCount();
			for (size_t Index = 0; Index < SlotCount && Archive.GetStatus(); ++Index)
			{
				FArdaGraphPackageStateSlot Slot;
				Archive.Fields(Slot.mName,
				    Slot.mBuffer,
				    Slot.mDesc.mDataType,
				    Slot.mDesc.mDimensions,
				    Slot.mDesc.mStrides,
				    Slot.mDesc.mByteOffset);
				Impl->mStateSlots.push_back(eastl::move(Slot));
			}
		}
		if (auto Status = Archive.Finish(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		if (auto Status = Impl->Validate(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		for (auto& Tensor : Impl->mTensors)
		{
			Tensor.mStorageByteSize = Impl->mBuffers[Tensor.mBuffer].mByteSize;
		}
		for (auto& Slot : Impl->mStateSlots)
		{
			Slot.mStorageByteSize = Impl->mBuffers[Slot.mBuffer].mByteSize;
		}
		auto Definitions = Impl->mKernels;
		if (eastl::any_of(Impl->mCommands.begin(),
		        Impl->mCommands.end(),
		        [](const auto& Command)
		        {
			        return Command.mKind == 1;
		        }))
		{
			Definitions.push_back(MakePackageFillDefinition());
		}
		if (eastl::any_of(Impl->mCommands.begin(),
		        Impl->mCommands.end(),
		        [](const auto& Command)
		        {
			        return Command.mKind == 2;
		        }))
		{
			Definitions.push_back(MakePackageCopyDefinition());
		}
		if (auto Status = FArdaRuntimeNodeDefinition::Register(Definitions); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		for (size_t Index = 0; Index < Definitions.size(); ++Index)
		{
			const auto Definition = FArdaNodeRegistry::Get().Find(Definitions[Index].mName);
			const auto Expected = FArdaRuntimeNodeDefinition::Serialize(Definitions[Index]);
			if (!Definition || !Expected || Definition->mPortableDefinition != Expected.mValue)
			{
				return {{}, PackageInvalid("Package registry changed while retaining its immutable definitions.")};
			}
			if (Index < Impl->mKernels.size())
			{
				Impl->mDefinitions.push_back(Definition);
			}
			else if (Definitions[Index].mName == "arda.runtime.fill.v1")
			{
				Impl->mFillDefinition = Definition;
			}
			else
			{
				Impl->mCopyDefinition = Definition;
			}
		}
		// Validation descriptors are no longer needed once immutable executable definitions are retained.
		Impl->mKernels.clear();
		Impl->mKernels.shrink_to_fit();
		return {eastl::shared_ptr<const FArdaGraphPackage>(new FArdaGraphPackage(eastl::move(Impl))), {}};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> FArdaGraphPackage::Load(
	    const std::filesystem::path& Path)
	{
		eastl::string Data;
		if (fileops::ReadBinaryFile(Path, Data, FArdaGraphArchiveWriter::mMaximumBytes) != fileops::EArdaFileReadResult::Success)
		{
			return {{}, PackageInvalid("Package file is unreadable or exceeds the archive size limit.")};
		}
		return Deserialize(Data);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> FArdaGraphPackage::DeserializeStateDict(
	    const eastl::string& Bytes) const
	{
		FArdaGraphArchiveReader Archive(Bytes);
		eastl::string Magic;
		Archive.ReadBytes(Magic, 8);
		uint32_t Version = 0;
		Archive.Read(Version);
		if (Magic != eastl::string("ARDASTAT", 8) || Version != 1)
		{
			return {{}, PackageInvalid("Unsupported model state dictionary format.")};
		}
		const auto Count = Archive.ReadCount();
		if (Count != mImpl->mStateSlots.size())
		{
			return {{}, PackageInvalid("State dictionary keys must match every model state slot exactly.")};
		}
		eastl::unordered_map<eastl::string, size_t> SlotIndices;
		for (size_t Index = 0; Index < mImpl->mStateSlots.size(); ++Index)
		{
			SlotIndices.emplace(mImpl->mStateSlots[Index].mName, Index);
		}
		eastl::vector<eastl::vector<uint8_t>> State(Count);
		eastl::unordered_set<eastl::string> Seen;
		for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
		{
			eastl::string Name;
			EArdaRHIDataType DataType = EArdaRHIDataType::Unknown;
			eastl::vector<uint64_t> Dimensions;
			eastl::vector<uint8_t> Data;
			Archive.Fields(Name, DataType, Dimensions, Data);
			if (!Archive.GetStatus())
			{
				return {{}, Archive.GetStatus()};
			}
			const auto Found = SlotIndices.find(Name);
			if (Found == SlotIndices.end() || !Seen.insert(Name).second)
			{
				return {{}, PackageInvalid("State dictionary contains an unknown or duplicate key.")};
			}
			const auto& Slot = mImpl->mStateSlots[Found->second];
			if (DataType != Slot.mDesc.mDataType || Dimensions != Slot.mDesc.mDimensions ||
			    Data.size() != Slot.mStorageByteSize)
			{
				return {{},
				    PackageInvalid(
				        "State dictionary tensor dtype, dimensions or byte count does not match the model.")};
			}
			State[Found->second] = eastl::move(Data);
		}
		if (auto Status = Archive.Finish(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		// Publish only after the whole checkpoint validates. Existing packages and graph initializers
		// keep their previous bytes, including when callers replace the result in their own handle.
		auto Impl = eastl::make_shared<FArdaImpl>(*mImpl);
		for (size_t Index = 0; Index < State.size(); ++Index)
		{
			Impl->mBuffers[Impl->mStateSlots[Index].mBuffer].mInitializer = eastl::move(State[Index]);
		}
		return {eastl::shared_ptr<const FArdaGraphPackage>(new FArdaGraphPackage(eastl::move(Impl))), {}};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> FArdaGraphPackage::LoadStateDict(
	    const std::filesystem::path& Path) const
	{
		eastl::string Data;
		if (fileops::ReadBinaryFile(Path, Data, FArdaGraphArchiveWriter::mMaximumBytes) != fileops::EArdaFileReadResult::Success)
		{
			return {{}, PackageInvalid("State dict file is unreadable or exceeds the archive size limit.")};
		}
		return DeserializeStateDict(Data);
	}

	TArdaRHIResult<FArdaGraphPackageInvocation> FArdaGraphPackage::Invoke(FArdaGraphEdit& Edit,
	    const eastl::vector<FArdaGraphPackageBinding>& Bindings,
	    eastl::string Name) const
	{
		const auto Fail = [&](FArdaRHIStatus Status) -> TArdaRHIResult<FArdaGraphPackageInvocation>
		{
			Edit.ReportError(Status);
			return {{}, eastl::move(Status)};
		};
		if (!Edit.CanRecord())
		{
			return {{}, Edit.GetStatus()};
		}
		if (!HasStateDict())
		{
			return Fail(
			    PackageInvalid("Model state is not loaded; call LoadStateDict before invoking this descriptor."));
		}
		Name = Name.empty() ? Edit.MakeName(mImpl->mName) : eastl::move(Name);
		eastl::vector<FArdaGraphBuffer> Buffers(mImpl->mBuffers.size());
		eastl::vector<uint64_t> BaseOffsets(Buffers.size(), 0);
		eastl::unordered_set<eastl::string> BoundNames;
		for (const auto& Binding : Bindings)
		{
			const auto Found = eastl::find_if(mImpl->mTensors.begin(),
			    mImpl->mTensors.end(),
			    [&](const auto& Tensor)
			    {
				    return Tensor.mName == Binding.mName && Tensor.mRole;
			    });
			if (Found == mImpl->mTensors.end() || !BoundNames.insert(Binding.mName).second ||
			    !Binding.mTensor.mBuffer || Binding.mTensor.mDesc.mDataType != Found->mDesc.mDataType ||
			    Binding.mTensor.mDesc.mDimensions != Found->mDesc.mDimensions ||
			    Binding.mTensor.mDesc.mStrides != Found->mDesc.mStrides ||
			    Binding.mTensor.mDesc.mByteOffset < Found->mDesc.mByteOffset)
			{
				return Fail(PackageInvalid("Package tensor binding has an unknown name or incompatible shape/layout."));
			}
			const auto Buffer = Found->mBuffer;
			const auto Offset = Binding.mTensor.mDesc.mByteOffset - Found->mDesc.mByteOffset;
			const auto* Resource = Edit.GetGraph().FindResource(Binding.mTensor.mBuffer.GetHandle());
			if (!Resource || Resource->mbTexture || Offset > Resource->mBuffer.mByteSize ||
			    mImpl->mBuffers[Buffer].mByteSize > Resource->mBuffer.mByteSize - Offset ||
			    (Buffers[Buffer] &&
			        (Buffers[Buffer].GetHandle() != Binding.mTensor.mBuffer.GetHandle() ||
			            BaseOffsets[Buffer] != Offset)))
			{
				return Fail(PackageInvalid("Package tensor binding has insufficient or inconsistent backing storage."));
			}
			if (auto Status = ValidateArdaRHITensorDesc(Binding.mTensor.mDesc,
			        Resource->mBuffer.mByteSize,
			        bool(Found->mRole & 2));
			    !Status)
			{
				return Fail(eastl::move(Status));
			}
			for (size_t Other = 0; Other < Buffers.size(); ++Other)
			{
				if (Other != Buffer && Buffers[Other] &&
				    Buffers[Other].GetHandle() == Binding.mTensor.mBuffer.GetHandle() &&
				    Offset < BaseOffsets[Other] + mImpl->mBuffers[Other].mByteSize &&
				    BaseOffsets[Other] < Offset + mImpl->mBuffers[Buffer].mByteSize)
				{
					return Fail(PackageInvalid("Distinct package buffers require nonoverlapping bound storage."));
				}
			}
			Buffers[Buffer] = Binding.mTensor.mBuffer;
			BaseOffsets[Buffer] = Offset;
		}
		for (const auto& Tensor : mImpl->mTensors)
		{
			if ((Tensor.mRole & 1) && BoundNames.find(Tensor.mName) == BoundNames.end())
			{
				return Fail(PackageInvalid("Package invocation is missing a required input tensor."));
			}
		}
		for (size_t Index = 0; Index < Buffers.size(); ++Index)
		{
			if (!Buffers[Index])
			{
				FArdaRHIBufferDesc Desc;
				Desc.mByteSize = mImpl->mBuffers[Index].mByteSize;
				Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
				Desc.mbCudaInterop = true;
				Buffers[Index] = mImpl->mBuffers[Index].mInitializer.empty()
				    ? AllocateBuffer(Edit, Desc, Name + "/" + mImpl->mBuffers[Index].mName)
				    : AllocateInitializedBuffer(Edit,
				          Desc,
				          mImpl->mBuffers[Index].mInitializer,
				          Name + "/" + mImpl->mBuffers[Index].mName);
			}
		}
		FArdaGraphOperation Previous;
		for (size_t Index = 0; Index < mImpl->mCommands.size() && Edit.GetStatus(); ++Index)
		{
			const auto& Command = mImpl->mCommands[Index];
			FArdaGraphOperation Operation;
			const auto OperationName = Name + "/command" + eastl::to_string(Index);
			if (Command.mKind == 0)
			{
				auto Parameters = Command.mParameters;
				for (const auto& Argument : Command.mArguments)
				{
					FArdaRuntimeNodeArgument Value;
					Value.mValue = Argument.mValue;
					if (Argument.mBuffer != UINT32_MAX)
					{
						Value.mBuffer = Buffers[Argument.mBuffer];
						Value.mRange = {BaseOffsets[Argument.mBuffer] + Argument.mOffset, Argument.mSize};
					}
					Parameters.mArguments.push_back(eastl::move(Value));
				}
				Operation = Edit.Invoke(mImpl->mDefinitions[Command.mKernel], eastl::move(Parameters), OperationName);
			}
			else if (Command.mKind == 1)
			{
				uint32_t Pattern = 0;
				for (uint32_t Byte = 0; Byte < 4; ++Byte)
				{
					Pattern |= uint32_t(Command.mPattern[Byte % Command.mPattern.size()]) << (Byte * 8);
				}
				FArdaRuntimeNodeParameters Parameters;
				Parameters.mGrid[0] = uint32_t((Command.mByteSize - 1) / 256 + 1);
				Parameters.mBlock[0] = 256;
				Parameters.mArguments = {
				    {Buffers[Command.mDestination],
				        {BaseOffsets[Command.mDestination] + Command.mDestinationOffset, Command.mByteSize},
				        {}},
				    {{}, {}, PackageScalar(Command.mByteSize, 8)},
				    {{}, {}, PackageScalar(Pattern, 4)}};
				Operation = Edit.Invoke(mImpl->mFillDefinition, eastl::move(Parameters), OperationName);
			}
			else
			{
				FArdaRuntimeNodeParameters Parameters;
				Parameters.mGrid[0] = uint32_t((Command.mByteSize - 1) / 256 + 1);
				Parameters.mBlock[0] = 256;
				Parameters.mArguments = {{Buffers[Command.mSource],
				                             {BaseOffsets[Command.mSource] + Command.mSourceOffset, Command.mByteSize},
				                             {}},
				    {Buffers[Command.mDestination],
				        {BaseOffsets[Command.mDestination] + Command.mDestinationOffset, Command.mByteSize},
				        {}},
				    {{}, {}, PackageScalar(Command.mByteSize, 8)}};
				Operation = Edit.Invoke(mImpl->mCopyDefinition, eastl::move(Parameters), OperationName);
			}
			if (Previous && Operation)
			{
				Edit.Order(Previous, Operation);
			}
			Previous = Operation;
		}
		if (!Edit.GetStatus())
		{
			return {{}, Edit.GetStatus()};
		}
		FArdaGraphPackageInvocation Result;
		for (const auto& Tensor : mImpl->mTensors)
		{
			if (Tensor.mRole)
			{
				auto Desc = Tensor.mDesc;
				Desc.mByteOffset += BaseOffsets[Tensor.mBuffer];
				Result.mBindings.push_back({Tensor.mName, {Buffers[Tensor.mBuffer], eastl::move(Desc)}});
			}
		}
		return {eastl::move(Result), {}};
	}
}
