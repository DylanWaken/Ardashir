#include "ArdaCudaHandoff.h"
#include "ArdaUnrealGraphNodes.h"
#include "ArdaUnrealRuntime.h"
#include "BackendImpl/ArdaUnrealBackend.h"

#include "Containers/Ticker.h"
#include "GlobalShader.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

#include <atomic>

namespace arda
{
	static TAutoConsoleVariable<int32> ArdaPostProcessEnabled(TEXT("r.Arda.PostProcess"),
	    1,
	    TEXT("Enable the Arda compute + CUDA graph postprocess fixture."),
	    ECVF_RenderThreadSafe);

	BEGIN_SHADER_PARAMETER_STRUCT(FArdaPostProcessNativeParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, ArdaInput)
	SHADER_PARAMETER_UAV(RWTexture2D<float4>, ArdaOutput)
	SHADER_PARAMETER(FUintVector2, ArdaExtent)
	SHADER_PARAMETER(FUintVector2, ArdaOrigin)
	SHADER_PARAMETER(uint32, ArdaValidation)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FArdaPostProcessParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ArdaInput)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ArdaOutput)
	SHADER_PARAMETER(FUintVector2, ArdaExtent)
	SHADER_PARAMETER(FUintVector2, ArdaOrigin)
	SHADER_PARAMETER(uint32, ArdaValidation)
	END_SHADER_PARAMETER_STRUCT()

	class FArdaPostProcessCS final : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FArdaPostProcessCS);
		SHADER_USE_PARAMETER_STRUCT(FArdaPostProcessCS, FGlobalShader);

	public:
		// FParameters is the alias required by Unreal's shader parameter protocol.
		using FParameters = FArdaPostProcessNativeParameters;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	class FArdaUnrealFinishCS final : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FArdaUnrealFinishCS);
		SHADER_USE_PARAMETER_STRUCT(FArdaUnrealFinishCS, FGlobalShader);

	public:
		using FParameters = FArdaPostProcessParameters;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FArdaPostProcessCS, "/ArdaUnrealTest/ArdaPostProcess.usf", "ArdaComputeMain", SF_Compute);
	IMPLEMENT_GLOBAL_SHADER(FArdaUnrealFinishCS,
	    "/ArdaUnrealTest/ArdaPostProcess.usf",
	    "ArdaUnrealFinishMain",
	    SF_Compute);

	BEGIN_SHADER_PARAMETER_STRUCT(FArdaGraphPassParameters, )
	RDG_TEXTURE_ACCESS(mInput, ERHIAccess::SRVCompute)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, mOutput)
	END_SHADER_PARAMETER_STRUCT()

	struct FArdaValidationState
	{
		std::atomic<int32> mResult{0};
		std::atomic<int32> mCompleted{0};
		std::atomic<int32> mGraphSubmissions{0};
		std::atomic<bool> mbStopping{false};
		FString mError;
		bool mbValidate = false;

		void Fail(const FString& Error)
		{
			if (mResult.load() == 0)
			{
				mError = Error;
				UE_LOG(LogTemp, Error, TEXT("ARDA_UNREAL_HANDOFF_FAIL: %s"), *Error);
				mResult.store(-1);
			}
		}
	};

	struct FArdaFrameSlot
	{
		eastl::shared_ptr<FArdaCudaHandoff> mHandoff;
		TRefCountPtr<IPooledRenderTarget> mPooledTexture;
		TUniquePtr<FRHIGPUTextureReadback> mReadback;
		TUniquePtr<FArdaDependencyGraph> mGraph;
		FArdaDependencyFrameTicket mTicket;
		uint32 mSubmittedFrame = 0;
		bool mbPending = false;
	};

	class FArdaPostProcessExtension final : public FSceneViewExtensionBase
	{
	public:
		FArdaPostProcessExtension(const FAutoRegister& AutoRegister,
		    TSharedRef<FArdaValidationState, ESPMode::ThreadSafe> State,
		    eastl::shared_ptr<FArdaUnrealTestCudaInterop> Interop)
		    : FSceneViewExtensionBase(AutoRegister),
		      mState(State),
		      mInterop(eastl::move(Interop))
		{
		}

		void SubscribeToPostProcessingPass(EPostProcessingPass Pass,
		    const FSceneView& View,
		    FPostProcessingPassDelegateArray& Callbacks,
		    bool bEnabled) override
		{
			if (Pass == EPostProcessingPass::Tonemap)
			{
				Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FArdaPostProcessExtension::Process));
			}
		}

		void Retire()
		{
			// The caller has drained the GPU. Clear graph-owned callbacks even if a view still retains this extension.
			for (FArdaFrameSlot& Slot : mSlots)
			{
				Slot.mbPending = false;
				Slot.mTicket = {};
				Slot.mGraph.Reset();
				Slot.mReadback.Reset();
				Slot.mPooledTexture.SafeRelease();
				Slot.mHandoff.reset();
			}
			mInterop.reset();
		}

	private:
		void RetireReadbacks()
		{
			for (FArdaFrameSlot& Slot : mSlots)
			{
				if (!Slot.mbPending || Slot.mSubmittedFrame == GFrameNumberRenderThread || !Slot.mReadback->IsReady())
				{
					continue;
				}
				if (Slot.mTicket)
				{
					const auto Complete = Slot.mGraph->IsComplete(Slot.mTicket);
					if (!Complete)
					{
						mState->Fail(UTF8_TO_TCHAR(Complete.mStatus.mMessage.c_str()));
						continue;
					}
					if (!Complete.mValue)
					{
						continue;
					}
					const FArdaGraphExecutionResult Execution = Slot.mGraph->Wait(Slot.mTicket);
					if (!Execution.mStatus)
					{
						mState->Fail(UTF8_TO_TCHAR(Execution.mStatus.mMessage.c_str()));
					}
					Slot.mTicket = {};
					Slot.mGraph.Reset();
				}
				int32 RowPitch = 0;
				int32 Height = 0;
				const FVector4f* Pixels = static_cast<const FVector4f*>(Slot.mReadback->Lock(RowPitch, &Height));
				if (!Pixels || RowPitch < 8 || Height < 8)
				{
					mState->Fail(TEXT("Final UE texture readback returned an invalid pointer or extent."));
				}
				else if (mState->mbValidate)
				{
					const FVector4f Expected(0.375f, 0.25f, 0.625f, 1.0f);
					for (int32 Y = 0; Y < 8; ++Y)
					{
						for (int32 X = 0; X < 8; ++X)
						{
							const FVector4f Actual = Pixels[Y * RowPitch + X];
							if (!Actual.Equals(Expected, 0.00001f))
							{
								mState->Fail(FString::Printf(
								    TEXT("Frame %d pixel (%d,%d): got (%g,%g,%g,%g), expected (.375,.25,.625,1)."),
								    mState->mCompleted.load(),
								    X,
								    Y,
								    Actual.X,
								    Actual.Y,
								    Actual.Z,
								    Actual.W));
							}
						}
					}
				}
				if (Pixels)
				{
					Slot.mReadback->Unlock();
				}
				Slot.mbPending = false;
				const int32 Completed = ++mState->mCompleted;
				if (mState->mbValidate && Completed >= 6 && mState->mResult.load() == 0)
				{
					UE_LOG(LogTemp,
					    Display,
					    TEXT(
					        "ARDA_UNREAL_HANDOFF_PASS: %d frames, %d two-node graph submissions, 64 RGBA pixels per frame, Unreal -> Arda -> CUDA -> Unreal."),
					    Completed,
					    mState->mGraphSubmissions.load());
					mState->mResult.store(1);
				}
			}
		}

		FScreenPassTexture Process(FRDGBuilder& GraphBuilder,
		    const FSceneView& View,
		    const FPostProcessMaterialInputs& Inputs)
		{
			RetireReadbacks();
			if (mState->mbStopping.load() || mState->mResult.load() != 0 ||
			    ArdaPostProcessEnabled.GetValueOnRenderThread() == 0)
			{
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
			const FScreenPassTexture SceneColor =
			    FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
			const FIntPoint Extent = SceneColor.ViewRect.Size();
			if (Extent.X < 8 || Extent.Y < 8)
			{
				mState->Fail(TEXT("The postprocess view must be at least 8 by 8 pixels."));
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
			FArdaFrameSlot* AvailableSlot = nullptr;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FArdaFrameSlot& Candidate = mSlots[(mNextSlot + Index) % 3];
				if (!Candidate.mbPending)
				{
					AvailableSlot = &Candidate;
					mNextSlot = (mNextSlot + Index + 1) % 3;
					break;
				}
			}
			if (!AvailableSlot)
			{
				// Bounded in-flight resources: defer this frame instead of blocking a CPU thread.
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
			FArdaFrameSlot& Slot = *AvailableSlot;
			if (!Slot.mHandoff || Slot.mHandoff->GetExtent() != Extent)
			{
				Slot.mPooledTexture.SafeRelease();
				Slot.mHandoff = eastl::make_shared<FArdaCudaHandoff>();
				FString Error;
				if (!Slot.mHandoff->Initialize(Extent, Error))
				{
					mState->Fail(Error);
					return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
				}
				mInterop->RegisterSurface(Slot.mHandoff);
				Slot.mPooledTexture = CreateRenderTarget(Slot.mHandoff->GetTexture(), TEXT("ArdaCudaSharedTexture"));
				Slot.mReadback = MakeUnique<FRHIGPUTextureReadback>(TEXT("ArdaUnrealFinalReadback"));
			}
			FRDGTextureRef SharedTexture = GraphBuilder.RegisterExternalTexture(Slot.mPooledTexture);
			FRDGTextureRef FinalTexture =
			    GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Extent,
			                                   PF_A32B32G32R32F,
			                                   FClearValueBinding::None,
			                                   ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV),
			        TEXT("ArdaUnrealFinalTexture"));
			const FIntVector Groups(FMath::DivideAndRoundUp(Extent.X, 8), FMath::DivideAndRoundUp(Extent.Y, 8), 1);
			TShaderMapRef<FArdaPostProcessCS> ArdaShader(GetGlobalShaderMap(View.GetFeatureLevel()));
			FArdaGraphPassParameters* ArdaParameters = GraphBuilder.AllocParameters<FArdaGraphPassParameters>();
			ArdaParameters->mInput = SceneColor.Texture;
			ArdaParameters->mOutput = GraphBuilder.CreateUAV(SharedTexture);
			const FIntPoint Origin = SceneColor.ViewRect.Min;
			GraphBuilder.AddPass(RDG_EVENT_NAME("Arda.DependencyGraph.ComputeThenCuda"),
			    ArdaParameters,
			    ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			    [ArdaShader, ArdaParameters, Groups, Origin, Extent, FrameSlot = &Slot, State = mState](
			        FRHICommandListImmediate& RHICmdList)
			    {
				    if (!InitializeUnrealBackend())
				    {
					    State->Fail(GetUnrealBackendError());
					    return;
				    }
				    FArdaUnrealRHIScope Scope(RHICmdList);
				    if (!Scope.GetStatus())
				    {
					    State->Fail(UTF8_TO_TCHAR(Scope.GetStatus().mMessage.c_str()));
					    return;
				    }
				    const FArdaRHIDeviceRef Device = GetUnrealBackendDevice();
				    auto ImportedShader = ImportUnrealComputeShader(Device, ArdaShader.GetComputeShader());
				    if (!ImportedShader)
				    {
					    State->Fail(UTF8_TO_TCHAR(ImportedShader.mStatus.mMessage.c_str()));
					    return;
				    }
				    FArdaRHIComputePipelineDesc PipelineDescription;
				    PipelineDescription.mComputeShader = ImportedShader.mValue;
				    const auto Pipeline = Device->CreateComputePipeline(PipelineDescription);
				    if (!Pipeline)
				    {
					    State->Fail(UTF8_TO_TCHAR(Pipeline.mStatus.mMessage.c_str()));
					    return;
				    }
				    const FTextureRHIRef InputTexture = ArdaParameters->mInput->GetRHI();
				    const FUnorderedAccessViewRHIRef OutputUAV = ArdaParameters->mOutput->GetRHI();
				    const FTextureRHIRef OutputTexture = FrameSlot->mHandoff->GetTexture();
				    const auto ImportedInput =
				        ImportUnrealTexture(Device, InputTexture, EArdaRHIResourceState::NonPixelShaderResource);
				    const auto ImportedOutput =
				        ImportUnrealTexture(Device, OutputTexture, EArdaRHIResourceState::UnorderedAccess);
				    if (!ImportedInput || !ImportedOutput)
				    {
					    const FArdaRHIStatus& Status = !ImportedInput ? ImportedInput.mStatus : ImportedOutput.mStatus;
					    State->Fail(FString::Printf(TEXT("Graph %s import failed: %s"),
					        !ImportedInput ? TEXT("scene-color") : TEXT("shared output"),
					        UTF8_TO_TCHAR(Status.mMessage.c_str())));
					    return;
				    }
				    auto Invocation = eastl::make_shared<FArdaUnrealComputeInvocation>();
				    Invocation->mPipeline = Pipeline.mValue;
				    Invocation->mGroups = Groups;
				    Invocation->mBind =
				        [ArdaShader, InputTexture, OutputUAV, Extent, Origin, bValidate = State->mbValidate](
				            FRHICommandListImmediate& Commands)
				    {
					    FArdaPostProcessNativeParameters Values;
					    Values.ArdaInput = InputTexture;
					    Values.ArdaOutput = OutputUAV;
					    Values.ArdaExtent = FUintVector2(Extent.X, Extent.Y);
					    Values.ArdaOrigin = FUintVector2(Origin.X, Origin.Y);
					    Values.ArdaValidation = bValidate ? 1u : 0u;
					    SetShaderParameters(Commands, ArdaShader, ArdaShader.GetComputeShader(), Values);
					    return FArdaRHIStatus{};
				    };
				    Invocation->mUnbind = [ArdaShader](FRHICommandListImmediate& Commands)
				    {
					    UnsetShaderUAVs(Commands, ArdaShader, ArdaShader.GetComputeShader());
					    return FArdaRHIStatus{};
				    };
				    FrameSlot->mGraph = MakeUnique<FArdaDependencyGraph>(Device);
				    FArdaDependencyGraph& Graph = *FrameSlot->mGraph;
				    FArdaRHIStatus EditStatus;
				    {
					    FArdaGraphEdit Edit(Graph);
					    FArdaInductorOptions Options;
					    Options.mbEnableAsyncCompute = false;
					    Options.mbEnableCopyQueue = false;
					    Options.mCudaGraphMode = EArdaCudaGraphMode::Disabled;
					    Edit.SetOptions(Options);
					    const FArdaGraphTexture Input = BindTexture(Edit, ImportedInput.mValue, "Unreal.SceneColor");
					    const FArdaGraphTexture Output =
					        BindTexture(Edit, ImportedOutput.mValue, "Unreal.CudaSharedPostprocess");
					    const FArdaUnrealComputeNodeParameters ComputeParameters{Input, Output, Invocation};
					    const FArdaUnrealCudaNodeParameters CudaParameters{Output,
					        FrameSlot->mHandoff,
					        uint32(Extent.X),
					        uint32(Extent.Y)};
					    const auto ComputeSupport =
					        FArdaUnrealComputeNode::CheckRequiredFeatures(Device, ComputeParameters);
					    const auto CudaSupport = FArdaUnrealCudaNode::CheckRequiredFeatures(Device, CudaParameters);
					    if (!ComputeSupport || !CudaSupport)
					    {
						    State->Fail(
						        UTF8_TO_TCHAR((!ComputeSupport ? ComputeSupport : CudaSupport).mMessage.c_str()));
						    return;
					    }
					    ArdaUnrealCompute(Edit, ComputeParameters, "Compute postprocess");
					    ArdaUnrealCuda(Edit, CudaParameters, "CUDA postprocess");
					    Edit.Keep(Output);
					    EditStatus = Edit.Commit();
				    }
				    if (!EditStatus)
				    {
					    State->Fail(UTF8_TO_TCHAR(EditStatus.mMessage.c_str()));
					    return;
				    }
				    const FArdaRHIStatus Compiled = Graph.Compile();
				    if (!Compiled)
				    {
					    State->Fail(UTF8_TO_TCHAR(Compiled.mMessage.c_str()));
					    return;
				    }
				    if (Graph.GetCompileResult().mExecutionOrder.size() != 2 ||
				        Graph.GetCompileResult().mCudaBatches.size() != 1)
				    {
					    State->Fail(
					        TEXT("The compiled Arda graph must contain both registered nodes and one CUDA batch."));
					    return;
				    }
				    FArdaGraphExecuteOptions ExecuteOptions;
				    ExecuteOptions.mbValidateResourceStates = false;
				    const auto Submitted = Graph.Submit(ExecuteOptions);
				    if (!Submitted)
				    {
					    State->Fail(UTF8_TO_TCHAR(Submitted.mStatus.mMessage.c_str()));
					    return;
				    }
				    FrameSlot->mTicket = Submitted.mValue;
				    ++State->mGraphSubmissions;
			    });
			TShaderMapRef<FArdaUnrealFinishCS> FinishShader(GetGlobalShaderMap(View.GetFeatureLevel()));
			FArdaPostProcessParameters* FinishParameters = GraphBuilder.AllocParameters<FArdaPostProcessParameters>();
			FinishParameters->ArdaInput = SharedTexture;
			FinishParameters->ArdaOutput = GraphBuilder.CreateUAV(FinalTexture);
			FinishParameters->ArdaExtent = FUintVector2(Extent.X, Extent.Y);
			FinishParameters->ArdaOrigin = FUintVector2::ZeroValue;
			FinishParameters->ArdaValidation = 0;
			FComputeShaderUtils::AddPass(GraphBuilder,
			    RDG_EVENT_NAME("Arda.UnrealConsumeCuda"),
			    FinishShader,
			    FinishParameters,
			    Groups);
			AddEnqueueCopyPass(GraphBuilder, Slot.mReadback.Get(), FinalTexture, FResolveRect(0, 0, 8, 8));
			Slot.mbPending = true;
			Slot.mSubmittedFrame = GFrameNumberRenderThread;
			FScreenPassTexture Output(FinalTexture, FIntRect(FIntPoint::ZeroValue, Extent));
			if (Inputs.OverrideOutput.IsValid())
			{
				AddDrawTexturePass(GraphBuilder, View, Output, Inputs.OverrideOutput);
				return Inputs.OverrideOutput;
			}
			return Output;
		}

		TSharedRef<FArdaValidationState, ESPMode::ThreadSafe> mState;
		eastl::shared_ptr<FArdaUnrealTestCudaInterop> mInterop;
		FArdaFrameSlot mSlots[3];
		int32 mNextSlot = 0;
	};

	class FArdaUnrealTestModule final : public FDefaultGameModuleImpl
	{
	public:
		void StartupModule() override
		{
			AddShaderSourceDirectoryMapping(TEXT("/ArdaUnrealTest"), FPaths::ProjectDir() / TEXT("Shaders"));
			mState = MakeShared<FArdaValidationState, ESPMode::ThreadSafe>();
			mState->mbValidate = FParse::Param(FCommandLine::Get(), TEXT("ArdaValidate"));
			mStartTime = FPlatformTime::Seconds();
			mPostInitHandle =
			    FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FArdaUnrealTestModule::StartExtension);
			mPreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FArdaUnrealTestModule::StopExtension);
			if (mState->mbValidate)
			{
				mTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				    FTickerDelegate::CreateRaw(this, &FArdaUnrealTestModule::Tick));
			}
		}

		void ShutdownModule() override
		{
			FCoreDelegates::GetOnPostEngineInit().Remove(mPostInitHandle);
			FCoreDelegates::OnEnginePreExit.Remove(mPreExitHandle);
			FTSTicker::GetCoreTicker().RemoveTicker(mTickerHandle);
			StopExtension();
		}

	private:
		void StopExtension()
		{
			mState->mbStopping.store(true);
			if (mExtension || mInterop)
			{
				ENQUEUE_RENDER_COMMAND(ArdaRetireTestExtension)(
				    [Extension = MoveTemp(mExtension), Interop = eastl::move(mInterop)](
				        FRHICommandListImmediate& RHICmdList) mutable
				    {
					    // Resource retirement is the only deliberate GPU idle in this fixture.
					    RHICmdList.SubmitAndBlockUntilGPUIdle();
					    if (Extension)
					    {
						    Extension->Retire();
					    }
					    Extension.Reset();
					    FArdaNodeRegistry::Get().Unregister(FArdaUnrealComputeNode::GetMetadata().mName);
					    FArdaNodeRegistry::Get().Unregister(FArdaUnrealCudaNode::GetMetadata().mName);
					    ShutdownUnrealBackend();
					    const bool bClearedInterop = SetUnrealCudaInterop(nullptr);
					    ensureAlwaysMsgf(bClearedInterop,
					        TEXT("Arda CUDA adapter could not be cleared before the project module unloads."));
					    Interop.reset();
				    });
				FlushRenderingCommands();
			}
		}

		void StartExtension()
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("ArdaDisablePostProcess")))
			{
				return;
			}
			mInterop = eastl::make_shared<FArdaUnrealTestCudaInterop>();
			// This read-only CVar is not render-thread safe; capture it once on the game thread.
			const IConsoleVariable* BarrierMode =
			    IConsoleManager::Get().FindConsoleVariable(TEXT("r.D3D12.PreferredBarrierImplementation"));
			const bool bLegacyBarriers = BarrierMode && BarrierMode->GetInt() == 1;
			ENQUEUE_RENDER_COMMAND(ArdaInitializeTestCuda)(
			    [Interop = mInterop, State = mState, bLegacyBarriers](FRHICommandListImmediate&)
			    {
				    FString Error;
				    if (!Interop->Initialize(Error, bLegacyBarriers))
				    {
					    State->Fail(Error);
					    return;
				    }
				    if (!SetUnrealCudaInterop(Interop))
				    {
					    State->Fail(TEXT("CUDA interop must be installed before Arda backend initialization."));
				    }
			    });
			mExtension = FSceneViewExtensions::NewExtension<FArdaPostProcessExtension>(mState.ToSharedRef(), mInterop);
		}

		bool Tick(float DeltaTime)
		{
			int32 Result = mState->mResult.load();
			if (Result == 0 && FPlatformTime::Seconds() - mStartTime < 600.0)
			{
				return true;
			}
			if (Result == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("ARDA_UNREAL_HANDOFF_FAIL: timeout; no six validated postprocess frames."));
				Result = -1;
			}
			const FString ResultPath = FPaths::ProjectSavedDir() / TEXT("ArdaHandoffResult.json");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResultPath), true);
			const FString Json = FString::Printf(
			    TEXT(
			        "{\"passed\":%s,\"completedFrames\":%d,\"graphSubmissions\":%d,\"registeredNodes\":2,\"pixelsPerFrame\":64,\"path\":\"Unreal-Arda-CUDA-Unreal\"}\n"),
			    Result > 0 ? TEXT("true") : TEXT("false"),
			    mState->mCompleted.load(),
			    mState->mGraphSubmissions.load());
			const bool bWritten = FFileHelper::SaveStringToFile(Json, *ResultPath);
			FPlatformMisc::RequestExitWithStatus(false, Result > 0 && bWritten ? 0 : 1);
			return false;
		}

		TSharedPtr<FArdaValidationState, ESPMode::ThreadSafe> mState;
		TSharedPtr<FArdaPostProcessExtension, ESPMode::ThreadSafe> mExtension;
		eastl::shared_ptr<FArdaUnrealTestCudaInterop> mInterop;
		FDelegateHandle mPostInitHandle;
		FDelegateHandle mPreExitHandle;
		FTSTicker::FDelegateHandle mTickerHandle;
		double mStartTime = 0.0;
	};
}

IMPLEMENT_PRIMARY_GAME_MODULE(arda::FArdaUnrealTestModule, ArdashirUnrealTest, "ArdashirUnrealTest");
