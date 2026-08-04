#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphCompiler.h"
#include "ArdaRenderGraphExecutor.h"
#include "ArdaRenderGraphLog.h"

#include <EASTL/algorithm.h>
#include <sstream>
#include <string>
#include <EASTL/unordered_set.h>

namespace arda::render_graph
{
    namespace
    {
        /**
         * Converts an EASTL string to the standard string type required by NVRHI
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
        [[nodiscard]] bool IsWriteState(nvrhi::ResourceStates State) noexcept
        {
            constexpr uint32_t WriteMask =
                static_cast<uint32_t>(nvrhi::ResourceStates::UnorderedAccess) |
                static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
                static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
                static_cast<uint32_t>(nvrhi::ResourceStates::CopyDest) |
                static_cast<uint32_t>(nvrhi::ResourceStates::ResolveDest) |
                static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructWrite) |
                static_cast<uint32_t>(nvrhi::ResourceStates::OpacityMicromapWrite) |
                static_cast<uint32_t>(nvrhi::ResourceStates::ConvertCoopVecMatrixOutput);
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

        /** Maps generated parameter semantics to their NVRHI register namespace. */
        [[nodiscard]] EARDGBindingClass GetBindingClass(
            EARDGParameterType Type) noexcept
        {
            switch (Type)
            {
            case EARDGParameterType::Texture:
            case EARDGParameterType::Buffer:
            case EARDGParameterType::TextureShaderResourceView:
            case EARDGParameterType::BufferShaderResourceView:
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
            nvrhi::ResourceType LayoutType) noexcept
        {
            switch (ParameterType)
            {
            case EARDGParameterType::Texture:
            case EARDGParameterType::TextureShaderResourceView:
                return LayoutType == nvrhi::ResourceType::Texture_SRV;

            case EARDGParameterType::TextureUnorderedAccessView:
                return LayoutType == nvrhi::ResourceType::Texture_UAV;

            case EARDGParameterType::Buffer:
            case EARDGParameterType::BufferShaderResourceView:
                return LayoutType == nvrhi::ResourceType::TypedBuffer_SRV ||
                    LayoutType == nvrhi::ResourceType::StructuredBuffer_SRV ||
                    LayoutType == nvrhi::ResourceType::RawBuffer_SRV;

            case EARDGParameterType::BufferUnorderedAccessView:
                return LayoutType == nvrhi::ResourceType::TypedBuffer_UAV ||
                    LayoutType == nvrhi::ResourceType::StructuredBuffer_UAV ||
                    LayoutType == nvrhi::ResourceType::RawBuffer_UAV;

            case EARDGParameterType::UniformBuffer:
                return LayoutType == nvrhi::ResourceType::ConstantBuffer ||
                    LayoutType == nvrhi::ResourceType::VolatileConstantBuffer;

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
         * Appends a human-readable bitset for one NVRHI resource state.
         *
         * This debug-output helper preserves combined read states by joining every
         * recognized bit; it has no effect on barrier compilation or state tracking.
         */
        void AppendStateName(
            std::ostream& Stream,
            nvrhi::ResourceStates State)
        {
            struct FStateName
            {
                /**
                 * Single NVRHI state bit matched while formatting a diagnostic
                 * state mask.
                 */
                nvrhi::ResourceStates mState;

                /** Non-owning, static-lifetime label emitted when mState is present. */
                const char* mName;
            };
            static constexpr FStateName Names[] = {
                {nvrhi::ResourceStates::Common, "Common"},
                {nvrhi::ResourceStates::ConstantBuffer, "ConstantBuffer"},
                {nvrhi::ResourceStates::VertexBuffer, "VertexBuffer"},
                {nvrhi::ResourceStates::IndexBuffer, "IndexBuffer"},
                {nvrhi::ResourceStates::IndirectArgument, "IndirectArgument"},
                {nvrhi::ResourceStates::PixelShaderResource, "PixelSRV"},
                {nvrhi::ResourceStates::NonPixelShaderResource, "NonPixelSRV"},
                {nvrhi::ResourceStates::UnorderedAccess, "UAV"},
                {nvrhi::ResourceStates::RenderTarget, "RenderTarget"},
                {nvrhi::ResourceStates::DepthWrite, "DepthWrite"},
                {nvrhi::ResourceStates::DepthRead, "DepthRead"},
                {nvrhi::ResourceStates::StreamOut, "StreamOut"},
                {nvrhi::ResourceStates::CopyDest, "CopyDest"},
                {nvrhi::ResourceStates::CopySource, "CopySource"},
                {nvrhi::ResourceStates::ResolveDest, "ResolveDest"},
                {nvrhi::ResourceStates::ResolveSource, "ResolveSource"},
                {nvrhi::ResourceStates::Present, "Present"},
                {nvrhi::ResourceStates::AccelStructRead, "AccelStructRead"},
                {nvrhi::ResourceStates::AccelStructWrite, "AccelStructWrite"},
                {nvrhi::ResourceStates::AccelStructBuildInput, "ASBuildInput"},
                {nvrhi::ResourceStates::AccelStructBuildBlas, "ASBuildBLAS"},
                {nvrhi::ResourceStates::ShadingRateSurface, "ShadingRate"},
                {nvrhi::ResourceStates::OpacityMicromapWrite, "OMMWrite"},
                {nvrhi::ResourceStates::OpacityMicromapBuildInput, "OMMInput"},
                {nvrhi::ResourceStates::ConvertCoopVecMatrixInput, "CoopVecInput"},
                {nvrhi::ResourceStates::ConvertCoopVecMatrixOutput, "CoopVecOutput"}};
            if (State == nvrhi::ResourceStates::Unknown)
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
                nvrhi::ResourceStates State,
                nvrhi::TextureSubresourceSet Subresources = nvrhi::AllSubresources)
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
                if (State == nvrhi::ResourceStates::Unknown)
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
                nvrhi::ResourceStates State,
                nvrhi::BufferRange Range = nvrhi::EntireBuffer)
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
                if (State == nvrhi::ResourceStates::Unknown)
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
                                        nvrhi::ResourceStates::RenderTarget,
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
                                    nvrhi::ResourceStates::DepthWrite,
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
        nvrhi::ICommandList& CommandList,
        EARDGPipeline Pipeline)
        : mCommandList(CommandList)
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
    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTexture* Texture) const
    {
        return mGraph.ResolveTextureForPass(mPass, Texture);
    }

    /** Resolves the parent physical texture of a declared logical SRV. */
    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureSRV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    /** Resolves the parent physical texture of a declared logical UAV. */
    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureUAV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    /** Resolves a directly declared logical buffer during pass recording. */
    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBuffer* Buffer) const
    {
        return mGraph.ResolveBufferForPass(mPass, Buffer);
    }

    /** Resolves the parent physical buffer of a declared logical SRV. */
    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferSRV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    /** Resolves the parent physical buffer of a declared logical UAV. */
    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferUAV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    /** Resolves a declared logical uniform buffer's physical constant buffer. */
    nvrhi::IBuffer* FARDGPassExecutionContext::GetUniformBuffer(
        FARDGUniformBuffer* UniformBuffer) const
    {
        return mGraph.ResolveUniformBufferForPass(mPass, UniformBuffer);
    }

    /** Builds a binding set directly from the active pass parameter descriptors. */
    nvrhi::BindingSetHandle FARDGPassExecutionContext::CreateBindingSet(
        nvrhi::IBindingLayout* BindingLayout) const
    {
        return mGraph.CreateBindingSetForPass(mPass, BindingLayout);
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
     * Resolves a logical texture to its materialized NVRHI object during recording.
     *
     * Ownership, active-pass scope, physical availability, and declaration membership
     * are all checked under the access mutex before the raw pointer is returned.
     */
    nvrhi::ITexture* FARDGBuilder::ResolveTextureForPass(
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
        return Texture->GetTexture();
    }

    /**
     * Validates an exact logical texture view, then resolves its parent texture.
     *
     * View identity is checked separately from parent access so a pass cannot use a
     * different subresource/format declaration merely because the parent was present.
     */
    nvrhi::ITexture* FARDGBuilder::ResolveTextureViewForPass(
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
     * Resolves a logical buffer to its materialized NVRHI object during recording.
     *
     * The same gated ownership and declaration checks used for textures protect
     * parallel pass callbacks from undeclared or cross-graph physical access.
     */
    nvrhi::IBuffer* FARDGBuilder::ResolveBufferForPass(
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
        return Buffer->GetBuffer();
    }

    /**
     * Validates an exact logical buffer view, then resolves its parent buffer.
     *
     * This preserves the selected range/format declaration as part of pass identity
     * while returning the parent NVRHI buffer used to build binding items.
     */
    nvrhi::IBuffer* FARDGBuilder::ResolveBufferViewForPass(
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
    nvrhi::IBuffer* FARDGBuilder::ResolveUniformBufferForPass(
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
        return UniformBuffer->GetBuffer();
    }

    /**
     * Materializes descriptor bindings from the active pass's frozen parameters.
     *
     * Parameter declaration order assigns independent t/u/b register slots. Arrays
     * occupy one slot and use NVRHI array elements. The supplied layout selects the
     * subset belonging to that binding set and supplies the concrete buffer-view
     * flavor (typed, structured, or raw).
     */
    nvrhi::BindingSetHandle FARDGBuilder::CreateBindingSetForPass(
        FARDGPassHandle Pass,
        nvrhi::IBindingLayout* BindingLayout) const
    {
        if (BindingLayout == nullptr)
        {
            ARDA_CHECK_MSG(
                "Cannot create pass bindings from a null binding layout.");
        }

        const FARDGPass* PassRecord = nullptr;
        nvrhi::IDevice* Device = nullptr;
        {
            std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
            PassRecord = mImpl->mPasses.TryGet(Pass);
            Device = mImpl->mContext.mDevice;
            if (PassRecord == nullptr ||
                mImpl->mActivePassAccess.find(Pass.GetIndex()) ==
                    mImpl->mActivePassAccess.end() ||
                PassRecord->GetParameters() == nullptr ||
                PassRecord->GetParameterMetadata() == nullptr ||
                Device == nullptr)
            {
                ARDA_CHECK_MSG(
                    "Pass bindings require active parameters and an NVRHI device.");
            }
        }

        const nvrhi::BindingLayoutDesc* LayoutDesc = BindingLayout->getDesc();
        if (LayoutDesc == nullptr)
        {
            ARDA_CHECK_MSG(
                "Pass parameters cannot populate a bindless binding layout.");
        }

        auto MakeBufferItem =
            [](nvrhi::ResourceType Type,
               uint32_t Slot,
               nvrhi::IBuffer* Buffer,
               nvrhi::Format Format,
               nvrhi::BufferRange Range)
            {
                switch (Type)
                {
                case nvrhi::ResourceType::TypedBuffer_SRV:
                    return nvrhi::BindingSetItem::TypedBuffer_SRV(
                        Slot, Buffer, Format, Range);
                case nvrhi::ResourceType::TypedBuffer_UAV:
                    return nvrhi::BindingSetItem::TypedBuffer_UAV(
                        Slot, Buffer, Format, Range);
                case nvrhi::ResourceType::StructuredBuffer_SRV:
                    return nvrhi::BindingSetItem::StructuredBuffer_SRV(
                        Slot, Buffer, Format, Range);
                case nvrhi::ResourceType::StructuredBuffer_UAV:
                    return nvrhi::BindingSetItem::StructuredBuffer_UAV(
                        Slot, Buffer, Format, Range);
                case nvrhi::ResourceType::RawBuffer_SRV:
                    return nvrhi::BindingSetItem::RawBuffer_SRV(
                        Slot, Buffer, Range);
                case nvrhi::ResourceType::RawBuffer_UAV:
                    return nvrhi::BindingSetItem::RawBuffer_UAV(
                        Slot, Buffer, Range);
                default:
                    ARDA_CHECK_MSG(
                        "A buffer parameter matched a non-buffer binding layout item.");
                    return nvrhi::BindingSetItem::None(Slot);
                }
            };

        nvrhi::BindingSetDesc BindingDesc;
        eastl::vector<uint32_t> MatchedElements(
            LayoutDesc->bindings.size(),
            0u);
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

                size_t LayoutIndex = LayoutDesc->bindings.size();
                for (size_t Index = 0;
                     Index < LayoutDesc->bindings.size();
                     ++Index)
                {
                    const nvrhi::BindingLayoutItem& Candidate =
                        LayoutDesc->bindings[Index];
                    if (Candidate.slot == Slot &&
                        IsCompatibleBindingType(ParameterType, Candidate.type))
                    {
                        LayoutIndex = Index;
                        break;
                    }
                }
                if (LayoutIndex == LayoutDesc->bindings.size())
                {
                    return;
                }

                const nvrhi::BindingLayoutItem& LayoutBinding =
                    LayoutDesc->bindings[LayoutIndex];
                if (Parameter.mArrayIndex >= LayoutBinding.getArraySize())
                {
                    ARDA_CHECK_MSG(
                        "A pass parameter array exceeds its binding layout.");
                }

                nvrhi::BindingSetItem Item =
                    nvrhi::BindingSetItem::None(Slot);
                switch (ParameterType)
                {
                case EARDGParameterType::Texture:
                    Item = nvrhi::BindingSetItem::Texture_SRV(
                        Slot,
                        ResolveTextureForPass(
                            Pass,
                            Parameter.GetValue<FARDGTextureRef>()));
                    break;

                case EARDGParameterType::TextureShaderResourceView:
                {
                    const FARDGTextureSRVRef View =
                        Parameter.GetValue<FARDGTextureSRVRef>();
                    Item = nvrhi::BindingSetItem::Texture_SRV(
                        Slot,
                        ResolveTextureViewForPass(Pass, View),
                        View->GetDesc().mFormat,
                        View->GetDesc().mSubresources,
                        View->GetDesc().mDimension);
                    break;
                }

                case EARDGParameterType::TextureUnorderedAccessView:
                {
                    const FARDGTextureUAVRef View =
                        Parameter.GetValue<FARDGTextureUAVRef>();
                    Item = nvrhi::BindingSetItem::Texture_UAV(
                        Slot,
                        ResolveTextureViewForPass(Pass, View),
                        View->GetDesc().mFormat,
                        View->GetDesc().mSubresources,
                        View->GetDesc().mDimension);
                    break;
                }

                case EARDGParameterType::Buffer:
                {
                    const FARDGBufferRef Buffer =
                        Parameter.GetValue<FARDGBufferRef>();
                    nvrhi::IBuffer* Physical =
                        ResolveBufferForPass(Pass, Buffer);
                    Item = MakeBufferItem(
                        LayoutBinding.type,
                        Slot,
                        Physical,
                        Physical->getDesc().format,
                        nvrhi::EntireBuffer);
                    break;
                }

                case EARDGParameterType::BufferShaderResourceView:
                {
                    const FARDGBufferSRVRef View =
                        Parameter.GetValue<FARDGBufferSRVRef>();
                    Item = MakeBufferItem(
                        LayoutBinding.type,
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
                        LayoutBinding.type,
                        Slot,
                        ResolveBufferViewForPass(Pass, View),
                        View->GetDesc().mFormat,
                        View->GetDesc().mRange);
                    break;
                }

                case EARDGParameterType::UniformBuffer:
                    Item = nvrhi::BindingSetItem::ConstantBuffer(
                        Slot,
                        ResolveUniformBufferForPass(
                            Pass,
                            Parameter.GetValue<FARDGUniformBufferRef>()));
                    break;

                case EARDGParameterType::Value:
                case EARDGParameterType::TextureAccess:
                case EARDGParameterType::BufferAccess:
                case EARDGParameterType::NestedStruct:
                case EARDGParameterType::RenderTargetBindingSlots:
                    return;
                }

                if (Item.type != LayoutBinding.type)
                {
                    ARDA_CHECK_MSG(
                        "A pass parameter does not match its binding layout type.");
                }
                Item.arrayElement =
                    static_cast<uint32_t>(Parameter.mArrayIndex);
                BindingDesc.addItem(Item);
                ++MatchedElements[LayoutIndex];
            });

        for (size_t Index = 0; Index < LayoutDesc->bindings.size(); ++Index)
        {
            if (MatchedElements[Index] !=
                LayoutDesc->bindings[Index].getArraySize())
            {
                ARDA_CHECK_MSG(
                    "A binding layout item has no matching pass parameter descriptor.");
            }
        }

        nvrhi::BindingSetHandle BindingSet =
            Device->createBindingSet(BindingDesc, BindingLayout);
        if (!BindingSet)
        {
            ARDA_CHECK_MSG(
                "NVRHI failed to create a pass parameter binding set.");
        }
        return BindingSet;
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
        nvrhi::TextureDesc Desc,
        EARDGResourceFlags Flags)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a texture outside graph building.");
        }
        const uint8_t FlagValue = static_cast<uint8_t>(Flags);
        const uint8_t AllowedFlags =
            static_cast<uint8_t>(EARDGResourceFlags::Transient);
        if (Desc.debugName.empty() ||
            Desc.width == 0 ||
            Desc.height == 0 ||
            Desc.depth == 0 ||
            Desc.arraySize == 0 ||
            Desc.mipLevels == 0 ||
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
     * record; no NVRHI buffer is allocated until execution materializes live work.
     */
    FARDGBufferRef FARDGBuilder::CreateBuffer(
        nvrhi::BufferDesc Desc,
        EARDGResourceFlags Flags)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot create a buffer outside graph building.");
        }
        const uint8_t FlagValue = static_cast<uint8_t>(Flags);
        const uint8_t AllowedFlags =
            static_cast<uint8_t>(EARDGResourceFlags::Transient);
        if (Desc.debugName.empty() ||
            Desc.byteSize == 0 ||
            (FlagValue & ~AllowedFlags) != 0)
        {
            ARDA_CHECK_MSG("Invalid logical buffer declaration.");
        }
        return &mImpl->mBuffers.Get(mImpl->mBuffers.Emplace(eastl::move(Desc), Flags));
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
     * the same NVRHI object deduplicates to one logical record when states agree.
     */
    FARDGTextureRef FARDGBuilder::RegisterExternalTexture(
        nvrhi::TextureHandle Texture,
        nvrhi::ResourceStates InitialState,
        eastl::string Name)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot import a texture after graph compilation.");
        }
        if (!Texture || InitialState == nvrhi::ResourceStates::Unknown)
        {
            ARDA_CHECK_MSG(
                "An external NVRHI texture requires a handle and known initial state.");
        }
        const auto Existing = mImpl->mImportedTextures.find(Texture.Get());
        if (Existing != mImpl->mImportedTextures.end())
        {
            if (Existing->second->GetInitialState() != InitialState)
            {
                ARDA_CHECK_MSG(
                    "An imported texture was registered with conflicting initial states.");
            }
            return Existing->second;
        }

        nvrhi::TextureDesc Desc = Texture->getDesc();
        if (!Name.empty())
        {
            Desc.debugName = ToStdString(Name);
        }
        if (Desc.debugName.empty())
        {
            Desc.debugName = "ExternalTexture";
        }
        Desc.initialState = InitialState;
        const FARDGTextureHandle Handle = mImpl->mTextures.Emplace(
            eastl::move(Desc),
            EARDGResourceFlags::External,
            Texture);
        FARDGTextureRef Resource = mImpl->mTextures.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedTextures.emplace(Texture.Get(), Resource);
        return Resource;
    }

    /**
     * Imports a caller-owned physical buffer at the graph boundary.
     *
     * As with textures, identity is deduplicated, conflicting initial states fail, and
     * prologue producer history makes the pre-graph contents available to first use.
     */
    FARDGBufferRef FARDGBuilder::RegisterExternalBuffer(
        nvrhi::BufferHandle Buffer,
        nvrhi::ResourceStates InitialState,
        eastl::string Name)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot import a buffer after graph compilation.");
        }
        if (!Buffer || InitialState == nvrhi::ResourceStates::Unknown)
        {
            ARDA_CHECK_MSG(
                "An external NVRHI buffer requires a handle and known initial state.");
        }
        const auto Existing = mImpl->mImportedBuffers.find(Buffer.Get());
        if (Existing != mImpl->mImportedBuffers.end())
        {
            if (Existing->second->GetInitialState() != InitialState)
            {
                ARDA_CHECK_MSG(
                    "An imported buffer was registered with conflicting initial states.");
            }
            return Existing->second;
        }

        nvrhi::BufferDesc Desc = Buffer->getDesc();
        if (!Name.empty())
        {
            Desc.debugName = ToStdString(Name);
        }
        if (Desc.debugName.empty())
        {
            Desc.debugName = "ExternalBuffer";
        }
        Desc.initialState = InitialState;
        const FARDGBufferHandle Handle = mImpl->mBuffers.Emplace(
            eastl::move(Desc),
            EARDGResourceFlags::External,
            Buffer);
        FARDGBufferRef Resource = mImpl->mBuffers.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedBuffers.emplace(Buffer.Get(), Resource);
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
        nvrhi::BufferDesc Desc;
        Desc.setDebugName(ToStdString(Name))
            .setByteSize(ByteSize)
            .setIsConstantBuffer(true)
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer);
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
        nvrhi::TextureHandle* Output,
        nvrhi::ResourceStates FinalState)
    {
        if (!IsBuilding(*mImpl) ||
            Texture == nullptr ||
            Output == nullptr ||
            mImpl->mTextures.TryGet(Texture->GetHandle()) != Texture ||
            FinalState == nvrhi::ResourceStates::Unknown)
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
        nvrhi::BufferHandle* Output,
        nvrhi::ResourceStates FinalState)
    {
        if (!IsBuilding(*mImpl) ||
            Buffer == nullptr ||
            Output == nullptr ||
            mImpl->mBuffers.TryGet(Buffer->GetHandle()) != Buffer ||
            FinalState == nvrhi::ResourceStates::Unknown)
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
                       << " mip=" << Transition.mSubresources.baseMipLevel
                       << "+" << Transition.mSubresources.numMipLevels
                       << " slice=" << Transition.mSubresources.baseArraySlice
                       << "+" << Transition.mSubresources.numArraySlices
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
