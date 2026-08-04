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
    };

    /** Reports the completed CPU submission phase of graph execution. */
    struct FARDGExecutionResult
    {
        /** Number of pass and boundary-barrier command lists submitted. */
        uint32_t mSubmittedCommandListCount = 0;

        /** Number of explicit waits inserted between different queues. */
        uint32_t mQueueWaitCount = 0;

        /** Number of logical textures served by a reusable descriptor match. */
        uint32_t mTexturePoolReuseCount = 0;

        /** Number of logical buffers served by a reusable descriptor match. */
        uint32_t mBufferPoolReuseCount = 0;

        /** Whether pass command lists were recorded concurrently. */
        bool mbUsedParallelRecording = false;

        /** Whether NVRHI virtual-resource heaps were used. */
        bool mbUsedVirtualHeaps = false;

        /** Whether physical memory was safely aliased between resources. */
        bool mbUsedTransientAliasing = false;

        /** Whether transient candidates used committed-resource fallback. */
        bool mbUsedTransientFallback = false;

        /** Whether immediate-mode serial execution was selected. */
        bool mbUsedImmediateMode = false;

        /** Number of resources clobbered before a safely supported first write. */
        uint32_t mClobberedResourceCount = 0;

        /** Last submitted NVRHI instance for graphics, compute, and copy queues. */
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

    /** Describes a texture handle requested from graph execution. */
    struct FARDGTextureExtraction
    {
        /** The logical texture to extract. */
        FARDGTextureRef mTexture = nullptr;

        /** Receives the physical handle after graph submission. */
        nvrhi::TextureHandle* mOutput = nullptr;

        /** The state required when graph execution completes. */
        nvrhi::ResourceStates mFinalState = nvrhi::ResourceStates::Unknown;
    };

    /** Describes a buffer handle requested from graph execution. */
    struct FARDGBufferExtraction
    {
        /** The logical buffer to extract. */
        FARDGBufferRef mBuffer = nullptr;

        /** Receives the physical handle after graph submission. */
        nvrhi::BufferHandle* mOutput = nullptr;

        /** The state required when graph execution completes. */
        nvrhi::ResourceStates mFinalState = nvrhi::ResourceStates::Unknown;
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

    /** Builds and compiles one deferred NVRHI render dependency graph. */
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
            nvrhi::TextureDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient);

        /** Creates a deferred logical buffer. */
        [[nodiscard]] FARDGBufferRef CreateBuffer(
            nvrhi::BufferDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient);

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
            nvrhi::TextureHandle Texture,
            nvrhi::ResourceStates InitialState,
            eastl::string Name = {});

        /** Imports an externally owned buffer into the logical graph. */
        [[nodiscard]] FARDGBufferRef RegisterExternalBuffer(
            nvrhi::BufferHandle Buffer,
            nvrhi::ResourceStates InitialState,
            eastl::string Name = {});

        /** Imports a texture using the initial state stored in its descriptor. */
        [[nodiscard]] FARDGTextureRef RegisterExternalTexture(
            nvrhi::TextureHandle Texture,
            eastl::string Name = {})
        {
            if (!Texture)
            {
                ARDA_CHECK_MSG("Cannot import a null NVRHI texture.");
            }
            return RegisterExternalTexture(
                Texture,
                Texture->getDesc().initialState,
                eastl::move(Name));
        }

        /** Imports a buffer using the initial state stored in its descriptor. */
        [[nodiscard]] FARDGBufferRef RegisterExternalBuffer(
            nvrhi::BufferHandle Buffer,
            eastl::string Name = {})
        {
            if (!Buffer)
            {
                ARDA_CHECK_MSG("Cannot import a null NVRHI buffer.");
            }
            return RegisterExternalBuffer(
                Buffer,
                Buffer->getDesc().initialState,
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
            nvrhi::TextureHandle* Output,
            nvrhi::ResourceStates FinalState);

        /** Declares texture extraction using an NVRHI handle reference. */
        void QueueTextureExtraction(
            FARDGTextureRef Texture,
            nvrhi::TextureHandle& Output,
            nvrhi::ResourceStates FinalState)
        {
            QueueTextureExtraction(Texture, eastl::addressof(Output), FinalState);
        }

        /** Declares that a logical buffer must survive graph completion. */
        void QueueBufferExtraction(
            FARDGBufferRef Buffer,
            nvrhi::BufferHandle* Output,
            nvrhi::ResourceStates FinalState);

        /** Declares buffer extraction using an NVRHI handle reference. */
        void QueueBufferExtraction(
            FARDGBufferRef Buffer,
            nvrhi::BufferHandle& Output,
            nvrhi::ResourceStates FinalState)
        {
            QueueBufferExtraction(Buffer, eastl::addressof(Output), FinalState);
        }

        /**
         * Registers a typed lambda pass and freezes its parameter storage.
         *
         * Supported lambda signatures accept a pass execution context or NVRHI
         * command list, optionally followed by the immutable parameter object.
         * Context physical-resource getters validate declarations at pass time.
         * A raw command-list callback can use independently retained NVRHI
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
         * should bind the NVRHI compute state required by the dispatch.
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
                    Context.mCommandList.dispatch(
                        Dispatch.mGroupCountX,
                        Dispatch.mGroupCountY,
                        Dispatch.mGroupCountZ);
                });
        }

        /** Adds an explicit producer dependency between registered passes. */
        void AddDependency(FARDGPassHandle Producer, FARDGPassHandle Consumer);

        /** Compiles culling, queues, async metadata, and raster groups once. */
        [[nodiscard]] const FARDGCompileResult& Compile();

        /**
         * Compiles, records, and submits the graph once.
         *
         * Submission is asynchronous with respect to GPU completion. NVRHI
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
        [[nodiscard]] nvrhi::ITexture* ResolveTextureForPass(
            FARDGPassHandle Pass,
            FARDGTexture* Texture) const;
        [[nodiscard]] nvrhi::ITexture* ResolveTextureViewForPass(
            FARDGPassHandle Pass,
            FARDGView* View) const;
        [[nodiscard]] nvrhi::IBuffer* ResolveBufferForPass(
            FARDGPassHandle Pass,
            FARDGBuffer* Buffer) const;
        [[nodiscard]] nvrhi::IBuffer* ResolveBufferViewForPass(
            FARDGPassHandle Pass,
            FARDGView* View) const;
        [[nodiscard]] nvrhi::IBuffer* ResolveUniformBufferForPass(
            FARDGPassHandle Pass,
            FARDGUniformBuffer* UniformBuffer) const;
        [[nodiscard]] nvrhi::BindingSetHandle CreateBindingSetForPass(
            FARDGPassHandle Pass,
            nvrhi::IBindingLayout* BindingLayout) const;

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
                                   nvrhi::ICommandList&,
                                   const ParameterType&>)
            {
                Execute(Context.mCommandList, Parameters);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   const ParameterType&,
                                   nvrhi::ICommandList&>)
            {
                Execute(Parameters, Context.mCommandList);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   FARDGPassExecutionContext&>)
            {
                Execute(Context);
            }
            else if constexpr (eastl::is_invocable_v<
                                   ExecuteType&,
                                   nvrhi::ICommandList&>)
            {
                Execute(Context.mCommandList);
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
            else if constexpr (eastl::is_invocable_v<ExecuteType&, nvrhi::ICommandList&>)
            {
                Execute(Context.mCommandList);
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
