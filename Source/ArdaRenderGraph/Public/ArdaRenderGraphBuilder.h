#pragma once

#include "ArdaRenderGraphBlackboard.h"
#include "ArdaRenderGraphLog.h"
#include "ArdaRenderGraphParameters.h"
#include "ArdaRenderGraphPass.h"
#include "ArdaRenderGraphResources.h"

#include <EASTL/array.h>
#include <cstdint>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <new>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda::render_graph
{
    /** Records a live logical resource interval in execution-order indices. */
    struct FARDGResourceLifetime
    {
        /** The texture or buffer resource kind. */
        EARDGResourceType mType = EARDGResourceType::Texture;

        /** The resource registry index for mType. */
        uint32_t mResourceIndex = 0;

        /** The first execution-order index that uses the resource. */
        uint32_t mFirstUse = 0;

        /** The last execution-order index that uses the resource. */
        uint32_t mLastUse = 0;

        /** Whether the resource is eligible for transient allocation. */
        bool mbTransient = false;
    };

    /** Records one cross-queue dependency lowered by compilation. */
    struct FARDGQueueDependency
    {
        /** The pass whose submitted instance must complete first. */
        FARDGPassHandle mProducer;

        /** The pass whose queue waits for the producer. */
        FARDGPassHandle mConsumer;

        /** The producer queue pipeline. */
        EARDGPipeline mProducerPipeline = EARDGPipeline::Graphics;

        /** The consumer queue pipeline. */
        EARDGPipeline mConsumerPipeline = EARDGPipeline::Graphics;
    };

    /** Configures command-list recording for graph execution. */
    struct FARDGExecuteOptions
    {
        /** Records independent pass waves concurrently when true. */
        bool mbParallelRecording = true;

        /** Maximum recording workers, or zero to use hardware concurrency. */
        uint32_t mMaxRecordingThreads = 0;

        /** Captures and validates RHI/native state at every graph checkpoint. */
        bool mbValidateResourceStates = true;
    };

    /** Identifies when a render-graph resource-state snapshot was captured. */
    enum class EARDGStateCheckpoint : uint8_t
    {
        /** Captured before lowering a physical transition. */
        BeforeTransition,
        /** Captured after a conservative forced-Common barrier. */
        ForcedCommon,
        /** Captured after transition lowering. */
        AfterTransition,
        /** Captured after the pass callback completes. */
        AfterPass,
        /** Captured after the producer releases a resource in Common state. */
        QueueRelease,
        /** Captured after the consumer acquires a resource in Common state. */
        QueueAcquire
    };

    /** Records one expected RDG state and the state observed through ArdaRHI. */
    struct FARDGStateConformanceRecord
    {
        /** Pass associated with the state checkpoint. */
        FARDGPassHandle mPass;
        /** Human-readable pass name. */
        eastl::string mPassName;
        /** Texture or buffer resource kind. */
        EARDGResourceType mResourceType = EARDGResourceType::Texture;
        /** Resource registry index for mResourceType. */
        uint32_t mResourceIndex = 0;
        /** Human-readable logical resource name. */
        eastl::string mResourceName;
        /** Texture subresources, or the default range for a buffer. */
        rhi::FArdaRHITextureSubresourceRange mTextureSubresources;
        /** Checkpoint within transition recording or pass execution. */
        EARDGStateCheckpoint mCheckpoint =
            EARDGStateCheckpoint::BeforeTransition;
        /** State expected by physical RDG transition lowering. */
        rhi::EArdaRHIResourceState mExpectedState =
            rhi::EArdaRHIResourceState::Unknown;
        /** Queue expected to own the resource at an ownership checkpoint. */
        rhi::EArdaRHIQueueType mExpectedQueueOwner =
            rhi::EArdaRHIQueueType::Graphics;
        /** Expected Vulkan family, or the invalid-family sentinel on D3D12. */
        uint32_t mExpectedQueueFamily =
            rhi::ArdaRHIInvalidQueueFamily;
        /** Whether queue and native-family ownership participate in consistency. */
        bool mbValidateQueueOwnership = false;
        /** Independently observed facade/backend/native state. */
        rhi::FArdaRHIResourceStateSnapshot mObserved;
        /** Query status when the observation could not be produced. */
        rhi::FArdaRHIStatus mStatus;

        /**
         * Tests whether RDG, facade, backend, and native encoding agree.
         * @return True when every state source matches.
         */
        [[nodiscard]] bool IsConsistent() const noexcept
        {
            if (!mStatus.IsSuccess() || !mObserved.IsConsistent() ||
                mObserved.mFacadeState != mExpectedState)
            {
                return false;
            }
            if (!mbValidateQueueOwnership)
                return true;
            return mObserved.mbFacadeQueueOwnerKnown &&
                mObserved.mFacadeQueueOwner == mExpectedQueueOwner &&
                (mExpectedQueueFamily == rhi::ArdaRHIInvalidQueueFamily ||
                 mObserved.mNative.mQueueFamily == mExpectedQueueFamily);
        }
    };

    /** Reports the completed CPU submission phase of graph execution. */
    struct FARDGExecutionResult
    {
        /** Overall recording, conformance-validation, and submission status. */
        rhi::FArdaRHIStatus mStatus;

        /** Number of pass and boundary-barrier command lists submitted. */
        uint32_t mSubmittedCommandListCount = 0;

        /** Number of command lists rejected by the RHI during submission. */
        uint32_t mSubmissionFailureCount = 0;

        /** Number of explicit waits inserted between different queues. */
        uint32_t mQueueWaitCount = 0;

        /** Number of logical textures served by a reusable descriptor match. */
        uint32_t mTexturePoolReuseCount = 0;

        /** Number of logical buffers served by a reusable descriptor match. */
        uint32_t mBufferPoolReuseCount = 0;

        /** Whether pass command lists were recorded concurrently. */
        bool mbUsedParallelRecording = false;

        /** Whether RHI virtual-resource heaps were used. */
        bool mbUsedVirtualHeaps = false;

        /** Whether physical memory was safely aliased between resources. */
        bool mbUsedTransientAliasing = false;

        /** Whether transient candidates used committed-resource fallback. */
        bool mbUsedTransientFallback = false;

        /** Whether immediate-mode serial execution was selected. */
        bool mbUsedImmediateMode = false;

        /** Number of resources clobbered before a safely supported first write. */
        uint32_t mClobberedResourceCount = 0;

        /** Per-checkpoint RDG/facade/backend/native state evidence. */
        eastl::vector<FARDGStateConformanceRecord> mStateConformanceRecords;

        /** Number of state checkpoints that did not agree across all layers. */
        uint32_t mStateConformanceFailureCount = 0;

        /** Last submitted RHI instance for graphics, compute, and copy queues. */
        eastl::array<uint64_t, 3> mLastSubmittedInstances{};
    };

    /** Defines direct compute dispatch dimensions. */
    struct FARDGDispatchArguments
    {
        /** Thread-group count along X. */
        uint32_t mGroupCountX = 1;

        /** Thread-group count along Y. */
        uint32_t mGroupCountY = 1;

        /** Thread-group count along Z. */
        uint32_t mGroupCountZ = 1;
    };

    /** Defines direct hardware ray-dispatch dimensions. */
    struct FARDGRayDispatchArguments
    {
        /** Ray-generation launch width. */
        uint32_t mWidth = 1;

        /** Ray-generation launch height. */
        uint32_t mHeight = 1;

        /** Ray-generation launch depth. */
        uint32_t mDepth = 1;
    };

    /** Describes a texture handle requested from graph execution. */
    struct FARDGTextureExtraction
    {
        /** The logical texture to extract. */
        FARDGTextureRef mTexture = nullptr;

        /** Receives the physical handle after graph submission. */
        rhi::FArdaRHITextureRef* mOutput = nullptr;

        /** The state required when graph execution completes. */
        rhi::EArdaRHIResourceState mFinalState = rhi::EArdaRHIResourceState::Unknown;
    };

    /** Describes a buffer handle requested from graph execution. */
    struct FARDGBufferExtraction
    {
        /** The logical buffer to extract. */
        FARDGBufferRef mBuffer = nullptr;

        /** Receives the physical handle after graph submission. */
        rhi::FArdaRHIBufferRef* mOutput = nullptr;

        /** The state required when graph execution completes. */
        rhi::EArdaRHIResourceState mFinalState = rhi::EArdaRHIResourceState::Unknown;
    };

    /** Describes an acceleration-structure handle requested from graph execution. */
    struct FARDGAccelStructExtraction
    {
        /** The logical acceleration structure to extract. */
        FARDGAccelStructRef mAccelStruct = nullptr;

        /** Receives the physical handle after graph submission. */
        rhi::FArdaRHIAccelStructRef* mOutput = nullptr;

        /** The state required when graph execution completes. */
        rhi::EArdaRHIResourceState mFinalState =
            rhi::EArdaRHIResourceState::Unknown;
    };

    /** Immutable products emitted by device-independent graph compilation. */
    struct FARDGCompileResult
    {
        /** Synthetic graph-entry pass. */
        FARDGPassHandle mPrologue;

        /** Synthetic graph-exit pass. */
        FARDGPassHandle mEpilogue;

        /** Live passes in deterministic registration order, including sentinels. */
        eastl::vector<FARDGPassHandle> mExecutionOrder;

        /** Number of compatible raster groups formed by compilation. */
        uint32_t mRasterGroupCount = 0;

        /** Live texture and buffer intervals in deterministic registry order. */
        eastl::vector<FARDGResourceLifetime> mResourceLifetimes;

        /** Cross-queue dependencies in consumer execution order. */
        eastl::vector<FARDGQueueDependency> mQueueDependencies;
    };

    class FARDGCompiler;
    class FARDGExecutor;

    /** Builds and compiles one deferred RHI render dependency graph. */
    class FARDGBuilder final
    {
    public:
        /** Opaque implementation record used by private compiler stages. */
        struct FImpl;

        /** Constructs a graph using a device and queue capability context. */
        explicit FARDGBuilder(FARDGRenderGraphContext Context = {});

        /** Releases all graph-scoped records and parameter storage. */
        ~FARDGBuilder();

        FARDGBuilder(const FARDGBuilder&) = delete;
        FARDGBuilder& operator=(const FARDGBuilder&) = delete;
        FARDGBuilder(FARDGBuilder&&) = delete;
        FARDGBuilder& operator=(FARDGBuilder&&) = delete;

        /** Allocates and constructs immutable pass parameter storage. */
        template <typename ParameterType, typename... ArgumentTypes>
        [[nodiscard]] ParameterType* AllocateParameters(ArgumentTypes&&... Arguments)
        {
            static_assert(
                eastl::is_standard_layout_v<ParameterType>,
                "Render-graph parameters must use standard layout.");

            void* Storage = AllocateParameterStorage(
                sizeof(ParameterType),
                alignof(ParameterType));
            ParameterType* Parameters = new (Storage) ParameterType(
                eastl::forward<ArgumentTypes>(Arguments)...);
            if constexpr (!eastl::is_trivially_destructible_v<ParameterType>)
            {
                RegisterParameterDestructor(
                    Parameters,
                    [](void* Address)
                    {
                        static_cast<ParameterType*>(Address)->~ParameterType();
                    });
            }
            MarkParameterStorage(Parameters);
            return Parameters;
        }

        /** Creates a deferred logical texture. */
        [[nodiscard]] FARDGTextureRef CreateTexture(
            rhi::FArdaRHITextureDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient);

        /** Creates a deferred logical buffer. */
        [[nodiscard]] FARDGBufferRef CreateBuffer(
            rhi::FArdaRHIBufferDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient);

        /** Creates a deferred logical acceleration structure. */
        [[nodiscard]] FARDGAccelStructRef CreateAccelStruct(
            rhi::FArdaRHIAccelStructDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::None);

        /** Creates a logical texture shader-resource view. */
        [[nodiscard]] FARDGTextureSRVRef CreateTextureSRV(
            eastl::string Name,
            FARDGTextureViewDesc Desc);

        /** Creates a logical texture unordered-access view. */
        [[nodiscard]] FARDGTextureUAVRef CreateTextureUAV(
            eastl::string Name,
            FARDGTextureViewDesc Desc);

        /** Creates a logical buffer shader-resource view. */
        [[nodiscard]] FARDGBufferSRVRef CreateBufferSRV(
            eastl::string Name,
            FARDGBufferViewDesc Desc);

        /** Creates a logical buffer unordered-access view. */
        [[nodiscard]] FARDGBufferUAVRef CreateBufferUAV(
            eastl::string Name,
            FARDGBufferViewDesc Desc);

        /** UE-style alias for creating a texture SRV. */
        [[nodiscard]] FARDGTextureSRVRef CreateSRV(
            eastl::string Name,
            FARDGTextureViewDesc Desc)
        {
            return CreateTextureSRV(eastl::move(Name), eastl::move(Desc));
        }

        /** UE-style alias for creating a buffer SRV. */
        [[nodiscard]] FARDGBufferSRVRef CreateSRV(
            eastl::string Name,
            FARDGBufferViewDesc Desc)
        {
            return CreateBufferSRV(eastl::move(Name), eastl::move(Desc));
        }

        /** UE-style alias for creating a texture UAV. */
        [[nodiscard]] FARDGTextureUAVRef CreateUAV(
            eastl::string Name,
            FARDGTextureViewDesc Desc)
        {
            return CreateTextureUAV(eastl::move(Name), eastl::move(Desc));
        }

        /** UE-style alias for creating a buffer UAV. */
        [[nodiscard]] FARDGBufferUAVRef CreateUAV(
            eastl::string Name,
            FARDGBufferViewDesc Desc)
        {
            return CreateBufferUAV(eastl::move(Name), eastl::move(Desc));
        }

        /** Imports an externally owned texture into the logical graph. */
        [[nodiscard]] FARDGTextureRef RegisterExternalTexture(
            rhi::FArdaRHITextureRef Texture,
            rhi::EArdaRHIResourceState InitialState,
            eastl::string Name = {});

        /** Imports an externally owned buffer into the logical graph. */
        [[nodiscard]] FARDGBufferRef RegisterExternalBuffer(
            rhi::FArdaRHIBufferRef Buffer,
            rhi::EArdaRHIResourceState InitialState,
            eastl::string Name = {});

        /** Imports an externally owned acceleration structure. */
        [[nodiscard]] FARDGAccelStructRef RegisterExternalAccelStruct(
            rhi::FArdaRHIAccelStructRef AccelStruct,
            rhi::EArdaRHIResourceState InitialState,
            eastl::string Name = {});

        /** Imports a texture using the initial state stored in its descriptor. */
        [[nodiscard]] FARDGTextureRef RegisterExternalTexture(
            rhi::FArdaRHITextureRef Texture,
            eastl::string Name = {})
        {
            if (!Texture)
            {
                ARDA_CHECK_MSG("Cannot import a null RHI texture.");
            }
            return RegisterExternalTexture(
                Texture,
                Texture->GetDesc().mInitialState,
                eastl::move(Name));
        }

        /** Imports a buffer using the initial state stored in its descriptor. */
        [[nodiscard]] FARDGBufferRef RegisterExternalBuffer(
            rhi::FArdaRHIBufferRef Buffer,
            eastl::string Name = {})
        {
            if (!Buffer)
            {
                ARDA_CHECK_MSG("Cannot import a null RHI buffer.");
            }
            return RegisterExternalBuffer(
                Buffer,
                Buffer->GetDesc().mInitialState,
                eastl::move(Name));
        }

        /** Creates a logical uniform buffer from graph-scoped parameter contents. */
        template <typename ParameterType>
        [[nodiscard]] FARDGUniformBufferRef CreateUniformBuffer(
            eastl::string Name,
            const ParameterType* Parameters)
        {
            if (Parameters == nullptr)
            {
                ARDA_CHECK_MSG(
                    "Cannot create a render-graph uniform buffer from null parameters.");
            }
            const ParameterType* FrozenParameters = FreezeParameters(Parameters);
            return CreateUniformBufferInternal(
                eastl::move(Name),
                sizeof(ParameterType),
                &ParameterType::GetStaticMetadata(),
                FrozenParameters);
        }

        /** Declares that a logical texture must survive graph completion. */
        void QueueTextureExtraction(
            FARDGTextureRef Texture,
            rhi::FArdaRHITextureRef* Output,
            rhi::EArdaRHIResourceState FinalState);

        /** Declares texture extraction using an RHI reference. */
        void QueueTextureExtraction(
            FARDGTextureRef Texture,
            rhi::FArdaRHITextureRef& Output,
            rhi::EArdaRHIResourceState FinalState)
        {
            QueueTextureExtraction(Texture, eastl::addressof(Output), FinalState);
        }

        /** Declares that a logical buffer must survive graph completion. */
        void QueueBufferExtraction(
            FARDGBufferRef Buffer,
            rhi::FArdaRHIBufferRef* Output,
            rhi::EArdaRHIResourceState FinalState);

        /** Declares buffer extraction using an RHI reference. */
        void QueueBufferExtraction(
            FARDGBufferRef Buffer,
            rhi::FArdaRHIBufferRef& Output,
            rhi::EArdaRHIResourceState FinalState)
        {
            QueueBufferExtraction(Buffer, eastl::addressof(Output), FinalState);
        }

        /** Declares that a logical acceleration structure survives graph completion. */
        void QueueAccelStructExtraction(
            FARDGAccelStructRef AccelStruct,
            rhi::FArdaRHIAccelStructRef* Output,
            rhi::EArdaRHIResourceState FinalState);

        /** Declares acceleration-structure extraction using an RHI reference. */
        void QueueAccelStructExtraction(
            FARDGAccelStructRef AccelStruct,
            rhi::FArdaRHIAccelStructRef& Output,
            rhi::EArdaRHIResourceState FinalState)
        {
            QueueAccelStructExtraction(
                AccelStruct, eastl::addressof(Output), FinalState);
        }

        /**
         * Adds a blocking host-to-device buffer copy pass. SourceData is
         * copied into graph-owned storage immediately, matching Unreal's
         * QueueBufferUpload ownership behavior.
         */
        [[nodiscard]] FARDGPassHandle AddHostToDeviceCopyPass(
            FARDGBufferRef Destination,
            const void* SourceData,
            size_t Size,
            uint64_t DestinationOffset = 0,
            eastl::string Name = "HostToDeviceCopy");

        /** Adds a nonblocking host-to-device copy pass with a GPU callback. */
        [[nodiscard]] FARDGPassHandle AddHostToDeviceCopyPassAsync(
            FARDGBufferRef Destination,
            const void* SourceData,
            size_t Size,
            rhi::FArdaRHIHostToDeviceCopyCallback Completion,
            uint64_t DestinationOffset = 0,
            eastl::string Name = "HostToDeviceCopyAsync");

        /** Adds a blocking device-to-host buffer readback pass. */
        [[nodiscard]] FARDGPassHandle AddDeviceToHostCopyPass(
            FARDGBufferRef Source,
            eastl::vector<uint8_t>& Output,
            uint64_t SourceOffset = 0,
            uint64_t Size = rhi::ArdaRHIWholeBuffer,
            eastl::string Name = "DeviceToHostCopy");

        /** Adds a nonblocking device-to-host readback pass with owned bytes. */
        [[nodiscard]] FARDGPassHandle AddDeviceToHostCopyPassAsync(
            FARDGBufferRef Source,
            rhi::FArdaRHIDeviceToHostCopyCallback Completion,
            uint64_t SourceOffset = 0,
            uint64_t Size = rhi::ArdaRHIWholeBuffer,
            eastl::string Name = "DeviceToHostCopyAsync");

        /** Unreal-style alias for a graph-owned host buffer upload. */
        [[nodiscard]] FARDGPassHandle QueueBufferUpload(
            FARDGBufferRef Destination,
            const void* SourceData,
            size_t Size,
            uint64_t DestinationOffset = 0,
            eastl::string Name = "QueueBufferUpload")
        {
            return AddHostToDeviceCopyPass(
                Destination, SourceData, Size, DestinationOffset,
                eastl::move(Name));
        }

        /** Unreal-style alias for an asynchronous GPU buffer readback pass. */
        [[nodiscard]] FARDGPassHandle AddEnqueueCopyPass(
            FARDGBufferRef Source,
            rhi::FArdaRHIDeviceToHostCopyCallback Completion,
            uint64_t SourceOffset = 0,
            uint64_t Size = rhi::ArdaRHIWholeBuffer,
            eastl::string Name = "EnqueueBufferReadback")
        {
            return AddDeviceToHostCopyPassAsync(
                Source, eastl::move(Completion), SourceOffset, Size,
                eastl::move(Name));
        }

        /**
         * Registers a typed lambda pass and freezes its parameter storage.
         *
         * Supported lambda signatures accept a pass execution context or RHI
         * command list, optionally followed by the immutable parameter object.
         * Context physical-resource getters validate declarations at pass time.
         * A raw command-list callback can use independently retained RHI
         * handles, so declaration completeness cannot be proven for that form.
         */
        template <typename ParameterType, typename ExecuteType>
        [[nodiscard]] FARDGPassHandle AddPass(
            eastl::string Name,
            const ParameterType* Parameters,
            EARDGPassFlags Flags,
            ExecuteType&& Execute)
        {
            if (Parameters == nullptr)
            {
                ARDA_CHECK_MSG(
                    "Cannot register a render-graph pass with null parameters.");
            }
            const ParameterType* FrozenParameters = FreezeParameters(Parameters);
            using StoredExecuteType = eastl::decay_t<ExecuteType>;
            FARDGPassExecuteFunction ExecuteFunction =
                [Function = StoredExecuteType(eastl::forward<ExecuteType>(Execute)),
                 FrozenParameters](FARDGPassExecutionContext& Context) mutable
                {
                    InvokePassLambda(Function, Context, *FrozenParameters);
                };

            return AddPassInternal(
                eastl::move(Name),
                Flags,
                FrozenParameters,
                &ParameterType::GetStaticMetadata(),
                eastl::move(ExecuteFunction));
        }

        /** Registers a parameterless lambda pass. */
        template <typename ExecuteType>
        [[nodiscard]] FARDGPassHandle AddPass(
            eastl::string Name,
            EARDGPassFlags Flags,
            ExecuteType&& Execute)
        {
            using StoredExecuteType = eastl::decay_t<ExecuteType>;
            FARDGPassExecuteFunction ExecuteFunction =
                [Function = StoredExecuteType(eastl::forward<ExecuteType>(Execute))](
                    FARDGPassExecutionContext& Context) mutable
                {
                    InvokeParameterlessPassLambda(Function, Context);
                };
            return AddPassInternal(
                eastl::move(Name),
                Flags,
                nullptr,
                nullptr,
                eastl::move(ExecuteFunction));
        }

        /**
         * Registers a typed compute pass that dispatches after its setup lambda.
         *
         * The setup lambda uses the same supported signatures as AddPass. It
         * should bind the RHI compute state required by the dispatch.
         */
        template <typename ParameterType, typename ExecuteType>
        [[nodiscard]] FARDGPassHandle AddDispatchPass(
            eastl::string Name,
            const ParameterType* Parameters,
            FARDGDispatchArguments Dispatch,
            ExecuteType&& Setup,
            EARDGPassFlags Flags = EARDGPassFlags::Compute)
        {
            Flags |= EARDGPassFlags::Compute;
            return AddPass(
                eastl::move(Name),
                Parameters,
                Flags,
                [Function = eastl::decay_t<ExecuteType>(
                     eastl::forward<ExecuteType>(Setup)),
                 Dispatch](
                    FARDGPassExecutionContext& Context,
                    const ParameterType& FrozenParameters) mutable
                {
                    InvokePassLambda(
                        Function,
                        Context,
                        FrozenParameters);
                    Context.mUnsafeRawCommandList.Dispatch(
                        Dispatch.mGroupCountX,
                        Dispatch.mGroupCountY,
                        Dispatch.mGroupCountZ);
                });
        }

        /** Registers a typed hardware ray-tracing pass that dispatches after setup. */
        template <typename ParameterType, typename ExecuteType>
        [[nodiscard]] FARDGPassHandle AddRayDispatchPass(
            eastl::string Name,
            const ParameterType* Parameters,
            FARDGRayDispatchArguments Dispatch,
            ExecuteType&& Setup,
            EARDGPassFlags Flags = EARDGPassFlags::Compute)
        {
            Flags |= EARDGPassFlags::Compute;
            return AddPass(
                eastl::move(Name),
                Parameters,
                Flags,
                [Function = eastl::decay_t<ExecuteType>(
                     eastl::forward<ExecuteType>(Setup)),
                 Dispatch](
                    FARDGPassExecutionContext& Context,
                    const ParameterType& FrozenParameters) mutable
                {
                    InvokePassLambda(Function, Context, FrozenParameters);
                    Context.mUnsafeRawCommandList.DispatchRays(
                        Dispatch.mWidth,
                        Dispatch.mHeight,
                        Dispatch.mDepth);
                });
        }

        /** Adds an explicit producer dependency between registered passes. */
        void AddDependency(FARDGPassHandle Producer, FARDGPassHandle Consumer);

        /** Compiles culling, queues, async metadata, and raster groups once. */
        [[nodiscard]] const FARDGCompileResult& Compile();

        /**
         * Compiles, records, and submits the graph once.
         *
         * Submission is asynchronous with respect to GPU completion. RHI
         * retains command-list resources, and device garbage collection runs
         * once at this graph-submission boundary.
         */
        [[nodiscard]] const FARDGExecutionResult& Execute(
            const FARDGExecuteOptions& Options = {});

        /** Returns the latest execution report, or null before Execute. */
        [[nodiscard]] const FARDGExecutionResult*
        GetLastExecutionResult() const noexcept;

        /** Returns whether this graph has completed device-independent compilation. */
        [[nodiscard]] bool IsCompiled() const noexcept;

        /** Returns the immutable device and queue capability context. */
        [[nodiscard]] const FARDGRenderGraphContext& GetContext() const noexcept;

        /** Returns the graph-scoped typed blackboard. */
        [[nodiscard]] FARDGBlackboard& GetBlackboard();

        /** Returns the immutable graph-scoped typed blackboard. */
        [[nodiscard]] const FARDGBlackboard& GetBlackboard() const noexcept;

        /** Returns the synthetic graph-entry pass handle. */
        [[nodiscard]] FARDGPassHandle GetProloguePass() const noexcept;

        /** Returns the synthetic graph-exit pass handle after compilation. */
        [[nodiscard]] FARDGPassHandle GetEpiloguePass() const noexcept;

        /** Returns a registered pass, or null for an invalid handle. */
        [[nodiscard]] FARDGPass* TryGetPass(FARDGPassHandle Handle) noexcept;

        /** Returns an immutable registered pass, or null for an invalid handle. */
        [[nodiscard]] const FARDGPass* TryGetPass(FARDGPassHandle Handle) const noexcept;

        /** Returns a registered texture, or null for an invalid handle. */
        [[nodiscard]] FARDGTexture* TryGetTexture(FARDGTextureHandle Handle) noexcept;

        /** Returns an immutable registered texture, or null for an invalid handle. */
        [[nodiscard]] const FARDGTexture* TryGetTexture(
            FARDGTextureHandle Handle) const noexcept;

        /** Returns a registered buffer, or null for an invalid handle. */
        [[nodiscard]] FARDGBuffer* TryGetBuffer(FARDGBufferHandle Handle) noexcept;

        /** Returns an immutable registered buffer, or null for an invalid handle. */
        [[nodiscard]] const FARDGBuffer* TryGetBuffer(
            FARDGBufferHandle Handle) const noexcept;

        [[nodiscard]] FARDGAccelStruct* TryGetAccelStruct(
            FARDGAccelStructHandle Handle) noexcept;
        [[nodiscard]] const FARDGAccelStruct* TryGetAccelStruct(
            FARDGAccelStructHandle Handle) const noexcept;

        /** Returns a registered logical view, or null for an invalid handle. */
        [[nodiscard]] FARDGView* TryGetView(FARDGViewHandle Handle) noexcept;

        /** Returns an immutable logical view, or null for an invalid handle. */
        [[nodiscard]] const FARDGView* TryGetView(
            FARDGViewHandle Handle) const noexcept;

        /** Returns a logical uniform buffer, or null for an invalid handle. */
        [[nodiscard]] FARDGUniformBuffer* TryGetUniformBuffer(
            FARDGUniformBufferHandle Handle) noexcept;

        /** Returns an immutable uniform buffer, or null for an invalid handle. */
        [[nodiscard]] const FARDGUniformBuffer* TryGetUniformBuffer(
            FARDGUniformBufferHandle Handle) const noexcept;

        /** Returns texture extraction declarations in registration order. */
        [[nodiscard]] const eastl::vector<FARDGTextureExtraction>&
        GetTextureExtractions() const noexcept;

        /** Returns buffer extraction declarations in registration order. */
        [[nodiscard]] const eastl::vector<FARDGBufferExtraction>&
        GetBufferExtractions() const noexcept;

        /** Returns acceleration-structure extraction declarations in registration order. */
        [[nodiscard]] const eastl::vector<FARDGAccelStructExtraction>&
        GetAccelStructExtractions() const noexcept;

        /** Returns a deterministic textual description of the compiled graph. */
        [[nodiscard]] eastl::string DumpGraph() const;

    private:
        friend class FARDGCompiler;
        friend class FARDGExecutor;
        friend class FARDGPassExecutionContext;

        [[nodiscard]] void* AllocateParameterStorage(size_t Size, size_t Alignment);
        void RegisterParameterDestructor(void* Object, void (*Destroy)(void*));
        void MarkParameterStorage(const void* Parameters);
        [[nodiscard]] bool IsParameterStorage(const void* Parameters) const noexcept;

        template <typename ParameterType>
        [[nodiscard]] const ParameterType* FreezeParameters(const ParameterType* Parameters)
        {
            if (IsParameterStorage(Parameters))
            {
                return Parameters;
            }
            return AllocateParameters<ParameterType>(*Parameters);
        }

        [[nodiscard]] FARDGUniformBufferRef CreateUniformBufferInternal(
            eastl::string Name,
            size_t ByteSize,
            const FARDGParameterMetadata* Metadata,
            const void* Contents);

        [[nodiscard]] FARDGPassHandle AddPassInternal(
            eastl::string Name,
            EARDGPassFlags Flags,
            const void* Parameters,
            const FARDGParameterMetadata* Metadata,
            FARDGPassExecuteFunction Execute);

        void BeginPassAccess(FARDGPassHandle Pass);
        void EndPassAccess(FARDGPassHandle Pass) noexcept;
        [[nodiscard]] rhi::IArdaRHITexture* ResolveTextureForPass(
            FARDGPassHandle Pass,
            FARDGTexture* Texture) const;
        [[nodiscard]] rhi::IArdaRHITexture* ResolveTextureViewForPass(
            FARDGPassHandle Pass,
            FARDGView* View) const;
        [[nodiscard]] rhi::IArdaRHIBuffer* ResolveBufferForPass(
            FARDGPassHandle Pass,
            FARDGBuffer* Buffer) const;
        [[nodiscard]] rhi::IArdaRHIBuffer* ResolveBufferViewForPass(
            FARDGPassHandle Pass,
            FARDGView* View) const;
        [[nodiscard]] rhi::IArdaRHIAccelStruct* ResolveAccelStructForPass(
            FARDGPassHandle Pass,
            FARDGAccelStruct* AccelStruct) const;
        [[nodiscard]] rhi::IArdaRHIBuffer* ResolveUniformBufferForPass(
            FARDGPassHandle Pass,
            FARDGUniformBuffer* UniformBuffer) const;
        [[nodiscard]] rhi::FArdaRHIBindingSetRef CreateBindingSetForPass(
            FARDGPassHandle Pass,
            rhi::IArdaRHIBindingLayout* BindingLayout) const;
        [[nodiscard]] rhi::FArdaRHIBindingSetRef CreateBindingSetForPass(
            FARDGPassHandle Pass,
            const backend::FArdaShaderParameterMetadata& ShaderParameters,
            rhi::IArdaRHIBindingLayout* BindingLayout) const;
        [[nodiscard]] rhi::FArdaRHIBindingSetRef CreateBindingSetForPassInternal(
            FARDGPassHandle Pass,
            const backend::FArdaShaderParameterMetadata* ShaderParameters,
            rhi::IArdaRHIBindingLayout* BindingLayout) const;

        template <typename ExecuteType, typename ParameterType>
        static void InvokePassLambda(
            ExecuteType& Execute,
            FARDGPassExecutionContext& Context,
            const ParameterType& Parameters)
        {
            if constexpr (eastl::is_invocable_v<
                              ExecuteType&,
                              FARDGPassExecutionContext&,
                              const ParameterType&>)
            {
                Execute(Context, Parameters);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   const ParameterType&,
                                   FARDGPassExecutionContext&>)
            {
                Execute(Parameters, Context);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   rhi::IArdaRHICommandList&,
                                   const ParameterType&>)
            {
                Execute(Context.mUnsafeRawCommandList, Parameters);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   const ParameterType&,
                                   rhi::IArdaRHICommandList&>)
            {
                Execute(Parameters, Context.mUnsafeRawCommandList);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   FARDGPassExecutionContext&>)
            {
                Execute(Context);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   rhi::IArdaRHICommandList&>)
            {
                Execute(Context.mUnsafeRawCommandList);
            }
            else if constexpr (eastl::is_invocable_v<ExecuteType&, const ParameterType&>)
            {
                Execute(Parameters);
            }
            else if constexpr (eastl::is_invocable_v<ExecuteType&>)
            {
                Execute();
            }
            else
            {
                static_assert(
                    eastl::is_invocable_v<ExecuteType&>,
                    "Unsupported render-graph pass lambda signature.");
            }
        }

        template <typename ExecuteType>
        static void InvokeParameterlessPassLambda(
            ExecuteType& Execute,
            FARDGPassExecutionContext& Context)
        {
            if constexpr (eastl::is_invocable_v<ExecuteType&, FARDGPassExecutionContext&>)
            {
                Execute(Context);
            }
            else if constexpr (eastl::is_invocable_v<ExecuteType&, rhi::IArdaRHICommandList&>)
            {
                Execute(Context.mUnsafeRawCommandList);
            }
            else if constexpr (eastl::is_invocable_v<ExecuteType&>)
            {
                Execute();
            }
            else
            {
                static_assert(
                    eastl::is_invocable_v<ExecuteType&>,
                    "Unsupported render-graph pass lambda signature.");
            }
        }

        eastl::unique_ptr<FImpl> mImpl;
    };
}
