#include "NVAPI/ArdaNativeNvapiProvider.h"
#include "RHI/Providers/ArdaNvapiShaderExtension.h"
#include <EASTL/algorithm.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIStatus UnsupportedNvapi(const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason);
		}

		uint32_t DxilWord(const uint8_t* Data)
		{
			uint32_t Word;
			std::memcpy(&Word, Data, sizeof(Word));
			return Word;
		}

		// DXC's PSV0 chunk precedes the binding table with a length-prefixed runtime info record.
		// PSVResourceBindInfo0 contains type, space, lower register and upper register.
		bool HasNvapiBinding(const uint8_t* Data, size_t Size, uint32_t Slot, uint32_t Space)
		{
			if (Size < 12) return false;
			const size_t RuntimeSize = DxilWord(Data);
			if (RuntimeSize > Size - 12) return false;
			const uint32_t Count = DxilWord(Data + 4 + RuntimeSize);
			const uint32_t Stride = DxilWord(Data + 8 + RuntimeSize);
			const size_t Start = 12 + RuntimeSize;
			if (Stride < 16 || Count > (Size - Start) / Stride) return false;
			for (size_t Index = 0; Index < Count; ++Index)
			{
				const uint8_t* Binding = Data + Start + Index * Stride;
				constexpr uint32_t UavStructuredWithCounter = 9;
				if (DxilWord(Binding) == UavStructuredWithCounter && DxilWord(Binding + 4) == Space &&
				    DxilWord(Binding + 8) == Slot && DxilWord(Binding + 12) == Slot)
				{
					return true;
				}
			}
			return false;
		}

		bool HasNvapiComputeShader(const FArdaNativeNvapiSourceDesc& Desc)
		{
			const auto& Bytes = Desc.mDxil;
			if (Bytes.size() < 32 || std::memcmp(Bytes.data(), "DXBC", 4) != 0 ||
			    DxilWord(Bytes.data() + 24) != Bytes.size()) return false;
			const uint32_t Count = DxilWord(Bytes.data() + 28);
			if (Count > (Bytes.size() - 32) / 4) return false;
			bool bCompute = false, bExtensionUav = false;
			for (size_t Index = 0; Index < Count; ++Index)
			{
				const size_t Offset = DxilWord(Bytes.data() + 32 + Index * 4);
				if (Offset < 32 + size_t(Count) * 4 || Offset > Bytes.size() - 8) return false;
				const auto* Part = Bytes.data() + Offset;
				const size_t Size = DxilWord(Part + 4);
				if (Size > Bytes.size() - Offset - 8) return false;
				if (std::memcmp(Part, "DXIL", 4) == 0 && Size >= 24)
				{
					// DXIL program header encodes ShaderKind::Compute (5) in the upper 16 bits.
					bCompute = (DxilWord(Part + 8) >> 16) == 5 && std::memcmp(Part + 16, "DXIL", 4) == 0;
				}
				if (std::memcmp(Part, "PSV0", 4) == 0)
				{
					bExtensionUav = HasNvapiBinding(Part + 8, Size, Desc.mExtensionUavSlot, Desc.mExtensionRegisterSpace);
				}
			}
			return bCompute && bExtensionUav;
		}

		class FArdaNativeNvapiSource final : public IArdaNativeKernelSource
		{
		public:
			explicit FArdaNativeNvapiSource(FArdaNativeNvapiSourceDesc Desc) : mDxil(eastl::move(Desc.mDxil))
			{
				mExtension.mId = ArdaNvapiShaderExtensionId;
				mExtension.mArguments = {Desc.mExtensionUavSlot, Desc.mExtensionRegisterSpace};
				mExtension.mArguments.insert(mExtension.mArguments.end(), Desc.mRequiredOpcodes.begin(), Desc.mRequiredOpcodes.end());
			}
			const char* GetProviderId() const noexcept override { return ArdaNativeNvapiProviderId; }
			const char* GetFormat() const noexcept override { return "nvapi-dxil"; }
			eastl::vector<uint8_t> mDxil;
			FArdaRHIComputePipelineExtension mExtension;
		};

		EArdaRHIBindingType NvapiBindingType(const FArdaNativeResourceSlot& Slot)
		{
			const bool bRead = Slot.mAccess == EArdaComputeAccess::Read;
			if (Slot.mType == EArdaRHIResourceType::Buffer)
			{
				return bRead ? EArdaRHIBindingType::RawBufferSRV : EArdaRHIBindingType::RawBufferUAV;
			}
			return bRead ? EArdaRHIBindingType::TextureSRV : EArdaRHIBindingType::TextureUAV;
		}

		class FArdaNativeNvapiProvider final : public IArdaNativeComputeProvider
		{
		public:
			const FArdaNativeProviderDesc& GetDesc() const noexcept override { return mDesc; }

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext& Context, const FArdaNativeOperandDesc& Operand,
			    const IArdaNativeKernelSource& Source) const override
			{
				const auto* Kernel = dynamic_cast<const FArdaNativeNvapiSource*>(&Source);
				if (!Kernel || !Context.mDevice ||
				    (Context.mQueue != EArdaRHIQueueType::Compute && Context.mQueue != EArdaRHIQueueType::Graphics))
				{
					return UnsupportedNvapi("NVAPI requires its DXIL source and a D3D12 graphics or compute queue.");
				}
				// Each operand resource and the hidden extension UAV consume a root descriptor table.
				if (Operand.mParameterSize % 4 || Operand.mParameterSize > 252 ||
				    Operand.mResources.size() + Operand.mParameterSize / 4 > 63)
				{
					return UnsupportedNvapi("NVAPI requires whole DWORD constants and a layout within the D3D12 root signature limit.");
				}
				for (const auto& Slot : Operand.mResources)
				{
					if (Slot.mType != EArdaRHIResourceType::Buffer && Slot.mType != EArdaRHIResourceType::Texture)
					{
						return UnsupportedNvapi("NVAPI supports raw buffer and texture slots.");
					}
				}
				return Context.mDevice->CheckComputePipelineExtension(Kernel->mExtension);
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext& Context,
			    const FArdaNativeResourceSlot& Slot, const FArdaNativeResourceView& View) const override
			{
				const bool bRead = Slot.mAccess == EArdaComputeAccess::Read;
				bool bDirect = false;
				if (!Context.mDevice || !View.mResource || View.mResource->GetResourceType() != Slot.mType)
				{
					return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "NVAPI handoff needs the declared resource type.")};
				}
				if (const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(View.mResource.Get()))
				{
					const auto& Desc = Buffer->GetDesc();
					if (View.mBufferRange.mByteOffset >= Desc.mByteSize || !View.mBufferRange.mByteSize ||
					    (View.mBufferRange.mByteSize != ArdaRHIWholeBuffer &&
					        View.mBufferRange.mByteSize > Desc.mByteSize - View.mBufferRange.mByteOffset))
					{
						return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "NVAPI buffer view exceeds the resource.")};
					}
					const auto Range = View.mBufferRange.Resolve(Desc);
					const auto& Limits = Context.mDevice->GetCapabilities().mLimits;
					const uint64_t Alignment = eastl::max<uint64_t>(4, Limits.mMinStorageBufferOffsetAlignment);
					bDirect = HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Raw) &&
					    HasAnyFlags(Desc.mUsage, bRead ? EArdaRHIBufferUsage::ShaderResource : EArdaRHIBufferUsage::UnorderedAccess) &&
					    Range.mByteOffset % Alignment == 0 && Range.mByteSize % 4 == 0 && Range.mByteSize / 4 <= UINT32_MAX &&
					    (!Limits.mMaxStorageBufferRange || Range.mByteSize <= Limits.mMaxStorageBufferRange);
				}
				else if (const auto* Texture = dynamic_cast<const IArdaRHITexture*>(View.mResource.Get()))
				{
					const auto& Desc = Texture->GetDesc();
					if (View.mMipLevel >= Desc.mMipLevels)
					{
						return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "NVAPI texture mip is out of range.")};
					}
					bDirect = HasAnyFlags(Desc.mUsage, bRead ? EArdaRHITextureUsage::ShaderResource : EArdaRHITextureUsage::UnorderedAccess);
				}
				return {{bDirect ? EArdaNativeHandoff::Direct : EArdaNativeHandoff::ConversionRequired,
				    bRead ? EArdaRHIResourceState::NonPixelShaderResource : EArdaRHIResourceState::UnorderedAccess}, {}};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext& Context,
			    const FArdaNativeOperandDesc& Operand, const IArdaNativeKernelSource& Source,
			    const FArdaNativeInvocation& Invocation, const FArdaNativeLaunch& Launch) const override
			{
				const auto* Kernel = dynamic_cast<const FArdaNativeNvapiSource*>(&Source);
				if (!Kernel || !Context.mDevice || Operand.mResources.size() != Invocation.mResources.size())
				{
					return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid NVAPI preparation inputs.")};
				}
				for (size_t Axis = 0; Axis < 3; ++Axis)
				{
					if (!Launch.mGridSize[Axis] || Launch.mGridSize[Axis] > 65535 ||
					    Launch.mBlockSize[Axis] != 1 || Launch.mSharedMemoryBytes)
					{
						return {{}, UnsupportedNvapi("NVAPI uses DXIL numthreads and shared memory. Specify only D3D12 dispatch group counts.")};
					}
				}
				FArdaRHIBindingLayoutDesc LayoutDesc;
				LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
				FArdaRHIBindingSetDesc Bindings;
				for (uint32_t Index = 0; Index < Operand.mResources.size(); ++Index)
				{
					const auto& Slot = Operand.mResources[Index];
					const auto& View = Invocation.mResources[Index];
					const auto Type = NvapiBindingType(Slot);
					LayoutDesc.mItems.push_back({Index, 1, Type});
					FArdaRHIBindingItem Item;
					Item.mSlot = Index;
					Item.mType = Type;
					Item.mResource = View.mResource;
					Item.mView.mBufferRange = View.mBufferRange;
					Item.mView.mTextureRange.mBaseMipLevel = View.mMipLevel;
					Item.mView.mTextureRange.mMipLevelCount = 1;
					Bindings.mItems.push_back(eastl::move(Item));
				}
				if (Operand.mParameterSize)
				{
					LayoutDesc.mItems.push_back({0, static_cast<uint32_t>(Operand.mParameterSize), EArdaRHIBindingType::PushConstants});
				}
				FArdaRHIShaderDesc ShaderDesc;
				ShaderDesc.mStage = EArdaRHIShaderStage::Compute;
				ShaderDesc.mBytecode = Kernel->mDxil.data();
				ShaderDesc.mBytecodeSize = Kernel->mDxil.size();
				ShaderDesc.mDebugName = Operand.mId;
				auto Shader = Context.mDevice->CreateShader(ShaderDesc);
				if (!Shader) return {{}, eastl::move(Shader.mStatus)};
				FArdaRHIComputePipelineDesc PipelineDesc;
				PipelineDesc.mComputeShader = eastl::move(Shader.mValue);
				PipelineDesc.mExtension = Kernel->mExtension;
				FArdaRHIComputeState State;
				if (!LayoutDesc.mItems.empty())
				{
					auto Layout = Context.mDevice->CreateBindingLayout(LayoutDesc);
					if (!Layout) return {{}, eastl::move(Layout.mStatus)};
					Bindings.mLayout = Layout.mValue;
					auto Set = Context.mDevice->CreateBindingSet(Bindings);
					if (!Set) return {{}, eastl::move(Set.mStatus)};
					PipelineDesc.mBindingLayouts.push_back(eastl::move(Layout.mValue));
					State.mBindings.push_back(eastl::move(Set.mValue));
				}
				auto Pipeline = Context.mDevice->CreateComputePipeline(PipelineDesc);
				if (!Pipeline) return {{}, eastl::move(Pipeline.mStatus)};
				State.mPipeline = eastl::move(Pipeline.mValue);
				return {MakeArdaNativeWork([State = eastl::move(State), Constants = Invocation.mParameters, Launch](IArdaRHICommandList& Commands)
				{
					auto Status = Commands.SetComputeState(State);
					if (!Status) return Status;
					if (!Constants.empty()) Commands.SetPushConstants(Constants.data(), Constants.size());
					Commands.Dispatch(Launch.mGridSize[0], Launch.mGridSize[1], Launch.mGridSize[2]);
					return FArdaRHIStatus{};
				}), {}};
			}
		private:
			FArdaNativeProviderDesc mDesc{ArdaNativeNvapiProviderId, {"native-d3d12"}, {"nvapi-dxil"}};
		};
	}

	TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeNvapiSource(FArdaNativeNvapiSourceDesc Desc)
	{
		if (Desc.mRequiredOpcodes.empty() || Desc.mExtensionUavSlot == UINT32_MAX || !Desc.mExtensionRegisterSpace ||
		    Desc.mExtensionRegisterSpace >= 0xfffffff0u || !HasNvapiComputeShader(Desc))
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "NVAPI requires compute DXIL declaring the extension counter UAV at the specified slot and space, and the required opcodes.")};
		}
		return {eastl::make_shared<FArdaNativeNvapiSource>(eastl::move(Desc)), {}};
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeNvapiProvider()
	{
		return eastl::make_shared<FArdaNativeNvapiProvider>();
	}
}
