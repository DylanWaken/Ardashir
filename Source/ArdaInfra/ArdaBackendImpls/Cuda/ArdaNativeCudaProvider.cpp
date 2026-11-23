#include "ArdaNativeCudaProviderPrivate.h"
#include "../ArdaNativeResourceCopies.h"

#include "ArdaNativeCudaCompiler.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Resources/ArdaCudaTextureBuffer.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
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

		// Use fixed-width little-endian words so legacy keys are stable across hosts.
		void HashKernelWord(uint64_t& Hash, uint64_t Value)
		{
			for (uint32_t Shift = 0; Shift < 64; Shift += 8)
			{
				Hash = (Hash ^ uint8_t(Value >> Shift)) * 1099511628211ull;
			}
		}

		FArdaRHIStatus CheckCudaQueue(EArdaCudaLaunchMode Mode, EArdaRHIQueueType Queue)
		{
			if (Queue != EArdaRHIQueueType::Graphics &&
			    (Queue != EArdaRHIQueueType::Compute || Mode == EArdaCudaLaunchMode::D3D12CiG))
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

			FArdaRHIStatus CheckCommandList(const IArdaRHICommandList& Commands) const
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
				return {};
			}

			FArdaRHIStatus Record(IArdaRHICommandList& Commands) const override
			{
				auto Status = CheckCommandList(Commands);
				return Status ? Commands.DispatchCuda(mPlan->mDispatch) : Status;
			}

			const eastl::shared_ptr<const FArdaCudaDispatchPlan> mPlan;
		};

		class FArdaNativeCudaDeviceContext final : public IArdaNativeDeviceContext
		{
		public:
			FArdaNativeCudaDeviceContext(const IArdaNativeComputeProvider& Provider, const IArdaRHIDevice& Device,
			    FArdaCudaCapabilities Capabilities)
			    : mProvider(&Provider),
			      mDevice(&Device),
			      mCapabilities(eastl::move(Capabilities))
			{
			}

			const IArdaNativeComputeProvider* const mProvider;
			const IArdaRHIDevice* const mDevice;
			const FArdaCudaCapabilities mCapabilities;
		};

		class FArdaNativeCudaProvider final : public IArdaNativeComputeProvider
		{
		public:
			FArdaNativeCudaProvider(const char* Id, eastl::vector<eastl::string> GraphicsApis,
			    FArdaCudaModeCheck CheckMode)
			    : mCheckMode(CheckMode)
			{
				mDesc.mId = Id;
				mDesc.mResourceCategories = {{ArdaNativeCudaBufferCategory, EArdaRHIResourceType::Buffer},
				    {ArdaNativeCudaSurfaceCategory, EArdaRHIResourceType::Texture}};
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

			TArdaRHIResult<FArdaNativeProviderSupport> ProbeDevice(const IArdaRHIDevice& Device) const override
			{
				auto Capabilities = Device.GetCudaCapabilities();
				if (auto Status = CheckDeviceMode(Device, Capabilities); !Status)
				{
					return {{}, eastl::move(Status)};
				}
				// Vulkan joins the CUDA stream to an external compute queue, but still submits
				// Vulkan and CUDA segments separately. D3D12 CiG captures on the graphics queue.
				const auto Mode = Capabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG
				    ? EArdaNativeExecutionMode::Direct
				    : EArdaNativeExecutionMode::ContextSwitching;
				return {{Mode, HasArdaNativeCudaCompiler(),
				            eastl::make_shared<FArdaNativeCudaDeviceContext>(*this, Device, eastl::move(Capabilities))}, {}};
			}

			void ConfigureBuffer(FArdaRHIBufferDesc& Desc) const override
			{
				Desc.mbCudaInterop = true;
			}

			void ConfigureTexture(FArdaRHITextureDesc& Desc) const override
			{
				Desc.mbCudaInterop = true;
			}

			TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> LoadKernel(
			    const FArdaNativeKernelBinary& Binary, const FArdaNativeKernelLayout& Layout) const override
			{
				const bool bSource = Binary.mFormat == CudaCppFormat;
				if (!bSource && Binary.mFormat != "ptx" && Binary.mFormat != "cubin")
				{
					return {{}, Unsupported("CUDA kernel libraries support PTX, cubin and CUDA C++ payloads.")};
				}
				if (Binary.mBytes.empty())
				{
					return {{}, Invalid("The CUDA kernel payload is empty.")};
				}

				FArdaNativeCudaSourceDesc Desc;
				Desc.mModuleKernel.mEntryPoint = Binary.mEntryPoint;
				Desc.mModuleKernel.mParameterSize = Layout.mParameterSize;
				Desc.mModuleKernel.mParameterAlignment = Layout.mParameterAlignment;
				uint32_t Binding = 0;
				for (const auto& Argument : Layout.mArguments)
				{
					EArdaCudaModuleArgumentKind Kind;
					switch (Argument.mKind)
					{
						case EArdaNativeArgumentKind::Value: Kind = EArdaCudaModuleArgumentKind::Value; break;
						case EArdaNativeArgumentKind::Buffer: Kind = EArdaCudaModuleArgumentKind::Buffer; break;
						case EArdaNativeArgumentKind::Surface: Kind = EArdaCudaModuleArgumentKind::Surface; break;
						default: return {{}, Invalid("Unknown CUDA kernel argument kind.")};
					}
					Desc.mModuleKernel.mArguments.push_back({Argument.mOffset, Argument.mSize, Argument.mAlignment, Kind});
					if (Kind != EArdaCudaModuleArgumentKind::Value)
					{
						// The ABI alignment describes the pointer's storage, independent of its pointee.
						Desc.mPatches.push_back({Binding++, Argument.mOffset, 1});
					}
				}

				if (bSource)
				{
					if (Binary.mAttributes.size() > 1 ||
					    (!Binary.mAttributes.empty() && (!(Binary.mAttributes[0] >> 1) || (Binary.mAttributes[0] & 1))))
					{
						return {{}, Invalid("CUDA C++ accepts at most one compatible target architecture.")};
					}
					Desc.mComputeCapability = Binary.mAttributes.empty() ? 0 : Binary.mAttributes[0] >> 1;
					Desc.mCudaCpp.assign(reinterpret_cast<const char*>(Binary.mBytes.data()), Binary.mBytes.size());
				}
				else
				{
					Desc.mModule.mFormat = Binary.mFormat == "ptx" ? EArdaCudaModuleFormat::Ptx : EArdaCudaModuleFormat::Cubin;
					Desc.mModule.mImage = Binary.mBytes;
					Desc.mModule.mBuild.mIdentity = "ArdaNativeKernelLibrary";
					for (const uint32_t Target : Binary.mAttributes)
					{
						Desc.mModule.mBuild.mArchitectures.push_back({Target >> 1, bool(Target & 1)});
					}
				}
				return CreateArdaNativeCudaSource(eastl::move(Desc));
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
				auto Support = GetExecutionCapabilities(Context);
				if (!Support)
				{
					return eastl::move(Support.mStatus);
				}
				const auto& Capabilities = Support.mValue;
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

			EArdaRHIResourceState GetRequiredState(const FArdaNativeResourceSlot&) const override
			{
				return EArdaRHIResourceState::UnorderedAccess;
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext& Context,
			    const FArdaNativeResourceSlot& Slot,
			    const FArdaNativeResourceView& View) const override
			{
				if (!Context.mDevice || !View.mResource || View.mResource->GetResourceType() != Slot.mType)
				{
					return {{}, Invalid("CUDA handoff requires a device and a resource of the declared type.")};
				}
				auto Support = GetExecutionCapabilities(Context);
				if (!Support)
				{
					return {{}, eastl::move(Support.mStatus)};
				}
				const auto& Capabilities = Support.mValue;

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
					            GetRequiredState(Slot)}, {}};
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
					            GetRequiredState(Slot)}, {}};
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
				auto Support = GetExecutionCapabilities(Context);
				if (!Support)
				{
					return {{}, eastl::move(Support.mStatus)};
				}
				const auto& Capabilities = Support.mValue;
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

			FArdaRHIStatus RecordBatch(IArdaRHICommandList& Commands,
			    const eastl::vector<FArdaNativeWorkRef>& Works) const override
			{
				eastl::vector<FArdaCudaDispatch> Dispatches;
				Dispatches.reserve(Works.size());
				for (const auto& Work : Works)
				{
					const auto* CudaWork = dynamic_cast<const FArdaNativeCudaWork*>(Work.get());
					if (!CudaWork)
					{
						return Invalid("CUDA batches require work prepared by a CUDA provider.");
					}
					if (auto Status = CudaWork->CheckCommandList(Commands); !Status)
					{
						return Status;
					}
					Dispatches.push_back(CudaWork->mPlan->mDispatch);
				}
				return Commands.DispatchCudaSequence(Dispatches);
			}

		private:
			TArdaRHIResult<FArdaCudaCapabilities> GetExecutionCapabilities(
			    const FArdaNativeComputeContext& Context) const
			{
				if (const auto* Snapshot =
				        dynamic_cast<const FArdaNativeCudaDeviceContext*>(Context.mProviderContext.get());
				    Snapshot && Snapshot->mProvider == this && Snapshot->mDevice == Context.mDevice.Get())
				{
					return {Snapshot->mCapabilities, CheckCudaQueue(Snapshot->mCapabilities.mLaunchMode, Context.mQueue)};
				}
				auto Capabilities = Context.mDevice->GetCudaCapabilities();
				if (auto Status = CheckDeviceMode(*Context.mDevice, Capabilities); !Status)
				{
					return {{}, eastl::move(Status)};
				}
				auto Status = CheckCudaQueue(Capabilities.mLaunchMode, Context.mQueue);
				return {eastl::move(Capabilities), eastl::move(Status)};
			}

			FArdaRHIStatus CheckDeviceMode(const IArdaRHIDevice& Device, const FArdaCudaCapabilities& Capabilities) const
			{
				if (!Capabilities)
				{
					return Unsupported(Capabilities.mUnavailableReason.c_str());
				}
				return mCheckMode ? mCheckMode(Device.GetBackendName(), Capabilities.mLaunchMode)
				                  : FArdaRHIStatus{};
			}

			FArdaNativeProviderDesc mDesc;
			FArdaCudaModeCheck mCheckMode;
		};

		TArdaRHIResult<FArdaNativeResourceView> AllocateCudaStage(IArdaRHIDevice& Device,
		    const FArdaNativeResourceSlot& Slot,
		    const FArdaNativeResourceView& Graphics)
		{
			using namespace native_provider_detail;
			if (Slot.mType == EArdaRHIResourceType::Texture)
			{
				return CreateStagingTexture(Device, Graphics, Slot,
				    EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess, true);
			}
			if (Slot.mType != EArdaRHIResourceType::Buffer)
			{
				return {{}, UnsupportedCopy("CUDA stages a linear buffer or a surface.")};
			}
			if (const auto* Texture = dynamic_cast<const IArdaRHITexture*>(Graphics.mResource.Get()))
			{
				if (Texture->GetDesc().mArraySize != 1)
				{
					return {{}, UnsupportedCopy("One pitched CUDA buffer represents one texture mip or 3D region.")};
				}
				auto Buffer = CreateArdaCudaTextureBuffer(Device, Texture->GetDesc(), TextureSlice(Graphics));
				if (!Buffer)
				{
					return {{}, eastl::move(Buffer.mStatus)};
				}
				const auto Bytes = Buffer.mValue.mBuffer->GetDesc().mByteSize;
				return {{eastl::move(Buffer.mValue.mBuffer), {0, Bytes}}, {}};
			}
			return CreateStagingBuffer(Device, Graphics,
			    EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess, true);
		}

		FArdaRHIStatus CheckCudaConversionProvider(const FArdaNativeConversionContext& Context)
		{
			const auto& Id = Context.mProvider.GetDesc().mId;
			return Id == ArdaNativeCudaProviderId || Id == ArdaNativeCudaInGraphicsProviderId
			    ? FArdaRHIStatus{}
			    : Unsupported("CUDA conversion helpers require a CUDA or CiG provider.");
		}
	}

	FArdaRHIStatus PrepareArdaCudaGraphicsToNative(const FArdaNativeConversionContext& Context,
	    FArdaNativeConversion& Conversion)
	{
		auto Status = CheckCudaConversionProvider(Context);
		return Status ? native_provider_detail::StageGraphicsToNative(Context, Conversion, &AllocateCudaStage) : Status;
	}

	FArdaRHIStatus PrepareArdaCudaNativeToGraphics(const FArdaNativeConversionContext& Context,
	    FArdaNativeConversion& Conversion)
	{
		auto Status = CheckCudaConversionProvider(Context);
		return Status ? native_provider_detail::StageNativeToGraphics(Context, Conversion) : Status;
	}

	void UseArdaNativeCudaResources(FArdaNativeOperand& Operand)
	{
		Operand.mGraphicsToNative = PrepareArdaCudaGraphicsToNative;
		Operand.mNativeToGraphics = PrepareArdaCudaNativeToGraphics;
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> MakeArdaNativeCudaProvider(const char* Id,
	    eastl::vector<eastl::string> GraphicsApis, FArdaCudaModeCheck CheckMode)
	{
		return eastl::make_shared<FArdaNativeCudaProvider>(Id, eastl::move(GraphicsApis), CheckMode);
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaProvider(
	    eastl::vector<eastl::string> GraphicsApis)
	{
		if (GraphicsApis.empty())
		{
			GraphicsApis = {"native-d3d12", "native-vulkan"};
		}
		return MakeArdaNativeCudaProvider(ArdaNativeCudaProviderId, eastl::move(GraphicsApis), nullptr);
	}

	FArdaRHIStatus RegisterArdaCudaNativeComputeProvider(FArdaNativeComputeRegistry& Registry)
	{
		// Legacy model transfer commands refer to these provider-supplied packaged operands.
		static constexpr uint8_t TransferKernels[] = {
#include "ArdaModelTransferLibrary.inl"
		};
		static const auto Assets = LoadArdaNativeKernelLibrary(TransferKernels, sizeof(TransferKernels));
		if (!Assets) return Assets;
		static const auto Provider = CreateArdaNativeCudaProvider({"*"});
		return Registry.RegisterProvider(Provider);
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

	TArdaRHIResult<FArdaNativeKernelKey> ImportArdaLegacyCudaKernel(
	    const FArdaCudaModuleDesc& Module, const FArdaCudaModuleKernelDesc& Kernel)
	{
		if (auto Status = ValidateArdaCudaModuleKernelDesc(Kernel); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		auto Artifact = CreateArdaCudaModule(Module);
		if (!Artifact)
		{
			return {{}, eastl::move(Artifact.mStatus)};
		}
		const auto& Validated = Artifact.mValue->GetDesc();
		FArdaNativeKernelBinary Binary;
		Binary.mFormat = Validated.mFormat == EArdaCudaModuleFormat::Ptx ? "ptx" : "cubin";
		Binary.mEntryPoint = Kernel.mEntryPoint;
		Binary.mBytes = Validated.mImage;
		uint64_t Signature = 14695981039346656037ull;
		HashKernelWord(Signature, static_cast<uint8_t>(Validated.mFormat));
		HashKernelWord(Signature, Validated.mBuild.mArchitectures.size());
		for (const auto& Architecture : Validated.mBuild.mArchitectures)
		{
			if (Architecture.mComputeCapability > UINT32_MAX >> 1)
			{
				return {{}, Invalid("CUDA architecture exceeds the native kernel catalog encoding.")};
			}
			const uint32_t Encoded = (Architecture.mComputeCapability << 1) | uint32_t(Architecture.mbExact);
			Binary.mAttributes.push_back(Encoded);
			HashKernelWord(Signature, Encoded);
		}
		HashKernelWord(Signature, Kernel.mParameterSize);
		HashKernelWord(Signature, Kernel.mParameterAlignment);
		HashKernelWord(Signature, Kernel.mArguments.size());
		for (const auto& Argument : Kernel.mArguments)
		{
			HashKernelWord(Signature, static_cast<uint8_t>(Argument.mKind));
			HashKernelWord(Signature, Argument.mOffset);
			HashKernelWord(Signature, Argument.mSize);
			HashKernelWord(Signature, Argument.mAlignment);
		}
		for (const auto Byte : Binary.mBytes)
		{
			Signature = (Signature ^ Byte) * 1099511628211ull;
		}
		FArdaNativeKernelKey Key{"legacy-cuda-", ArdaNativeCudaProviderId, Kernel.mEntryPoint, {}};
		Key.mSignature.sprintf("%016llx", static_cast<unsigned long long>(Signature));
		Key.mLibrary += Key.mSignature;
		if (auto Status = RegisterArdaNativeKernelBinary(Key, eastl::move(Binary)); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(Key), {}};
	}

	FArdaRHIStatus AppendArdaNativeWorkToCudaSequence(const FArdaNativeWorkRef& Work, FArdaCudaSequence& Sequence)
	{
		const auto* CudaWork = dynamic_cast<const FArdaNativeCudaWork*>(Work.get());
		return CudaWork ? Sequence.AddPlan(CudaWork->mPlan)
		                : Unsupported("The selected native provider cannot append to a CUDA sequence.");
	}

	void ConfigureArdaLegacyCudaSelection(FArdaNativeComputeContext& Context)
	{
		Context.mPreference = EArdaNativeComputePreference::NativeOnly;
		Context.mAllowedProviderIds = {ArdaNativeCudaInGraphicsProviderId, ArdaNativeCudaProviderId};
	}

	eastl::shared_ptr<const FArdaCudaDispatchPlan> GetArdaNativeCudaWorkPlan(const FArdaNativeWorkRef& Work)
	{
		const auto* CudaWork = dynamic_cast<const FArdaNativeCudaWork*>(Work.get());
		return CudaWork ? CudaWork->mPlan : nullptr;
	}
}
