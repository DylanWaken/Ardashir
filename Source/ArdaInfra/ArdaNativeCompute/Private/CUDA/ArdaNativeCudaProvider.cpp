#include "CUDA/ArdaNativeCudaProvider.h"

#include "ArdaNativeCudaCompiler.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace arda
{
	namespace
	{
		constexpr char CudaCppFormat[] = "cuda-cpp";
		constexpr char CudaLinkedFormat[] = "cuda-linked";
		constexpr char CudaExternalFormat[] = "cuda-external";

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		FArdaRHIStatus Unsupported(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
		}

		FArdaRHIStatus CheckCudaQueue(const FArdaCudaCapabilities& Capabilities, EArdaRHIQueueType Queue)
		{
			if (!Capabilities)
			{
				return Unsupported(Capabilities.mUnavailableReason.c_str());
			}
			if (Queue != EArdaRHIQueueType::Graphics &&
			    (Queue != EArdaRHIQueueType::Compute || Capabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG))
			{
				return Unsupported("This CUDA execution mode does not support the selected command queue.");
			}
			return {};
		}

		FArdaRHIStatus CheckCudaAbi(const FArdaNativeOperandDesc& Operand, const FArdaNativeCudaSourceDesc& Source)
		{
			const auto* Schema = Source.mEntry ? Source.mEntry->GetModuleKernel()
			    : !Source.mCudaCpp.empty()     ? &Source.mModuleKernel
			                                   : nullptr;
			const auto Signature = Source.mEntry ? Source.mEntry->GetSignature()
			    : Source.mExternalCall           ? Source.mExternalCall->GetSignature()
			                                     : FArdaCudaKernelSignature{};
			const size_t Size = Schema ? Schema->mParameterSize : Signature.mSize;
			const size_t Alignment = Schema ? Schema->mParameterAlignment : Signature.mAlignment;
			if (!Size || Size != Operand.mParameterSize || !Alignment || (Alignment & (Alignment - 1)) ||
			    (!Schema && (!Signature.mbSupported || !Signature.mType)))
			{
				return Invalid("CUDA source ABI does not match the operand's parameter storage.");
			}

			for (const auto& Patch : Source.mPatches)
			{
				if (Patch.mBindingIndex >= Operand.mResources.size() || Patch.mOffset > Size ||
				    sizeof(uint64_t) > Size - Patch.mOffset || !Patch.mAlignment ||
				    (Patch.mAlignment & (Patch.mAlignment - 1)))
				{
					return Invalid("The CUDA resource patch is outside the source ABI or has invalid alignment.");
				}
				if (!Schema)
				{
					continue;
				}
				const auto Kind = Operand.mResources[Patch.mBindingIndex].mType == EArdaRHIResourceType::Buffer
				    ? EArdaCudaModuleArgumentKind::Buffer
				    : EArdaCudaModuleArgumentKind::Surface;
				for (const auto& Argument : Schema->mArguments)
				{
					if (Argument.mOffset == Patch.mOffset && Argument.mKind != Kind)
					{
						return Invalid("CUDA module argument kind does not match its resource slot.");
					}
				}
			}
			if (Schema)
			{
				FArdaCudaKernel Kernel;
				Kernel.mPatches = Source.mPatches;
				return ValidateArdaCudaModuleParameters(*Schema, Kernel);
			}
			return {};
		}

		class FArdaNativeCudaSource final : public IArdaNativeKernelSource
		{
		public:
			FArdaNativeCudaSource(FArdaNativeCudaSourceDesc Desc, const char* Format)
			    : mDesc(eastl::move(Desc)),
			      mFormat(Format)
			{
			}

			const char* GetProviderId() const noexcept override
			{
				return ArdaNativeCudaProviderId;
			}

			const char* GetFormat() const noexcept override
			{
				return mFormat;
			}

			TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>> ResolveEntry(uint32_t DeviceCapability) const
			{
				if (mDesc.mCudaCpp.empty())
				{
					return {mDesc.mEntry, {}};
				}
				const auto Target = mDesc.mComputeCapability ? mDesc.mComputeCapability : DeviceCapability;
				if (!FArdaCudaArchitecture{Target}.Supports(DeviceCapability))
				{
					return {{}, Unsupported("The CUDA C++ target does not support the device's CUDA capability.")};
				}

				std::lock_guard Lock(mMutex);
				if (const auto Cached = mCompiledEntries.find(Target); Cached != mCompiledEntries.end())
				{
					return {Cached->second, {}};
				}
				auto Image = CompileArdaNativeCudaSource(mDesc.mCudaCpp, Target);
				if (!Image)
				{
					return {{}, eastl::move(Image.mStatus)};
				}
				auto Module = CreateArdaCudaModule(eastl::move(Image.mValue));
				if (!Module)
				{
					return {{}, eastl::move(Module.mStatus)};
				}
				auto Entry = CreateArdaCudaModuleKernel(eastl::move(Module.mValue), mDesc.mModuleKernel);
				if (Entry)
				{
					mCompiledEntries.emplace(Target, Entry.mValue);
				}
				return Entry;
			}

			const FArdaNativeCudaSourceDesc mDesc;

		private:
			const char* const mFormat;
			mutable std::mutex mMutex;
			mutable std::unordered_map<uint32_t, eastl::shared_ptr<const IArdaCudaKernelEntry>> mCompiledEntries;
		};

		class FArdaNativeCudaWork final : public IArdaNativePreparedWork
		{
		public:
			explicit FArdaNativeCudaWork(eastl::shared_ptr<const FArdaCudaDispatchPlan> Plan)
			    : mPlan(eastl::move(Plan))
			{
			}

			FArdaRHIStatus Record(IArdaRHICommandList& Commands) const override
			{
				if (Commands.GetDevice() != mPlan->mDevice.Get())
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
					    "CUDA work belongs to a different device.");
				}
				if (Commands.GetQueueType() != mPlan->mQueue)
				{
					return Invalid("CUDA work was prepared for a different command queue.");
				}
				return Commands.DispatchCuda(mPlan->mDispatch);
			}

			const eastl::shared_ptr<const FArdaCudaDispatchPlan> mPlan;
		};

		class FArdaNativeCudaProvider final : public IArdaNativeComputeProvider
		{
		public:
			FArdaNativeCudaProvider(const char* Id, eastl::vector<eastl::string> GraphicsApis)
			{
				mDesc.mId = Id;
				mDesc.mGraphicsApis = eastl::move(GraphicsApis);
				mDesc.mSourceFormats = {CudaLinkedFormat, "ptx", "cubin", CudaExternalFormat};
				if (HasArdaNativeCudaCompiler())
				{
					mDesc.mSourceFormats.push_back(CudaCppFormat);
				}
			}

			const FArdaNativeProviderDesc& GetDesc() const noexcept override
			{
				return mDesc;
			}

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext& Context,
			    const FArdaNativeOperandDesc& Operand,
			    const IArdaNativeKernelSource& Source) const override
			{
				const auto* CudaSource = dynamic_cast<const FArdaNativeCudaSource*>(&Source);
				if (!Context.mDevice)
				{
					return Invalid("CUDA support queries require a device.");
				}
				if (!CudaSource)
				{
					return Unsupported("The CUDA provider needs source metadata created by CreateArdaNativeCudaSource.");
				}
				const auto Capabilities = Context.mDevice->GetCudaCapabilities();
				if (auto Status = CheckExecutionMode(Context, Capabilities); !Status)
				{
					return Status;
				}
				for (const auto& Slot : Operand.mResources)
				{
					if (Slot.mType == EArdaRHIResourceType::Buffer)
					{
						continue;
					}
					if (Slot.mType != EArdaRHIResourceType::Texture)
					{
						return Unsupported("CUDA supports buffer and surface slots.");
					}
					if (!Capabilities.mbSurfaceAccess)
					{
						return Unsupported(Capabilities.mSurfaceUnavailableReason.c_str());
					}
					if (Slot.mFormat != EArdaRHIFormat::Unknown && !GetArdaCudaFormatInfo(Slot.mFormat).mChannels)
					{
						return Unsupported("The declared texture format cannot supply a CUDA surface.");
					}
				}

				const auto& Desc = CudaSource->mDesc;
				if (auto Status = CheckCudaAbi(Operand, Desc); !Status)
				{
					return Status;
				}
				if (Desc.mExternalCall)
				{
					return Desc.mExternalCall->CheckSupport(Capabilities);
				}
				if (Desc.mEntry)
				{
					const auto* Module = Desc.mEntry->GetModule();
					for (const auto& Target : Desc.mEntry->GetBuildInfo().mArchitectures)
					{
						const bool bVirtual =
						    Module && Module->GetDesc().mFormat == EArdaCudaModuleFormat::Ptx && !Target.mbExact;
						if (bVirtual ? Target.mComputeCapability <= Capabilities.mComputeCapability
						             : Target.Supports(Capabilities.mComputeCapability))
						{
							return {};
						}
					}
					return Unsupported("No compiled kernel artifact supports this CUDA architecture.");
				}
				const auto Target = Desc.mComputeCapability ? Desc.mComputeCapability : Capabilities.mComputeCapability;
				if (!FArdaCudaArchitecture{Target}.Supports(Capabilities.mComputeCapability))
				{
					return Unsupported("The CUDA C++ target does not support the device's CUDA capability.");
				}
				return CheckArdaNativeCudaCompilerTarget(Target);
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext& Context,
			    const FArdaNativeResourceSlot& Slot,
			    const FArdaNativeResourceView& View) const override
			{
				if (!Context.mDevice || !View.mResource || View.mResource->GetResourceType() != Slot.mType)
				{
					return {{}, Invalid("CUDA handoff requires a device and a resource of the declared type.")};
				}
				const auto Capabilities = Context.mDevice->GetCudaCapabilities();
				if (auto Status = CheckExecutionMode(Context, Capabilities); !Status)
				{
					return {{}, eastl::move(Status)};
				}

				const auto Info = View.mResource->GetCudaResourceInfo();
				if (const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(View.mResource.Get()))
				{
					const auto& Desc = Buffer->GetDesc();
					const auto& Range = View.mBufferRange;
					if (Range.mByteOffset >= Desc.mByteSize ||
					    (Range.mByteSize != ArdaRHIWholeBuffer &&
					        (!Range.mByteSize || Range.mByteSize > Desc.mByteSize - Range.mByteOffset)))
					{
						return {{}, Invalid("CUDA buffer byte range is out of bounds.")};
					}
					const bool bDirect =
					    Info.mbSharingEnabled && Info.mSupportedRepresentation == EArdaCudaRepresentation::LinearBuffer;
					return {{bDirect ? EArdaNativeHandoff::Direct : EArdaNativeHandoff::ConversionRequired,
					            EArdaRHIResourceState::UnorderedAccess}, {}};
				}
				if (const auto* Texture = dynamic_cast<const IArdaRHITexture*>(View.mResource.Get()))
				{
					const auto& Desc = Texture->GetDesc();
					if (View.mMipLevel >= Desc.mMipLevels)
					{
						return {{}, Invalid("CUDA surface mip is out of bounds.")};
					}
					if (!Capabilities.mbSurfaceAccess)
					{
						return {{}, Unsupported(Capabilities.mSurfaceUnavailableReason.c_str())};
					}
					if (!Capabilities.mbLayeredSurfaceAccess &&
					    (Desc.mDimension == EArdaRHITextureDimension::Texture1DArray ||
					        Desc.mDimension == EArdaRHITextureDimension::Texture2DArray))
					{
						return {{}, Unsupported("Layered CUDA surfaces are unavailable on this device.")};
					}
					const bool bDirect = Info.mbSharingEnabled &&
					    Info.mSupportedRepresentation == EArdaCudaRepresentation::Surface &&
					    (Slot.mFormat == EArdaRHIFormat::Unknown || Slot.mFormat == Desc.mFormat);
					return {{bDirect ? EArdaNativeHandoff::Direct : EArdaNativeHandoff::ConversionRequired,
					            EArdaRHIResourceState::UnorderedAccess}, {}};
				}
				return {{}, Unsupported("CUDA handoff requires a buffer or surface.")};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext& Context,
			    const FArdaNativeOperandDesc& Operand,
			    const IArdaNativeKernelSource& Source,
			    const FArdaNativeInvocation& Invocation,
			    const FArdaNativeLaunch& Launch) const override
			{
				const auto* CudaSource = dynamic_cast<const FArdaNativeCudaSource*>(&Source);
				if (!CudaSource || !Context.mDevice || Operand.mResources.size() != Invocation.mResources.size())
				{
					return {{},
					    Invalid("CUDA preparation requires a CUDA source, a device, and matching resource slots.")};
				}
				const auto Capabilities = Context.mDevice->GetCudaCapabilities();
				if (auto Status = CheckExecutionMode(Context, Capabilities); !Status)
				{
					return {{}, eastl::move(Status)};
				}
				auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>();
				Plan->mDevice = Context.mDevice;
				Plan->mQueue = Context.mQueue;
				auto& Dispatch = Plan->mDispatch;
				Dispatch.mBindings.reserve(Invocation.mResources.size());
				for (size_t Index = 0; Index < Invocation.mResources.size(); ++Index)
				{
					const auto& View = Invocation.mResources[Index];
					const auto& Slot = Operand.mResources[Index];
					// The registry has already qualified this view with QueryHandoff.
					Dispatch.mBindings.push_back({View.mResource, Slot.mAccess, View.mBufferRange, View.mMipLevel});
				}

				auto Entry = CudaSource->ResolveEntry(Capabilities.mComputeCapability);
				if (!Entry)
				{
					return {{}, eastl::move(Entry.mStatus)};
				}
				auto& Kernel = Dispatch.mKernels.emplace_back();
				Kernel.mEntry = eastl::move(Entry.mValue);
				Kernel.mExternalCall = CudaSource->mDesc.mExternalCall;
				Kernel.mParameters = Invocation.mParameters;
				Kernel.mPatches = CudaSource->mDesc.mPatches;
				for (size_t Axis = 0; Axis < 3; ++Axis)
				{
					Kernel.mGridSize[Axis] = Launch.mGridSize[Axis];
					Kernel.mBlockSize[Axis] = Launch.mBlockSize[Axis];
				}
				Kernel.mSharedMemoryBytes = Launch.mSharedMemoryBytes;
				if (auto Status = ValidateArdaCudaKernels(Dispatch.mKernels, Dispatch.mBindings.size(), Capabilities);
				    !Status)
				{
					return {{}, eastl::move(Status)};
				}

				for (const auto& Patch : Kernel.mPatches)
				{
					const auto& Slot = Operand.mResources[Patch.mBindingIndex];
					const auto& Binding = Dispatch.mBindings[Patch.mBindingIndex];
					if (Slot.mType == EArdaRHIResourceType::Buffer &&
					    Binding.mBufferRange.mByteOffset % Patch.mAlignment)
					{
						return {{}, Invalid("CUDA buffer byte offset does not satisfy its argument alignment.")};
					}
				}
				return {eastl::make_shared<FArdaNativeCudaWork>(eastl::move(Plan)), {}};
			}

		private:
			FArdaRHIStatus CheckExecutionMode(const FArdaNativeComputeContext& Context,
			    const FArdaCudaCapabilities& Capabilities) const
			{
				if (auto Status = CheckCudaQueue(Capabilities, Context.mQueue); !Status)
				{
					return Status;
				}
				if (mDesc.mId == ArdaNativeCudaInGraphicsProviderId)
				{
					const char* Backend = Context.mDevice->GetBackendName();
					const bool bD3D12 = std::strcmp(Backend, "native-d3d12") == 0 &&
					    Capabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG;
					const bool bVulkan = std::strcmp(Backend, "native-vulkan") == 0 &&
					    Capabilities.mLaunchMode == EArdaCudaLaunchMode::VulkanCiG;
					if (!bD3D12 && !bVulkan)
					{
						return Unsupported("CUDA in Graphics requires a qualified CiG context for this graphics API.");
					}
				}
				return {};
			}

			FArdaNativeProviderDesc mDesc;
		};
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaProvider(
	    eastl::vector<eastl::string> GraphicsApis)
	{
		if (GraphicsApis.empty())
		{
			GraphicsApis = {"native-d3d12", "native-vulkan"};
		}
		return eastl::make_shared<FArdaNativeCudaProvider>(ArdaNativeCudaProviderId, eastl::move(GraphicsApis));
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaInGraphicsProvider()
	{
		return eastl::make_shared<FArdaNativeCudaProvider>(ArdaNativeCudaInGraphicsProviderId,
		    eastl::vector<eastl::string>{"native-d3d12", "native-vulkan"});
	}

	void AddArdaNativeCudaVariants(FArdaNativeOperandDesc& Operand, FArdaNativeKernelVariant Variant)
	{
		auto CiG = Variant;
		CiG.mProviderId = ArdaNativeCudaInGraphicsProviderId;
		CiG.mName += ".cig";
		Operand.mVariants.push_back(eastl::move(CiG));
		Variant.mProviderId = ArdaNativeCudaProviderId;
		Operand.mVariants.push_back(eastl::move(Variant));
	}

	TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaSource(
	    FArdaNativeCudaSourceDesc Desc)
	{
		const bool bModule = !Desc.mModule.mImage.empty();
		const bool bSource = !Desc.mCudaCpp.empty();
		if (int(bool(Desc.mEntry)) + int(bool(Desc.mExternalCall)) + int(bModule) + int(bSource) != 1)
		{
			return {{},
			    Invalid("CUDA source requires exactly one linked entry, external call, module image, or C++ source.")};
		}

		const char* Format = Desc.mExternalCall ? CudaExternalFormat : CudaLinkedFormat;
		if (bModule)
		{
			auto Module = CreateArdaCudaModule(eastl::move(Desc.mModule));
			if (!Module)
			{
				return {{}, eastl::move(Module.mStatus)};
			}
			auto Entry = CreateArdaCudaModuleKernel(eastl::move(Module.mValue), eastl::move(Desc.mModuleKernel));
			if (!Entry)
			{
				return {{}, eastl::move(Entry.mStatus)};
			}
			Desc.mEntry = eastl::move(Entry.mValue);
		}
		if (const auto* Module = Desc.mEntry ? Desc.mEntry->GetModule() : nullptr)
		{
			Format = Module->GetDesc().mFormat == EArdaCudaModuleFormat::Ptx ? "ptx" : "cubin";
		}
		if (bSource)
		{
			if (Desc.mCudaCpp.find('\0') != eastl::string::npos)
			{
				return {{}, Invalid("CUDA C++ source contains a null byte.")};
			}
			if (auto Status = ValidateArdaCudaModuleKernelDesc(Desc.mModuleKernel); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			Format = CudaCppFormat;
		}
		return {eastl::make_shared<FArdaNativeCudaSource>(eastl::move(Desc), Format), {}};
	}

	eastl::shared_ptr<const FArdaCudaDispatchPlan> GetArdaNativeCudaWorkPlan(const FArdaNativeWorkRef& Work)
	{
		const auto* CudaWork = dynamic_cast<const FArdaNativeCudaWork*>(Work.get());
		return CudaWork ? CudaWork->mPlan : nullptr;
	}
}
