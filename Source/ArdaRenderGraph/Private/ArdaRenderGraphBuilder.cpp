#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphCompiler.h"
#include "ArdaRenderGraphExecutor.h"
#include "ArdaRenderGraphLog.h"

#include <EASTL/algorithm.h>
#include <cstring>
#include <sstream>
#include <string>
#include <EASTL/unordered_set.h>

namespace arda::render_graph
{
    namespace
    {
        ARDG_BEGIN_PARAMETER_STRUCT(FARDGHostToDeviceCopyParameters)
            ARDG_BUFFER_ACCESS(mDestination)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FARDGDeviceToHostCopyParameters)
            ARDG_BUFFER_ACCESS(mSource)
        ARDG_END_PARAMETER_STRUCT()

        /**
         * Converts an EASTL string to the standard string type required by the RHI
         * descriptor debug names. This build-stage adapter copies exactly the
         * recorded byte range and does not mutate graph state.
         */
        [[nodiscard]] std::string ToStdString(const eastl::string& Value)
        {
            return std::string(Value.data(), Value.size());
        }

        /**
         * Classifies a declared pass state during graph setup.
         *
         * Any contained write bit makes the whole declaration a write for
         * dependency-history purposes; finer legality checks happen at compile time.
         */
        [[nodiscard]] bool IsWriteState(rhi::EArdaRHIResourceState State) noexcept
        {
            constexpr uint32_t WriteMask =
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::UnorderedAccess) |
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopyDest) |
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::ResolveDest) |
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::AccelStructWrite);
            return (static_cast<uint32_t>(State) & WriteMask) != 0;
        }

        /** Identifies the shader register namespace used by a parameter binding. */
        enum class EARDGBindingClass : uint8_t
        {
            None,
            ShaderResource,
            UnorderedAccess,
            ConstantBuffer
        };

        /** Maps generated parameter semantics to their RHI register namespace. */
        [[nodiscard]] EARDGBindingClass GetBindingClass(
            EARDGParameterType Type) noexcept
        {
            switch (Type)
            {
            case EARDGParameterType::Texture:
            case EARDGParameterType::Buffer:
            case EARDGParameterType::TextureShaderResourceView:
            case EARDGParameterType::BufferShaderResourceView:
            case EARDGParameterType::AccelStructAccess:
                return EARDGBindingClass::ShaderResource;

            case EARDGParameterType::TextureUnorderedAccessView:
            case EARDGParameterType::BufferUnorderedAccessView:
                return EARDGBindingClass::UnorderedAccess;

            case EARDGParameterType::UniformBuffer:
                return EARDGBindingClass::ConstantBuffer;

            case EARDGParameterType::Value:
            case EARDGParameterType::TextureAccess:
            case EARDGParameterType::BufferAccess:
            case EARDGParameterType::NestedStruct:
            case EARDGParameterType::RenderTargetBindingSlots:
                return EARDGBindingClass::None;
            }
            return EARDGBindingClass::None;
        }

        /** Returns whether a layout item can represent one parameter descriptor. */
        [[nodiscard]] bool IsCompatibleBindingType(
            EARDGParameterType ParameterType,
            rhi::EArdaRHIBindingType LayoutType) noexcept
        {
            switch (ParameterType)
            {
            case EARDGParameterType::Texture:
            case EARDGParameterType::TextureShaderResourceView:
                return LayoutType == rhi::EArdaRHIBindingType::TextureSRV;

            case EARDGParameterType::TextureUnorderedAccessView:
                return LayoutType == rhi::EArdaRHIBindingType::TextureUAV;

            case EARDGParameterType::Buffer:
            case EARDGParameterType::BufferShaderResourceView:
                return LayoutType == rhi::EArdaRHIBindingType::TypedBufferSRV ||
                    LayoutType == rhi::EArdaRHIBindingType::StructuredBufferSRV ||
                    LayoutType == rhi::EArdaRHIBindingType::RawBufferSRV;

            case EARDGParameterType::BufferUnorderedAccessView:
                return LayoutType == rhi::EArdaRHIBindingType::TypedBufferUAV ||
                    LayoutType == rhi::EArdaRHIBindingType::StructuredBufferUAV ||
                    LayoutType == rhi::EArdaRHIBindingType::RawBufferUAV;

            case EARDGParameterType::UniformBuffer:
                return LayoutType == rhi::EArdaRHIBindingType::ConstantBuffer ||
                    LayoutType == rhi::EArdaRHIBindingType::VolatileConstantBuffer;

            case EARDGParameterType::AccelStructAccess:
                return LayoutType ==
                    rhi::EArdaRHIBindingType::RayTracingAccelStruct;

            case EARDGParameterType::Value:
            case EARDGParameterType::TextureAccess:
            case EARDGParameterType::BufferAccess:
            case EARDGParameterType::NestedStruct:
            case EARDGParameterType::RenderTargetBindingSlots:
                return false;
            }
            return false;
        }

        /**
         * Returns the stable diagnostic label used by the compiled-graph dump.
         * Unknown enum values are rendered defensively instead of affecting execution.
         */
        [[nodiscard]] const char* GetPipelineName(EARDGPipeline Pipeline) noexcept
        {
            switch (Pipeline)
            {
            case EARDGPipeline::Graphics:
                return "Graphics";
            case EARDGPipeline::AsyncCompute:
                return "AsyncCompute";
            case EARDGPipeline::Copy:
                return "Copy";
            }
            return "Unknown";
        }

        /**
         * Appends a human-readable bitset for one RHI resource state.
         *
         * This debug-output helper preserves combined read states by joining every
         * recognized bit; it has no effect on barrier compilation or state tracking.
         */
        void AppendStateName(
            std::ostream& Stream,
            rhi::EArdaRHIResourceState State)
        {
            struct FStateName
            {
                /**
                 * Single RHI state bit matched while formatting a diagnostic
                 * state mask.
                 */
                rhi::EArdaRHIResourceState mState;

                /** Non-owning, static-lifetime label emitted when mState is present. */
                const char* mName;
            };
            static constexpr FStateName Names[] = {
                {rhi::EArdaRHIResourceState::Common, "Common"},
                {rhi::EArdaRHIResourceState::ConstantBuffer, "ConstantBuffer"},
                {rhi::EArdaRHIResourceState::VertexBuffer, "VertexBuffer"},
                {rhi::EArdaRHIResourceState::IndexBuffer, "IndexBuffer"},
                {rhi::EArdaRHIResourceState::IndirectArgument, "IndirectArgument"},
                {rhi::EArdaRHIResourceState::PixelShaderResource, "PixelSRV"},
                {rhi::EArdaRHIResourceState::NonPixelShaderResource, "NonPixelSRV"},
                {rhi::EArdaRHIResourceState::UnorderedAccess, "UAV"},
                {rhi::EArdaRHIResourceState::RenderTarget, "RenderTarget"},
                {rhi::EArdaRHIResourceState::DepthWrite, "DepthWrite"},
                {rhi::EArdaRHIResourceState::DepthRead, "DepthRead"},
                {rhi::EArdaRHIResourceState::CopyDest, "CopyDest"},
                {rhi::EArdaRHIResourceState::CopySource, "CopySource"},
                {rhi::EArdaRHIResourceState::ResolveDest, "ResolveDest"},
                {rhi::EArdaRHIResourceState::ResolveSource, "ResolveSource"},
                {rhi::EArdaRHIResourceState::Present, "Present"}};
            if (State == rhi::EArdaRHIResourceState::Unknown)
            {
                Stream << "Unknown";
                return;
            }
            bool bFirst = true;
            for (const FStateName& Name : Names)
            {
                if ((State & Name.mState) == Name.mState)
                {
                    if (!bFirst)
                    {
                        Stream << "|";
                    }
                    Stream << Name.mName;
                    bFirst = false;
                }
            }
        }

        /**
         * Reports whether graph-building mutation is still legal.
         *
         * Build APIs close as soon as compilation, execution, or failure begins,
         * preserving the append-only pass and resource registries.
         */
        [[nodiscard]] bool IsBuilding(
            const FARDGBuilder::FImpl& Graph) noexcept
        {
            return !Graph.mbCompiled &&
                !Graph.mbCompiling &&
                !Graph.mbExecutionStarted &&
                !Graph.mbFailed;
        }

        struct FARDGSetupContext
        {
            /**
             * Graph-owned pass registry used during setup; handles use the graph
             * pass index domain.
             */
            TARDGHandleRegistry<FARDGPass, FARDGPassHandle>& mPasses;

            /**
             * Graph-owned texture registry used to validate and resolve texture
             * declarations.
             */
            TARDGHandleRegistry<FARDGTexture, FARDGTextureHandle>& mTextures;

            /**
             * Graph-owned buffer registry used to validate and resolve buffer
             * declarations.
             */
            TARDGHandleRegistry<FARDGBuffer, FARDGBufferHandle>& mBuffers;
            TARDGHandleRegistry<FARDGAccelStruct, FARDGAccelStructHandle>& mAccelStructs;

            /**
             * Graph-owned view registry used to validate exact logical view
             * identities.
             */
            TARDGHandleRegistry<FARDGView, FARDGViewHandle>& mViews;

            /**
             * Graph-owned uniform-buffer registry used for recursive parameter
             * discovery.
             */
            TARDGHandleRegistry<FARDGUniformBuffer, FARDGUniformBufferHandle>& mUniformBuffers;

            /**
             * Pass currently being populated; its setup state remains valid for
             * the graph lifetime.
             */
            FARDGPass& mPass;

            /**
             * Uniform-buffer indices visited while setting up mPass; empty at
             * setup start and local to this pass.
             */
            eastl::unordered_set<uint32_t> mVisitedUniformBuffers;

            /**
             * Adds a build-stage data-flow predecessor to the pass.
             * Self-dependencies are suppressed because one pass may declare the same
             * resource more than once while metadata is enumerated.
             */
            void AddProducer(FARDGPassHandle Producer)
            {
                if (Producer != mPass.GetHandle())
                {
                    mPass.AddProducer(Producer);
                }
            }

            /**
             * Records an exact logical view used by the pass for execution-time
             * access validation. Duplicate parameter references remain one view entry.
             */
            void AddView(FARDGViewHandle View)
            {
                if (eastl::find(
                        mPass.GetState().mViews.begin(),
                        mPass.GetState().mViews.end(),
                        View) == mPass.GetState().mViews.end())
                {
                    mPass.GetState().mViews.push_back(View);
                }
            }

            /**
             * Records one texture access and updates build-time hazard history.
             *
             * Every access depends on the whole texture's latest writer. A read joins
             * the current reader epoch; a write orders after those readers, clears the
             * epoch, and becomes the latest writer. The selected subresources are
             * retained for later validation and barrier lowering, not edge discovery.
             */
            void AddTexture(
                FARDGTextureRef Texture,
                rhi::EArdaRHIResourceState State,
                rhi::FArdaRHITextureSubresourceRange Subresources = {})
            {
                if (Texture == nullptr)
                {
                    return;
                }
                if (mTextures.TryGet(Texture->GetHandle()) != Texture)
                {
                    ARDA_CHECK_MSG(
                        "A pass references a texture owned by another render graph.");
                }
                if (State == rhi::EArdaRHIResourceState::Unknown)
                {
                    ARDA_CHECK_MSG(
                        "A pass declares a texture access with an unknown state.");
                }

                const bool bWrite = IsWriteState(State);
                mPass.AddTextureState(
                    {Texture->GetHandle(), Subresources, State, bWrite});
                Texture->MarkUsed(mPass.GetHandle());

                AddProducer(Texture->GetLastProducer());
                if (bWrite)
                {
                    for (FARDGPassHandle Reader : Texture->GetReaders())
                    {
                        if (Reader != mPass.GetHandle())
                        {
                            mPass.AddSynchronizationProducer(Reader);
                        }
                    }
                    Texture->ClearReaders();
                    Texture->SetLastProducer(mPass.GetHandle());
                }
                else
                {
                    Texture->AddReader(mPass.GetHandle());
                }
            }

            /**
             * Records one buffer access and updates build-time hazard history.
             *
             * The algorithm mirrors AddTexture: RAW/WAW hazards use producer edges,
             * while WAR hazards use synchronization-only edges so dead readers do not
             * become live. Dependency history is whole-buffer even though Range is
             * preserved for declaration validation.
             */
            void AddBuffer(
                FARDGBufferRef Buffer,
                rhi::EArdaRHIResourceState State,
                rhi::FArdaRHIBufferRange Range = {})
            {
                if (Buffer == nullptr)
                {
                    return;
                }
                if (mBuffers.TryGet(Buffer->GetHandle()) != Buffer)
                {
                    ARDA_CHECK_MSG(
                        "A pass references a buffer owned by another render graph.");
                }
                if (State == rhi::EArdaRHIResourceState::Unknown)
                {
                    ARDA_CHECK_MSG(
                        "A pass declares a buffer access with an unknown state.");
                }

                const bool bWrite = IsWriteState(State);
                mPass.AddBufferState({Buffer->GetHandle(), Range, State, bWrite});
                Buffer->MarkUsed(mPass.GetHandle());

                AddProducer(Buffer->GetLastProducer());
                if (bWrite)
                {
                    for (FARDGPassHandle Reader : Buffer->GetReaders())
                    {
                        if (Reader != mPass.GetHandle())
                        {
                            mPass.AddSynchronizationProducer(Reader);
                        }
                    }
                    Buffer->ClearReaders();
                    Buffer->SetLastProducer(mPass.GetHandle());
                }
                else
                {
                    Buffer->AddReader(mPass.GetHandle());
                }
            }

            void AddAccelStruct(
                FARDGAccelStructRef AccelStruct,
                rhi::EArdaRHIResourceState State)
            {
                if (AccelStruct == nullptr)
                {
                    return;
                }
                if (mAccelStructs.TryGet(AccelStruct->GetHandle()) != AccelStruct)
                {
                    ARDA_CHECK_MSG(
                        "A pass references an acceleration structure owned by another render graph.");
                }
                if (State == rhi::EArdaRHIResourceState::Unknown)
                {
                    ARDA_CHECK_MSG(
                        "A pass declares acceleration-structure access with an unknown state.");
                }
                const bool bWrite = IsWriteState(State);
                mPass.AddAccelStructState(
                    {AccelStruct->GetHandle(), State, bWrite});
                AccelStruct->MarkUsed(mPass.GetHandle());
                AddProducer(AccelStruct->GetLastProducer());
                if (bWrite)
                {
                    for (FARDGPassHandle Reader : AccelStruct->GetReaders())
                    {
                        if (Reader != mPass.GetHandle())
                        {
                            mPass.AddSynchronizationProducer(Reader);
                        }
                    }
                    AccelStruct->ClearReaders();
                    AccelStruct->SetLastProducer(mPass.GetHandle());
                }
                else
                {
                    AccelStruct->AddReader(mPass.GetHandle());
                }
            }

            /**
             * Records a logical uniform buffer and recursively discovers resources
             * referenced by its frozen contents during pass setup.
             *
             * The visited set prevents duplicate traversal within this pass, which
             * also bounds recursive metadata graphs and keeps declarations stable.
             */
            void AddUniformBuffer(FARDGUniformBufferRef UniformBuffer)
            {
                if (UniformBuffer == nullptr)
                {
                    return;
                }
                if (mUniformBuffers.TryGet(UniformBuffer->GetHandle()) != UniformBuffer)
                {
                    ARDA_CHECK_MSG(
                        "A pass references a uniform buffer owned by another render graph.");
                }
                if (!mVisitedUniformBuffers.insert(
                        UniformBuffer->GetHandle().GetIndex()).second)
                {
                    return;
                }
                mPass.GetState().mUniformBuffers.push_back(
                    UniformBuffer->GetHandle());
                if (UniformBuffer->GetMetadata() != nullptr &&
                    UniformBuffer->GetContents() != nullptr)
                {
                    Visit(
                        *UniformBuffer->GetMetadata(),
                        UniformBuffer->GetContents());
                }
            }

            /**
             * Enumerates frozen parameter metadata during pass registration and
             * dispatches each leaf to the corresponding resource setup path.
             *
             * Besides collecting accesses and edges, view leaves preserve exact view
             * identity and raster bindings build the later grouping signature.
             */
            void Visit(const FARDGParameterMetadata& Metadata, const void* Parameters)
            {
                Metadata.Enumerate(
                    Parameters,
                    // Interpret each generated metadata leaf in declaration order so
                    // edge and state collection remain deterministic.
                    [this](const FARDGParameter& Parameter)
                    {
                        const FARDGParameterMember& Member = *Parameter.mMember;
                        switch (Member.mType)
                        {
                        case EARDGParameterType::Value:
                        case EARDGParameterType::NestedStruct:
                            break;

                        case EARDGParameterType::Texture:
                            AddTexture(
                                Parameter.GetValue<FARDGTextureRef>(),
                                Member.mDefaultState);
                            break;

                        case EARDGParameterType::Buffer:
                            AddBuffer(
                                Parameter.GetValue<FARDGBufferRef>(),
                                Member.mDefaultState);
                            break;

                        case EARDGParameterType::TextureShaderResourceView:
                        {
                            const FARDGTextureSRVRef View =
                                Parameter.GetValue<FARDGTextureSRVRef>();
                            if (View == nullptr)
                            {
                                break;
                            }
                            if (mViews.TryGet(View->GetHandle()) != View)
                            {
                                ARDA_CHECK_MSG(
                                    "A pass references a texture SRV owned by another graph.");
                            }
                            AddView(View->GetHandle());
                            AddTexture(
                                mTextures.TryGet(View->GetDesc().mTexture),
                                Member.mDefaultState,
                                View->GetDesc().mSubresources);
                            break;
                        }

                        case EARDGParameterType::TextureUnorderedAccessView:
                        {
                            const FARDGTextureUAVRef View =
                                Parameter.GetValue<FARDGTextureUAVRef>();
                            if (View == nullptr)
                            {
                                break;
                            }
                            if (mViews.TryGet(View->GetHandle()) != View)
                            {
                                ARDA_CHECK_MSG(
                                    "A pass references a texture UAV owned by another graph.");
                            }
                            AddView(View->GetHandle());
                            AddTexture(
                                mTextures.TryGet(View->GetDesc().mTexture),
                                Member.mDefaultState,
                                View->GetDesc().mSubresources);
                            break;
                        }

                        case EARDGParameterType::BufferShaderResourceView:
                        {
                            const FARDGBufferSRVRef View =
                                Parameter.GetValue<FARDGBufferSRVRef>();
                            if (View == nullptr)
                            {
                                break;
                            }
                            if (mViews.TryGet(View->GetHandle()) != View)
                            {
                                ARDA_CHECK_MSG(
                                    "A pass references a buffer SRV owned by another graph.");
                            }
                            AddView(View->GetHandle());
                            AddBuffer(
                                mBuffers.TryGet(View->GetDesc().mBuffer),
                                Member.mDefaultState,
                                View->GetDesc().mRange);
                            break;
                        }

                        case EARDGParameterType::BufferUnorderedAccessView:
                        {
                            const FARDGBufferUAVRef View =
                                Parameter.GetValue<FARDGBufferUAVRef>();
                            if (View == nullptr)
                            {
                                break;
                            }
                            if (mViews.TryGet(View->GetHandle()) != View)
                            {
                                ARDA_CHECK_MSG(
                                    "A pass references a buffer UAV owned by another graph.");
                            }
                            AddView(View->GetHandle());
                            AddBuffer(
                                mBuffers.TryGet(View->GetDesc().mBuffer),
                                Member.mDefaultState,
                                View->GetDesc().mRange);
                            break;
                        }

                        case EARDGParameterType::TextureAccess:
                        {
                            const FARDGTextureAccess& Access =
                                Parameter.GetValue<FARDGTextureAccess>();
                            AddTexture(
                                Access.mTexture,
                                Access.mState,
                                Access.mSubresources);
                            break;
                        }

                        case EARDGParameterType::BufferAccess:
                        {
                            const FARDGBufferAccess& Access =
                                Parameter.GetValue<FARDGBufferAccess>();
                            AddBuffer(Access.mBuffer, Access.mState, Access.mRange);
                            break;
                        }

                        case EARDGParameterType::AccelStructAccess:
                        {
                            const FARDGAccelStructAccess& Access =
                                Parameter.GetValue<FARDGAccelStructAccess>();
                            AddAccelStruct(Access.mAccelStruct, Access.mState);
                            break;
                        }

                        case EARDGParameterType::UniformBuffer:
                            AddUniformBuffer(
                                Parameter.GetValue<FARDGUniformBufferRef>());
                            break;

                        case EARDGParameterType::RenderTargetBindingSlots:
                        {
                            const FARDGRenderTargetBindingSlots& Bindings =
                                Parameter.GetValue<FARDGRenderTargetBindingSlots>();
                            for (size_t Index = 0; Index < Bindings.mColor.size(); ++Index)
                            {
                                const FARDGRenderTargetBinding& Binding =
                                    Bindings.mColor[Index];
                                if (Binding.mTexture != nullptr)
                                {
                                    mPass.GetState().mRasterBindings.mColor[Index] =
                                        Binding.mTexture->GetHandle();
                                    mPass.GetState()
                                        .mRasterBindings
                                        .mColorSubresources[Index] =
                                        Binding.mSubresources;
                                    AddTexture(
                                        Binding.mTexture,
                                        rhi::EArdaRHIResourceState::RenderTarget,
                                        Binding.mSubresources);
                                }
                            }
                            if (Bindings.mDepthStencil.mTexture != nullptr)
                            {
                                mPass.GetState().mRasterBindings.mDepthStencil =
                                    Bindings.mDepthStencil.mTexture->GetHandle();
                                mPass.GetState()
                                    .mRasterBindings
                                    .mDepthStencilSubresources =
                                    Bindings.mDepthStencil.mSubresources;
                                AddTexture(
                                    Bindings.mDepthStencil.mTexture,
                                    rhi::EArdaRHIResourceState::DepthWrite,
                                    Bindings.mDepthStencil.mSubresources);
                            }
                            break;
                        }
                        }
                    });
            }
        };

        /**
         * Validates mutually dependent pass flags at build time.
         *
         * This rejects unknown bits and impossible operation categories before a pass
         * enters the append-only registry; resource-state compatibility is validated
         * later by the compiler.
         */
        void ValidatePassFlags(EARDGPassFlags Flags)
        {
            constexpr uint16_t KnownFlags =
                static_cast<uint16_t>(EARDGPassFlags::Raster) |
                static_cast<uint16_t>(EARDGPassFlags::Compute) |
                static_cast<uint16_t>(EARDGPassFlags::AsyncCompute) |
                static_cast<uint16_t>(EARDGPassFlags::Copy) |
                static_cast<uint16_t>(EARDGPassFlags::NeverCull) |
                static_cast<uint16_t>(EARDGPassFlags::SkipRenderPass) |
                static_cast<uint16_t>(EARDGPassFlags::NeverParallel);
            const bool bRaster = HasAllFlags(Flags, EARDGPassFlags::Raster);
            const bool bCompute = HasAllFlags(Flags, EARDGPassFlags::Compute);
            const bool bAsync = HasAllFlags(Flags, EARDGPassFlags::AsyncCompute);
            const bool bCopy = HasAllFlags(Flags, EARDGPassFlags::Copy);

            if ((static_cast<uint16_t>(Flags) & ~KnownFlags) != 0 ||
                (bRaster && (bCompute || bAsync || bCopy)) ||
                (bCopy && (bCompute || bAsync)) ||
                (bAsync && !bCompute) ||
                (HasAllFlags(Flags, EARDGPassFlags::SkipRenderPass) && !bRaster))
            {
                ARDA_CHECK_MSG("Incompatible render-graph pass flags.");
            }
        }
    }

    /**
     * Starts the graph-building stage and creates its private arena-backed state.
     *
     * The implementation constructor installs the prologue sentinel. Graphics queue
     * capability is required because sentinels and fallback work use that pipeline.
     */
    FARDGBuilder::FARDGBuilder(FARDGRenderGraphContext Context)
        : mImpl(eastl::make_unique<FImpl>(eastl::move(Context)))
    {
        if (!mImpl->mContext.mQueueCapabilities.mbGraphics)
        {
            ARDA_CHECK_MSG(
                "A render graph requires graphics queue capability.");
        }
    }

    /**
     * Releases all graph-scoped registries, frozen parameters, and physical handles.
     * Arena-registered parameter destructors run as part of implementation teardown.
     */
    FARDGBuilder::~FARDGBuilder() = default;

    /**
     * Opens the execution-stage physical-resource access gate for one recording pass.
     *
     * The gate ties subsequent getters to the active pass and command list, preventing
     * access outside the callback or through undeclared graph resources.
     */
    FARDGPassExecutionContext::FARDGPassExecutionContext(
        FARDGBuilder& Graph,
        FARDGPassHandle Pass,
        rhi::IArdaRHICommandList& CommandList,
        EARDGPipeline Pipeline)
        : mUnsafeRawCommandList(CommandList)
        , mCommandList(CommandList)
        , mPipeline(Pipeline)
        , mGraph(Graph)
        , mPass(Pass)
    {
        mGraph.BeginPassAccess(mPass);
        mbAccessGateOpen = true;
    }

    /**
     * Closes the pass access gate when callback recording leaves its scope.
     * The guard flag keeps teardown safe if construction did not finish opening it.
     */
    FARDGPassExecutionContext::~FARDGPassExecutionContext() noexcept
    {
        if (mbAccessGateOpen)
        {
            mGraph.EndPassAccess(mPass);
        }
    }

    /** Resolves a directly declared logical texture during pass recording. */
    rhi::IArdaRHITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTexture* Texture) const
    {
        return mGraph.ResolveTextureForPass(mPass, Texture);
    }

    /** Resolves the parent physical texture of a declared logical SRV. */
    rhi::IArdaRHITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureSRV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    /** Resolves the parent physical texture of a declared logical UAV. */
    rhi::IArdaRHITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureUAV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    /** Resolves a directly declared logical buffer during pass recording. */
    rhi::IArdaRHIBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBuffer* Buffer) const
    {
        return mGraph.ResolveBufferForPass(mPass, Buffer);
    }

    /** Resolves the parent physical buffer of a declared logical SRV. */
    rhi::IArdaRHIBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferSRV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    /** Resolves the parent physical buffer of a declared logical UAV. */
    rhi::IArdaRHIBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferUAV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    /** Resolves a declared logical uniform buffer's physical constant buffer. */
    rhi::IArdaRHIBuffer* FARDGPassExecutionContext::GetUniformBuffer(
        FARDGUniformBuffer* UniformBuffer) const
    {
        return mGraph.ResolveUniformBufferForPass(mPass, UniformBuffer);
    }

    rhi::IArdaRHIAccelStruct* FARDGPassExecutionContext::GetAccelStruct(
        FARDGAccelStruct* AccelStruct) const
    {
        return mGraph.ResolveAccelStructForPass(mPass, AccelStruct);
    }

    /** Builds a binding set directly from the active pass parameter descriptors. */
    rhi::FArdaRHIBindingSetRef FARDGPassExecutionContext::CreateBindingSet(
        rhi::IArdaRHIBindingLayout* BindingLayout) const
    {
        return mGraph.CreateBindingSetForPass(mPass, BindingLayout);
    }

    rhi::FArdaRHIBindingSetRef FARDGPassExecutionContext::CreateBindingSet(
        const backend::FArdaShaderParameterMetadata& ShaderParameters,
        rhi::IArdaRHIBindingLayout* BindingLayout) const
    {
        if (BindingLayout == nullptr)
        {
            ARDA_CHECK_MSG("A registered shader binding layout is null.");
        }
        eastl::vector<rhi::FArdaRHIBindingLayoutDesc> Generated;
        const backend::FArdaShaderStructStatus Status =
            ShaderParameters.BuildBindingLayoutDescs(Generated);
        if (!Status)
        {
            ARDA_CHECK_MSG("Registered shader parameter metadata is invalid.");
        }
        const bool bMatches = eastl::any_of(
            Generated.begin(),
            Generated.end(),
            [BindingLayout](const rhi::FArdaRHIBindingLayoutDesc& Desc)
            {
                return Desc == BindingLayout->GetDesc();
            });
        if (!bMatches)
        {
            ARDA_CHECK_MSG(
                "The supplied binding layout was not generated from the registered shader metadata.");
        }
        return mGraph.CreateBindingSetForPass(
            mPass,
            ShaderParameters,
            BindingLayout);
    }

    rhi::FArdaRHIBindingSetRef FARDGPassExecutionContext::CreateBindingSet(
        const backend::FArdaGlobalShaderInstance& Shader,
        size_t LayoutIndex) const
    {
        const backend::FArdaShaderParameterMetadata* Metadata =
            Shader.GetParameterMetadata();
        if (Metadata == nullptr ||
            LayoutIndex >= Shader.GetBindingLayouts().size())
        {
            ARDA_CHECK_MSG(
                "The registered shader has no requested parameter binding layout.");
        }
        return CreateBindingSet(
            *Metadata,
            Shader.GetBindingLayouts()[LayoutIndex].Get());
    }

    eastl::vector<rhi::FArdaRHIBindingSetRef>
    FARDGPassExecutionContext::CreateBindingSets(
        const backend::FArdaGlobalShaderInstance& Shader) const
    {
        eastl::vector<rhi::FArdaRHIBindingSetRef> Result;
        Result.reserve(Shader.GetBindingLayouts().size());
        for (size_t LayoutIndex = 0;
             LayoutIndex < Shader.GetBindingLayouts().size();
             ++LayoutIndex)
        {
            Result.push_back(CreateBindingSet(Shader, LayoutIndex));
        }
        return Result;
    }

    /**
     * Opens the execution-stage access gate for a live pass.
     *
     * The mutex supports parallel command recording. A pass may have only one active
     * context, and execution must have begun without already finishing or failing.
     */
    void FARDGBuilder::BeginPassAccess(FARDGPassHandle Pass)
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
        if (!mImpl->mbExecutionStarted ||
            mImpl->mbExecuted ||
            mImpl->mbFailed ||
            PassRecord == nullptr ||
            PassRecord->GetState().mbCulled ||
            PassRecord->GetState().mbSentinel ||
            !mImpl->mActivePassAccess.insert(Pass.GetIndex()).second)
        {
            ARDA_CHECK_MSG(
                "A render-graph pass physical-access gate cannot be opened.");
        }
    }

    /**
     * Closes a pass's execution-stage access gate.
     * Erasing a missing entry is harmless, which keeps context destruction noexcept.
     */
    void FARDGBuilder::EndPassAccess(FARDGPassHandle Pass) noexcept
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        mImpl->mActivePassAccess.erase(Pass.GetIndex());
    }

    /**
     * Resolves a logical texture to its materialized RHI object during recording.
     *
     * Ownership, active-pass scope, physical availability, and declaration membership
     * are all checked under the access mutex before the raw pointer is returned.
     */
    rhi::IArdaRHITexture* FARDGBuilder::ResolveTextureForPass(
        FARDGPassHandle Pass,
        FARDGTexture* Texture) const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
        if (Texture == nullptr ||
            PassRecord == nullptr ||
            mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                mImpl->mActivePassAccess.end() ||
            mImpl->mTextures.TryGet(Texture->GetHandle()) != Texture ||
            !Texture->GetTexture())
        {
            ARDA_CHECK_MSG(
                "A pass requested an unavailable render-graph texture.");
        }
        // A parent texture may have been declared directly or indirectly through a
        // view; both paths contribute a texture-state record during setup.
        const bool bDeclared = eastl::any_of(
            PassRecord->GetState().mTextureStates.begin(),
            PassRecord->GetState().mTextureStates.end(),
            [Texture](const FARDGPassTextureState& State)
            {
                return State.mTexture == Texture->GetHandle();
            });
        if (!bDeclared)
        {
            ARDA_CHECK_MSG(
                "A pass requested a texture absent from its parameter declarations.");
        }
        return Texture->GetTexture().Get();
    }

    /**
     * Validates an exact logical texture view, then resolves its parent texture.
     *
     * View identity is checked separately from parent access so a pass cannot use a
     * different subresource/format declaration merely because the parent was present.
     */
    rhi::IArdaRHITexture* FARDGBuilder::ResolveTextureViewForPass(
        FARDGPassHandle Pass,
        FARDGView* View) const
    {
        if (View == nullptr ||
            mImpl->mViews.TryGet(View->GetHandle()) != View)
        {
            ARDA_CHECK_MSG(
                "A pass requested a texture view owned by another graph.");
        }
        {
            std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
            const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
            if (PassRecord == nullptr ||
                mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                    mImpl->mActivePassAccess.end() ||
                eastl::find(
                    PassRecord->GetState().mViews.begin(),
                    PassRecord->GetState().mViews.end(),
                    View->GetHandle()) ==
                    PassRecord->GetState().mViews.end())
            {
                ARDA_CHECK_MSG(
                    "A pass requested a texture view absent from its parameters.");
            }
        }
        FARDGTextureHandle Texture;
        if (View->GetType() == EARDGResourceType::TextureShaderResourceView)
        {
            Texture =
                static_cast<FARDGTextureSRV*>(View)->GetDesc().mTexture;
        }
        else if (View->GetType() ==
                 EARDGResourceType::TextureUnorderedAccessView)
        {
            Texture =
                static_cast<FARDGTextureUAV*>(View)->GetDesc().mTexture;
        }
        else
        {
            ARDA_CHECK_MSG(
                "A pass requested a non-texture view as a texture.");
        }
        return ResolveTextureForPass(Pass, mImpl->mTextures.TryGet(Texture));
    }

    /**
     * Resolves a logical buffer to its materialized RHI object during recording.
     *
     * The same gated ownership and declaration checks used for textures protect
     * parallel pass callbacks from undeclared or cross-graph physical access.
     */
    rhi::IArdaRHIBuffer* FARDGBuilder::ResolveBufferForPass(
        FARDGPassHandle Pass,
        FARDGBuffer* Buffer) const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
        if (Buffer == nullptr ||
            PassRecord == nullptr ||
            mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                mImpl->mActivePassAccess.end() ||
            mImpl->mBuffers.TryGet(Buffer->GetHandle()) != Buffer ||
            !Buffer->GetBuffer())
        {
            ARDA_CHECK_MSG(
                "A pass requested an unavailable render-graph buffer.");
        }
        // Buffer ranges affect validation but any state entry for this logical buffer
        // establishes that its parent object was declared by the pass.
        const bool bDeclared = eastl::any_of(
            PassRecord->GetState().mBufferStates.begin(),
            PassRecord->GetState().mBufferStates.end(),
            [Buffer](const FARDGPassBufferState& State)
            {
                return State.mBuffer == Buffer->GetHandle();
            });
        if (!bDeclared)
        {
            ARDA_CHECK_MSG(
                "A pass requested a buffer absent from its parameter declarations.");
        }
        return Buffer->GetBuffer().Get();
    }

    rhi::IArdaRHIAccelStruct* FARDGBuilder::ResolveAccelStructForPass(
        FARDGPassHandle Pass,
        FARDGAccelStruct* AccelStruct) const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
        if (AccelStruct == nullptr ||
            PassRecord == nullptr ||
            mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                mImpl->mActivePassAccess.end() ||
            mImpl->mAccelStructs.TryGet(AccelStruct->GetHandle()) != AccelStruct ||
            !AccelStruct->GetAccelStruct())
        {
            ARDA_CHECK_MSG(
                "A pass requested an unavailable render-graph acceleration structure.");
        }
        const bool bDeclared = eastl::find_if(
            PassRecord->GetState().mAccelStructStates.begin(),
            PassRecord->GetState().mAccelStructStates.end(),
            [AccelStruct](const FARDGPassAccelStructState& State)
            {
                return State.mAccelStruct == AccelStruct->GetHandle();
            }) != PassRecord->GetState().mAccelStructStates.end();
        if (!bDeclared)
        {
            ARDA_CHECK_MSG(
                "A pass requested an acceleration structure absent from its parameters.");
        }
        return AccelStruct->GetAccelStruct().Get();
    }

    /**
     * Validates an exact logical buffer view, then resolves its parent buffer.
     *
     * This preserves the selected range/format declaration as part of pass identity
     * while returning the parent RHI buffer used to build binding items.
     */
    rhi::IArdaRHIBuffer* FARDGBuilder::ResolveBufferViewForPass(
        FARDGPassHandle Pass,
        FARDGView* View) const
    {
        if (View == nullptr ||
            mImpl->mViews.TryGet(View->GetHandle()) != View)
        {
            ARDA_CHECK_MSG(
                "A pass requested a buffer view owned by another graph.");
        }
        {
            std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
            const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
            if (PassRecord == nullptr ||
                mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                    mImpl->mActivePassAccess.end() ||
                eastl::find(
                    PassRecord->GetState().mViews.begin(),
                    PassRecord->GetState().mViews.end(),
                    View->GetHandle()) ==
                    PassRecord->GetState().mViews.end())
            {
                ARDA_CHECK_MSG(
                    "A pass requested a buffer view absent from its parameters.");
            }
        }
        FARDGBufferHandle Buffer;
        if (View->GetType() == EARDGResourceType::BufferShaderResourceView)
        {
            Buffer =
                static_cast<FARDGBufferSRV*>(View)->GetDesc().mBuffer;
        }
        else if (View->GetType() ==
                 EARDGResourceType::BufferUnorderedAccessView)
        {
            Buffer =
                static_cast<FARDGBufferUAV*>(View)->GetDesc().mBuffer;
        }
        else
        {
            ARDA_CHECK_MSG(
                "A pass requested a non-buffer view as a buffer.");
        }
        return ResolveBufferForPass(Pass, mImpl->mBuffers.TryGet(Buffer));
    }

    /**
     * Resolves a declared uniform buffer during execution-stage pass recording.
     *
     * The buffer must belong to this graph, be materialized, appear in the pass's
     * uniform-buffer declarations, and be requested while that pass gate is active.
     */
    rhi::IArdaRHIBuffer* FARDGBuilder::ResolveUniformBufferForPass(
        FARDGPassHandle Pass,
        FARDGUniformBuffer* UniformBuffer) const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        const FARDGPass* PassRecord = mImpl->mPasses.TryGet(Pass);
        if (UniformBuffer == nullptr ||
            PassRecord == nullptr ||
            mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                mImpl->mActivePassAccess.end() ||
            mImpl->mUniformBuffers.TryGet(UniformBuffer->GetHandle()) !=
                UniformBuffer ||
            !UniformBuffer->GetBuffer())
        {
            ARDA_CHECK_MSG(
                "A pass requested an unavailable graph uniform buffer.");
        }
        const bool bDeclared = eastl::find(
            PassRecord->GetState().mUniformBuffers.begin(),
            PassRecord->GetState().mUniformBuffers.end(),
            UniformBuffer->GetHandle()) !=
            PassRecord->GetState().mUniformBuffers.end();
        if (!bDeclared)
        {
            ARDA_CHECK_MSG(
                "A pass requested a uniform buffer absent from its parameters.");
        }
        return UniformBuffer->GetBuffer().Get();
    }

    /**
     * Materializes descriptor bindings from the active pass's frozen parameters.
     *
     * Parameter declaration order assigns independent t/u/b register slots. Arrays
     * occupy one slot and use RHI array elements. The supplied layout selects the
     * subset belonging to that binding set and supplies the concrete buffer-view
     * flavor (typed, structured, or raw).
     */
    rhi::FArdaRHIBindingSetRef FARDGBuilder::CreateBindingSetForPass(
        FARDGPassHandle Pass,
        rhi::IArdaRHIBindingLayout* BindingLayout) const
    {
        return CreateBindingSetForPassInternal(Pass, nullptr, BindingLayout);
    }

    rhi::FArdaRHIBindingSetRef FARDGBuilder::CreateBindingSetForPass(
        FARDGPassHandle Pass,
        const backend::FArdaShaderParameterMetadata& ShaderParameters,
        rhi::IArdaRHIBindingLayout* BindingLayout) const
    {
        return CreateBindingSetForPassInternal(
            Pass,
            &ShaderParameters,
            BindingLayout);
    }

    rhi::FArdaRHIBindingSetRef FARDGBuilder::CreateBindingSetForPassInternal(
        FARDGPassHandle Pass,
        const backend::FArdaShaderParameterMetadata* ShaderParameters,
        rhi::IArdaRHIBindingLayout* BindingLayout) const
    {
        if (BindingLayout == nullptr)
        {
            ARDA_CHECK_MSG(
                "Cannot create pass bindings from a null binding layout.");
        }

        const FARDGPass* PassRecord = nullptr;
        rhi::IArdaRHIDevice* Device = nullptr;
        {
            std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
            PassRecord = mImpl->mPasses.TryGet(Pass);
            Device = mImpl->mContext.mDevice.Get();
            if (PassRecord == nullptr ||
                mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                    mImpl->mActivePassAccess.end() ||
                PassRecord->GetParameters() == nullptr ||
                PassRecord->GetParameterMetadata() == nullptr ||
                Device == nullptr)
            {
                ARDA_CHECK_MSG(
                    "Pass bindings require active parameters and an RHI device.");
            }
        }

        const rhi::FArdaRHIBindingLayoutDesc& LayoutDesc = BindingLayout->GetDesc();

        auto MakeBufferItem =
            [](rhi::EArdaRHIBindingType Type,
               uint32_t Slot,
               rhi::IArdaRHIBuffer* Buffer,
               rhi::EArdaRHIFormat Format,
               rhi::FArdaRHIBufferRange Range)
            {
                rhi::FArdaRHIBindingItem Item;
                Item.mSlot = Slot;
                Item.mType = Type;
                Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(Buffer);
                Item.mView.mFormat = Format;
                Item.mView.mBufferRange = Range;
                switch (Type)
                {
                case rhi::EArdaRHIBindingType::TypedBufferSRV:
                case rhi::EArdaRHIBindingType::TypedBufferUAV:
                case rhi::EArdaRHIBindingType::StructuredBufferSRV:
                case rhi::EArdaRHIBindingType::StructuredBufferUAV:
                case rhi::EArdaRHIBindingType::RawBufferSRV:
                case rhi::EArdaRHIBindingType::RawBufferUAV:
                    return Item;
                default:
                    ARDA_CHECK_MSG(
                        "A buffer parameter matched a non-buffer binding layout item.");
                    return Item;
                }
            };

        rhi::FArdaRHIBindingSetDesc BindingDesc;
        BindingDesc.mLayout = rhi::FArdaRHIBindingLayoutRef(BindingLayout);
        eastl::vector<uint32_t> MatchedElements(
            LayoutDesc.mItems.size(),
            0u);

        struct FShaderAssignment
        {
            eastl::string mParameterPath;
            size_t mParameterArrayIndex = 0;
            const backend::FArdaShaderParameterMember* mShaderMember = nullptr;
            uint32_t mShaderArrayElement = 0;
        };
        eastl::vector<FShaderAssignment> ShaderAssignments;
        eastl::vector<backend::FArdaFlattenedShaderParameterMember>
            FlattenedShaderMembers;
        if (ShaderParameters != nullptr)
        {
            ShaderParameters->GetFlattenedMembers(FlattenedShaderMembers);
            eastl::vector<FARDGParameter> BindableParameters;
            PassRecord->GetParameterMetadata()->Enumerate(
                PassRecord->GetParameters(),
                [&BindableParameters](const FARDGParameter& Parameter)
                {
                    if (GetBindingClass(Parameter.mMember->mType) !=
                        EARDGBindingClass::None)
                    {
                        BindableParameters.push_back(Parameter);
                    }
                });

            size_t NextParameter = 0;
            for (const backend::FArdaFlattenedShaderParameterMember& Resolved :
                 FlattenedShaderMembers)
            {
                const backend::FArdaShaderParameterMember& Member =
                    *Resolved.mMember;
                if (Member.mKind == backend::EArdaShaderParameterKind::Value ||
                    Member.mKind == backend::EArdaShaderParameterKind::NestedStruct ||
                    Member.mKind == backend::EArdaShaderParameterKind::PushConstants)
                {
                    continue;
                }
                for (uint32_t Element = 0;
                     Element < Member.mArrayCount;
                     ++Element)
                {
                    while (NextParameter < BindableParameters.size() &&
                           !IsCompatibleBindingType(
                               BindableParameters[NextParameter].mMember->mType,
                               Member.mBindingType))
                    {
                        ++NextParameter;
                    }
                    if (NextParameter == BindableParameters.size())
                    {
                        ARDA_CHECK_MSG(
                            "ARDG parameters cannot satisfy registered shader metadata.");
                    }
                    const FARDGParameter& Parameter =
                        BindableParameters[NextParameter++];
                    ShaderAssignments.push_back({
                        Parameter.mPath,
                        Parameter.mArrayIndex,
                        &Member,
                        Element });
                }
            }
        }

        uint32_t NextShaderResourceSlot = 0;
        uint32_t NextUnorderedAccessSlot = 0;
        uint32_t NextConstantBufferSlot = 0;

        PassRecord->GetParameterMetadata()->Enumerate(
            PassRecord->GetParameters(),
            [&](const FARDGParameter& Parameter)
            {
                const EARDGParameterType ParameterType =
                    Parameter.mMember->mType;
                const EARDGBindingClass BindingClass =
                    GetBindingClass(ParameterType);
                if (BindingClass == EARDGBindingClass::None)
                {
                    return;
                }

                uint32_t Slot = 0;
                uint32_t ArrayElement =
                    static_cast<uint32_t>(Parameter.mArrayIndex);
                const backend::FArdaShaderParameterMember* ShaderMember = nullptr;
                if (ShaderParameters != nullptr)
                {
                    const auto Assignment = eastl::find_if(
                        ShaderAssignments.begin(),
                        ShaderAssignments.end(),
                        [&Parameter](const FShaderAssignment& Candidate)
                        {
                            return Candidate.mParameterPath == Parameter.mPath &&
                                Candidate.mParameterArrayIndex ==
                                    Parameter.mArrayIndex;
                        });
                    if (Assignment == ShaderAssignments.end())
                        return;
                    ShaderMember = Assignment->mShaderMember;
                    if (ShaderMember->mRegisterSpace != LayoutDesc.mRegisterSpace ||
                        ShaderMember->mVisibility != LayoutDesc.mVisibility)
                    {
                        return;
                    }
                    Slot = ShaderMember->mSlot;
                    ArrayElement = Assignment->mShaderArrayElement;
                }
                else
                {
                    uint32_t* NextSlot = nullptr;
                    switch (BindingClass)
                    {
                    case EARDGBindingClass::ShaderResource:
                        NextSlot = &NextShaderResourceSlot;
                        break;
                    case EARDGBindingClass::UnorderedAccess:
                        NextSlot = &NextUnorderedAccessSlot;
                        break;
                    case EARDGBindingClass::ConstantBuffer:
                        NextSlot = &NextConstantBufferSlot;
                        break;
                    case EARDGBindingClass::None:
                        return;
                    }
                    Slot = *NextSlot;
                    if (Parameter.mArrayIndex + 1 ==
                        Parameter.mMember->mElementCount)
                    {
                        ++*NextSlot;
                    }
                }

                size_t LayoutIndex = LayoutDesc.mItems.size();
                for (size_t Index = 0;
                     Index < LayoutDesc.mItems.size();
                     ++Index)
                {
                    const rhi::FArdaRHIBindingLayoutItem& Candidate =
                        LayoutDesc.mItems[Index];
                    if (Candidate.mSlot == Slot &&
                        IsCompatibleBindingType(ParameterType, Candidate.mType) &&
                        (ShaderMember == nullptr ||
                         Candidate.mType == ShaderMember->mBindingType))
                    {
                        LayoutIndex = Index;
                        break;
                    }
                }
                if (LayoutIndex == LayoutDesc.mItems.size())
                {
                    return;
                }

                const rhi::FArdaRHIBindingLayoutItem& LayoutBinding =
                    LayoutDesc.mItems[LayoutIndex];
                if (ArrayElement >= LayoutBinding.mArraySize)
                {
                    ARDA_CHECK_MSG(
                        "A pass parameter array exceeds its binding layout.");
                }

                rhi::FArdaRHIBindingItem Item;
                Item.mSlot = Slot;
                Item.mType = LayoutBinding.mType;
                switch (ParameterType)
                {
                case EARDGParameterType::Texture:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        ResolveTextureForPass(Pass, Parameter.GetValue<FARDGTextureRef>()));
                    break;

                case EARDGParameterType::TextureShaderResourceView:
                {
                    const FARDGTextureSRVRef View =
                        Parameter.GetValue<FARDGTextureSRVRef>();
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        ResolveTextureViewForPass(Pass, View));
                    Item.mView.mFormat = View->GetDesc().mFormat;
                    Item.mView.mTextureRange = View->GetDesc().mSubresources;
                    Item.mView.mDimension = View->GetDesc().mDimension;
                    break;
                }

                case EARDGParameterType::TextureUnorderedAccessView:
                {
                    const FARDGTextureUAVRef View =
                        Parameter.GetValue<FARDGTextureUAVRef>();
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        ResolveTextureViewForPass(Pass, View));
                    Item.mView.mFormat = View->GetDesc().mFormat;
                    Item.mView.mTextureRange = View->GetDesc().mSubresources;
                    Item.mView.mDimension = View->GetDesc().mDimension;
                    break;
                }

                case EARDGParameterType::Buffer:
                {
                    const FARDGBufferRef Buffer =
                        Parameter.GetValue<FARDGBufferRef>();
                    rhi::IArdaRHIBuffer* Physical =
                        ResolveBufferForPass(Pass, Buffer);
                    Item = MakeBufferItem(
                        LayoutBinding.mType,
                        Slot,
                        Physical,
                        Physical->GetDesc().mFormat,
                        {});
                    break;
                }

                case EARDGParameterType::BufferShaderResourceView:
                {
                    const FARDGBufferSRVRef View =
                        Parameter.GetValue<FARDGBufferSRVRef>();
                    Item = MakeBufferItem(
                        LayoutBinding.mType,
                        Slot,
                        ResolveBufferViewForPass(Pass, View),
                        View->GetDesc().mFormat,
                        View->GetDesc().mRange);
                    break;
                }

                case EARDGParameterType::BufferUnorderedAccessView:
                {
                    const FARDGBufferUAVRef View =
                        Parameter.GetValue<FARDGBufferUAVRef>();
                    Item = MakeBufferItem(
                        LayoutBinding.mType,
                        Slot,
                        ResolveBufferViewForPass(Pass, View),
                        View->GetDesc().mFormat,
                        View->GetDesc().mRange);
                    break;
                }

                case EARDGParameterType::UniformBuffer:
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        ResolveUniformBufferForPass(
                            Pass, Parameter.GetValue<FARDGUniformBufferRef>()));
                    break;

                case EARDGParameterType::AccelStructAccess:
                {
                    const FARDGAccelStructAccess& Access =
                        Parameter.GetValue<FARDGAccelStructAccess>();
                    Item.mResource = rhi::TArdaRHIRef<rhi::IArdaRHIResource>(
                        ResolveAccelStructForPass(Pass, Access.mAccelStruct));
                    break;
                }

                case EARDGParameterType::Value:
                case EARDGParameterType::TextureAccess:
                case EARDGParameterType::BufferAccess:
                case EARDGParameterType::NestedStruct:
                case EARDGParameterType::RenderTargetBindingSlots:
                    return;
                }

                if (Item.mType != LayoutBinding.mType)
                {
                    ARDA_CHECK_MSG(
                        "A pass parameter does not match its binding layout type.");
                }
                Item.mArrayElement =
                    ArrayElement;
                BindingDesc.mItems.push_back(eastl::move(Item));
                ++MatchedElements[LayoutIndex];
            });

        if (ShaderParameters != nullptr)
        {
            for (const backend::FArdaFlattenedShaderParameterMember& Resolved :
                 FlattenedShaderMembers)
            {
                const backend::FArdaShaderParameterMember& Member =
                    *Resolved.mMember;
                if (Member.mKind !=
                        backend::EArdaShaderParameterKind::PushConstants ||
                    Member.mRegisterSpace != LayoutDesc.mRegisterSpace ||
                    Member.mVisibility != LayoutDesc.mVisibility)
                {
                    continue;
                }
                for (size_t Index = 0;
                     Index < LayoutDesc.mItems.size();
                     ++Index)
                {
                    const rhi::FArdaRHIBindingLayoutItem& LayoutItem =
                        LayoutDesc.mItems[Index];
                    if (LayoutItem.mSlot == Member.mSlot &&
                        LayoutItem.mType ==
                            rhi::EArdaRHIBindingType::PushConstants)
                    {
                        rhi::FArdaRHIBindingItem Item;
                        Item.mSlot = Member.mSlot;
                        Item.mType =
                            rhi::EArdaRHIBindingType::PushConstants;
                        Item.mView.mBufferRange.mByteSize = Member.mSize;
                        BindingDesc.mItems.push_back(eastl::move(Item));
                        ++MatchedElements[Index];
                        break;
                    }
                }
            }
        }

        for (size_t Index = 0; Index < LayoutDesc.mItems.size(); ++Index)
        {
            if (MatchedElements[Index] !=
                LayoutDesc.mItems[Index].mArraySize)
            {
                ARDA_CHECK_MSG(
                    "A binding layout item has no matching pass parameter descriptor.");
            }
        }

        auto BindingSet = Device->CreateBindingSet(BindingDesc);
        if (!BindingSet)
        {
            ARDA_CHECK_MSG(
                "The RHI failed to create a pass parameter binding set.");
        }
        return eastl::move(BindingSet.mValue);
    }

    /**
     * Allocates immutable parameter bytes from the graph arena during building.
     * Arena lifetime keeps callback-visible parameter addresses stable through execute.
     */
    void* FARDGBuilder::AllocateParameterStorage(size_t Size, size_t Alignment)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot allocate parameters outside graph building.");
        }
        return mImpl->mArena.AllocateBytes(Size, Alignment);
    }

    /**
     * Registers cleanup for a non-trivially destructible arena parameter object.
     * Destruction is deferred until graph teardown because individual arena blocks are
     * not released during the graph lifecycle.
     */
    void FARDGBuilder::RegisterParameterDestructor(
        void* Object,
        void (*Destroy)(void*))
    {
        mImpl->mArena.RegisterDestructor(Object, Destroy);
    }

    /**
     * Marks an address as already frozen in this builder's arena.
     * FreezeParameters uses this identity set to avoid copying graph-owned objects.
     */
    void FARDGBuilder::MarkParameterStorage(const void* Parameters)
    {
        mImpl->mParameterStorage.insert(Parameters);
    }

    /** Returns whether a parameter address is already graph-owned frozen storage. */
    bool FARDGBuilder::IsParameterStorage(const void* Parameters) const noexcept
    {
        return mImpl->mParameterStorage.find(Parameters) !=
            mImpl->mParameterStorage.end();
    }

    /**
     * Registers a deferred logical texture during the graph-building stage.
     *
     * Only caller-created transient/ordinary records are accepted here; the descriptor
     * is validated and retained for compile checks and execution materialization.
     */
    FARDGTextureRef FARDGBuilder::CreateTexture(
        rhi::FArdaRHITextureDesc Desc,
        EARDGResourceFlags Flags)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a texture outside graph building.");
        }
        const uint8_t FlagValue = static_cast<uint8_t>(Flags);
        const uint8_t AllowedFlags =
            static_cast<uint8_t>(EARDGResourceFlags::Transient);
        if (Desc.mDebugName.empty() ||
            Desc.mWidth == 0 ||
            Desc.mHeight == 0 ||
            Desc.mDepth == 0 ||
            Desc.mArraySize == 0 ||
            Desc.mMipLevels == 0 ||
            (FlagValue & ~AllowedFlags) != 0)
        {
            ARDA_CHECK_MSG("Invalid logical texture declaration.");
        }
        return &mImpl->mTextures.Get(mImpl->mTextures.Emplace(eastl::move(Desc), Flags));
    }

    /**
     * Registers a deferred logical buffer during the graph-building stage.
     *
     * The non-empty descriptor and allowed creation flags become an arena-backed
     * record; no RHI buffer is allocated until execution materializes live work.
     */
    FARDGBufferRef FARDGBuilder::CreateBuffer(
        rhi::FArdaRHIBufferDesc Desc,
        EARDGResourceFlags Flags)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a buffer outside graph building.");
        }
        const uint8_t FlagValue = static_cast<uint8_t>(Flags);
        const uint8_t AllowedFlags =
            static_cast<uint8_t>(EARDGResourceFlags::Transient);
        if (Desc.mDebugName.empty() ||
            Desc.mByteSize == 0 ||
            (FlagValue & ~AllowedFlags) != 0)
        {
            ARDA_CHECK_MSG("Invalid logical buffer declaration.");
        }
        return &mImpl->mBuffers.Get(mImpl->mBuffers.Emplace(eastl::move(Desc), Flags));
    }

    FARDGAccelStructRef FARDGBuilder::CreateAccelStruct(
        rhi::FArdaRHIAccelStructDesc Desc,
        EARDGResourceFlags Flags)
    {
        if (!IsBuilding(*mImpl) || Desc.mDebugName.empty() ||
            (Desc.mbTopLevel ? Desc.mTopLevelMaxInstances == 0 :
                Desc.mBottomLevelGeometries.empty()) ||
            HasAllFlags(Flags, EARDGResourceFlags::External))
        {
            ARDA_CHECK_MSG("Invalid logical acceleration-structure declaration.");
        }
        return &mImpl->mAccelStructs.Get(
            mImpl->mAccelStructs.Emplace(eastl::move(Desc), Flags));
    }

    /**
     * Creates a logical texture SRV whose descriptor references a texture in this
     * builder. The view remains metadata until a pass uses its parent for binding.
     */
    FARDGTextureSRVRef FARDGBuilder::CreateTextureSRV(
        eastl::string Name,
        FARDGTextureViewDesc Desc)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a view after graph compilation.");
        }
        if (Name.empty() || mImpl->mTextures.TryGet(Desc.mTexture) == nullptr)
        {
            ARDA_CHECK_MSG("Invalid logical texture SRV declaration.");
        }
        const FARDGViewHandle Handle =
            mImpl->mViews.Emplace<FARDGTextureSRV>(eastl::move(Name), eastl::move(Desc));
        return static_cast<FARDGTextureSRV*>(mImpl->mViews.TryGet(Handle));
    }

    /**
     * Creates a logical texture UAV during building and records its parent,
     * subresources, and optional format for later setup and access validation.
     */
    FARDGTextureUAVRef FARDGBuilder::CreateTextureUAV(
        eastl::string Name,
        FARDGTextureViewDesc Desc)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a view after graph compilation.");
        }
        if (Name.empty() || mImpl->mTextures.TryGet(Desc.mTexture) == nullptr)
        {
            ARDA_CHECK_MSG("Invalid logical texture UAV declaration.");
        }
        const FARDGViewHandle Handle =
            mImpl->mViews.Emplace<FARDGTextureUAV>(eastl::move(Name), eastl::move(Desc));
        return static_cast<FARDGTextureUAV*>(mImpl->mViews.TryGet(Handle));
    }

    /**
     * Creates a logical buffer SRV whose parent belongs to this graph.
     * Its selected range is consumed when parameter setup records the parent access.
     */
    FARDGBufferSRVRef FARDGBuilder::CreateBufferSRV(
        eastl::string Name,
        FARDGBufferViewDesc Desc)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a view after graph compilation.");
        }
        if (Name.empty() || mImpl->mBuffers.TryGet(Desc.mBuffer) == nullptr)
        {
            ARDA_CHECK_MSG("Invalid logical buffer SRV declaration.");
        }
        const FARDGViewHandle Handle =
            mImpl->mViews.Emplace<FARDGBufferSRV>(eastl::move(Name), eastl::move(Desc));
        return static_cast<FARDGBufferSRV*>(mImpl->mViews.TryGet(Handle));
    }

    /**
     * Creates a logical buffer UAV during building.
     * The record preserves parent, range, and format metadata but owns no physical view.
     */
    FARDGBufferUAVRef FARDGBuilder::CreateBufferUAV(
        eastl::string Name,
        FARDGBufferViewDesc Desc)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a view after graph compilation.");
        }
        if (Name.empty() || mImpl->mBuffers.TryGet(Desc.mBuffer) == nullptr)
        {
            ARDA_CHECK_MSG("Invalid logical buffer UAV declaration.");
        }
        const FARDGViewHandle Handle =
            mImpl->mViews.Emplace<FARDGBufferUAV>(eastl::move(Name), eastl::move(Desc));
        return static_cast<FARDGBufferUAV*>(mImpl->mViews.TryGet(Handle));
    }

    /**
     * Imports a caller-owned physical texture at the graph boundary.
     *
     * The known initial state also becomes its default final state, and the prologue
     * is installed as latest producer so first use depends on graph entry. Reimporting
     * the same RHI object deduplicates to one logical record when states agree.
     */
    FARDGTextureRef FARDGBuilder::RegisterExternalTexture(
        rhi::FArdaRHITextureRef Texture,
        rhi::EArdaRHIResourceState InitialState,
        eastl::string Name)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot import a texture after graph compilation.");
        }
        if (!Texture || InitialState == rhi::EArdaRHIResourceState::Unknown)
        {
            ARDA_CHECK_MSG(
                "An external RHI texture requires a reference and known initial state.");
        }
        const auto Existing = mImpl->mImportedTextures.find(Texture->GetPhysicalIdentity());
        if (Existing != mImpl->mImportedTextures.end())
        {
            if (Existing->second->GetInitialState() != InitialState)
            {
                ARDA_CHECK_MSG(
                    "An imported texture was registered with conflicting initial states.");
            }
            return Existing->second;
        }

        rhi::FArdaRHITextureDesc Desc = Texture->GetDesc();
        if (!Name.empty())
        {
            Desc.mDebugName = Name;
        }
        if (Desc.mDebugName.empty())
        {
            Desc.mDebugName = "ExternalTexture";
        }
        Desc.mInitialState = InitialState;
        const FARDGTextureHandle Handle = mImpl->mTextures.Emplace(
            eastl::move(Desc),
            EARDGResourceFlags::External,
            Texture);
        FARDGTextureRef Resource = mImpl->mTextures.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedTextures.emplace(Texture->GetPhysicalIdentity(), Resource);
        return Resource;
    }

    /**
     * Imports a caller-owned physical buffer at the graph boundary.
     *
     * As with textures, identity is deduplicated, conflicting initial states fail, and
     * prologue producer history makes the pre-graph contents available to first use.
     */
    FARDGBufferRef FARDGBuilder::RegisterExternalBuffer(
        rhi::FArdaRHIBufferRef Buffer,
        rhi::EArdaRHIResourceState InitialState,
        eastl::string Name)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot import a buffer after graph compilation.");
        }
        if (!Buffer || InitialState == rhi::EArdaRHIResourceState::Unknown)
        {
            ARDA_CHECK_MSG(
                "An external RHI buffer requires a reference and known initial state.");
        }
        const auto Existing = mImpl->mImportedBuffers.find(Buffer->GetPhysicalIdentity());
        if (Existing != mImpl->mImportedBuffers.end())
        {
            if (Existing->second->GetInitialState() != InitialState)
            {
                ARDA_CHECK_MSG(
                    "An imported buffer was registered with conflicting initial states.");
            }
            return Existing->second;
        }

        rhi::FArdaRHIBufferDesc Desc = Buffer->GetDesc();
        if (!Name.empty())
        {
            Desc.mDebugName = Name;
        }
        if (Desc.mDebugName.empty())
        {
            Desc.mDebugName = "ExternalBuffer";
        }
        Desc.mInitialState = InitialState;
        const FARDGBufferHandle Handle = mImpl->mBuffers.Emplace(
            eastl::move(Desc),
            EARDGResourceFlags::External,
            Buffer);
        FARDGBufferRef Resource = mImpl->mBuffers.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedBuffers.emplace(Buffer->GetPhysicalIdentity(), Resource);
        return Resource;
    }

    FARDGAccelStructRef FARDGBuilder::RegisterExternalAccelStruct(
        rhi::FArdaRHIAccelStructRef AccelStruct,
        rhi::EArdaRHIResourceState InitialState,
        eastl::string Name)
    {
        if (!IsBuilding(*mImpl) || !AccelStruct ||
            InitialState == rhi::EArdaRHIResourceState::Unknown)
        {
            ARDA_CHECK_MSG(
                "An external acceleration structure requires a reference and known initial state.");
        }
        const void* Identity = AccelStruct->GetPhysicalIdentity();
        if (!Identity)
        {
            ARDA_CHECK_MSG(
                "An imported acceleration structure requires physical identity.");
        }
        const auto Existing = mImpl->mImportedAccelStructs.find(Identity);
        if (Existing != mImpl->mImportedAccelStructs.end())
        {
            if (Existing->second->GetInitialState() != InitialState)
            {
                ARDA_CHECK_MSG(
                    "An imported acceleration structure was registered with conflicting initial states.");
            }
            return Existing->second;
        }
        rhi::FArdaRHIAccelStructDesc Desc = AccelStruct->GetDesc();
        if (!Name.empty()) Desc.mDebugName = Name;
        if (Desc.mDebugName.empty()) Desc.mDebugName = "ExternalAccelStruct";
        const FARDGAccelStructHandle Handle = mImpl->mAccelStructs.Emplace(
            eastl::move(Desc), EARDGResourceFlags::External, AccelStruct, InitialState);
        FARDGAccelStructRef Resource = mImpl->mAccelStructs.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedAccelStructs.emplace(Identity, Resource);
        return Resource;
    }

    /**
     * Registers a logical uniform buffer backed by frozen parameter bytes.
     *
     * During building it stores descriptor, metadata, and contents only. Execution
     * later allocates a dedicated constant buffer and uploads the exact frozen bytes.
     */
    FARDGUniformBufferRef FARDGBuilder::CreateUniformBufferInternal(
        eastl::string Name,
        size_t ByteSize,
        const FARDGParameterMetadata* Metadata,
        const void* Contents)
    {
        if (!IsBuilding(*mImpl) ||
            Name.empty() ||
            Metadata == nullptr ||
            Contents == nullptr)
        {
            ARDA_CHECK_MSG("Invalid logical uniform-buffer declaration.");
        }
        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = Name;
        Desc.mByteSize = ByteSize;
        Desc.mUsage = rhi::EArdaRHIBufferUsage::Constant;
        Desc.mInitialState = rhi::EArdaRHIResourceState::ConstantBuffer;
        const FARDGUniformBufferHandle Handle = mImpl->mUniformBuffers.Emplace(
            Name,
            eastl::move(Desc),
            Metadata,
            Contents);
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

    /**
     * Declares a texture as an observable graph output during building.
     *
     * Extraction removes transient eligibility, requests the epilogue final state, and
     * records caller-owned output storage populated after command-list submission.
     */
    void FARDGBuilder::QueueTextureExtraction(
        FARDGTextureRef Texture,
        rhi::FArdaRHITextureRef* Output,
        rhi::EArdaRHIResourceState FinalState)
    {
        if (!IsBuilding(*mImpl) ||
            Texture == nullptr ||
            Output == nullptr ||
            mImpl->mTextures.TryGet(Texture->GetHandle()) != Texture ||
            FinalState == rhi::EArdaRHIResourceState::Unknown)
        {
            ARDA_CHECK_MSG("Invalid logical texture extraction.");
        }
        // Enforce both sides of the one-to-one extraction contract: a resource and an
        // output address may each participate in only one queued extraction.
        const auto Existing = eastl::find_if(
            mImpl->mTextureExtractions.begin(),
            mImpl->mTextureExtractions.end(),
            [Texture, Output](const FARDGTextureExtraction& Extraction)
            {
                return Extraction.mTexture == Texture ||
                    Extraction.mOutput == Output;
            });
        if (Existing != mImpl->mTextureExtractions.end())
        {
            ARDA_CHECK_MSG(
                "A logical texture extraction cannot be queued twice.");
        }
        Texture->AddFlags(EARDGResourceFlags::Extracted);
        Texture->SetFinalState(FinalState);
        mImpl->mTextureExtractions.push_back({Texture, Output, FinalState});
    }

    /**
     * Declares a buffer as an observable graph output during building.
     *
     * The requested final state is lowered onto the epilogue, and execution writes the
     * retained physical handle to the unique caller output after graph submission.
     */
    void FARDGBuilder::QueueBufferExtraction(
        FARDGBufferRef Buffer,
        rhi::FArdaRHIBufferRef* Output,
        rhi::EArdaRHIResourceState FinalState)
    {
        if (!IsBuilding(*mImpl) ||
            Buffer == nullptr ||
            Output == nullptr ||
            mImpl->mBuffers.TryGet(Buffer->GetHandle()) != Buffer ||
            FinalState == rhi::EArdaRHIResourceState::Unknown)
        {
            ARDA_CHECK_MSG("Invalid logical buffer extraction.");
        }
        // Resource and destination uniqueness prevents ambiguous final states or two
        // logical resources from racing to publish into the same handle object.
        const auto Existing = eastl::find_if(
            mImpl->mBufferExtractions.begin(),
            mImpl->mBufferExtractions.end(),
            [Buffer, Output](const FARDGBufferExtraction& Extraction)
            {
                return Extraction.mBuffer == Buffer ||
                    Extraction.mOutput == Output;
            });
        if (Existing != mImpl->mBufferExtractions.end())
        {
            ARDA_CHECK_MSG(
                "A logical buffer extraction cannot be queued twice.");
        }
        Buffer->AddFlags(EARDGResourceFlags::Extracted);
        Buffer->SetFinalState(FinalState);
        mImpl->mBufferExtractions.push_back({Buffer, Output, FinalState});
    }

    FARDGPassHandle FARDGBuilder::AddHostToDeviceCopyPass(
        FARDGBufferRef Destination,
        const void* SourceData,
        size_t Size,
        uint64_t DestinationOffset,
        eastl::string Name)
    {
        if (!Destination || !SourceData || Size == 0 ||
            DestinationOffset > Destination->GetDesc().mByteSize ||
            Size > Destination->GetDesc().mByteSize - DestinationOffset)
            ARDA_CHECK_MSG("Invalid host-to-device render-graph copy range.");

        eastl::vector<uint8_t> OwnedBytes(Size);
        std::memcpy(OwnedBytes.data(), SourceData, Size);
        FARDGHostToDeviceCopyParameters Parameters;
        Parameters.mDestination = {
            Destination,
            rhi::EArdaRHIResourceState::CopyDest,
            { DestinationOffset, Size }};
        return AddPass(
            eastl::move(Name), &Parameters,
            EARDGPassFlags::Copy | EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [Bytes = eastl::move(OwnedBytes), DestinationOffset](
                FARDGPassExecutionContext& Context,
                const FARDGHostToDeviceCopyParameters& Frozen)
            {
                (void)Context.mCommandList.CopyBufferHostToDevice(
                    *Context.GetBuffer(Frozen.mDestination.mBuffer),
                    Bytes.data(), Bytes.size(), DestinationOffset);
            });
    }

    FARDGPassHandle FARDGBuilder::AddHostToDeviceCopyPassAsync(
        FARDGBufferRef Destination,
        const void* SourceData,
        size_t Size,
        rhi::FArdaRHIHostToDeviceCopyCallback Completion,
        uint64_t DestinationOffset,
        eastl::string Name)
    {
        if (!Destination || !SourceData || Size == 0 || !Completion ||
            DestinationOffset > Destination->GetDesc().mByteSize ||
            Size > Destination->GetDesc().mByteSize - DestinationOffset)
            ARDA_CHECK_MSG("Invalid asynchronous host-to-device render-graph copy.");

        eastl::vector<uint8_t> OwnedBytes(Size);
        std::memcpy(OwnedBytes.data(), SourceData, Size);
        FARDGHostToDeviceCopyParameters Parameters;
        Parameters.mDestination = {
            Destination,
            rhi::EArdaRHIResourceState::CopyDest,
            { DestinationOffset, Size }};
        return AddPass(
            eastl::move(Name), &Parameters,
            EARDGPassFlags::Copy | EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [Bytes = eastl::move(OwnedBytes),
             Callback = eastl::move(Completion),
             DestinationOffset](
                FARDGPassExecutionContext& Context,
                const FARDGHostToDeviceCopyParameters& Frozen) mutable
            {
                const auto Status =
                    Context.mCommandList.CopyBufferHostToDeviceAsync(
                        *Context.GetBuffer(Frozen.mDestination.mBuffer),
                        Bytes.data(), Bytes.size(), Callback,
                        DestinationOffset);
                if (!Status) Callback(Status);
            });
    }

    FARDGPassHandle FARDGBuilder::AddDeviceToHostCopyPass(
        FARDGBufferRef Source,
        eastl::vector<uint8_t>& Output,
        uint64_t SourceOffset,
        uint64_t Size,
        eastl::string Name)
    {
        if (!Source || SourceOffset > Source->GetDesc().mByteSize)
            ARDA_CHECK_MSG("Invalid device-to-host render-graph copy offset.");
        const uint64_t ResolvedSize = Size == rhi::ArdaRHIWholeBuffer
            ? Source->GetDesc().mByteSize - SourceOffset : Size;
        if (ResolvedSize == 0 ||
            ResolvedSize > Source->GetDesc().mByteSize - SourceOffset)
            ARDA_CHECK_MSG("Invalid device-to-host render-graph copy range.");

        FARDGDeviceToHostCopyParameters Parameters;
        Parameters.mSource = {
            Source,
            rhi::EArdaRHIResourceState::CopySource,
            { SourceOffset, ResolvedSize }};
        return AddPass(
            eastl::move(Name), &Parameters,
            EARDGPassFlags::Copy | EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [&Output, SourceOffset, ResolvedSize](
                FARDGPassExecutionContext& Context,
                const FARDGDeviceToHostCopyParameters& Frozen)
            {
                const auto Status =
                    Context.mCommandList.CopyBufferDeviceToHost(
                        *Context.GetBuffer(Frozen.mSource.mBuffer),
                        Output, SourceOffset, ResolvedSize);
                if (!Status) Output.clear();
            });
    }

    FARDGPassHandle FARDGBuilder::AddDeviceToHostCopyPassAsync(
        FARDGBufferRef Source,
        rhi::FArdaRHIDeviceToHostCopyCallback Completion,
        uint64_t SourceOffset,
        uint64_t Size,
        eastl::string Name)
    {
        if (!Source || !Completion ||
            SourceOffset > Source->GetDesc().mByteSize)
            ARDA_CHECK_MSG("Invalid asynchronous device-to-host render-graph copy.");
        const uint64_t ResolvedSize = Size == rhi::ArdaRHIWholeBuffer
            ? Source->GetDesc().mByteSize - SourceOffset : Size;
        if (ResolvedSize == 0 ||
            ResolvedSize > Source->GetDesc().mByteSize - SourceOffset)
            ARDA_CHECK_MSG("Invalid asynchronous device-to-host copy range.");

        FARDGDeviceToHostCopyParameters Parameters;
        Parameters.mSource = {
            Source,
            rhi::EArdaRHIResourceState::CopySource,
            { SourceOffset, ResolvedSize }};
        return AddPass(
            eastl::move(Name), &Parameters,
            EARDGPassFlags::Copy | EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [Callback = eastl::move(Completion),
             SourceOffset, ResolvedSize](
                FARDGPassExecutionContext& Context,
                const FARDGDeviceToHostCopyParameters& Frozen) mutable
            {
                const auto Status =
                    Context.mCommandList.CopyBufferDeviceToHostAsync(
                        *Context.GetBuffer(Frozen.mSource.mBuffer),
                        Callback, SourceOffset, ResolvedSize);
                if (!Status) Callback({ {}, Status });
            });
    }

    /**
     * Appends a type-erased pass and performs its build-stage metadata setup.
     *
     * The pass is registered before parameters are visited so it already has a stable,
     * greater-than-predecessors handle while accesses update resource history. Setup
     * derives raw states, views, raster bindings, and incoming dependency edges.
     */
    FARDGPassHandle FARDGBuilder::AddPassInternal(
        eastl::string Name,
        EARDGPassFlags Flags,
        const void* Parameters,
        const FARDGParameterMetadata* Metadata,
        FARDGPassExecuteFunction Execute)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot add a pass outside graph building.");
        }
        if (Name.empty() || !Execute)
        {
            ARDA_CHECK_MSG("A render-graph pass requires a name and body.");
        }
        ValidatePassFlags(Flags);

        const FARDGPassHandle Handle = mImpl->mPasses.Emplace<FARDGLambdaPass>(
            eastl::move(Name),
            Flags,
            Parameters,
            Metadata,
            eastl::move(Execute));
        FARDGPass& Pass = mImpl->mPasses.Get(Handle);
        if (Metadata != nullptr)
        {
            FARDGSetupContext Setup{
                mImpl->mPasses,
                mImpl->mTextures,
                mImpl->mBuffers,
                mImpl->mAccelStructs,
                mImpl->mViews,
                mImpl->mUniformBuffers,
                Pass,
                {}};
            Setup.Visit(*Metadata, Parameters);
        }
        return Handle;
    }

    /**
     * Adds explicit non-resource causality as a build-stage producer edge.
     *
     * Both passes must already be registered in forward handle order. Because this is
     * a producer edge, compilation uses it for ordering and backward liveness.
     */
    void FARDGBuilder::AddDependency(
        FARDGPassHandle Producer,
        FARDGPassHandle Consumer)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot add a dependency outside graph building.");
        }
        if (!Producer.IsValid() ||
            !Consumer.IsValid() ||
            Producer == Consumer ||
            Consumer < Producer ||
            mImpl->mPasses.TryGet(Producer) == nullptr ||
            mImpl->mPasses.TryGet(Consumer) == nullptr)
        {
            ARDA_CHECK_MSG(
                "Manual dependencies must name distinct registered passes in execution order.");
        }
        mImpl->mPasses.Get(Consumer).AddProducer(Producer);
    }

    /**
     * Runs device-independent graph compilation once and returns its stable result.
     *
     * Repeated calls reuse the published result. The compiling guard closes build
     * mutation and prevents re-entry while compiler stages validate and lower state.
     */
    const FARDGCompileResult& FARDGBuilder::Compile()
    {
        if (mImpl->mbCompiled)
        {
            return mImpl->mCompileResult;
        }
        if (mImpl->mbCompiling || mImpl->mbExecutionStarted || mImpl->mbFailed)
        {
            ARDA_CHECK_MSG(
                "A render graph cannot compile in its current lifecycle state.");
        }
        mImpl->mbCompiling = true;
        const FARDGCompileResult& Result = FARDGCompiler::Compile(*this);
        mImpl->mbCompiling = false;
        return Result;
    }

    /**
     * Hands the graph to the execution pipeline for compile-on-demand,
     * materialization, command recording, queue submission, and extraction publishing.
     */
    const FARDGExecutionResult& FARDGBuilder::Execute(
        const FARDGExecuteOptions& Options)
    {
        return FARDGExecutor::Execute(*this, Options);
    }

    /**
     * Returns the published execution report after the graph has executed.
     * A null result distinguishes the pre-execution lifecycle state.
     */
    const FARDGExecutionResult*
    FARDGBuilder::GetLastExecutionResult() const noexcept
    {
        return mImpl->mbExecuted ? &mImpl->mExecutionResult : nullptr;
    }

    /** Reports whether all device-independent compiler stages completed successfully. */
    bool FARDGBuilder::IsCompiled() const noexcept
    {
        return mImpl->mbCompiled;
    }

    /** Returns the immutable device, queue-capability, and debug execution context. */
    const FARDGRenderGraphContext& FARDGBuilder::GetContext() const noexcept
    {
        return mImpl->mContext;
    }

    /**
     * Returns mutable graph-scoped blackboard storage during building.
     * Mutation is rejected after the graph closes so compiled callbacks see stable data.
     */
    FARDGBlackboard& FARDGBuilder::GetBlackboard()
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG(
                "Cannot mutate the render-graph blackboard after building.");
        }
        return mImpl->mBlackboard;
    }

    /** Returns read-only graph-scoped blackboard storage in any lifecycle stage. */
    const FARDGBlackboard& FARDGBuilder::GetBlackboard() const noexcept
    {
        return mImpl->mBlackboard;
    }

    /** Returns the synthetic graph-entry pass created with the private implementation. */
    FARDGPassHandle FARDGBuilder::GetProloguePass() const noexcept
    {
        return mImpl->mCompileResult.mPrologue;
    }

    /** Returns the synthetic graph-exit pass handle populated during compilation. */
    FARDGPassHandle FARDGBuilder::GetEpiloguePass() const noexcept
    {
        return mImpl->mCompileResult.mEpilogue;
    }

    /** Resolves a pass handle to mutable graph storage, or null when invalid. */
    FARDGPass* FARDGBuilder::TryGetPass(FARDGPassHandle Handle) noexcept
    {
        return mImpl->mPasses.TryGet(Handle);
    }

    /** Resolves a pass handle to immutable graph storage, or null when invalid. */
    const FARDGPass* FARDGBuilder::TryGetPass(FARDGPassHandle Handle) const noexcept
    {
        return mImpl->mPasses.TryGet(Handle);
    }

    /** Resolves a texture handle to mutable logical resource storage, or null. */
    FARDGTexture* FARDGBuilder::TryGetTexture(FARDGTextureHandle Handle) noexcept
    {
        return mImpl->mTextures.TryGet(Handle);
    }

    /** Resolves a texture handle to immutable logical resource storage, or null. */
    const FARDGTexture* FARDGBuilder::TryGetTexture(
        FARDGTextureHandle Handle) const noexcept
    {
        return mImpl->mTextures.TryGet(Handle);
    }

    /** Resolves a buffer handle to mutable logical resource storage, or null. */
    FARDGBuffer* FARDGBuilder::TryGetBuffer(FARDGBufferHandle Handle) noexcept
    {
        return mImpl->mBuffers.TryGet(Handle);
    }

    /** Resolves a buffer handle to immutable logical resource storage, or null. */
    const FARDGBuffer* FARDGBuilder::TryGetBuffer(
        FARDGBufferHandle Handle) const noexcept
    {
        return mImpl->mBuffers.TryGet(Handle);
    }

    FARDGAccelStruct* FARDGBuilder::TryGetAccelStruct(
        FARDGAccelStructHandle Handle) noexcept
    {
        return mImpl->mAccelStructs.TryGet(Handle);
    }

    const FARDGAccelStruct* FARDGBuilder::TryGetAccelStruct(
        FARDGAccelStructHandle Handle) const noexcept
    {
        return mImpl->mAccelStructs.TryGet(Handle);
    }

    /** Resolves a view handle to mutable logical view storage, or null. */
    FARDGView* FARDGBuilder::TryGetView(FARDGViewHandle Handle) noexcept
    {
        return mImpl->mViews.TryGet(Handle);
    }

    /** Resolves a view handle to immutable logical view storage, or null. */
    const FARDGView* FARDGBuilder::TryGetView(
        FARDGViewHandle Handle) const noexcept
    {
        return mImpl->mViews.TryGet(Handle);
    }

    /** Resolves a uniform-buffer handle to mutable logical storage, or null. */
    FARDGUniformBuffer* FARDGBuilder::TryGetUniformBuffer(
        FARDGUniformBufferHandle Handle) noexcept
    {
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

    /** Resolves a uniform-buffer handle to immutable logical storage, or null. */
    const FARDGUniformBuffer* FARDGBuilder::TryGetUniformBuffer(
        FARDGUniformBufferHandle Handle) const noexcept
    {
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

    /**
     * Returns texture extraction declarations in build registration order.
     * The executor consumes this stable list to publish physical handles.
     */
    const eastl::vector<FARDGTextureExtraction>&
    FARDGBuilder::GetTextureExtractions() const noexcept
    {
        return mImpl->mTextureExtractions;
    }

    /**
     * Returns buffer extraction declarations in build registration order.
     * The executor consumes this stable list after submission.
     */
    const eastl::vector<FARDGBufferExtraction>&
    FARDGBuilder::GetBufferExtractions() const noexcept
    {
        return mImpl->mBufferExtractions;
    }

    /**
     * Serializes the compiled graph into a deterministic diagnostic report.
     *
     * The dump exposes execution order, culling, dependencies, lowered transitions,
     * lifetimes, and queue edges for inspection; it does not mutate or serialize an
     * executable graph and is unavailable before compilation completes.
     */
    eastl::string FARDGBuilder::DumpGraph() const
    {
        if (!mImpl->mbCompiled)
        {
            ARDA_CHECK_MSG("Compile the render graph before dumping it.");
        }

        std::ostringstream Stream;
        Stream << "ArdaRenderGraph\n";
        Stream << "Prologue=" << mImpl->mCompileResult.mPrologue.GetIndex()
               << " Epilogue=" << mImpl->mCompileResult.mEpilogue.GetIndex()
               << " RasterGroups=" << mImpl->mCompileResult.mRasterGroupCount << "\n";
        Stream << "Debug immediate="
               << mImpl->mContext.mDebugOptions.mbImmediateMode
               << " conservativeBarriers="
               << mImpl->mContext.mDebugOptions.mbConservativeBarriers
               << " extendedLifetimes="
               << mImpl->mContext.mDebugOptions.mbExtendResourceLifetimes
               << " clobberFirstWrites="
               << mImpl->mContext.mDebugOptions.mbClobberFirstWrites
               << "\n";
        Stream << "ExecutionOrder [";
        for (size_t Index = 0;
             Index < mImpl->mCompileResult.mExecutionOrder.size();
             ++Index)
        {
            if (Index != 0)
            {
                Stream << ",";
            }
            Stream << "P"
                   << mImpl->mCompileResult.mExecutionOrder[Index].GetIndex();
        }
        Stream << "]\n";

        Stream << "Textures " << mImpl->mTextures.GetCount() << "\n";
        for (const FARDGTexture* Texture : mImpl->mTextures.GetEntries())
        {
            Stream << " T" << Texture->GetHandle().GetIndex()
                   << " \"" << Texture->GetName().c_str() << "\""
                   << " external=" << Texture->IsExternal()
                   << " extracted=" << Texture->IsExtracted()
                   << " producer=" << Texture->GetLastProducer().GetIndex()
                   << "\n";
        }

        Stream << "Buffers " << mImpl->mBuffers.GetCount() << "\n";
        for (const FARDGBuffer* Buffer : mImpl->mBuffers.GetEntries())
        {
            Stream << " B" << Buffer->GetHandle().GetIndex()
                   << " \"" << Buffer->GetName().c_str() << "\""
                   << " external=" << Buffer->IsExternal()
                   << " extracted=" << Buffer->IsExtracted()
                   << " producer=" << Buffer->GetLastProducer().GetIndex()
                   << "\n";
        }

        Stream << "Passes " << mImpl->mPasses.GetCount() << "\n";
        // Include culled records as well as live ones so the report explains compiler
        // decisions rather than showing only the final execution schedule.
        for (const FARDGPass* Pass : mImpl->mPasses.GetEntries())
        {
            const FARDGPassState& State = Pass->GetState();
            Stream << " P" << Pass->GetHandle().GetIndex()
                   << " \"" << Pass->GetName().c_str() << "\""
                   << " flags=" << static_cast<uint16_t>(Pass->GetFlags())
                   << " pipeline=" << GetPipelineName(State.mPipeline)
                   << " culled=" << State.mbCulled
                   << " sentinel=" << State.mbSentinel
                   << " rasterGroup=" << State.mRasterGroup
                   << " fork=" << State.mAsyncFork.GetIndex()
                   << " join=" << State.mAsyncJoin.GetIndex()
                   << " producers=[";
            for (size_t Index = 0; Index < State.mProducers.size(); ++Index)
            {
                if (Index != 0)
                {
                    Stream << ",";
                }
                Stream << State.mProducers[Index].GetIndex();
            }
            Stream << "] sync=[";
            for (size_t Index = 0;
                 Index < State.mSynchronizationProducers.size();
                 ++Index)
            {
                if (Index != 0)
                {
                    Stream << ",";
                }
                Stream << State.mSynchronizationProducers[Index].GetIndex();
            }
            Stream << "]\n";
            Stream << "  transitions textures="
                   << State.mTextureTransitions.size()
                   << " buffers=" << State.mBufferTransitions.size()
                   << "\n";
            for (const FARDGTextureTransition& Transition :
                 State.mTextureTransitions)
            {
                Stream << "   T" << Transition.mTexture.GetIndex()
                       << " mip=" << Transition.mSubresources.mBaseMipLevel
                       << "+" << Transition.mSubresources.mMipLevelCount
                       << " slice=" << Transition.mSubresources.mBaseArraySlice
                       << "+" << Transition.mSubresources.mArraySliceCount
                       << " ";
                AppendStateName(Stream, Transition.mStateBefore);
                Stream << "->";
                AppendStateName(Stream, Transition.mStateAfter);
                Stream << " uav=" << Transition.mbUAVBarrier
                       << " forced=" << Transition.mbForceBarrier << "\n";
            }
            for (const FARDGBufferTransition& Transition :
                 State.mBufferTransitions)
            {
                Stream << "   B" << Transition.mBuffer.GetIndex() << " ";
                AppendStateName(Stream, Transition.mStateBefore);
                Stream << "->";
                AppendStateName(Stream, Transition.mStateAfter);
                Stream << " uav=" << Transition.mbUAVBarrier
                       << " forced=" << Transition.mbForceBarrier << "\n";
            }
        }

        Stream << "Lifetimes "
               << mImpl->mCompileResult.mResourceLifetimes.size() << "\n";
        for (const FARDGResourceLifetime& Lifetime :
             mImpl->mCompileResult.mResourceLifetimes)
        {
            Stream << " " << (Lifetime.mType == EARDGResourceType::Texture
                                  ? "T"
                                  : "B")
                   << Lifetime.mResourceIndex
                   << " first=" << Lifetime.mFirstUse
                   << " last=" << Lifetime.mLastUse
                   << " transient=" << Lifetime.mbTransient
                   << "\n";
        }
        Stream << "QueueDependencies "
               << mImpl->mCompileResult.mQueueDependencies.size() << "\n";
        for (const FARDGQueueDependency& Dependency :
             mImpl->mCompileResult.mQueueDependencies)
        {
            Stream << " P" << Dependency.mProducer.GetIndex()
                   << "->P" << Dependency.mConsumer.GetIndex()
                   << " " << GetPipelineName(Dependency.mProducerPipeline)
                   << "->" << GetPipelineName(Dependency.mConsumerPipeline)
                   << "\n";
        }
        const std::string Result = Stream.str();
        return eastl::string(Result.data(), Result.size());
    }
}
