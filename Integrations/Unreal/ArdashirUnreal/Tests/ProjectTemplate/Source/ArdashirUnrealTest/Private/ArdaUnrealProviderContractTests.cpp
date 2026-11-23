#include "ArdaUnrealRuntime.h"

#include "BackendImpl/ArdaUnrealBackend.h"

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

#include <EASTL/shared_ptr.h>

#if WITH_DEV_AUTOMATION_TESTS

namespace arda
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArdaUnrealProviderContractTest,
	    "Ardashir.Unreal.ProviderContract",
	    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FArdaUnrealProviderContractTest::RunTest(const FString& Parameters)
	{
		static_cast<void>(Parameters);
		ENQUEUE_RENDER_COMMAND(ArdaValidateProviderContract)(
		    [this](FRHICommandListImmediate& RHICmdList)
		    {
			    if (!TestTrue(TEXT("Unreal backend initializes"), InitializeUnrealBackend()))
			    {
				    AddError(GetUnrealBackendError());
				    return;
			    }
			    const FArdaRHIDeviceRef Device = GetUnrealBackendDevice();
			    if (!TestTrue(TEXT("Runtime exports its registered device"), static_cast<bool>(Device)))
			    {
				    return;
			    }
			    TestTrue(TEXT("Repeated initialization succeeds"), InitializeUnrealBackend());
			    TestTrue(TEXT("Initialization preserves the shared device registry"),
			        Device.Get() == GetUnrealBackendDevice().Get());

#if __has_include("RHI/Config/ArdaBackendFeatures.h")
			    if (const auto Features = Device->GetFeatures())
			    {
				    TestTrue(TEXT("The host graphics queue is supported"), Features->Supports("queue.graphics"));
				    TestFalse(TEXT("No independent compute queue is advertised"), Features->Supports("queue.compute"));
				    TestFalse(TEXT("No independent copy queue is advertised"), Features->Supports("queue.copy"));
				    TestFalse(TEXT("Unsupported ray pipelines are not advertised"),
				        Features->Supports("ray-tracing.pipeline"));
				    TestFalse(TEXT("Unsupported placed heaps are not advertised"), Features->Supports("memory.explicit-heap"));
			    }
			    else
			    {
				    AddError(TEXT("The Unreal backend did not publish a feature list"));
			    }
#else
			    const FArdaRHICapabilities& Capabilities = Device->GetCapabilities();
			    TestTrue(TEXT("The host graphics queue is supported"),
			        Capabilities.IsQueueSupported(EArdaRHIQueueType::Graphics));
			    TestFalse(TEXT("No independent compute queue is advertised"),
			        Capabilities.IsQueueSupported(EArdaRHIQueueType::Compute));
			    TestFalse(TEXT("No independent copy queue is advertised"),
			        Capabilities.IsQueueSupported(EArdaRHIQueueType::Copy));
			    TestFalse(TEXT("Unsupported ray pipelines are not advertised"),
			        Capabilities.mRayTracing.mbPipelineShaders);
			    TestFalse(TEXT("Unsupported placed heaps are not advertised"), Capabilities.mbHeaps);
#endif

			    const auto OutsideList = Device->CreateCommandList(EArdaRHIQueueType::Graphics, true);
			    TestFalse(TEXT("Recording cannot begin without a borrowed Unreal list"),
			        OutsideList && OutsideList.mValue && OutsideList.mValue->Open());

			    FArdaRHICommandListRef EscapedList;
			    {
				    FArdaUnrealRHIScope Scope(RHICmdList);
				    if (!TestTrue(TEXT("Render-thread immediate scope is accepted"),
				            static_cast<bool>(Scope.GetStatus())))
				    {
					    return;
				    }
				    {
					    FArdaUnrealRHIScope Nested(RHICmdList);
					    TestFalse(TEXT("Nested borrowing is rejected"), static_cast<bool>(Nested.GetStatus()));
				    }

				    const auto Deferred = Device->CreateCommandList(EArdaRHIQueueType::Graphics, false);
				    TestTrue(TEXT("The graph can record deferred work inside the borrowed scope"),
				        static_cast<bool>(Deferred));
				    if (Deferred)
				    {
					    int32 CallbackOrder = 0;
					    TestTrue(TEXT("Deferred recording opens"), static_cast<bool>(Deferred.mValue->Open()));
					    TestTrue(TEXT("First host operation records"),
					        static_cast<bool>(RecordUnrealCommands(
					            [&CallbackOrder](FRHICommandListImmediate&)
					            {
						            CallbackOrder = CallbackOrder * 10 + 1;
						            return FArdaRHIStatus{};
					            })));
					    TestTrue(TEXT("Second host operation records"),
					        static_cast<bool>(RecordUnrealCommands(
					            [&CallbackOrder](FRHICommandListImmediate&)
					            {
						            CallbackOrder = CallbackOrder * 10 + 2;
						            return FArdaRHIStatus{};
					            })));
					    TestTrue(TEXT("Deferred recording closes"), static_cast<bool>(Deferred.mValue->Close()));
					    TestEqual(TEXT("Recording does not execute deferred callbacks"), CallbackOrder, 0);
					    TestTrue(TEXT("Deferred execution succeeds"),
					        static_cast<bool>(Device->ExecuteCommandList(Deferred.mValue)));
					    TestEqual(TEXT("Deferred callbacks execute in recording order"), CallbackOrder, 12);
					    TestFalse(TEXT("The same deferred recording cannot execute twice"),
					        static_cast<bool>(Device->ExecuteCommandList(Deferred.mValue)));
					    TestEqual(TEXT("Rejected resubmission leaves prior work unchanged"), CallbackOrder, 12);
				    }

				    const auto FailingDeferred = Device->CreateCommandList(EArdaRHIQueueType::Graphics, false);
				    if (TestTrue(TEXT("A failure-case deferred list is created"), static_cast<bool>(FailingDeferred)))
				    {
					    // Owning captures remain valid while the provider retains the failed submission's fence.
					    const auto CallbackOrder = eastl::make_shared<int32>(0);
					    TestTrue(TEXT("Failure-case recording opens"),
					        static_cast<bool>(FailingDeferred.mValue->Open()));
					    TestTrue(TEXT("A successful prefix records"),
					        static_cast<bool>(RecordUnrealCommands(
					            [CallbackOrder](FRHICommandListImmediate&)
					            {
						            *CallbackOrder = *CallbackOrder * 10 + 1;
						            return FArdaRHIStatus{};
					            })));
					    TestTrue(TEXT("An execution failure records without running"),
					        static_cast<bool>(RecordUnrealCommands(
					            [CallbackOrder](FRHICommandListImmediate&)
					            {
						            *CallbackOrder = *CallbackOrder * 10 + 2;
						            return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
						                "Intentional provider contract replay failure.");
					            })));
					    TestTrue(TEXT("An operation after the failure records"),
					        static_cast<bool>(RecordUnrealCommands(
					            [CallbackOrder](FRHICommandListImmediate&)
					            {
						            *CallbackOrder = *CallbackOrder * 10 + 3;
						            return FArdaRHIStatus{};
					            })));
					    TestTrue(TEXT("Failure-case recording closes"),
					        static_cast<bool>(FailingDeferred.mValue->Close()));
					    TestEqual(TEXT("Failure-case callbacks remain deferred"), *CallbackOrder, 0);
					    const auto Submission = Device->ExecuteCommandList(FailingDeferred.mValue);
					    TestFalse(TEXT("Replay reports its callback failure"), static_cast<bool>(Submission));
					    TestTrue(TEXT("Replay preserves the callback's failure code"),
					        Submission.mStatus.mCode == EArdaRHIResult::BackendFailure);
					    TestEqual(TEXT("Replay stops after the failing callback"), *CallbackOrder, 12);
					    TestFalse(TEXT("A partially replayed list cannot execute again"),
					        static_cast<bool>(Device->ExecuteCommandList(FailingDeferred.mValue)));
					    TestEqual(TEXT("Rejected retry cannot duplicate the successful prefix"), *CallbackOrder, 12);
				    }
				    TestFalse(TEXT("Independent compute submission is rejected"),
				        static_cast<bool>(Device->CreateCommandList(EArdaRHIQueueType::Compute, true)));
				    TestFalse(TEXT("Independent copy submission is rejected"),
				        static_cast<bool>(Device->CreateCommandList(EArdaRHIQueueType::Copy, true)));
				    TestFalse(TEXT("An implicit GPU idle cannot be introduced inside a borrowed scope"),
				        static_cast<bool>(Device->WaitForIdle()));

				    FArdaRHITextureDesc CommonTexture;
				    CommonTexture.mWidth = 4;
				    CommonTexture.mHeight = 4;
				    CommonTexture.mFormat = EArdaRHIFormat::RGBA8UNorm;
				    CommonTexture.mUsage = EArdaRHITextureUsage::ShaderResource;
				    CommonTexture.mInitialState = EArdaRHIResourceState::Common;
				    const auto RejectedTexture = Device->CreateTexture(CommonTexture);
				    TestTrue(TEXT("Common texture state is rejected before Unreal resource creation"),
				        !RejectedTexture && RejectedTexture.mStatus.mCode == EArdaRHIResult::Unsupported);
				    FArdaRHIBufferDesc CommonBuffer;
				    CommonBuffer.mByteSize = 64;
				    CommonBuffer.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Raw;
				    CommonBuffer.mInitialState = EArdaRHIResourceState::Common;
				    const auto RejectedBuffer = Device->CreateBuffer(CommonBuffer);
				    TestTrue(TEXT("Common buffer state is rejected before Unreal resource creation"),
				        !RejectedBuffer && RejectedBuffer.mStatus.mCode == EArdaRHIResult::Unsupported);

				    // Failed nested borrowing must leave the outer recording context usable.
				    const auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Graphics, true);
				    if (TestTrue(TEXT("The outer scope still creates immediate lists"), static_cast<bool>(Commands)))
				    {
					    TestTrue(TEXT("The outer scope still opens its list"),
					        static_cast<bool>(Commands.mValue->Open()));
					    TestTrue(TEXT("An empty immediate list closes"), static_cast<bool>(Commands.mValue->Close()));
					    TestFalse(TEXT("Immediate Unreal recording cannot be silently rewound"),
					        static_cast<bool>(Commands.mValue->Reset()));
					    EscapedList = Commands.mValue;
				    }
			    }

			    const auto AfterScope = Device->CreateCommandList(EArdaRHIQueueType::Graphics, true);
			    TestFalse(TEXT("Scope destruction releases the borrowed recording context"),
			        AfterScope && AfterScope.mValue && AfterScope.mValue->Open());
			    {
				    FArdaUnrealRHIScope LaterScope(RHICmdList);
				    TestTrue(TEXT("A subsequent scope can borrow the same Unreal list"),
				        static_cast<bool>(LaterScope.GetStatus()));
				    if (EscapedList)
				    {
					    TestFalse(TEXT("A recording cannot be submitted through a later scope with the same host list"),
					        static_cast<bool>(Device->ExecuteCommandList(EscapedList)));
				    }
			    }
			    TestFalse(TEXT("A null Unreal texture cannot enter Arda"),
			        static_cast<bool>(ImportUnrealTexture(Device, nullptr, EArdaRHIResourceState::ShaderResource)));
			    TestFalse(TEXT("A null Unreal compute shader cannot enter Arda"),
			        static_cast<bool>(ImportUnrealComputeShader(Device, nullptr)));
		    });
		// Automation waits only for these CPU-side contract checks. No GPU idle is used.
		FlushRenderingCommands();
		return !HasAnyErrors();
	}
}

#endif
