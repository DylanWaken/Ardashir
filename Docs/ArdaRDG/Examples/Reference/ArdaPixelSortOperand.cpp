// Documentation snapshot of Source/ArdaTests/Examples/PixelSort/Private/Nodes/ArdaPixelSortOperand.cpp.
#include "ArdaPixelSortNodeInternal.h"
#include "NativeCompute/ArdaNativeCompute.h"
#include "CUDA/ArdaNativeCudaProvider.h"
#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
#include "RHI/Scheduling/ArdaRHICommandList.h"

#include <cstddef>
#include <cstring>

namespace arda
{
	namespace
	{
		constexpr char PixelSortSignature[] = "rgba8-radix-v1";

		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaPixelSortFallbackParameters)
			ARDA_SHADER_TEXTURE_UAV_ACCESS(mInput, 1, 0, EArdaRHIShaderStage::Compute, EArdaShaderResourceAccess::Read)
			ARDA_SHADER_TEXTURE_UAV_ACCESS(mOutput,
			    2,
			    0,
			    EArdaRHIShaderStage::Compute,
			    EArdaShaderResourceAccess::Write)
			ARDA_SHADER_PUSH_CONSTANTS(FArdaPixelSortShaderConstants, mConstants, 1, 0, EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()

		class FArdaPixelSortFallbackShader final : public FArdaGlobalShader
		{
		public:
			using FArdaParameters = FArdaPixelSortFallbackParameters;
			ARDA_DECLARE_GLOBAL_SHADER(FArdaPixelSortFallbackShader);
		};

		ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaPixelSortFallbackShader,
		    GetPixelSortShaderSource(),
		    "PixelSortFallback",
		    "SortCS",
		    EArdaRHIShaderStage::Compute)

		FArdaPixelSortShaderConstants ReadPixelSortParameters(const FArdaNativeInvocation& Invocation)
		{
			FArdaPixelSortKernelParameters Parameters{};
			std::memcpy(&Parameters, Invocation.mParameters.data(), sizeof(Parameters));
			return Parameters.mConstants;
		}

		FArdaRHIStatus ValidatePixelSortInvocation(const FArdaNativeInvocation& Invocation)
		{
			const auto P = ReadPixelSortParameters(Invocation);
			if (!P.mWidth || !P.mHeight || P.mChannel > 2 || P.mThreshold > 255 ||
			    Invocation.mResources[0].mResource == Invocation.mResources[1].mResource)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "PixelSort needs distinct textures, nonempty dimensions and a valid RGB channel and threshold.");
			}
			for (const auto& View : Invocation.mResources)
			{
				const auto& Desc = dynamic_cast<const IArdaRHITexture*>(View.mResource.Get())->GetDesc();
				if (Desc.mWidth != P.mWidth || Desc.mHeight != P.mHeight || Desc.mMipLevels != 1 ||
				    Desc.mDimension != EArdaRHITextureDimension::Texture2D || Desc.mArraySize != 1 ||
				    Desc.mSampleCount != 1)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "PixelSort dimensions must match a single-sample 2D texture with one mip and layer.");
				}
			}
			return {};
		}

		FArdaNativeLaunch PixelSortLaunch(const FArdaPixelSortShaderConstants& P)
		{
			const bool bVertical = P.mHeight > P.mWidth;
			const uint32_t Threads = bVertical ? ArdaPixelSortColumnThreads : ArdaPixelSortRowThreads;
			const uint32_t Tile = Threads * ArdaPixelSortItemsPerThread;
			const uint32_t Length = bVertical ? P.mHeight : P.mWidth;
			FArdaNativeLaunch Launch;
			Launch.mGridSize[0] = 1 + (Length - 1) / Tile;
			Launch.mGridSize[1] = bVertical ? P.mWidth : P.mHeight;
			Launch.mBlockSize[0] = Threads;
			return Launch;
		}

#if defined(ARDA_PIXEL_SORT_NATIVE_KERNELS)
		TArdaRHIResult<FArdaNativeOperand> AddPixelSortKernelVariants(FArdaNativeOperand Operand)
		{
			using FArdaParameters = FArdaPixelSortKernelParameters;
			using FArdaConstants = FArdaPixelSortShaderConstants;
			using EArdaArgumentKind = EArdaNativeArgumentKind;
			const size_t ScalarStart = offsetof(FArdaParameters, mConstants);
			const FArdaNativeKernelLayout Layout{sizeof(FArdaParameters),
			    alignof(FArdaParameters),
			    {{offsetof(FArdaParameters, mInput), sizeof(uint64_t), alignof(uint64_t), EArdaArgumentKind::Surface},
			        {offsetof(FArdaParameters, mOutput),
			            sizeof(uint64_t),
			            alignof(uint64_t),
			            EArdaArgumentKind::Surface},
			        {ScalarStart + offsetof(FArdaConstants, mWidth),
			            sizeof(uint32_t),
			            alignof(uint32_t),
			            EArdaArgumentKind::Value},
			        {ScalarStart + offsetof(FArdaConstants, mHeight),
			            sizeof(uint32_t),
			            alignof(uint32_t),
			            EArdaArgumentKind::Value},
			        {ScalarStart + offsetof(FArdaConstants, mChannel),
			            sizeof(uint32_t),
			            alignof(uint32_t),
			            EArdaArgumentKind::Value},
			        {ScalarStart + offsetof(FArdaConstants, mThreshold),
			            sizeof(uint32_t),
			            alignof(uint32_t),
			            EArdaArgumentKind::Value}}};
			const char* Architectures[] = {
#include "ArdaPixelSortArchitectures.inl"
			};
			auto NativeResources = Operand.mResources;
			for (auto& Slot : NativeResources)
			{
				Slot.mCategory = ArdaNativeCudaSurfaceCategory;
			}
			for (const char* Provider : {ArdaNativeCudaInGraphicsProviderId, ArdaNativeCudaProviderId})
			{
				for (const char* Arch : Architectures)
				{
					for (const bool bVertical : {false, true})
					{
						const auto Name = eastl::string(bVertical ? "Columns.sm" : "Rows.sm") + Arch;
						auto Packaged =
						    MakeArdaPackagedNativeOperand({"arda-pixel-sort", Provider, Name, PixelSortSignature},
						        Layout,
						        eastl::move(Operand));
						if (!Packaged)
						{
							return Packaged;
						}
						Operand = eastl::move(Packaged.mValue);
						auto& Variant = Operand.mVariants.back();
						Variant.mName += eastl::string(".") + Provider;
						Variant.mNativeResources = NativeResources;
						Variant.mGraphicsStates = {EArdaRHIResourceState::UnorderedAccess,
						    EArdaRHIResourceState::UnorderedAccess};
						Variant.mSelect =
						    [bVertical](const FArdaNativeComputeContext&,
						        const FArdaNativeInvocation& Invocation) -> TArdaRHIResult<FArdaNativeLaunch>
						{
							const auto P = ReadPixelSortParameters(Invocation);
							if ((P.mHeight > P.mWidth) != bVertical)
							{
								return {{},
								    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
								        "PixelSort variant handles the other orientation.")};
							}
							return {PixelSortLaunch(P), {}};
						};
					}
				}
			}
			return {eastl::move(Operand), {}};
		}
#endif

		TArdaRHIResult<FArdaNativeWorkRef> PreparePixelSortShader(const FArdaNativeComputeContext& Context,
		    const FArdaNativeInvocation& Invocation)
		{
			const auto& Device = Context.mDevice;
			FArdaGlobalShaderMap Shaders;
			if (!Shaders.Initialize(Device))
			{
				return {{}, PixelSortShaderError(Shaders)};
			}
			const auto* Shader = Shaders.Find(FArdaPixelSortFallbackShader::GetStaticType());
			if (!Shader)
			{
				return {{}, PixelSortShaderError(Shaders)};
			}
			FArdaRHIComputePipelineDesc Desc;
			Desc.mComputeShader = Shader->GetShader();
			Desc.mBindingLayouts = Shader->GetBindingLayouts();
			Desc.mDebugName = "PixelSort shader fallback";
			auto Pipeline = Device->CreateComputePipeline(Desc);
			if (!Pipeline)
			{
				return {{}, eastl::move(Pipeline.mStatus)};
			}
			FArdaRHIBindingSetDesc Bindings;
			Bindings.mLayout = Pipeline.mValue->GetDesc().mBindingLayouts.front();
			Bindings.mItems = {{1, 0, EArdaRHIBindingType::TextureUAV, Invocation.mResources[0].mResource},
			    {2, 0, EArdaRHIBindingType::TextureUAV, Invocation.mResources[1].mResource}};
			auto Set = Context.mDevice->CreateBindingSet(Bindings);
			if (!Set)
			{
				return {{}, eastl::move(Set.mStatus)};
			}
			const auto Constants = ReadPixelSortParameters(Invocation);
			const auto Launch = PixelSortLaunch(Constants);
			FArdaRHIComputeState State{eastl::move(Pipeline.mValue), {eastl::move(Set.mValue)}};
			return {MakeArdaNativeWork(
			            [State = eastl::move(State), Constants, Launch](IArdaRHICommandList& Commands)
			            {
				            if (auto Status = Commands.SetComputeState(State); !Status)
				            {
					            return Status;
				            }
				            Commands.SetPushConstants(&Constants, sizeof(Constants));
				            Commands.Dispatch(Launch.mGridSize[0], Launch.mGridSize[1], 1);
				            return FArdaRHIStatus{};
			            }),
			    {}};
		}

		FArdaRHIStatus RegisterPixelSortOperand()
		{
			FArdaNativeOperand Operand;
			Operand.mId = ArdaPixelSortOperandId;
			Operand.mSignature = PixelSortSignature;
			Operand.mParameterSize = sizeof(FArdaPixelSortKernelParameters);
			FArdaNativeResourceSlotsCreator ResourceSlots;
			ResourceSlots.AddTexture("Input", EArdaComputeAccess::Read, EArdaRHIFormat::RGBA8UInt);
			ResourceSlots.AddTexture("Output", EArdaComputeAccess::Write, EArdaRHIFormat::RGBA8UInt);
			Operand.mResources = ResourceSlots.Finalize();
			Operand.mValidate = ValidatePixelSortInvocation;
			Operand.mShaderFallbacks.push_back({EArdaNativeComputeRoute::ComputeShader, PreparePixelSortShader});
#if defined(ARDA_PIXEL_SORT_NATIVE_KERNELS)
			AddArdaNativeKernelDirectory(GetArdaExampleDirectory());
			UseArdaNativeCudaResources(Operand);
			auto Result = AddPixelSortKernelVariants(eastl::move(Operand));
			if (!Result)
			{
				return eastl::move(Result.mStatus);
			}
			Operand = eastl::move(Result.mValue);
#endif
			return GetArdaNativeComputeRegistry().RegisterOperand(eastl::move(Operand));
		}
	}

	FArdaRHIStatus EnsurePixelSortOperandRegistered()
	{
		static const auto Status = RegisterPixelSortOperand();
		return Status;
	}

	FArdaRHIStatus CheckPixelSortNativeSupport(const IArdaRHIDevice* Device)
	{
		if (auto Status = EnsurePixelSortOperandRegistered(); !Status)
		{
			return Status;
		}
		// The preflight takes a temporary reference and only queries device capabilities.
		return GetArdaNativeComputeRegistry().CheckSupport(ArdaPixelSortOperandId,
		    {FArdaRHIDeviceRef(const_cast<IArdaRHIDevice*>(Device)),
		        EArdaRHIQueueType::Graphics,
		        EArdaNativeComputePreference::NativeOnly});
	}

	FArdaNativeInvocation MakePixelSortInvocation(FArdaRHITextureRef Input,
	    FArdaRHITextureRef Output,
	    FArdaPixelSortShaderConstants Constants)
	{
		FArdaNativeInvocation Invocation;
		Invocation.mResources = {{eastl::move(Input), {}, 0}, {eastl::move(Output), {}, 0}};
		const FArdaPixelSortKernelParameters Parameters{0, 0, Constants};
		Invocation.mParameters.resize(sizeof(Parameters));
		std::memcpy(Invocation.mParameters.data(), &Parameters, sizeof(Parameters));
		return Invocation;
	}
}
