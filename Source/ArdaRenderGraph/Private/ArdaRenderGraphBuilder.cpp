#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphCompiler.h"
#include "ArdaRenderGraphExecutor.h"
#include "ArdaRenderGraphLog.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace arda::render_graph
{
    namespace
    {
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

        void AppendStateName(
            std::ostream& Stream,
            nvrhi::ResourceStates State)
        {
            struct FStateName
            {
                nvrhi::ResourceStates mState;
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
            TARDGHandleRegistry<FARDGPass, FARDGPassHandle>& mPasses;
            TARDGHandleRegistry<FARDGTexture, FARDGTextureHandle>& mTextures;
            TARDGHandleRegistry<FARDGBuffer, FARDGBufferHandle>& mBuffers;
            TARDGHandleRegistry<FARDGView, FARDGViewHandle>& mViews;
            TARDGHandleRegistry<FARDGUniformBuffer, FARDGUniformBufferHandle>& mUniformBuffers;
            FARDGPass& mPass;
            std::unordered_set<uint32_t> mVisitedUniformBuffers;

            void AddProducer(FARDGPassHandle Producer)
            {
                if (Producer != mPass.GetHandle())
                {
                    mPass.AddProducer(Producer);
                }
            }

            void AddView(FARDGViewHandle View)
            {
                if (std::find(
                        mPass.GetState().mViews.begin(),
                        mPass.GetState().mViews.end(),
                        View) == mPass.GetState().mViews.end())
                {
                    mPass.GetState().mViews.push_back(View);
                }
            }

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

            void Visit(const FARDGParameterMetadata& Metadata, const void* Parameters)
            {
                Metadata.Enumerate(
                    Parameters,
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

    FARDGBuilder::FARDGBuilder(FARDGRenderGraphContext Context)
        : mImpl(std::make_unique<FImpl>(std::move(Context)))
    {
        if (!mImpl->mContext.mQueueCapabilities.mbGraphics)
        {
            ARDA_CHECK_MSG(
                "A render graph requires graphics queue capability.");
        }
    }

    FARDGBuilder::~FARDGBuilder() = default;

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

    FARDGPassExecutionContext::~FARDGPassExecutionContext() noexcept
    {
        if (mbAccessGateOpen)
        {
            mGraph.EndPassAccess(mPass);
        }
    }

    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTexture* Texture) const
    {
        return mGraph.ResolveTextureForPass(mPass, Texture);
    }

    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureSRV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    nvrhi::ITexture* FARDGPassExecutionContext::GetTexture(
        FARDGTextureUAV* View) const
    {
        return mGraph.ResolveTextureViewForPass(mPass, View);
    }

    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBuffer* Buffer) const
    {
        return mGraph.ResolveBufferForPass(mPass, Buffer);
    }

    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferSRV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    nvrhi::IBuffer* FARDGPassExecutionContext::GetBuffer(
        FARDGBufferUAV* View) const
    {
        return mGraph.ResolveBufferViewForPass(mPass, View);
    }

    nvrhi::IBuffer* FARDGPassExecutionContext::GetUniformBuffer(
        FARDGUniformBuffer* UniformBuffer) const
    {
        return mGraph.ResolveUniformBufferForPass(mPass, UniformBuffer);
    }

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

    void FARDGBuilder::EndPassAccess(FARDGPassHandle Pass) noexcept
    {
        std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
        mImpl->mActivePassAccess.erase(Pass.GetIndex());
    }

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
        const bool bDeclared = std::any_of(
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
                std::find(
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
        const bool bDeclared = std::any_of(
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
                std::find(
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
        const bool bDeclared = std::find(
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

    void* FARDGBuilder::AllocateParameterStorage(size_t Size, size_t Alignment)
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG("Cannot allocate parameters outside graph building.");
        }
        return mImpl->mArena.AllocateBytes(Size, Alignment);
    }

    void FARDGBuilder::RegisterParameterDestructor(
        void* Object,
        void (*Destroy)(void*))
    {
        mImpl->mArena.RegisterDestructor(Object, Destroy);
    }

    void FARDGBuilder::MarkParameterStorage(const void* Parameters)
    {
        mImpl->mParameterStorage.insert(Parameters);
    }

    bool FARDGBuilder::IsParameterStorage(const void* Parameters) const noexcept
    {
        return mImpl->mParameterStorage.find(Parameters) !=
            mImpl->mParameterStorage.end();
    }

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
        return &mImpl->mTextures.Get(mImpl->mTextures.Emplace(std::move(Desc), Flags));
    }

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
        return &mImpl->mBuffers.Get(mImpl->mBuffers.Emplace(std::move(Desc), Flags));
    }

    FARDGTextureSRVRef FARDGBuilder::CreateTextureSRV(
        std::string Name,
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
            mImpl->mViews.Emplace<FARDGTextureSRV>(std::move(Name), std::move(Desc));
        return static_cast<FARDGTextureSRV*>(mImpl->mViews.TryGet(Handle));
    }

    FARDGTextureUAVRef FARDGBuilder::CreateTextureUAV(
        std::string Name,
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
            mImpl->mViews.Emplace<FARDGTextureUAV>(std::move(Name), std::move(Desc));
        return static_cast<FARDGTextureUAV*>(mImpl->mViews.TryGet(Handle));
    }

    FARDGBufferSRVRef FARDGBuilder::CreateBufferSRV(
        std::string Name,
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
            mImpl->mViews.Emplace<FARDGBufferSRV>(std::move(Name), std::move(Desc));
        return static_cast<FARDGBufferSRV*>(mImpl->mViews.TryGet(Handle));
    }

    FARDGBufferUAVRef FARDGBuilder::CreateBufferUAV(
        std::string Name,
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
            mImpl->mViews.Emplace<FARDGBufferUAV>(std::move(Name), std::move(Desc));
        return static_cast<FARDGBufferUAV*>(mImpl->mViews.TryGet(Handle));
    }

    FARDGTextureRef FARDGBuilder::RegisterExternalTexture(
        nvrhi::TextureHandle Texture,
        nvrhi::ResourceStates InitialState,
        std::string Name)
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
            Desc.debugName = std::move(Name);
        }
        if (Desc.debugName.empty())
        {
            Desc.debugName = "ExternalTexture";
        }
        Desc.initialState = InitialState;
        const FARDGTextureHandle Handle = mImpl->mTextures.Emplace(
            std::move(Desc),
            EARDGResourceFlags::External,
            Texture);
        FARDGTextureRef Resource = mImpl->mTextures.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedTextures.emplace(Texture.Get(), Resource);
        return Resource;
    }

    FARDGBufferRef FARDGBuilder::RegisterExternalBuffer(
        nvrhi::BufferHandle Buffer,
        nvrhi::ResourceStates InitialState,
        std::string Name)
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
            Desc.debugName = std::move(Name);
        }
        if (Desc.debugName.empty())
        {
            Desc.debugName = "ExternalBuffer";
        }
        Desc.initialState = InitialState;
        const FARDGBufferHandle Handle = mImpl->mBuffers.Emplace(
            std::move(Desc),
            EARDGResourceFlags::External,
            Buffer);
        FARDGBufferRef Resource = mImpl->mBuffers.TryGet(Handle);
        Resource->SetLastProducer(mImpl->mCompileResult.mPrologue);
        mImpl->mImportedBuffers.emplace(Buffer.Get(), Resource);
        return Resource;
    }

    FARDGUniformBufferRef FARDGBuilder::CreateUniformBufferInternal(
        std::string Name,
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
        Desc.setDebugName(Name)
            .setByteSize(ByteSize)
            .setIsConstantBuffer(true)
            .setInitialState(nvrhi::ResourceStates::ConstantBuffer);
        const FARDGUniformBufferHandle Handle = mImpl->mUniformBuffers.Emplace(
            Name,
            std::move(Desc),
            Metadata,
            Contents);
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

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
        const auto Existing = std::find_if(
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
        const auto Existing = std::find_if(
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

    FARDGPassHandle FARDGBuilder::AddPassInternal(
        std::string Name,
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
            std::move(Name),
            Flags,
            Parameters,
            Metadata,
            std::move(Execute));
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

    const FARDGExecutionResult& FARDGBuilder::Execute(
        const FARDGExecuteOptions& Options)
    {
        return FARDGExecutor::Execute(*this, Options);
    }

    const FARDGExecutionResult*
    FARDGBuilder::GetLastExecutionResult() const noexcept
    {
        return mImpl->mbExecuted ? &mImpl->mExecutionResult : nullptr;
    }

    bool FARDGBuilder::IsCompiled() const noexcept
    {
        return mImpl->mbCompiled;
    }

    const FARDGRenderGraphContext& FARDGBuilder::GetContext() const noexcept
    {
        return mImpl->mContext;
    }

    FARDGBlackboard& FARDGBuilder::GetBlackboard()
    {
        if (!IsBuilding(*mImpl))
        {
            ARDA_CHECK_MSG(
                "Cannot mutate the render-graph blackboard after building.");
        }
        return mImpl->mBlackboard;
    }

    const FARDGBlackboard& FARDGBuilder::GetBlackboard() const noexcept
    {
        return mImpl->mBlackboard;
    }

    FARDGPassHandle FARDGBuilder::GetProloguePass() const noexcept
    {
        return mImpl->mCompileResult.mPrologue;
    }

    FARDGPassHandle FARDGBuilder::GetEpiloguePass() const noexcept
    {
        return mImpl->mCompileResult.mEpilogue;
    }

    FARDGPass* FARDGBuilder::TryGetPass(FARDGPassHandle Handle) noexcept
    {
        return mImpl->mPasses.TryGet(Handle);
    }

    const FARDGPass* FARDGBuilder::TryGetPass(FARDGPassHandle Handle) const noexcept
    {
        return mImpl->mPasses.TryGet(Handle);
    }

    FARDGTexture* FARDGBuilder::TryGetTexture(FARDGTextureHandle Handle) noexcept
    {
        return mImpl->mTextures.TryGet(Handle);
    }

    const FARDGTexture* FARDGBuilder::TryGetTexture(
        FARDGTextureHandle Handle) const noexcept
    {
        return mImpl->mTextures.TryGet(Handle);
    }

    FARDGBuffer* FARDGBuilder::TryGetBuffer(FARDGBufferHandle Handle) noexcept
    {
        return mImpl->mBuffers.TryGet(Handle);
    }

    const FARDGBuffer* FARDGBuilder::TryGetBuffer(
        FARDGBufferHandle Handle) const noexcept
    {
        return mImpl->mBuffers.TryGet(Handle);
    }

    FARDGView* FARDGBuilder::TryGetView(FARDGViewHandle Handle) noexcept
    {
        return mImpl->mViews.TryGet(Handle);
    }

    const FARDGView* FARDGBuilder::TryGetView(
        FARDGViewHandle Handle) const noexcept
    {
        return mImpl->mViews.TryGet(Handle);
    }

    FARDGUniformBuffer* FARDGBuilder::TryGetUniformBuffer(
        FARDGUniformBufferHandle Handle) noexcept
    {
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

    const FARDGUniformBuffer* FARDGBuilder::TryGetUniformBuffer(
        FARDGUniformBufferHandle Handle) const noexcept
    {
        return mImpl->mUniformBuffers.TryGet(Handle);
    }

    const std::vector<FARDGTextureExtraction>&
    FARDGBuilder::GetTextureExtractions() const noexcept
    {
        return mImpl->mTextureExtractions;
    }

    const std::vector<FARDGBufferExtraction>&
    FARDGBuilder::GetBufferExtractions() const noexcept
    {
        return mImpl->mBufferExtractions;
    }

    std::string FARDGBuilder::DumpGraph() const
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
                   << " \"" << Texture->GetName() << "\""
                   << " external=" << Texture->IsExternal()
                   << " extracted=" << Texture->IsExtracted()
                   << " producer=" << Texture->GetLastProducer().GetIndex()
                   << "\n";
        }

        Stream << "Buffers " << mImpl->mBuffers.GetCount() << "\n";
        for (const FARDGBuffer* Buffer : mImpl->mBuffers.GetEntries())
        {
            Stream << " B" << Buffer->GetHandle().GetIndex()
                   << " \"" << Buffer->GetName() << "\""
                   << " external=" << Buffer->IsExternal()
                   << " extracted=" << Buffer->IsExtracted()
                   << " producer=" << Buffer->GetLastProducer().GetIndex()
                   << "\n";
        }

        Stream << "Passes " << mImpl->mPasses.GetCount() << "\n";
        for (const FARDGPass* Pass : mImpl->mPasses.GetEntries())
        {
            const FARDGPassState& State = Pass->GetState();
            Stream << " P" << Pass->GetHandle().GetIndex()
                   << " \"" << Pass->GetName() << "\""
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
        return Stream.str();
    }
}
