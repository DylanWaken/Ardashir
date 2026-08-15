#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphExecutor.h"
#include "ArdaRenderGraphLog.h"
#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"

#include <EASTL/algorithm.h>
#include <cstring>
#include <future>
#include <EASTL/sort.h>
#include <thread>
#include <EASTL/unordered_map.h>

namespace arda::render_graph
{
    namespace
    {
        struct FARDGRuntimePassTransitions
        {
            /** Physical texture transitions emitted while recording this pass. */
            eastl::vector<FARDGTextureTransition> mTextures;
            /** Physical buffer transitions emitted while recording this pass. */
            eastl::vector<FARDGBufferTransition> mBuffers;
            eastl::vector<FARDGAccelStructTransition> mAccelStructs;
            /** First-write texture accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassTextureState> mTextureClobbers;
            /** First-write buffer accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassBufferState> mBufferClobbers;
        };

        struct FARDGRecordedPass
        {
            /** RHI command list populated during the recording stage. */
            rhi::FArdaRHICommandListRef mCommandList;
            /** Submission queue selected from the pass pipeline; graphics is the default. */
            rhi::EArdaRHIQueueType mQueue = rhi::EArdaRHIQueueType::Graphics;
            /** Number of debug resource clears encoded into this pass's command list. */
            uint32_t mClobberedResourceCount = 0;
        };

        struct FARDGExecutionFailureGuard
        {
            /** Non-owning graph implementation protected for this execution attempt. */
            FARDGBuilder::FImpl& mGraph;
            /** Disarm flag set only after submission and extraction publication succeed. */
            bool mbCompleted = false;

            /**
             * Makes an interrupted execution attempt terminal for this builder.
             *
             * Execute disarms the guard only after all submissions and
             * extraction publication succeed, so exceptions cannot leave a
             * partially submitted graph looking reusable.
             */
            ~FARDGExecutionFailureGuard()
            {
                if (!mbCompleted)
                {
                    mGraph.mbFailed = true;
                }
            }
        };

        /** Maps the compiler-selected graph pipeline to its RHI submit queue. */
        [[nodiscard]] rhi::EArdaRHIQueueType GetCommandQueue(
            EARDGPipeline Pipeline) noexcept
        {
            switch (Pipeline)
            {
            case EARDGPipeline::Graphics:
                return rhi::EArdaRHIQueueType::Graphics;
            case EARDGPipeline::AsyncCompute:
                return rhi::EArdaRHIQueueType::Compute;
            case EARDGPipeline::Copy:
                return rhi::EArdaRHIQueueType::Copy;
            }
            return rhi::EArdaRHIQueueType::Graphics;
        }

        /** Converts an RHI queue enum to the result-array bookkeeping index. */
        [[nodiscard]] size_t GetQueueIndex(
            rhi::EArdaRHIQueueType Queue) noexcept
        {
            return static_cast<size_t>(Queue);
        }

        /** Identifies states that require explicit unordered-access ordering. */
        [[nodiscard]] bool IsUAVState(
            rhi::EArdaRHIResourceState State) noexcept
        {
            return (State & rhi::EArdaRHIResourceState::UnorderedAccess) !=
                rhi::EArdaRHIResourceState::Unknown;
        }

        /**
         * Replaces an unspecified resource start state with the executable
         * Common state used during materialization and transition tracking.
         */
        [[nodiscard]] rhi::EArdaRHIResourceState NormalizeInitialState(
            rhi::EArdaRHIResourceState State) noexcept
        {
            return State == rhi::EArdaRHIResourceState::Unknown
                ? rhi::EArdaRHIResourceState::Common
                : State;
        }

        /**
         * Performs the definitive compatibility check for texture-pool reuse.
         *
         * This execution-materialization check includes every descriptor field
         * relevant to the pool policy; hash equality alone is never trusted.
         */
        [[nodiscard]] bool TextureDescriptorsEqual(
            const rhi::FArdaRHITextureDesc& Left,
            const rhi::FArdaRHITextureDesc& Right) noexcept
        {
            return Left.mWidth == Right.mWidth &&
                Left.mHeight == Right.mHeight &&
                Left.mDepth == Right.mDepth &&
                Left.mArraySize == Right.mArraySize &&
                Left.mMipLevels == Right.mMipLevels &&
                Left.mSampleCount == Right.mSampleCount &&
                Left.mFormat == Right.mFormat &&
                Left.mDimension == Right.mDimension &&
                Left.mUsage == Right.mUsage &&
                Left.mbUseClearValue == Right.mbUseClearValue &&
                (!Left.mbUseClearValue || Left.mClearValue == Right.mClearValue);
        }

        /** Performs the definitive descriptor check for committed-buffer reuse. */
        [[nodiscard]] bool BufferDescriptorsEqual(
            const rhi::FArdaRHIBufferDesc& Left,
            const rhi::FArdaRHIBufferDesc& Right) noexcept
        {
            return Left.mByteSize == Right.mByteSize &&
                Left.mStructureStride == Right.mStructureStride &&
                Left.mMaxVersions == Right.mMaxVersions &&
                Left.mFormat == Right.mFormat &&
                Left.mUsage == Right.mUsage &&
                Left.mCpuAccess == Right.mCpuAccess;
        }

        /**
         * Mixes one descriptor field into a pool bucket key.
         *
         * Collisions are harmless because acquisition always follows hashing
         * with full descriptor equality.
         */
        template <typename ValueType>
        void HashCombine(size_t& Seed, const ValueType& Value)
        {
            Seed ^= eastl::hash<ValueType>{}(Value) +
                0x9e3779b9u +
                (Seed << 6u) +
                (Seed >> 2u);
        }

        /**
         * Builds a fast texture-pool bucket key from high-selectivity fields.
         *
         * Omitted fields are checked by TextureDescriptorsEqual before reuse.
         */
        [[nodiscard]] size_t HashTextureDescriptor(
            const rhi::FArdaRHITextureDesc& Desc)
        {
            rhi::FArdaRHITextureDesc PoolDesc = Desc;
            PoolDesc.mDebugName.clear();
            return rhi::HashValue(PoolDesc);
        }

        /**
         * Builds a fast buffer-pool bucket key from high-selectivity fields.
         *
         * Omitted fields are checked by BufferDescriptorsEqual before reuse.
         */
        [[nodiscard]] size_t HashBufferDescriptor(
            const rhi::FArdaRHIBufferDesc& Desc)
        {
            rhi::FArdaRHIBufferDesc PoolDesc = Desc;
            PoolDesc.mDebugName.clear();
            return rhi::HashValue(PoolDesc);
        }

        class FARDGTexturePool final
        {
        public:
            /** Creates an execution-local texture pool and result counter sink. */
            FARDGTexturePool(
                rhi::IArdaRHIDevice& Device,
                FARDGExecutionResult& Result)
                : mDevice(Device)
                , mResult(Result)
            {
            }

            /**
             * Binds one logical lifetime to a compatible committed texture.
             *
             * Descriptors are normalized before lookup. Reuse requires exact
             * compatibility, the same non-negative queue domain, and an
             * earlier inclusive lifetime whose last use precedes FirstUse.
             * Reuse advances the entry's availability and updates the report;
             * otherwise a new RHI object is created.
             */
            [[nodiscard]] rhi::FArdaRHITextureRef Acquire(
                rhi::FArdaRHITextureDesc Desc,
                uint32_t FirstUse,
                uint32_t LastUse,
                int32_t ReuseDomain)
            {
                Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                Desc.mbKeepInitialState = false;
                Desc.mbVirtual = false;
                const size_t Key = HashTextureDescriptor(Desc);
                auto& Bucket = mEntries[Key];
                if (ReuseDomain >= 0)
                {
                    for (FEntry& Entry : Bucket)
                    {
                        if (Entry.mAvailableAfter < FirstUse &&
                            Entry.mReuseDomain == ReuseDomain &&
                            TextureDescriptorsEqual(Entry.mDesc, Desc))
                        {
                            Entry.mAvailableAfter = LastUse;
                            ++mResult.mTexturePoolReuseCount;
                            return Entry.mTexture;
                        }
                    }
                }

                auto TextureResult = mDevice.CreateTexture(Desc);
                if (!TextureResult)
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to create a render-graph texture.");
                }
                Bucket.push_back(
                    {eastl::move(Desc), TextureResult.mValue, LastUse, ReuseDomain});
                return TextureResult.mValue;
            }

        private:
            struct FEntry
            {
                /** Normalized descriptor used for definitive compatibility checks. */
                rhi::FArdaRHITextureDesc mDesc;
                /** Pool-owned reference to the reusable physical texture. */
                rhi::FArdaRHITextureRef mTexture;
                /** Inclusive execution-order index of the latest logical user's last use. */
                uint32_t mAvailableAfter = 0;
                /** Queue reuse domain index, or -1 when this entry cannot be recycled. */
                int32_t mReuseDomain = -1;
            };

            /** Non-owning device used to create textures during materialization. */
            rhi::IArdaRHIDevice& mDevice;
            /** Non-owning execution report updated with texture reuse statistics. */
            FARDGExecutionResult& mResult;
            /** Execution-local descriptor-hash buckets owning reusable texture handles. */
            eastl::unordered_map<size_t, eastl::vector<FEntry>> mEntries;
        };

        class FARDGBufferPool final
        {
        public:
            /** Creates an execution-local buffer pool and result counter sink. */
            FARDGBufferPool(
                rhi::IArdaRHIDevice& Device,
                FARDGExecutionResult& Result)
                : mDevice(Device)
                , mResult(Result)
            {
            }

            /**
             * Binds one logical lifetime to a compatible committed buffer.
             *
             * Reuse follows the same inclusive-lifetime, descriptor-equality,
             * and same-queue-domain invariants as texture acquisition. A
             * negative domain deliberately forces a fresh physical object.
             */
            [[nodiscard]] rhi::FArdaRHIBufferRef Acquire(
                rhi::FArdaRHIBufferDesc Desc,
                uint32_t FirstUse,
                uint32_t LastUse,
                int32_t ReuseDomain)
            {
                Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                Desc.mbKeepInitialState = false;
                Desc.mbVirtual = false;
                const size_t Key = HashBufferDescriptor(Desc);
                auto& Bucket = mEntries[Key];
                if (ReuseDomain >= 0)
                {
                    for (FEntry& Entry : Bucket)
                    {
                        if (Entry.mAvailableAfter < FirstUse &&
                            Entry.mReuseDomain == ReuseDomain &&
                            BufferDescriptorsEqual(Entry.mDesc, Desc))
                        {
                            Entry.mAvailableAfter = LastUse;
                            ++mResult.mBufferPoolReuseCount;
                            return Entry.mBuffer;
                        }
                    }
                }

                auto BufferResult = mDevice.CreateBuffer(Desc);
                if (!BufferResult)
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to create a render-graph buffer.");
                }
                Bucket.push_back(
                    {eastl::move(Desc), BufferResult.mValue, LastUse, ReuseDomain});
                return BufferResult.mValue;
            }

        private:
            struct FEntry
            {
                /** Normalized descriptor used for definitive compatibility checks. */
                rhi::FArdaRHIBufferDesc mDesc;
                /** Pool-owned reference to the reusable physical buffer. */
                rhi::FArdaRHIBufferRef mBuffer;
                /** Inclusive execution-order index of the latest logical user's last use. */
                uint32_t mAvailableAfter = 0;
                /** Queue reuse domain index, or -1 when this entry cannot be recycled. */
                int32_t mReuseDomain = -1;
            };

            /** Non-owning device used to create buffers during materialization. */
            rhi::IArdaRHIDevice& mDevice;
            /** Non-owning execution report updated with buffer reuse statistics. */
            FARDGExecutionResult& mResult;
            /** Execution-local descriptor-hash buckets owning reusable buffer handles. */
            eastl::unordered_map<size_t, eastl::vector<FEntry>> mEntries;
        };

        /**
         * Evaluates ideal placed-resource packing before materialization.
         *
         * Virtual probes provide size/alignment when the backend supports
         * them, but the computed aliasing layout is intentionally not applied:
         * the portable RHI surface does not expose all required aliasing
         * safety operations. Every transient candidate is therefore reported
         * as using committed-resource fallback.
         *
         * TODO(ArdaRenderGraph): Enable physical placed-resource aliasing here
         * after the RHI provides portable aliasing barriers and heap-compatibility
         * queries; then consume the computed layout instead of discarding it.
         */
        void EvaluateTransientHeapLayout(
            FARDGBuilder::FImpl& Graph,
            rhi::IArdaRHIDevice& Device,
            FARDGExecutionResult& Result)
        {
            eastl::vector<FARDGTransientAllocationRequest> Requests;
            if (!Device.GetCapabilities().mbVirtualResources)
            {
                for (const FARDGResourceLifetime& Lifetime :
                     Graph.mCompileResult.mResourceLifetimes)
                {
                    Result.mbUsedTransientFallback |= Lifetime.mbTransient;
                }
                return;
            }

            uint32_t Identifier = 0;
            for (const FARDGResourceLifetime& Lifetime :
                 Graph.mCompileResult.mResourceLifetimes)
            {
                if (!Lifetime.mbTransient)
                {
                    continue;
                }

                rhi::FArdaRHIMemoryRequirements Requirements;
                if (Lifetime.mType == EARDGResourceType::Texture)
                {
                    rhi::FArdaRHITextureDesc Desc =
                        Graph.mTextures
                            .Get(FARDGTextureHandle(Lifetime.mResourceIndex))
                            .GetDesc();
                    Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                    Desc.mbKeepInitialState = false;
                    Desc.mbVirtual = true;
                    auto Probe = Device.CreateTexture(Desc);
                    if (Probe)
                    {
                        auto Memory =
                            Device.GetTextureMemoryRequirements(Probe.mValue);
                        if (Memory)
                        {
                            Requirements = Memory.mValue;
                        }
                    }
                }
                else if (Lifetime.mType == EARDGResourceType::Buffer)
                {
                    rhi::FArdaRHIBufferDesc Desc =
                        Graph.mBuffers
                            .Get(FARDGBufferHandle(Lifetime.mResourceIndex))
                            .GetDesc();
                    Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                    Desc.mbKeepInitialState = false;
                    Desc.mbVirtual = true;
                    auto Probe = Device.CreateBuffer(Desc);
                    if (Probe)
                    {
                        auto Memory =
                            Device.GetBufferMemoryRequirements(Probe.mValue);
                        if (Memory)
                        {
                            Requirements = Memory.mValue;
                        }
                    }
                }

                if (Requirements.mSize != 0 && Requirements.mAlignment != 0)
                {
                    Requests.push_back(
                        {Identifier++,
                         Lifetime.mFirstUse,
                         Lifetime.mLastUse,
                         Requirements.mSize,
                         Requirements.mAlignment});
                }
                Result.mbUsedTransientFallback = true;
            }

            if (!Requests.empty())
            {
                const FARDGTransientHeapLayout IdealLayout =
                    FARDGTransientHeapAllocator::Allocate(Requests, true);
                (void)IdealLayout;
                // The RHI exposes placed resources but no portable aliasing
                // barrier or heap compatibility query. Using the ideal layout
                // would therefore be unsafe on at least one supported backend.
            }
        }

        /**
         * Returns the sole queue domain using a transient texture, or -1.
         *
         * Materialization permits committed-object recycling only when every
         * live use stays on one queue, avoiding unproven cross-queue lifetime
         * completion assumptions.
         */
        [[nodiscard]] int32_t GetTextureReuseDomain(
            const FARDGBuilder::FImpl& Graph,
            FARDGTextureHandle Texture)
        {
            int32_t Domain = -1;
            for (FARDGPassHandle Handle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                const bool bUsesTexture = eastl::any_of(
                    Pass.GetState().mTextureStates.begin(),
                    Pass.GetState().mTextureStates.end(),
                    // This predicate only establishes whether the pass
                    // contributes its selected queue to the resource domain.
                    [Texture](const FARDGPassTextureState& State)
                    {
                        return State.mTexture == Texture;
                    });
                if (!bUsesTexture)
                {
                    continue;
                }
                const int32_t PassDomain = static_cast<int32_t>(
                    GetQueueIndex(
                        GetCommandQueue(Pass.GetState().mPipeline)));
                if (Domain >= 0 && Domain != PassDomain)
                {
                    return -1;
                }
                Domain = PassDomain;
            }
            return Domain;
        }

        /** Buffer counterpart to GetTextureReuseDomain. */
        [[nodiscard]] int32_t GetBufferReuseDomain(
            const FARDGBuilder::FImpl& Graph,
            FARDGBufferHandle Buffer)
        {
            int32_t Domain = -1;
            for (FARDGPassHandle Handle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                const bool bUsesBuffer = eastl::any_of(
                    Pass.GetState().mBufferStates.begin(),
                    Pass.GetState().mBufferStates.end(),
                    // Capture the typed handle by value for a pure membership test.
                    [Buffer](const FARDGPassBufferState& State)
                    {
                        return State.mBuffer == Buffer;
                    });
                if (!bUsesBuffer)
                {
                    continue;
                }
                const int32_t PassDomain = static_cast<int32_t>(
                    GetQueueIndex(
                        GetCommandQueue(Pass.GetState().mPipeline)));
                if (Domain >= 0 && Domain != PassDomain)
                {
                    return -1;
                }
                Domain = PassDomain;
            }
            return Domain;
        }

        /**
         * Materializes every live logical resource before command recording.
         *
         * Lifetimes are visited deterministically so execution-local pools can
         * recycle exact committed-resource matches after non-overlapping uses.
         * Imported resources retain their handles; created textures/buffers
         * bind pool results; uniform buffers receive dedicated allocations.
         */
        void MaterializeResources(
            FARDGBuilder::FImpl& Graph,
            rhi::IArdaRHIDevice& Device,
            FARDGExecutionResult& Result)
        {
            EvaluateTransientHeapLayout(Graph, Device, Result);

            FARDGTexturePool TexturePool(Device, Result);
            FARDGBufferPool BufferPool(Device, Result);
            eastl::vector<FARDGResourceLifetime> Lifetimes =
                Graph.mCompileResult.mResourceLifetimes;
            eastl::sort(
                Lifetimes.begin(),
                Lifetimes.end(),
                // First-use order makes pool availability meaningful; type and
                // registry index make ties deterministic.
                [](const auto& Left, const auto& Right)
                {
                    if (Left.mFirstUse != Right.mFirstUse)
                    {
                        return Left.mFirstUse < Right.mFirstUse;
                    }
                    if (Left.mType != Right.mType)
                    {
                        return Left.mType < Right.mType;
                    }
                    return Left.mResourceIndex < Right.mResourceIndex;
                });

            for (const FARDGResourceLifetime& Lifetime : Lifetimes)
            {
                if (Lifetime.mType == EARDGResourceType::Texture)
                {
                    FARDGTexture& Texture = Graph.mTextures.Get(
                        FARDGTextureHandle(Lifetime.mResourceIndex));
                    if (Texture.IsExternal())
                    {
                        if (!Texture.GetTexture())
                        {
                            ARDA_CHECK_MSG(
                                "A render-graph external texture lost its handle.");
                        }
                        continue;
                    }
                    Texture.BindTexture(TexturePool.Acquire(
                        Texture.GetDesc(),
                        Lifetime.mFirstUse,
                        Lifetime.mLastUse,
                        Lifetime.mbTransient
                            ? GetTextureReuseDomain(
                                  Graph,
                                  Texture.GetHandle())
                            : -1));
                }
                else if (Lifetime.mType == EARDGResourceType::Buffer)
                {
                    FARDGBuffer& Buffer = Graph.mBuffers.Get(
                        FARDGBufferHandle(Lifetime.mResourceIndex));
                    if (Buffer.IsExternal())
                    {
                        if (!Buffer.GetBuffer())
                        {
                            ARDA_CHECK_MSG(
                                "A render-graph external buffer lost its handle.");
                        }
                        continue;
                    }
                    Buffer.BindBuffer(BufferPool.Acquire(
                        Buffer.GetDesc(),
                        Lifetime.mFirstUse,
                        Lifetime.mLastUse,
                        Lifetime.mbTransient
                            ? GetBufferReuseDomain(
                                  Graph,
                                  Buffer.GetHandle())
                            : -1));
                }
                else if (Lifetime.mType == EARDGResourceType::AccelStruct)
                {
                    FARDGAccelStruct& AccelStruct = Graph.mAccelStructs.Get(
                        FARDGAccelStructHandle(Lifetime.mResourceIndex));
                    if (AccelStruct.IsExternal())
                    {
                        if (!AccelStruct.GetAccelStruct())
                        {
                            ARDA_CHECK_MSG(
                                "A render-graph external acceleration structure lost its handle.");
                        }
                        continue;
                    }
                    auto Created = Device.CreateAccelStruct(AccelStruct.GetDesc());
                    if (!Created)
                    {
                        ARDA_CHECK_MSG(
                            "The RHI failed to create a graph acceleration structure.");
                    }
                    AccelStruct.BindAccelStruct(eastl::move(Created.mValue));
                }
            }

            for (FARDGUniformBuffer* UniformBuffer :
                 Graph.mUniformBuffers.GetEntries())
            {
                auto Buffer = Device.CreateBuffer(UniformBuffer->GetDesc());
                if (!Buffer)
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to create a graph uniform buffer.");
                }
                UniformBuffer->BindBuffer(eastl::move(Buffer.mValue));
            }
        }

        /**
         * Rebuilds compiled logical transitions against physical identities.
         *
         * Pooling may bind disjoint logical resources to one RHI object, so
         * the second resource inherits the first resource's final state rather
         * than its own descriptor's initial state. This stage walks execution
         * order, tracks that physical history (per mip/slice for textures and
         * whole-resource for buffers), and emits pass-indexed runtime records.
         * It also plans supported debug clobbers before first writes.
         */
        [[nodiscard]] eastl::vector<FARDGRuntimePassTransitions>
        BuildPhysicalTransitions(FARDGBuilder::FImpl& Graph)
        {
            eastl::unordered_map<
                const void*,
                eastl::vector<rhi::EArdaRHIResourceState>>
                TextureStates;
            eastl::unordered_map<const void*, rhi::EArdaRHIResourceState>
                BufferStates;
            eastl::vector<FARDGRuntimePassTransitions> Runtime(
                Graph.mPasses.GetCount());

            for (FARDGPassHandle Handle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                auto& Out = Runtime[Handle.GetIndex()];
                for (const FARDGTextureTransition& Compiled :
                     Pass.GetState().mTextureTransitions)
                {
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Compiled.mTexture);
                    const void* Physical = Texture.GetTexture()->GetPhysicalIdentity();
                    if (Physical == nullptr)
                    {
                        ARDA_CHECK_MSG(
                            "A live graph texture was not materialized.");
                    }
                    auto& States = TextureStates[Physical];
                    const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                    if (States.empty())
                    {
                        // A physical texture enters history once; later logical
                        // aliases continue from the state left in this vector.
                        States.resize(
                            static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                            NormalizeInitialState(Texture.GetInitialState()));
                    }
                    const rhi::FArdaRHITextureSubresourceRange Subresources =
                        Compiled.mSubresources.Resolve(Desc);
                    for (uint32_t ArraySlice = Subresources.mBaseArraySlice;
                         ArraySlice <
                            Subresources.mBaseArraySlice +
                                Subresources.mArraySliceCount;
                         ++ArraySlice)
                    {
                        for (uint32_t MipLevel = Subresources.mBaseMipLevel;
                             MipLevel <
                                Subresources.mBaseMipLevel +
                                    Subresources.mMipLevelCount;
                             ++MipLevel)
                        {
                            const size_t Index =
                                static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                                MipLevel;
                            Out.mTextures.push_back(
                                {Compiled.mTexture,
                                 rhi::FArdaRHITextureSubresourceRange{
                                     MipLevel, 1, ArraySlice, 1 },
                                 States[Index],
                                 Compiled.mStateAfter,
                                 States[Index] == Compiled.mStateAfter &&
                                     IsUAVState(Compiled.mStateAfter),
                                 Compiled.mbForceBarrier &&
                                     States[Index] == Compiled.mStateAfter});
                            States[Index] = Compiled.mStateAfter;
                        }
                    }
                }

                for (const FARDGBufferTransition& Compiled :
                     Pass.GetState().mBufferTransitions)
                {
                    const FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Compiled.mBuffer);
                    const void* Physical = Buffer.GetBuffer()->GetPhysicalIdentity();
                    if (Physical == nullptr)
                    {
                        ARDA_CHECK_MSG(
                            "A live graph buffer was not materialized.");
                    }
                    auto Existing = BufferStates.find(Physical);
                    if (Existing == BufferStates.end())
                    {
                        Existing = BufferStates.emplace(
                            Physical,
                            NormalizeInitialState(Buffer.GetInitialState()))
                                       .first;
                    }
                    Out.mBuffers.push_back(
                        {Compiled.mBuffer,
                         Existing->second,
                         Compiled.mStateAfter,
                         Existing->second == Compiled.mStateAfter &&
                             IsUAVState(Compiled.mStateAfter),
                         Compiled.mbForceBarrier &&
                             Existing->second == Compiled.mStateAfter});
                    Existing->second = Compiled.mStateAfter;
                }
                for (const FARDGAccelStructTransition& Compiled :
                     Pass.GetState().mAccelStructTransitions)
                {
                    Out.mAccelStructs.push_back(Compiled);
                }
            }

            if (Graph.mContext.mDebugOptions.mbClobberFirstWrites)
            {
                // External resources are treated as already produced. Created
                // resources become produced at their first declared write,
                // independently for each texture mip/slice.
                eastl::vector<eastl::vector<bool>> ProducedTextures;
                ProducedTextures.reserve(Graph.mTextures.GetCount());
                for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
                {
                    const rhi::FArdaRHITextureDesc& Desc = Texture->GetDesc();
                    ProducedTextures.emplace_back(
                        static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                        Texture->IsExternal());
                }
                eastl::vector<bool> ProducedBuffers(
                    Graph.mBuffers.GetCount(),
                    false);
                for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
                {
                    ProducedBuffers[Buffer->GetHandle().GetIndex()] =
                        Buffer->IsExternal();
                }

                for (FARDGPassHandle Handle :
                     Graph.mCompileResult.mExecutionOrder)
                {
                    const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                    auto& Out = Runtime[Handle.GetIndex()];
                    if (Pass.GetState().mbSentinel)
                    {
                        continue;
                    }
                    const bool bCanIssueClobber =
                        Pass.GetState().mPipeline != EARDGPipeline::Copy;
                    for (const FARDGPassTextureState& Access :
                         Pass.GetState().mTextureStates)
                    {
                        if (!Access.mbWrite)
                        {
                            continue;
                        }
                        const FARDGTexture& Texture =
                            Graph.mTextures.Get(Access.mTexture);
                        const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                        const auto Range =
                            Access.mSubresources.Resolve(Desc);
                        bool bAllUnproduced = !Texture.IsExternal();
                        for (uint32_t Slice = Range.mBaseArraySlice;
                             Slice <
                                Range.mBaseArraySlice + Range.mArraySliceCount;
                             ++Slice)
                        {
                            for (uint32_t Mip = Range.mBaseMipLevel;
                                 Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                                 ++Mip)
                            {
                                bAllUnproduced &=
                                    !ProducedTextures
                                         [Access.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mMipLevels +
                                          Mip];
                            }
                        }
                        const rhi::FArdaRHIFormatInfo& Format =
                            rhi::GetArdaRHIFormatInfo(Desc.mFormat);
                        const bool bColorClear =
                            !Format.mbDepth && !Format.mbStencil &&
                            (rhi::HasAnyFlags(Desc.mUsage, rhi::EArdaRHITextureUsage::RenderTarget) ||
                             rhi::HasAnyFlags(Desc.mUsage, rhi::EArdaRHITextureUsage::UnorderedAccess));
                        const bool bDepthClear =
                            (Format.mbDepth || Format.mbStencil) &&
                            rhi::HasAnyFlags(Desc.mUsage, rhi::EArdaRHITextureUsage::DepthStencil) &&
                            Pass.GetState().mPipeline ==
                                EARDGPipeline::Graphics;
                        if (bCanIssueClobber &&
                            bAllUnproduced &&
                            (bColorClear || bDepthClear))
                        {
                            Out.mTextureClobbers.push_back(Access);
                        }
                        for (uint32_t Slice = Range.mBaseArraySlice;
                             Slice <
                                Range.mBaseArraySlice + Range.mArraySliceCount;
                             ++Slice)
                        {
                            for (uint32_t Mip = Range.mBaseMipLevel;
                                 Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                                 ++Mip)
                            {
                                ProducedTextures
                                    [Access.mTexture.GetIndex()]
                                    [static_cast<size_t>(Slice) *
                                         Desc.mMipLevels +
                                     Mip] = true;
                            }
                        }
                    }
                    for (const FARDGPassBufferState& Access :
                         Pass.GetState().mBufferStates)
                    {
                        if (!Access.mbWrite)
                        {
                            continue;
                        }
                        const FARDGBuffer& Buffer =
                            Graph.mBuffers.Get(Access.mBuffer);
                        const uint32_t Index = Access.mBuffer.GetIndex();
                        const bool bCanClobber =
                            bCanIssueClobber &&
                            !ProducedBuffers[Index] &&
                            !Buffer.IsExternal() &&
                            rhi::HasAnyFlags(Buffer.GetDesc().mUsage, rhi::EArdaRHIBufferUsage::UnorderedAccess) &&
                            Access.mRange.IsWholeBuffer(Buffer.GetDesc()) &&
                            (Access.mState &
                             rhi::EArdaRHIResourceState::UnorderedAccess) !=
                                rhi::EArdaRHIResourceState::Unknown;
                        if (bCanClobber)
                        {
                            Out.mBufferClobbers.push_back(Access);
                        }
                        ProducedBuffers[Index] = true;
                    }
                }
            }
            return Runtime;
        }

        /**
         * Records one compiled pass and its runtime transitions.
         *
         * This command-recording stage creates a list for the selected queue,
         * disables automatic barriers, establishes rebuilt physical start
         * states, emits forced/UAV ordering and optional first-write clobbers,
         * then invokes non-sentinel pass work inside a marker. No submission
         * occurs here, which allows independent passes to run on CPU workers.
         */
        [[nodiscard]] FARDGRecordedPass RecordPass(
            FARDGBuilder& Builder,
            FARDGBuilder::FImpl& Graph,
            FARDGPassHandle Handle,
            const FARDGRuntimePassTransitions& Transitions)
        {
            FARDGPass& Pass = Graph.mPasses.Get(Handle);
            FARDGRecordedPass Recorded;
            Recorded.mQueue = GetCommandQueue(Pass.GetState().mPipeline);
            auto CommandListResult =
                Graph.mContext.mDevice->CreateCommandList(
                    Recorded.mQueue,
                    Graph.mContext.mDebugOptions.mbImmediateMode);
            if (!CommandListResult)
            {
                ARDA_CHECK_MSG(
                    "The RHI failed to create a render-graph command list.");
            }

            Recorded.mCommandList = eastl::move(CommandListResult.mValue);
            Recorded.mCommandList->Open();
            Recorded.mCommandList->SetAutomaticBarriers(false);
                // Runtime records, rather than compiled logical before-states,
                // are authoritative after physical pooling has been resolved.
                for (const FARDGTextureTransition& Transition :
                     Transitions.mTextures)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(Transition.mTexture);
                    Recorded.mCommandList->BeginTrackingTextureState(
                        *Texture.GetTexture(),
                        Transition.mSubresources,
                        Transition.mStateBefore);
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->SetTextureState(
                            *Texture.GetTexture(),
                            Transition.mSubresources,
                            rhi::EArdaRHIResourceState::Common);
                        Recorded.mCommandList->CommitBarriers();
                    }
                    if (IsUAVState(Transition.mStateAfter))
                    {
                        Recorded.mCommandList->SetUAVBarriersForTexture(
                            *Texture.GetTexture(),
                            true);
                    }
                    Recorded.mCommandList->SetTextureState(
                        *Texture.GetTexture(),
                        Transition.mSubresources,
                        Transition.mStateAfter);
                }
                for (const FARDGBufferTransition& Transition :
                     Transitions.mBuffers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Transition.mBuffer);
                    Recorded.mCommandList->BeginTrackingBufferState(
                        *Buffer.GetBuffer(),
                        Transition.mStateBefore);
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->SetBufferState(
                            *Buffer.GetBuffer(),
                            rhi::EArdaRHIResourceState::Common);
                        Recorded.mCommandList->CommitBarriers();
                    }
                    if (IsUAVState(Transition.mStateAfter))
                    {
                        Recorded.mCommandList->SetUAVBarriersForBuffer(
                            *Buffer.GetBuffer(),
                            true);
                    }
                    Recorded.mCommandList->SetBufferState(
                        *Buffer.GetBuffer(),
                        Transition.mStateAfter);
                }
                for (const FARDGAccelStructTransition& Transition :
                     Transitions.mAccelStructs)
                {
                    FARDGAccelStruct& AccelStruct =
                        Graph.mAccelStructs.Get(Transition.mAccelStruct);
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->SetAccelStructState(
                            *AccelStruct.GetAccelStruct(),
                            rhi::EArdaRHIResourceState::Common);
                        Recorded.mCommandList->CommitBarriers();
                    }
                    Recorded.mCommandList->SetAccelStructState(
                        *AccelStruct.GetAccelStruct(),
                        Transition.mStateAfter);
                }
                Recorded.mCommandList->CommitBarriers();

                for (const FARDGPassTextureState& Clobber :
                     Transitions.mTextureClobbers)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(Clobber.mTexture);
                    const rhi::FArdaRHIFormatInfo& Format =
                        rhi::GetArdaRHIFormatInfo(Texture.GetDesc().mFormat);
                    if (Format.mbDepth || Format.mbStencil)
                    {
                        Recorded.mCommandList->ClearDepthStencilTexture(
                            *Texture.GetTexture(),
                            Clobber.mSubresources,
                            Format.mbDepth,
                            0.12345f,
                            Format.mbStencil,
                            0xCDu);
                    }
                    else if (Format.mbInteger)
                    {
                        Recorded.mCommandList->ClearTextureUInt(
                            *Texture.GetTexture(),
                            Clobber.mSubresources,
                            0xCDCDCDCDu);
                    }
                    else
                    {
                        Recorded.mCommandList->ClearTexture(
                            *Texture.GetTexture(),
                            Clobber.mSubresources,
                            rhi::FArdaRHIColor{1.0f, 0.0f, 1.0f, 1.0f});
                    }
                    ++Recorded.mClobberedResourceCount;
                }
                for (const FARDGPassBufferState& Clobber :
                     Transitions.mBufferClobbers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Clobber.mBuffer);
                    Recorded.mCommandList->ClearBufferUInt(
                        *Buffer.GetBuffer(),
                        0xCDCDCDCDu);
                    ++Recorded.mClobberedResourceCount;
                }

                if (!Pass.GetState().mbSentinel)
                {
                    Recorded.mCommandList->BeginMarker(Pass.GetName().c_str());
                    FARDGPassExecutionContext Context(
                        Builder,
                        Handle,
                        *Recorded.mCommandList,
                        Pass.GetState().mPipeline);
                    Pass.Execute(Context);
                    Recorded.mCommandList->EndMarker();
                }
                Recorded.mCommandList->Close();
            return Recorded;
        }

        /**
         * Uploads frozen uniform-buffer bytes on one graphics command list.
         *
         * The returned submission instance is zero when there is no upload.
         * Non-graphics queues wait on a non-zero instance before their first
         * pass submission; graphics work is naturally ordered on its queue.
         */
        [[nodiscard]] uint64_t UploadUniformBuffers(
            FARDGBuilder::FImpl& Graph)
        {
            if (Graph.mUniformBuffers.GetCount() == 0)
            {
                return 0;
            }

            auto CommandListResult = Graph.mContext.mDevice->CreateCommandList(
                rhi::EArdaRHIQueueType::Graphics);
            if (!CommandListResult)
            {
                ARDA_CHECK_MSG(
                    "The RHI failed to create a graph upload command list.");
            }
            rhi::FArdaRHICommandListRef CommandList = eastl::move(CommandListResult.mValue);
            CommandList->Open();
            for (const FARDGUniformBuffer* UniformBuffer :
                 Graph.mUniformBuffers.GetEntries())
            {
                CommandList->BeginTrackingBufferState(
                    *UniformBuffer->GetBuffer(),
                    NormalizeInitialState(
                        UniformBuffer->GetDesc().mInitialState));
                CommandList->WriteBuffer(
                    *UniformBuffer->GetBuffer(),
                    UniformBuffer->GetContents(),
                    UniformBuffer->GetDesc().mByteSize);
                CommandList->SetBufferState(
                    *UniformBuffer->GetBuffer(),
                    rhi::EArdaRHIResourceState::ConstantBuffer);
            }
            CommandList->CommitBarriers();
            CommandList->Close();
            auto Result = Graph.mContext.mDevice->ExecuteCommandList(CommandList);
            return Result ? Result.mValue : 0;
        }

        /**
         * Publishes materialized handles requested during graph construction.
         *
         * Compilation has already kept these resources live through the
         * epilogue and arranged final states. Assignment occurs after CPU
         * submission and does not imply GPU completion.
         */
        void CompleteExtractions(FARDGBuilder::FImpl& Graph)
        {
            for (const FARDGTextureExtraction& Extraction :
                 Graph.mTextureExtractions)
            {
                *Extraction.mOutput = Extraction.mTexture->GetTexture();
            }
            for (const FARDGBufferExtraction& Extraction :
                 Graph.mBufferExtractions)
            {
                *Extraction.mOutput = Extraction.mBuffer->GetBuffer();
            }
        }
    }

    /**
     * Runs the graph's one-shot execution pipeline through CPU submission.
     *
     * The graph is compiled if necessary, then resources are materialized,
     * physical transitions are rebuilt, command lists are recorded by
     * dependency wave, uniform data is uploaded, and pass lists are submitted
     * in deterministic execution order with required queue waits. Extractions
     * are published only after submission. A scope guard permanently marks
     * the builder failed if any stage exits unsuccessfully.
     */
    const FARDGExecutionResult& FARDGExecutor::Execute(
        FARDGBuilder& Builder,
        const FARDGExecuteOptions& Options)
    {
        ARDA_NAMED_SCOPE_TIMER("ARDG Execute");
        FARDGBuilder::FImpl& Graph = *Builder.mImpl;
        if (Graph.mbExecutionStarted)
        {
            ARDA_CHECK_MSG("A render graph can only execute once.");
        }
        if (Graph.mbFailed)
        {
            ARDA_CHECK_MSG("A failed render graph cannot be executed.");
        }
        if (!Graph.mContext.mDevice)
        {
            ARDA_CHECK_MSG(
                "Render-graph execution requires an RHI device.");
        }
        (void)Builder.Compile();
        Graph.mbExecutionStarted = true;
        FARDGExecutionFailureGuard FailureGuard{Graph};

        Graph.mExecutionResult = {};
        Graph.mExecutionResult.mbUsedImmediateMode =
            Graph.mContext.mDebugOptions.mbImmediateMode;
        // Physical handles must exist before transition rebuilding and before
        // worker threads invoke validated pass resource getters.
        MaterializeResources(
            Graph,
            *Graph.mContext.mDevice,
            Graph.mExecutionResult);
        const eastl::vector<FARDGRuntimePassTransitions> RuntimeTransitions =
            BuildPhysicalTransitions(Graph);

        eastl::vector<FARDGRecordedPass> Recorded(Graph.mPasses.GetCount());
        eastl::vector<uint32_t> Levels(Graph.mPasses.GetCount(), 0);
        uint32_t MaxLevel = 0;
        for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
        {
            const FARDGPass& Pass = Graph.mPasses.Get(Handle);
            if (Handle == Graph.mCompileResult.mPrologue)
            {
                continue;
            }
            uint32_t Level = 0;
            // Fold both data producers and explicit synchronization producers
            // into the earliest safe CPU recording wave for this pass.
            auto AccumulateLevel =
                [&Graph, &Levels, &Level](FARDGPassHandle Producer)
                {
                    const FARDGPass* ProducerPass =
                        Graph.mPasses.TryGet(Producer);
                    if (ProducerPass != nullptr &&
                        !ProducerPass->GetState().mbCulled)
                    {
                        Level = eastl::max(
                            Level,
                            Levels[Producer.GetIndex()] + 1u);
                    }
                };
            for (FARDGPassHandle Producer : Pass.GetState().mProducers)
            {
                AccumulateLevel(Producer);
            }
            for (FARDGPassHandle Producer :
                 Pass.GetState().mSynchronizationProducers)
            {
                AccumulateLevel(Producer);
            }
            Levels[Handle.GetIndex()] = Level;
            MaxLevel = eastl::max(MaxLevel, Level);
        }

        const uint32_t HardwareThreads =
            eastl::max(1u, std::thread::hardware_concurrency());
        const uint32_t MaxWorkers = Options.mMaxRecordingThreads == 0
            ? HardwareThreads
            : eastl::max(1u, Options.mMaxRecordingThreads);
        for (uint32_t Level = 0; Level <= MaxLevel; ++Level)
        {
            eastl::vector<FARDGPassHandle> ParallelPasses;
            eastl::vector<FARDGPassHandle> SerialPasses;
            for (FARDGPassHandle Handle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                if (Handle == Graph.mCompileResult.mPrologue ||
                    Levels[Handle.GetIndex()] != Level)
                {
                    continue;
                }
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                const bool bHasWork =
                    !Pass.GetState().mbSentinel ||
                    !RuntimeTransitions[Handle.GetIndex()].mTextures.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()].mBuffers.empty();
                if (!bHasWork)
                {
                    continue;
                }
                if (!Graph.mContext.mDebugOptions.mbImmediateMode &&
                    Options.mbParallelRecording &&
                    !HasAllFlags(
                        Pass.GetFlags(),
                        EARDGPassFlags::NeverParallel))
                {
                    ParallelPasses.push_back(Handle);
                }
                else
                {
                    SerialPasses.push_back(Handle);
                }
            }

            for (size_t Begin = 0;
                 Begin < ParallelPasses.size();
                 Begin += MaxWorkers)
            {
                const size_t End = eastl::min(
                    ParallelPasses.size(),
                    Begin + MaxWorkers);
                eastl::vector<std::future<void>> Futures;
                Futures.reserve(End - Begin);
                Graph.mExecutionResult.mbUsedParallelRecording |=
                    End - Begin > 1;
                for (size_t Index = Begin; Index < End; ++Index)
                {
                    const FARDGPassHandle Handle = ParallelPasses[Index];
                    Futures.push_back(std::async(
                        std::launch::async,
                        // Each job owns a distinct command list and writes a
                        // distinct pass-indexed result slot; shared graph data
                        // is read-only during recording except guarded access
                        // validation managed by execution contexts.
                        [&Builder,
                         &Graph,
                         &RuntimeTransitions,
                         &Recorded,
                         Handle]
                        {
                            Recorded[Handle.GetIndex()] = RecordPass(
                                Builder,
                                Graph,
                                Handle,
                                RuntimeTransitions[Handle.GetIndex()]);
                        }));
                }
                for (auto& Future : Futures)
                {
                    Future.get();
                }
            }
            for (FARDGPassHandle Handle : SerialPasses)
            {
                Recorded[Handle.GetIndex()] = RecordPass(
                    Builder,
                    Graph,
                    Handle,
                    RuntimeTransitions[Handle.GetIndex()]);
            }
        }

        const uint64_t UploadInstance = UploadUniformBuffers(Graph);
        eastl::array<bool, 3> bUploadWaited{};
        eastl::vector<uint64_t> PassInstances(Graph.mPasses.GetCount(), 0);
        // Submission order remains deterministic even when recording completed
        // out of order. Per-pass instances become synchronization tokens for
        // later cross-queue consumers.
        for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
        {
            FARDGRecordedPass& Pass = Recorded[Handle.GetIndex()];
            if (!Pass.mCommandList)
            {
                continue;
            }

            const size_t ConsumerQueueIndex = GetQueueIndex(Pass.mQueue);
            if (UploadInstance != 0 &&
                Pass.mQueue != rhi::EArdaRHIQueueType::Graphics &&
                !bUploadWaited[ConsumerQueueIndex])
            {
                Graph.mContext.mDevice->QueueWait(
                    Pass.mQueue,
                    rhi::EArdaRHIQueueType::Graphics,
                    UploadInstance);
                bUploadWaited[ConsumerQueueIndex] = true;
                ++Graph.mExecutionResult.mQueueWaitCount;
            }

            for (const FARDGQueueDependency& Dependency :
                 Graph.mCompileResult.mQueueDependencies)
            {
                if (Dependency.mConsumer != Handle)
                {
                    continue;
                }
                const uint64_t ProducerInstance =
                    PassInstances[Dependency.mProducer.GetIndex()];
                if (ProducerInstance == 0)
                {
                    continue;
                }
                Graph.mContext.mDevice->QueueWait(
                    GetCommandQueue(Dependency.mConsumerPipeline),
                    GetCommandQueue(Dependency.mProducerPipeline),
                    ProducerInstance);
                ++Graph.mExecutionResult.mQueueWaitCount;
            }

            const auto SubmitResult =
                Graph.mContext.mDevice->ExecuteCommandList(Pass.mCommandList);
            const uint64_t Instance = SubmitResult ? SubmitResult.mValue : 0;
            PassInstances[Handle.GetIndex()] = Instance;
            Graph.mExecutionResult.mLastSubmittedInstances[
                ConsumerQueueIndex] = Instance;
            ++Graph.mExecutionResult.mSubmittedCommandListCount;
            Graph.mExecutionResult.mClobberedResourceCount +=
                Pass.mClobberedResourceCount;
        }

        CompleteExtractions(Graph);
        Graph.mContext.mDevice->RunGarbageCollection();
        Graph.mbExecuted = true;
        FailureGuard.mbCompleted = true;
        ARDA_TRACE_COUNTER(
            "ARDG Submitted Command Lists",
            Graph.mExecutionResult.mSubmittedCommandListCount);
        ARDA_TRACE_COUNTER(
            "ARDG Queue Waits",
            Graph.mExecutionResult.mQueueWaitCount);
        ARDA_TRACE_COUNTER(
            "ARDG Clobbered Resources",
            Graph.mExecutionResult.mClobberedResourceCount);
        return Graph.mExecutionResult;
    }
}
