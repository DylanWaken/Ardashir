#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphCompiler.h"
#include "ArdaRenderGraphLog.h"
#include "ArdaRenderGraphValidation.h"
#include "ArdaScopeTimer.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>

namespace arda::render_graph
{
    namespace
    {
        constexpr uint32_t InvalidGroup = eastl::numeric_limits<uint32_t>::max();

        /**
         * Returns whether a state contains any state bit that permits a write.
         *
         * State lowering uses this when merging declarations for one resource
         * within one pass: writes must remain exclusive, while compatible
         * read-only requirements may be combined.
         */
        [[nodiscard]] bool IsWriteState(
            nvrhi::ResourceStates State) noexcept
        {
            constexpr uint32_t WriteMask =
                static_cast<uint32_t>(nvrhi::ResourceStates::UnorderedAccess) |
                static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
                static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
                static_cast<uint32_t>(nvrhi::ResourceStates::CopyDest) |
                static_cast<uint32_t>(nvrhi::ResourceStates::ResolveDest) |
                static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructWrite) |
                static_cast<uint32_t>(
                    nvrhi::ResourceStates::OpacityMicromapWrite) |
                static_cast<uint32_t>(
                    nvrhi::ResourceStates::ConvertCoopVecMatrixOutput);
            return (static_cast<uint32_t>(State) & WriteMask) != 0;
        }

        /**
         * Returns whether a state includes unordered access.
         *
         * Barrier lowering checks this even when before and after states are
         * equal, because consecutive UAV accesses still require memory ordering.
         */
        [[nodiscard]] bool IsUAVState(
            nvrhi::ResourceStates State) noexcept
        {
            return (State & nvrhi::ResourceStates::UnorderedAccess) !=
                nvrhi::ResourceStates::Unknown;
        }

        /**
         * Converts a declaration to the state required on the selected pipeline.
         *
         * Async compute cannot use the pixel stage. When a general shader-read
         * declaration contains both pixel and non-pixel bits, this removes only
         * the pixel bit; all other states and pipelines are preserved.
         */
        [[nodiscard]] nvrhi::ResourceStates NormalizeStateForPipeline(
            nvrhi::ResourceStates State,
            EARDGPipeline Pipeline) noexcept
        {
            if (Pipeline != EARDGPipeline::AsyncCompute ||
                (State & nvrhi::ResourceStates::PixelShaderResource) ==
                    nvrhi::ResourceStates::Unknown ||
                (State & nvrhi::ResourceStates::NonPixelShaderResource) ==
                    nvrhi::ResourceStates::Unknown)
            {
                return State;
            }

            return static_cast<nvrhi::ResourceStates>(
                static_cast<uint32_t>(State) &
                ~static_cast<uint32_t>(
                    nvrhi::ResourceStates::PixelShaderResource));
        }

        /**
         * Merges one pass's requirements for the same tracked resource unit.
         *
         * Unknown adopts the first requirement, equal states remain unchanged,
         * and distinct read-only states are ORed. A conflict involving a write
         * fails compilation. The unit is a texture mip/slice or a whole buffer.
         */
        [[nodiscard]] nvrhi::ResourceStates MergePassState(
            nvrhi::ResourceStates Existing,
            nvrhi::ResourceStates Required)
        {
            if (Existing == nvrhi::ResourceStates::Unknown)
            {
                return Required;
            }
            if (Existing == Required)
            {
                return Existing;
            }
            if (!IsWriteState(Existing) && !IsWriteState(Required))
            {
                return Existing | Required;
            }
            ARDA_CHECK_MSG(
                "A render-graph pass declares conflicting states for one resource.");
        }

        /**
         * Tests whether every state declared by a pass is legal on a copy queue.
         *
         * Each present declaration must be a nonzero subset of CopySource and
         * CopyDest. An empty pass is vacuously compatible.
         */
        [[nodiscard]] bool IsCopyCompatible(const FARDGPass& Pass) noexcept
        {
            constexpr uint32_t CopyMask =
                static_cast<uint32_t>(nvrhi::ResourceStates::CopySource) |
                static_cast<uint32_t>(nvrhi::ResourceStates::CopyDest);
            for (const FARDGPassTextureState& State :
                 Pass.GetState().mTextureStates)
            {
                const uint32_t Value = static_cast<uint32_t>(State.mState);
                if (Value == 0 || (Value & ~CopyMask) != 0)
                {
                    return false;
                }
            }
            for (const FARDGPassBufferState& State :
                 Pass.GetState().mBufferStates)
            {
                const uint32_t Value = static_cast<uint32_t>(State.mState);
                if (Value == 0 || (Value & ~CopyMask) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * Tests whether every pass state can execute on async compute.
         *
         * Graphics-only states are rejected. A pixel shader read is accepted
         * only when the non-pixel bit is also present, allowing pipeline
         * normalization to retain a valid compute read.
         */
        [[nodiscard]] bool IsAsyncComputeCompatible(
            const FARDGPass& Pass) noexcept
        {
            constexpr uint32_t GraphicsOnlyMask =
                static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
                static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
                static_cast<uint32_t>(nvrhi::ResourceStates::DepthRead) |
                static_cast<uint32_t>(nvrhi::ResourceStates::Present) |
                static_cast<uint32_t>(nvrhi::ResourceStates::ShadingRateSurface);
            // Use the same queue-compatibility rule for textures and buffers.
            const auto IsCompatibleState =
                [GraphicsOnlyMask](nvrhi::ResourceStates State)
                {
                    const bool bPixelShaderResource =
                        (State & nvrhi::ResourceStates::PixelShaderResource) !=
                        nvrhi::ResourceStates::Unknown;
                    const bool bNonPixelShaderResource =
                        (State & nvrhi::ResourceStates::NonPixelShaderResource) !=
                        nvrhi::ResourceStates::Unknown;
                    return (static_cast<uint32_t>(State) & GraphicsOnlyMask) == 0 &&
                        (!bPixelShaderResource || bNonPixelShaderResource);
                };
            for (const FARDGPassTextureState& State :
                 Pass.GetState().mTextureStates)
            {
                if (!IsCompatibleState(State.mState))
                {
                    return false;
                }
            }
            for (const FARDGPassBufferState& State :
                 Pass.GetState().mBufferStates)
            {
                if (!IsCompatibleState(State.mState))
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * Stage 4: selects a deterministic execution pipeline for every pass.
         *
         * Copy and async requests are honored only outside immediate mode for
         * non-sentinels with an available queue and compatible states. Failed
         * eligibility falls back to graphics. Only each pass's pipeline changes.
         */
        void AssignPipelines(FARDGBuilder::FImpl& Graph)
        {
            for (FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                EARDGPipeline Pipeline = EARDGPipeline::Graphics;
                if (!Graph.mContext.mDebugOptions.mbImmediateMode &&
                    !Pass->GetState().mbSentinel &&
                    HasAllFlags(Pass->GetFlags(), EARDGPassFlags::Copy) &&
                    Graph.mContext.mQueueCapabilities.mbCopy &&
                    IsCopyCompatible(*Pass))
                {
                    Pipeline = EARDGPipeline::Copy;
                }
                else if (!Graph.mContext.mDebugOptions.mbImmediateMode &&
                         !Pass->GetState().mbSentinel &&
                         HasAllFlags(
                             Pass->GetFlags(),
                             EARDGPassFlags::AsyncCompute) &&
                         Graph.mContext.mQueueCapabilities.mbCompute &&
                         IsAsyncComputeCompatible(*Pass))
                {
                    Pipeline = EARDGPipeline::AsyncCompute;
                }
                Pass->GetState().mPipeline = Pipeline;
            }
        }

        /**
         * Stage 5: validates incoming edges and builds reverse adjacency.
         *
         * Setup records predecessors on consumers. This stage sorts those lists,
         * requires every edge to point strictly forward in stable handle order,
         * and mirrors data and ordering-only edges onto producers. The forward
         * invariant makes registration order topological and precludes cycles.
         */
        void BuildConsumerEdges(FARDGBuilder::FImpl& Graph)
        {
            for (FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                Pass->GetState().mConsumers.clear();
                Pass->GetState().mSynchronizationConsumers.clear();
            }

            for (FARDGPass* Consumer : Graph.mPasses.GetEntries())
            {
                eastl::sort(
                    Consumer->GetState().mProducers.begin(),
                    Consumer->GetState().mProducers.end());
                for (FARDGPassHandle ProducerHandle :
                     Consumer->GetState().mProducers)
                {
                    FARDGPass* Producer = Graph.mPasses.TryGet(ProducerHandle);
                    if (Producer == nullptr ||
                        Consumer->GetHandle() < ProducerHandle ||
                        Consumer->GetHandle() == ProducerHandle)
                    {
                        ARDA_CHECK_MSG(
                            "The render graph contains an invalid producer edge.");
                    }
                    Producer->AddConsumer(Consumer->GetHandle());
                }

                eastl::sort(
                    Consumer->GetState().mSynchronizationProducers.begin(),
                    Consumer->GetState().mSynchronizationProducers.end());
                for (FARDGPassHandle ProducerHandle :
                     Consumer->GetState().mSynchronizationProducers)
                {
                    FARDGPass* Producer = Graph.mPasses.TryGet(ProducerHandle);
                    if (Producer == nullptr ||
                        Consumer->GetHandle() < ProducerHandle ||
                        Consumer->GetHandle() == ProducerHandle)
                    {
                        ARDA_CHECK_MSG(
                            "The render graph contains an invalid synchronization edge.");
                    }
                    Producer->AddSynchronizationConsumer(Consumer->GetHandle());
                }
            }
        }

        /**
         * Stage 6: removes passes that cannot contribute to an observable root.
         *
         * Normal mode walks data producers backward from the epilogue and
         * NeverCull roots. Synchronization edges order work but do not preserve
         * liveness. Execution order is the registration-order subsequence of
         * live passes; immediate mode instead keeps every pass.
         */
        void CullPasses(FARDGBuilder::FImpl& Graph)
        {
            if (Graph.mContext.mDebugOptions.mbImmediateMode)
            {
                Graph.mCompileResult.mExecutionOrder.clear();
                for (FARDGPass* Pass : Graph.mPasses.GetEntries())
                {
                    Pass->GetState().mbCulled = false;
                    Graph.mCompileResult.mExecutionOrder.push_back(
                        Pass->GetHandle());
                }
                return;
            }

            for (FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                Pass->GetState().mbCulled = !Pass->GetState().mbSentinel;
            }

            // Each popped producer extends a path from an observable root.
            eastl::vector<FARDGPassHandle> Worklist;
            Worklist.push_back(Graph.mCompileResult.mEpilogue);
            for (const FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                if (HasAllFlags(Pass->GetFlags(), EARDGPassFlags::NeverCull))
                {
                    Worklist.push_back(Pass->GetHandle());
                }
            }

            while (!Worklist.empty())
            {
                const FARDGPassHandle Handle = Worklist.back();
                Worklist.pop_back();
                FARDGPass& Pass = Graph.mPasses.Get(Handle);
                if (!Pass.GetState().mbCulled && !Pass.GetState().mbSentinel)
                {
                    continue;
                }
                Pass.GetState().mbCulled = false;
                for (FARDGPassHandle Producer : Pass.GetState().mProducers)
                {
                    if (Graph.mPasses.Get(Producer).GetState().mbCulled)
                    {
                        Worklist.push_back(Producer);
                    }
                }
            }

            Graph.mCompileResult.mExecutionOrder.clear();
            for (const FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                if (!Pass->GetState().mbCulled)
                {
                    Graph.mCompileResult.mExecutionOrder.push_back(Pass->GetHandle());
                }
            }
        }

        /**
         * Stage 7: rebuilds resource use intervals after culling.
         *
         * Build-time intervals include dead declarations, so this clears them
         * and replays only execution-order accesses. External and extracted
         * resources are also used at the epilogue for graph-exit state/ownership.
         */
        void RebuildLiveResourceIntervals(FARDGBuilder::FImpl& Graph)
        {
            for (FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                Texture->ResetUsage();
            }
            for (FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                Buffer->ResetUsage();
            }
            for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                for (const FARDGPassTextureState& State :
                     Pass.GetState().mTextureStates)
                {
                    Graph.mTextures.Get(State.mTexture).MarkUsed(Handle);
                }
                for (const FARDGPassBufferState& State :
                     Pass.GetState().mBufferStates)
                {
                    Graph.mBuffers.Get(State.mBuffer).MarkUsed(Handle);
                }
            }

            for (FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                if (Texture->IsExternal() || Texture->IsExtracted())
                {
                    Texture->MarkUsed(Graph.mCompileResult.mEpilogue);
                }
            }
            for (FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                if (Buffer->IsExternal() || Buffer->IsExtracted())
                {
                    Buffer->MarkUsed(Graph.mCompileResult.mEpilogue);
                }
            }
        }

        /**
         * Stage 8: emits inclusive lifetimes in execution-order coordinates.
         *
         * Resource records hold pass handles, so this first maps live handles
         * to compact execution indices. It emits textures then buffers in
         * registry order and identifies only graph-owned transient reuse
         * candidates. Debug lifetime extension widens all intervals.
         */
        void CompileResourceLifetimes(FARDGBuilder::FImpl& Graph)
        {
            eastl::vector<uint32_t> ExecutionIndices(
                Graph.mPasses.GetCount(),
                InvalidGroup);
            for (uint32_t Index = 0;
                 Index < Graph.mCompileResult.mExecutionOrder.size();
                 ++Index)
            {
                ExecutionIndices[
                    Graph.mCompileResult.mExecutionOrder[Index].GetIndex()] =
                    Index;
            }

            Graph.mCompileResult.mResourceLifetimes.clear();
            for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                if (!Texture->GetFirstUse().IsValid())
                {
                    continue;
                }
                Graph.mCompileResult.mResourceLifetimes.push_back(
                    {EARDGResourceType::Texture,
                     Texture->GetHandle().GetIndex(),
                     ExecutionIndices[Texture->GetFirstUse().GetIndex()],
                     ExecutionIndices[Texture->GetLastUse().GetIndex()],
                     HasAllFlags(
                         Texture->GetFlags(),
                         EARDGResourceFlags::Transient) &&
                         !Texture->IsExternal() &&
                         !Texture->IsExtracted()});
            }
            for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                if (!Buffer->GetFirstUse().IsValid())
                {
                    continue;
                }
                Graph.mCompileResult.mResourceLifetimes.push_back(
                    {EARDGResourceType::Buffer,
                     Buffer->GetHandle().GetIndex(),
                     ExecutionIndices[Buffer->GetFirstUse().GetIndex()],
                     ExecutionIndices[Buffer->GetLastUse().GetIndex()],
                     HasAllFlags(
                         Buffer->GetFlags(),
                         EARDGResourceFlags::Transient) &&
                         !Buffer->IsExternal() &&
                         !Buffer->IsExtracted()});
            }

            if ((Graph.mContext.mDebugOptions.mbExtendResourceLifetimes ||
                 Graph.mContext.mDebugOptions.mbImmediateMode) &&
                !Graph.mCompileResult.mExecutionOrder.empty())
            {
                const uint32_t Last = static_cast<uint32_t>(
                    Graph.mCompileResult.mExecutionOrder.size() - 1u);
                for (FARDGResourceLifetime& Lifetime :
                     Graph.mCompileResult.mResourceLifetimes)
                {
                    Lifetime.mFirstUse = 0;
                    Lifetime.mLastUse = Last;
                }
            }
        }

        /**
         * Stage 9: lowers live state declarations into transition metadata.
         *
         * Textures are tracked per mip and array slice; buffers are tracked as
         * whole resources. Repeated requirements within a pass are normalized
         * and merged before transitions are emitted. Equal UAV states request
         * ordering, conservative mode can force other equal-state barriers, and
         * graph-exit transitions are attached to the epilogue. No GPU calls occur.
         */
        void CompileBarriers(FARDGBuilder::FImpl& Graph)
        {
            eastl::vector<eastl::vector<nvrhi::ResourceStates>> TextureStates;
            TextureStates.reserve(Graph.mTextures.GetCount());
            for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                const nvrhi::TextureDesc& Desc = Texture->GetDesc();
                nvrhi::ResourceStates Initial = Texture->GetInitialState();
                if (Initial == nvrhi::ResourceStates::Unknown)
                {
                    Initial = nvrhi::ResourceStates::Common;
                }
                TextureStates.emplace_back(
                    static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
                    Initial);
            }

            eastl::vector<nvrhi::ResourceStates> BufferStates;
            BufferStates.reserve(Graph.mBuffers.GetCount());
            for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                nvrhi::ResourceStates Initial = Buffer->GetInitialState();
                if (Initial == nvrhi::ResourceStates::Unknown)
                {
                    Initial = nvrhi::ResourceStates::Common;
                }
                BufferStates.push_back(Initial);
            }

            for (FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                Pass->GetState().mTextureTransitions.clear();
                Pass->GetState().mBufferTransitions.clear();
            }

            for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
            {
                FARDGPass& Pass = Graph.mPasses.Get(Handle);
                if (Pass.GetState().mbSentinel)
                {
                    continue;
                }

                // Merge all aliases/views before advancing the tracked texture state.
                eastl::vector<eastl::vector<nvrhi::ResourceStates>> RequiredTextures(
                    Graph.mTextures.GetCount());
                for (const FARDGPassTextureState& Access :
                     Pass.GetState().mTextureStates)
                {
                    const nvrhi::ResourceStates RequiredState =
                        NormalizeStateForPipeline(
                            Access.mState,
                            Pass.GetState().mPipeline);
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Access.mTexture);
                    const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                    auto& Required = RequiredTextures[
                        Access.mTexture.GetIndex()];
                    if (Required.empty())
                    {
                        Required.resize(
                            static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
                            nvrhi::ResourceStates::Unknown);
                    }
                    const nvrhi::TextureSubresourceSet Subresources =
                        Access.mSubresources.resolve(Desc, false);
                    for (uint32_t ArraySlice = Subresources.baseArraySlice;
                         ArraySlice <
                             Subresources.baseArraySlice +
                                 Subresources.numArraySlices;
                         ++ArraySlice)
                    {
                        for (uint32_t MipLevel = Subresources.baseMipLevel;
                             MipLevel <
                                 Subresources.baseMipLevel +
                                     Subresources.numMipLevels;
                             ++MipLevel)
                        {
                            const size_t Index =
                                static_cast<size_t>(ArraySlice) * Desc.mipLevels +
                                MipLevel;
                            Required[Index] = MergePassState(
                                Required[Index],
                                RequiredState);
                        }
                    }
                }

                // Emit cell-sized records with explicit before/after continuity.
                for (uint32_t TextureIndex = 0;
                     TextureIndex < RequiredTextures.size();
                     ++TextureIndex)
                {
                    const auto& Required = RequiredTextures[TextureIndex];
                    if (Required.empty())
                    {
                        continue;
                    }
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(FARDGTextureHandle(TextureIndex));
                    const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                    auto& Current = TextureStates[TextureIndex];
                    for (uint32_t ArraySlice = 0;
                         ArraySlice < Desc.arraySize;
                         ++ArraySlice)
                    {
                        for (uint32_t MipLevel = 0;
                             MipLevel < Desc.mipLevels;
                             ++MipLevel)
                        {
                            const size_t Index =
                                static_cast<size_t>(ArraySlice) * Desc.mipLevels +
                                MipLevel;
                            if (Required[Index] ==
                                nvrhi::ResourceStates::Unknown)
                            {
                                continue;
                            }
                            const bool bUAVBarrier =
                                Current[Index] == Required[Index] &&
                                IsUAVState(Required[Index]);
                            const bool bForceBarrier =
                                Graph.mContext.mDebugOptions
                                    .mbConservativeBarriers &&
                                Current[Index] == Required[Index] &&
                                !bUAVBarrier &&
                                Required[Index] != nvrhi::ResourceStates::Common;
                            Pass.GetState().mTextureTransitions.push_back(
                                {FARDGTextureHandle(TextureIndex),
                                 nvrhi::TextureSubresourceSet(
                                     MipLevel,
                                     1,
                                     ArraySlice,
                                     1),
                                 Current[Index],
                                 Required[Index],
                                 bUAVBarrier,
                                 bForceBarrier});
                            Current[Index] = Required[Index];
                        }
                    }
                }

                // Buffer ranges are validated elsewhere but share one state slot.
                eastl::vector<nvrhi::ResourceStates> RequiredBuffers(
                    Graph.mBuffers.GetCount(),
                    nvrhi::ResourceStates::Unknown);
                for (const FARDGPassBufferState& Access :
                     Pass.GetState().mBufferStates)
                {
                    const uint32_t Index = Access.mBuffer.GetIndex();
                    RequiredBuffers[Index] = MergePassState(
                        RequiredBuffers[Index],
                        NormalizeStateForPipeline(
                            Access.mState,
                            Pass.GetState().mPipeline));
                }
                for (uint32_t BufferIndex = 0;
                     BufferIndex < RequiredBuffers.size();
                     ++BufferIndex)
                {
                    if (RequiredBuffers[BufferIndex] ==
                        nvrhi::ResourceStates::Unknown)
                    {
                        continue;
                    }
                    const bool bUAVBarrier =
                        BufferStates[BufferIndex] ==
                            RequiredBuffers[BufferIndex] &&
                        IsUAVState(RequiredBuffers[BufferIndex]);
                    const bool bForceBarrier =
                        Graph.mContext.mDebugOptions.mbConservativeBarriers &&
                        BufferStates[BufferIndex] ==
                            RequiredBuffers[BufferIndex] &&
                        !bUAVBarrier &&
                        RequiredBuffers[BufferIndex] !=
                            nvrhi::ResourceStates::Common;
                    Pass.GetState().mBufferTransitions.push_back(
                        {FARDGBufferHandle(BufferIndex),
                         BufferStates[BufferIndex],
                         RequiredBuffers[BufferIndex],
                         bUAVBarrier,
                         bForceBarrier});
                    BufferStates[BufferIndex] = RequiredBuffers[BufferIndex];
                }
            }

            // The synthetic epilogue owns graph-exit transitions and UAV ordering.
            FARDGPass& Epilogue =
                Graph.mPasses.Get(Graph.mCompileResult.mEpilogue);
            for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                if ((!Texture->IsExternal() && !Texture->IsExtracted()) ||
                    !Texture->GetFirstUse().IsValid())
                {
                    continue;
                }
                const nvrhi::TextureDesc& Desc = Texture->GetDesc();
                auto& Current =
                    TextureStates[Texture->GetHandle().GetIndex()];
                for (uint32_t ArraySlice = 0;
                     ArraySlice < Desc.arraySize;
                     ++ArraySlice)
                {
                    for (uint32_t MipLevel = 0;
                         MipLevel < Desc.mipLevels;
                         ++MipLevel)
                    {
                        const size_t Index =
                            static_cast<size_t>(ArraySlice) * Desc.mipLevels +
                            MipLevel;
                        const bool bUAVBarrier =
                            Current[Index] == Texture->GetFinalState() &&
                            IsUAVState(Current[Index]);
                        if (Current[Index] != Texture->GetFinalState() ||
                            bUAVBarrier)
                        {
                            Epilogue.GetState().mTextureTransitions.push_back(
                                {Texture->GetHandle(),
                                 nvrhi::TextureSubresourceSet(
                                     MipLevel,
                                     1,
                                     ArraySlice,
                                     1),
                                 Current[Index],
                                 Texture->GetFinalState(),
                                 bUAVBarrier});
                        }
                    }
                }
            }
            for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                if ((!Buffer->IsExternal() && !Buffer->IsExtracted()) ||
                    !Buffer->GetFirstUse().IsValid())
                {
                    continue;
                }
                const uint32_t Index = Buffer->GetHandle().GetIndex();
                const bool bUAVBarrier =
                    BufferStates[Index] == Buffer->GetFinalState() &&
                    IsUAVState(BufferStates[Index]);
                if (BufferStates[Index] != Buffer->GetFinalState() ||
                    bUAVBarrier)
                {
                    Epilogue.GetState().mBufferTransitions.push_back(
                        {Buffer->GetHandle(),
                         BufferStates[Index],
                         Buffer->GetFinalState(),
                         bUAVBarrier});
                }
            }
        }

        /**
         * Stage 12: emits synchronization metadata for cross-pipeline edges.
         *
         * Data and ordering-only producers are combined and deduplicated for
         * each live consumer. Culled and same-pipeline edges need no record;
         * every remaining edge becomes one deterministic queue dependency that
         * execution can use to derive waits.
         */
        void CompileQueueDependencies(FARDGBuilder::FImpl& Graph)
        {
            Graph.mCompileResult.mQueueDependencies.clear();
            for (FARDGPassHandle ConsumerHandle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Consumer =
                    Graph.mPasses.Get(ConsumerHandle);
                eastl::vector<FARDGPassHandle> Producers =
                    Consumer.GetState().mProducers;
                Producers.insert(
                    Producers.end(),
                    Consumer.GetState().mSynchronizationProducers.begin(),
                    Consumer.GetState().mSynchronizationProducers.end());
                eastl::sort(Producers.begin(), Producers.end());
                Producers.erase(
                    eastl::unique(Producers.begin(), Producers.end()),
                    Producers.end());
                for (FARDGPassHandle ProducerHandle : Producers)
                {
                    const FARDGPass& Producer =
                        Graph.mPasses.Get(ProducerHandle);
                    if (Producer.GetState().mbCulled ||
                        Producer.GetState().mPipeline ==
                            Consumer.GetState().mPipeline)
                    {
                        continue;
                    }
                    Graph.mCompileResult.mQueueDependencies.push_back(
                        {ProducerHandle,
                         ConsumerHandle,
                         Producer.GetState().mPipeline,
                         Consumer.GetState().mPipeline});
                }
            }
        }

        /**
         * Stage 11: computes graphics fork/join bounds for async-compute passes.
         *
         * Each async pass walks both edge kinds backward through non-graphics
         * work to the latest reachable graphics predecessor, and forward to the
         * earliest reachable graphics successor. Sentinels are fallbacks and
         * culled nodes are ignored. These fields are descriptive; explicit
         * queue dependencies drive submission synchronization.
         */
        void CompileAsyncMetadata(FARDGBuilder::FImpl& Graph)
        {
            for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
            {
                FARDGPass& Pass = Graph.mPasses.Get(Handle);
                Pass.GetState().mAsyncFork = FARDGPassHandle();
                Pass.GetState().mAsyncJoin = FARDGPassHandle();
                if (Pass.GetState().mPipeline != EARDGPipeline::AsyncCompute)
                {
                    continue;
                }

                FARDGPassHandle Fork = Graph.mCompileResult.mPrologue;
                eastl::vector<FARDGPassHandle> ProducerWorklist =
                    Pass.GetState().mProducers;
                ProducerWorklist.insert(
                    ProducerWorklist.end(),
                    Pass.GetState().mSynchronizationProducers.begin(),
                    Pass.GetState().mSynchronizationProducers.end());
                eastl::unordered_set<uint32_t> VisitedProducers;
                // Stop each ancestry path at its first live graphics boundary.
                while (!ProducerWorklist.empty())
                {
                    const FARDGPassHandle ProducerHandle =
                        ProducerWorklist.back();
                    ProducerWorklist.pop_back();
                    if (!VisitedProducers.insert(ProducerHandle.GetIndex()).second)
                    {
                        continue;
                    }
                    const FARDGPass& Producer = Graph.mPasses.Get(ProducerHandle);
                    if (Producer.GetState().mbCulled)
                    {
                        continue;
                    }
                    if (Producer.GetState().mPipeline == EARDGPipeline::Graphics)
                    {
                        if (Fork < ProducerHandle)
                        {
                            Fork = ProducerHandle;
                        }
                    }
                    else
                    {
                        ProducerWorklist.insert(
                            ProducerWorklist.end(),
                            Producer.GetState().mProducers.begin(),
                            Producer.GetState().mProducers.end());
                        ProducerWorklist.insert(
                            ProducerWorklist.end(),
                            Producer.GetState().mSynchronizationProducers.begin(),
                            Producer.GetState().mSynchronizationProducers.end());
                    }
                }

                FARDGPassHandle Join = Graph.mCompileResult.mEpilogue;
                eastl::vector<FARDGPassHandle> ConsumerWorklist =
                    Pass.GetState().mConsumers;
                ConsumerWorklist.insert(
                    ConsumerWorklist.end(),
                    Pass.GetState().mSynchronizationConsumers.begin(),
                    Pass.GetState().mSynchronizationConsumers.end());
                eastl::unordered_set<uint32_t> VisitedConsumers;
                // Mirror the search to find the nearest later graphics boundary.
                while (!ConsumerWorklist.empty())
                {
                    const FARDGPassHandle ConsumerHandle =
                        ConsumerWorklist.back();
                    ConsumerWorklist.pop_back();
                    if (!VisitedConsumers.insert(ConsumerHandle.GetIndex()).second)
                    {
                        continue;
                    }
                    const FARDGPass& Consumer = Graph.mPasses.Get(ConsumerHandle);
                    if (Consumer.GetState().mbCulled)
                    {
                        continue;
                    }
                    if (Consumer.GetState().mPipeline == EARDGPipeline::Graphics)
                    {
                        if (ConsumerHandle < Join)
                        {
                            Join = ConsumerHandle;
                        }
                    }
                    else
                    {
                        ConsumerWorklist.insert(
                            ConsumerWorklist.end(),
                            Consumer.GetState().mConsumers.begin(),
                            Consumer.GetState().mConsumers.end());
                        ConsumerWorklist.insert(
                            ConsumerWorklist.end(),
                            Consumer.GetState().mSynchronizationConsumers.begin(),
                            Consumer.GetState().mSynchronizationConsumers.end());
                    }
                }
                Pass.GetState().mAsyncFork = Fork;
                Pass.GetState().mAsyncJoin = Join;
            }
        }

        /**
         * Stage 13: groups adjacent compatible graphics raster passes.
         *
         * A maximal run shares a dense group index only when every pass is
         * groupable and has an identical logical attachment signature. Any
         * non-groupable pass breaks adjacency. Groups are metadata and do not
         * themselves merge command lists or render passes.
         */
        void CompileRasterGroups(FARDGBuilder::FImpl& Graph)
        {
            uint32_t GroupCount = 0;
            FARDGPass* PreviousRaster = nullptr;
            for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
            {
                FARDGPass& Pass = Graph.mPasses.Get(Handle);
                Pass.GetState().mRasterGroup = InvalidGroup;
                const bool bRaster =
                    HasAllFlags(Pass.GetFlags(), EARDGPassFlags::Raster) &&
                    !HasAllFlags(Pass.GetFlags(), EARDGPassFlags::SkipRenderPass) &&
                    Pass.GetState().mPipeline == EARDGPipeline::Graphics;
                if (!bRaster)
                {
                    PreviousRaster = nullptr;
                    continue;
                }

                if (PreviousRaster == nullptr ||
                    PreviousRaster->GetState().mRasterBindings !=
                        Pass.GetState().mRasterBindings)
                {
                    Pass.GetState().mRasterGroup = GroupCount++;
                }
                else
                {
                    Pass.GetState().mRasterGroup =
                        PreviousRaster->GetState().mRasterGroup;
                }
                PreviousRaster = &Pass;
            }
            Graph.mCompileResult.mRasterGroupCount = GroupCount;
        }
    }

    /**
     * Runs the full compile pipeline and publishes the builder-owned result.
     *
     * Validation precedes culling so dead work cannot hide bad declarations.
     * The epilogue makes external/extracted outputs observable, then queue
     * selection, reverse edges, liveness, lifetimes, barriers, transition replay,
     * async metadata, queue dependencies, and raster grouping run in dependency
     * order. The compiled flag is committed last; later calls return the cache.
     */
    const FARDGCompileResult& FARDGCompiler::Compile(FARDGBuilder& Builder)
    {
        ARDA_NAMED_SCOPE_TIMER("ARDG Compile");
        FARDGBuilder::FImpl& Graph = *Builder.mImpl;
        if (Graph.mbCompiled)
        {
            return Graph.mCompileResult;
        }

        FARDGValidation::ValidateBeforeCompile(Graph);

        for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
        {
            if (Texture->IsExtracted() &&
                !Texture->IsExternal() &&
                !Texture->GetLastProducer().IsValid())
            {
                ARDA_CHECK_MSG(
                    "A render-graph texture is extracted before it is produced.");
            }
        }
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            if (Buffer->IsExtracted() &&
                !Buffer->IsExternal() &&
                !Buffer->GetLastProducer().IsValid())
            {
                ARDA_CHECK_MSG(
                    "A render-graph buffer is extracted before it is produced.");
            }
        }

        // Anchor externally visible writes at one liveness root. Trailing
        // readers add ordering-only edges, so they remain culled if otherwise dead.
        Graph.mCompileResult.mEpilogue =
            Graph.mPasses.Emplace<FARDGSentinelPass>("GraphEpilogue");
        FARDGPass& Epilogue =
            Graph.mPasses.Get(Graph.mCompileResult.mEpilogue);

        for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
        {
            if ((Texture->IsExternal() || Texture->IsExtracted()) &&
                Texture->GetLastProducer().IsValid())
            {
                Epilogue.AddProducer(Texture->GetLastProducer());
            }
            if (Texture->IsExternal() || Texture->IsExtracted())
            {
                for (FARDGPassHandle Reader : Texture->GetReaders())
                {
                    Epilogue.AddSynchronizationProducer(Reader);
                }
            }
        }
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            if ((Buffer->IsExternal() || Buffer->IsExtracted()) &&
                Buffer->GetLastProducer().IsValid())
            {
                Epilogue.AddProducer(Buffer->GetLastProducer());
            }
            if (Buffer->IsExternal() || Buffer->IsExtracted())
            {
                for (FARDGPassHandle Reader : Buffer->GetReaders())
                {
                    Epilogue.AddSynchronizationProducer(Reader);
                }
            }
        }

        AssignPipelines(Graph);
        BuildConsumerEdges(Graph);
        CullPasses(Graph);
        RebuildLiveResourceIntervals(Graph);
        CompileResourceLifetimes(Graph);
        CompileBarriers(Graph);
        FARDGValidation::ValidateTransitions(Graph);
        CompileAsyncMetadata(Graph);
        CompileQueueDependencies(Graph);
        CompileRasterGroups(Graph);
        Graph.mbCompiled = true;
        return Graph.mCompileResult;
    }
}
