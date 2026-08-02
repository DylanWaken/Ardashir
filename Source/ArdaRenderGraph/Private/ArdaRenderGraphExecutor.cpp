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
            /** First-write texture accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassTextureState> mTextureClobbers;
            /** First-write buffer accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassBufferState> mBufferClobbers;
        };

        struct FARDGRecordedPass
        {
            /** Command list owned by NVRHI and populated during the recording stage. */
            nvrhi::CommandListHandle mCommandList;
            /** Submission queue selected from the pass pipeline; graphics is the default. */
            nvrhi::CommandQueue mQueue = nvrhi::CommandQueue::Graphics;
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

        /** Maps the compiler-selected graph pipeline to its NVRHI submit queue. */
        [[nodiscard]] nvrhi::CommandQueue GetCommandQueue(
            EARDGPipeline Pipeline) noexcept
        {
            switch (Pipeline)
            {
            case EARDGPipeline::Graphics:
                return nvrhi::CommandQueue::Graphics;
            case EARDGPipeline::AsyncCompute:
                return nvrhi::CommandQueue::Compute;
            case EARDGPipeline::Copy:
                return nvrhi::CommandQueue::Copy;
            }
            return nvrhi::CommandQueue::Graphics;
        }

        /** Converts an NVRHI queue enum to the result-array bookkeeping index. */
        [[nodiscard]] size_t GetQueueIndex(
            nvrhi::CommandQueue Queue) noexcept
        {
            return static_cast<size_t>(Queue);
        }

        /** Identifies states that require explicit unordered-access ordering. */
        [[nodiscard]] bool IsUAVState(
            nvrhi::ResourceStates State) noexcept
        {
            return (State & nvrhi::ResourceStates::UnorderedAccess) !=
                nvrhi::ResourceStates::Unknown;
        }

        /**
         * Replaces an unspecified resource start state with the executable
         * Common state used during materialization and transition tracking.
         */
        [[nodiscard]] nvrhi::ResourceStates NormalizeInitialState(
            nvrhi::ResourceStates State) noexcept
        {
            return State == nvrhi::ResourceStates::Unknown
                ? nvrhi::ResourceStates::Common
                : State;
        }

        /**
         * Performs the definitive compatibility check for texture-pool reuse.
         *
         * This execution-materialization check includes every descriptor field
         * relevant to the pool policy; hash equality alone is never trusted.
         */
        [[nodiscard]] bool TextureDescriptorsEqual(
            const nvrhi::TextureDesc& Left,
            const nvrhi::TextureDesc& Right) noexcept
        {
            return Left.width == Right.width &&
                Left.height == Right.height &&
                Left.depth == Right.depth &&
                Left.arraySize == Right.arraySize &&
                Left.mipLevels == Right.mipLevels &&
                Left.sampleCount == Right.sampleCount &&
                Left.sampleQuality == Right.sampleQuality &&
                Left.format == Right.format &&
                Left.dimension == Right.dimension &&
                Left.isShaderResource == Right.isShaderResource &&
                Left.isRenderTarget == Right.isRenderTarget &&
                Left.isUAV == Right.isUAV &&
                Left.isTypeless == Right.isTypeless &&
                Left.isShadingRateSurface == Right.isShadingRateSurface &&
                Left.sharedResourceFlags == Right.sharedResourceFlags &&
                Left.isTiled == Right.isTiled &&
                Left.useClearValue == Right.useClearValue &&
                (!Left.useClearValue ||
                 std::memcmp(
                     &Left.clearValue,
                     &Right.clearValue,
                     sizeof(Left.clearValue)) == 0);
        }

        /** Performs the definitive descriptor check for committed-buffer reuse. */
        [[nodiscard]] bool BufferDescriptorsEqual(
            const nvrhi::BufferDesc& Left,
            const nvrhi::BufferDesc& Right) noexcept
        {
            return Left.byteSize == Right.byteSize &&
                Left.structStride == Right.structStride &&
                Left.maxVersions == Right.maxVersions &&
                Left.format == Right.format &&
                Left.canHaveUAVs == Right.canHaveUAVs &&
                Left.canHaveTypedViews == Right.canHaveTypedViews &&
                Left.canHaveRawViews == Right.canHaveRawViews &&
                Left.isVertexBuffer == Right.isVertexBuffer &&
                Left.isIndexBuffer == Right.isIndexBuffer &&
                Left.isConstantBuffer == Right.isConstantBuffer &&
                Left.isDrawIndirectArgs == Right.isDrawIndirectArgs &&
                Left.isAccelStructBuildInput ==
                    Right.isAccelStructBuildInput &&
                Left.isAccelStructStorage == Right.isAccelStructStorage &&
                Left.isShaderBindingTable == Right.isShaderBindingTable &&
                Left.isVolatile == Right.isVolatile &&
                Left.cpuAccess == Right.cpuAccess &&
                Left.sharedResourceFlags == Right.sharedResourceFlags;
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
            const nvrhi::TextureDesc& Desc)
        {
            size_t Hash = 0;
            HashCombine(Hash, Desc.width);
            HashCombine(Hash, Desc.height);
            HashCombine(Hash, Desc.depth);
            HashCombine(Hash, Desc.arraySize);
            HashCombine(Hash, Desc.mipLevels);
            HashCombine(Hash, Desc.sampleCount);
            HashCombine(Hash, Desc.sampleQuality);
            HashCombine(Hash, static_cast<uint8_t>(Desc.format));
            HashCombine(Hash, static_cast<uint8_t>(Desc.dimension));
            HashCombine(Hash, Desc.isRenderTarget);
            HashCombine(Hash, Desc.isUAV);
            HashCombine(Hash, Desc.isTypeless);
            return Hash;
        }

        /**
         * Builds a fast buffer-pool bucket key from high-selectivity fields.
         *
         * Omitted fields are checked by BufferDescriptorsEqual before reuse.
         */
        [[nodiscard]] size_t HashBufferDescriptor(
            const nvrhi::BufferDesc& Desc)
        {
            size_t Hash = 0;
            HashCombine(Hash, Desc.byteSize);
            HashCombine(Hash, Desc.structStride);
            HashCombine(Hash, static_cast<uint8_t>(Desc.format));
            HashCombine(Hash, Desc.canHaveUAVs);
            HashCombine(Hash, Desc.canHaveTypedViews);
            HashCombine(Hash, Desc.canHaveRawViews);
            HashCombine(Hash, Desc.isConstantBuffer);
            HashCombine(Hash, static_cast<uint8_t>(Desc.cpuAccess));
            return Hash;
        }

        class FARDGTexturePool final
        {
        public:
            /** Creates an execution-local texture pool and result counter sink. */
            FARDGTexturePool(
                nvrhi::IDevice& Device,
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
             * otherwise a new NVRHI object is created.
             */
            [[nodiscard]] nvrhi::TextureHandle Acquire(
                nvrhi::TextureDesc Desc,
                uint32_t FirstUse,
                uint32_t LastUse,
                int32_t ReuseDomain)
            {
                Desc.initialState = NormalizeInitialState(Desc.initialState);
                Desc.keepInitialState = false;
                Desc.isVirtual = false;
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

                nvrhi::TextureHandle Texture = mDevice.createTexture(Desc);
                if (!Texture)
                {
                    ARDA_CHECK_MSG(
                        "NVRHI failed to create a render-graph texture.");
                }
                Bucket.push_back(
                    {eastl::move(Desc), Texture, LastUse, ReuseDomain});
                return Texture;
            }

        private:
            struct FEntry
            {
                /** Normalized descriptor used for definitive compatibility checks. */
                nvrhi::TextureDesc mDesc;
                /** Pool-owned reference to the reusable physical texture. */
                nvrhi::TextureHandle mTexture;
                /** Inclusive execution-order index of the latest logical user's last use. */
                uint32_t mAvailableAfter = 0;
                /** Queue reuse domain index, or -1 when this entry cannot be recycled. */
                int32_t mReuseDomain = -1;
            };

            /** Non-owning device used to create textures during materialization. */
            nvrhi::IDevice& mDevice;
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
                nvrhi::IDevice& Device,
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
            [[nodiscard]] nvrhi::BufferHandle Acquire(
                nvrhi::BufferDesc Desc,
                uint32_t FirstUse,
                uint32_t LastUse,
                int32_t ReuseDomain)
            {
                Desc.initialState = NormalizeInitialState(Desc.initialState);
                Desc.keepInitialState = false;
                Desc.isVirtual = false;
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

                nvrhi::BufferHandle Buffer = mDevice.createBuffer(Desc);
                if (!Buffer)
                {
                    ARDA_CHECK_MSG(
                        "NVRHI failed to create a render-graph buffer.");
                }
                Bucket.push_back(
                    {eastl::move(Desc), Buffer, LastUse, ReuseDomain});
                return Buffer;
            }

        private:
            struct FEntry
            {
                /** Normalized descriptor used for definitive compatibility checks. */
                nvrhi::BufferDesc mDesc;
                /** Pool-owned reference to the reusable physical buffer. */
                nvrhi::BufferHandle mBuffer;
                /** Inclusive execution-order index of the latest logical user's last use. */
                uint32_t mAvailableAfter = 0;
                /** Queue reuse domain index, or -1 when this entry cannot be recycled. */
                int32_t mReuseDomain = -1;
            };

            /** Non-owning device used to create buffers during materialization. */
            nvrhi::IDevice& mDevice;
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
         * the portable NVRHI surface does not expose all required aliasing
         * safety operations. Every transient candidate is therefore reported
         * as using committed-resource fallback.
         *
         * TODO(ArdaRenderGraph): Enable physical placed-resource aliasing here
         * after NVRHI provides portable aliasing barriers and heap-compatibility
         * queries; then consume the computed layout instead of discarding it.
         */
        void EvaluateTransientHeapLayout(
            FARDGBuilder::FImpl& Graph,
            nvrhi::IDevice& Device,
            FARDGExecutionResult& Result)
        {
            eastl::vector<FARDGTransientAllocationRequest> Requests;
            if (!Device.queryFeatureSupport(nvrhi::Feature::VirtualResources))
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

                nvrhi::MemoryRequirements Requirements;
                if (Lifetime.mType == EARDGResourceType::Texture)
                {
                    nvrhi::TextureDesc Desc =
                        Graph.mTextures
                            .Get(FARDGTextureHandle(Lifetime.mResourceIndex))
                            .GetDesc();
                    Desc.initialState =
                        NormalizeInitialState(Desc.initialState);
                    Desc.keepInitialState = false;
                    Desc.isVirtual = true;
                    nvrhi::TextureHandle Probe = Device.createTexture(Desc);
                    if (Probe)
                    {
                        Requirements =
                            Device.getTextureMemoryRequirements(Probe);
                    }
                }
                else
                {
                    nvrhi::BufferDesc Desc =
                        Graph.mBuffers
                            .Get(FARDGBufferHandle(Lifetime.mResourceIndex))
                            .GetDesc();
                    Desc.initialState =
                        NormalizeInitialState(Desc.initialState);
                    Desc.keepInitialState = false;
                    Desc.isVirtual = true;
                    nvrhi::BufferHandle Probe = Device.createBuffer(Desc);
                    if (Probe)
                    {
                        Requirements =
                            Device.getBufferMemoryRequirements(Probe);
                    }
                }

                if (Requirements.size != 0 && Requirements.alignment != 0)
                {
                    Requests.push_back(
                        {Identifier++,
                         Lifetime.mFirstUse,
                         Lifetime.mLastUse,
                         Requirements.size,
                         Requirements.alignment});
                }
                Result.mbUsedTransientFallback = true;
            }

            if (!Requests.empty())
            {
                const FARDGTransientHeapLayout IdealLayout =
                    FARDGTransientHeapAllocator::Allocate(Requests, true);
                (void)IdealLayout;
                // NVRHI exposes placed resources but no portable aliasing
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
            nvrhi::IDevice& Device,
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
                else
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
            }

            for (FARDGUniformBuffer* UniformBuffer :
                 Graph.mUniformBuffers.GetEntries())
            {
                nvrhi::BufferHandle Buffer =
                    Device.createBuffer(UniformBuffer->GetDesc());
                if (!Buffer)
                {
                    ARDA_CHECK_MSG(
                        "NVRHI failed to create a graph uniform buffer.");
                }
                UniformBuffer->BindBuffer(eastl::move(Buffer));
            }
        }

        /**
         * Rebuilds compiled logical transitions against physical identities.
         *
         * Pooling may bind disjoint logical resources to one NVRHI object, so
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
                nvrhi::ITexture*,
                eastl::vector<nvrhi::ResourceStates>>
                TextureStates;
            eastl::unordered_map<nvrhi::IBuffer*, nvrhi::ResourceStates>
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
                    nvrhi::ITexture* Physical = Texture.GetTexture();
                    if (Physical == nullptr)
                    {
                        ARDA_CHECK_MSG(
                            "A live graph texture was not materialized.");
                    }
                    auto& States = TextureStates[Physical];
                    const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                    if (States.empty())
                    {
                        // A physical texture enters history once; later logical
                        // aliases continue from the state left in this vector.
                        States.resize(
                            static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
                            NormalizeInitialState(Texture.GetInitialState()));
                    }
                    const nvrhi::TextureSubresourceSet Subresources =
                        Compiled.mSubresources.resolve(Desc, false);
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
                            Out.mTextures.push_back(
                                {Compiled.mTexture,
                                 nvrhi::TextureSubresourceSet(
                                     MipLevel,
                                     1,
                                     ArraySlice,
                                     1),
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
                    nvrhi::IBuffer* Physical = Buffer.GetBuffer();
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
                    const nvrhi::TextureDesc& Desc = Texture->GetDesc();
                    ProducedTextures.emplace_back(
                        static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
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
                        const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                        const auto Range =
                            Access.mSubresources.resolve(Desc, false);
                        bool bAllUnproduced = !Texture.IsExternal();
                        for (uint32_t Slice = Range.baseArraySlice;
                             Slice <
                                 Range.baseArraySlice + Range.numArraySlices;
                             ++Slice)
                        {
                            for (uint32_t Mip = Range.baseMipLevel;
                                 Mip < Range.baseMipLevel + Range.numMipLevels;
                                 ++Mip)
                            {
                                bAllUnproduced &=
                                    !ProducedTextures
                                         [Access.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mipLevels +
                                          Mip];
                            }
                        }
                        const nvrhi::FormatInfo& Format =
                            nvrhi::getFormatInfo(Desc.format);
                        const bool bColorClear =
                            !Format.hasDepth && !Format.hasStencil &&
                            (Desc.isRenderTarget || Desc.isUAV);
                        const bool bDepthClear =
                            (Format.hasDepth || Format.hasStencil) &&
                            Desc.isRenderTarget &&
                            Pass.GetState().mPipeline ==
                                EARDGPipeline::Graphics;
                        if (bCanIssueClobber &&
                            bAllUnproduced &&
                            (bColorClear || bDepthClear))
                        {
                            Out.mTextureClobbers.push_back(Access);
                        }
                        for (uint32_t Slice = Range.baseArraySlice;
                             Slice <
                                 Range.baseArraySlice + Range.numArraySlices;
                             ++Slice)
                        {
                            for (uint32_t Mip = Range.baseMipLevel;
                                 Mip < Range.baseMipLevel + Range.numMipLevels;
                                 ++Mip)
                            {
                                ProducedTextures
                                    [Access.mTexture.GetIndex()]
                                    [static_cast<size_t>(Slice) *
                                         Desc.mipLevels +
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
                            Buffer.GetDesc().canHaveUAVs &&
                            Access.mRange.resolve(Buffer.GetDesc())
                                .isEntireBuffer(Buffer.GetDesc()) &&
                            (Access.mState &
                             nvrhi::ResourceStates::UnorderedAccess) !=
                                nvrhi::ResourceStates::Unknown;
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
            nvrhi::CommandListParameters Parameters;
            Parameters.setEnableImmediateExecution(
                          Graph.mContext.mDebugOptions.mbImmediateMode)
                .setQueueType(Recorded.mQueue);
            Recorded.mCommandList =
                Graph.mContext.mDevice->createCommandList(Parameters);
            if (!Recorded.mCommandList)
            {
                ARDA_CHECK_MSG(
                    "NVRHI failed to create a render-graph command list.");
            }

            Recorded.mCommandList->open();
            Recorded.mCommandList->setEnableAutomaticBarriers(false);
                // Runtime records, rather than compiled logical before-states,
                // are authoritative after physical pooling has been resolved.
                for (const FARDGTextureTransition& Transition :
                     Transitions.mTextures)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(Transition.mTexture);
                    Recorded.mCommandList->beginTrackingTextureState(
                        Texture.GetTexture(),
                        Transition.mSubresources,
                        Transition.mStateBefore);
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->setTextureState(
                            Texture.GetTexture(),
                            Transition.mSubresources,
                            nvrhi::ResourceStates::Common);
                        Recorded.mCommandList->commitBarriers();
                    }
                    if (IsUAVState(Transition.mStateAfter))
                    {
                        Recorded.mCommandList->setEnableUavBarriersForTexture(
                            Texture.GetTexture(),
                            true);
                    }
                    Recorded.mCommandList->setTextureState(
                        Texture.GetTexture(),
                        Transition.mSubresources,
                        Transition.mStateAfter);
                }
                for (const FARDGBufferTransition& Transition :
                     Transitions.mBuffers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Transition.mBuffer);
                    Recorded.mCommandList->beginTrackingBufferState(
                        Buffer.GetBuffer(),
                        Transition.mStateBefore);
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->setBufferState(
                            Buffer.GetBuffer(),
                            nvrhi::ResourceStates::Common);
                        Recorded.mCommandList->commitBarriers();
                    }
                    if (IsUAVState(Transition.mStateAfter))
                    {
                        Recorded.mCommandList->setEnableUavBarriersForBuffer(
                            Buffer.GetBuffer(),
                            true);
                    }
                    Recorded.mCommandList->setBufferState(
                        Buffer.GetBuffer(),
                        Transition.mStateAfter);
                }
                Recorded.mCommandList->commitBarriers();

                for (const FARDGPassTextureState& Clobber :
                     Transitions.mTextureClobbers)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(Clobber.mTexture);
                    const nvrhi::FormatInfo& Format =
                        nvrhi::getFormatInfo(Texture.GetDesc().format);
                    if (Format.hasDepth || Format.hasStencil)
                    {
                        Recorded.mCommandList->clearDepthStencilTexture(
                            Texture.GetTexture(),
                            Clobber.mSubresources,
                            Format.hasDepth,
                            0.12345f,
                            Format.hasStencil,
                            0xCDu);
                    }
                    else if (Format.kind == nvrhi::FormatKind::Integer)
                    {
                        Recorded.mCommandList->clearTextureUInt(
                            Texture.GetTexture(),
                            Clobber.mSubresources,
                            0xCDCDCDCDu);
                    }
                    else
                    {
                        Recorded.mCommandList->clearTextureFloat(
                            Texture.GetTexture(),
                            Clobber.mSubresources,
                            nvrhi::Color(1.0f, 0.0f, 1.0f, 1.0f));
                    }
                    ++Recorded.mClobberedResourceCount;
                }
                for (const FARDGPassBufferState& Clobber :
                     Transitions.mBufferClobbers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Clobber.mBuffer);
                    Recorded.mCommandList->clearBufferUInt(
                        Buffer.GetBuffer(),
                        0xCDCDCDCDu);
                    ++Recorded.mClobberedResourceCount;
                }

                if (!Pass.GetState().mbSentinel)
                {
                    Recorded.mCommandList->beginMarker(Pass.GetName().c_str());
                    FARDGPassExecutionContext Context(
                        Builder,
                        Handle,
                        *Recorded.mCommandList,
                        Pass.GetState().mPipeline);
                    Pass.Execute(Context);
                    Recorded.mCommandList->endMarker();
                }
                Recorded.mCommandList->close();
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

            nvrhi::CommandListParameters Parameters;
            Parameters.setEnableImmediateExecution(
                          Graph.mContext.mDebugOptions.mbImmediateMode)
                .setQueueType(nvrhi::CommandQueue::Graphics);
            nvrhi::CommandListHandle CommandList =
                Graph.mContext.mDevice->createCommandList(Parameters);
            if (!CommandList)
            {
                ARDA_CHECK_MSG(
                    "NVRHI failed to create a graph upload command list.");
            }
            CommandList->open();
            for (const FARDGUniformBuffer* UniformBuffer :
                 Graph.mUniformBuffers.GetEntries())
            {
                CommandList->beginTrackingBufferState(
                    UniformBuffer->GetBuffer(),
                    NormalizeInitialState(
                        UniformBuffer->GetDesc().initialState));
                CommandList->writeBuffer(
                    UniformBuffer->GetBuffer(),
                    UniformBuffer->GetContents(),
                    UniformBuffer->GetDesc().byteSize);
                CommandList->setBufferState(
                    UniformBuffer->GetBuffer(),
                    nvrhi::ResourceStates::ConstantBuffer);
            }
            CommandList->commitBarriers();
            CommandList->close();
            return Graph.mContext.mDevice->executeCommandList(
                CommandList,
                nvrhi::CommandQueue::Graphics);
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
                "Render-graph execution requires an NVRHI device.");
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
                Pass.mQueue != nvrhi::CommandQueue::Graphics &&
                !bUploadWaited[ConsumerQueueIndex])
            {
                Graph.mContext.mDevice->queueWaitForCommandList(
                    Pass.mQueue,
                    nvrhi::CommandQueue::Graphics,
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
                Graph.mContext.mDevice->queueWaitForCommandList(
                    GetCommandQueue(Dependency.mConsumerPipeline),
                    GetCommandQueue(Dependency.mProducerPipeline),
                    ProducerInstance);
                ++Graph.mExecutionResult.mQueueWaitCount;
            }

            const uint64_t Instance =
                Graph.mContext.mDevice->executeCommandList(
                    Pass.mCommandList,
                    Pass.mQueue);
            PassInstances[Handle.GetIndex()] = Instance;
            Graph.mExecutionResult.mLastSubmittedInstances[
                ConsumerQueueIndex] = Instance;
            ++Graph.mExecutionResult.mSubmittedCommandListCount;
            Graph.mExecutionResult.mClobberedResourceCount +=
                Pass.mClobberedResourceCount;
        }

        CompleteExtractions(Graph);
        Graph.mContext.mDevice->runGarbageCollection();
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
