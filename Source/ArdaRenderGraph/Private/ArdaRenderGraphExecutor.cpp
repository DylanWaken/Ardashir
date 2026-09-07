#include "ArdaRenderGraphPch.h"
#include "ArdaRenderGraphState.h"

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
            struct FAliasingResource
            {
                EARDGResourceType mType = EARDGResourceType::Texture;
                uint32_t mResourceIndex = 0;
            };
            struct FTextureQueueTransfer
            {
                FARDGTextureHandle mTexture;
                rhi::FArdaRHITextureSubresourceRange mSubresources;
                rhi::EArdaRHIQueueType mSourceQueue =
                    rhi::EArdaRHIQueueType::Graphics;
                rhi::EArdaRHIQueueType mDestinationQueue =
                    rhi::EArdaRHIQueueType::Graphics;
            };
            struct FBufferQueueTransfer
            {
                FARDGBufferHandle mBuffer;
                rhi::EArdaRHIQueueType mSourceQueue =
                    rhi::EArdaRHIQueueType::Graphics;
                rhi::EArdaRHIQueueType mDestinationQueue =
                    rhi::EArdaRHIQueueType::Graphics;
            };
            /** Physical texture transitions emitted while recording this pass. */
            eastl::vector<FARDGTextureTransition> mTextures;
            /** Physical buffer transitions emitted while recording this pass. */
            eastl::vector<FARDGBufferTransition> mBuffers;
            eastl::vector<FARDGAccelStructTransition> mAccelStructs;
            /** Queue-ownership acquire barriers emitted before pass transitions. */
            eastl::vector<FTextureQueueTransfer> mTextureAcquires;
            eastl::vector<FBufferQueueTransfer> mBufferAcquires;
            /** Common-state releases emitted after pass work on the producer queue. */
            eastl::vector<FTextureQueueTransfer> mTextureReleases;
            eastl::vector<FBufferQueueTransfer> mBufferReleases;
            /** First-write texture accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassTextureState> mTextureClobbers;
            /** Initial attachment clears required when placed texture memory becomes active. */
            eastl::vector<FARDGPassTextureState> mTextureInitializations;
            /** First-write buffer accesses selected for debug clearing in this pass. */
            eastl::vector<FARDGPassBufferState> mBufferClobbers;
            /** Placed resources whose overlapping heap range becomes active in this pass. */
            eastl::vector<FAliasingResource> mAliasingResources;
        };

        struct FARDGAliasingTransition
        {
            uint32_t mExecutionIndex = 0;
            EARDGResourceType mType = EARDGResourceType::Texture;
            uint32_t mResourceIndex = 0;
            eastl::vector<FARDGPassHandle> mProducers;
        };

        struct FARDGRecordedPass
        {
            /** First failure from command recording or the pass callback. */
            rhi::FArdaRHIStatus mStatus;
            /** RHI command list populated during the recording stage. */
            rhi::FArdaRHICommandListRef mCommandList;
            /** Submission queue selected from the pass pipeline; graphics is the default. */
            rhi::EArdaRHIQueueType mQueue = rhi::EArdaRHIQueueType::Graphics;
            /** Number of debug resource clears encoded into this pass's command list. */
            uint32_t mClobberedResourceCount = 0;
            /** State evidence captured while recording this pass. */
            eastl::vector<FARDGStateConformanceRecord>
                mStateConformanceRecords;
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

        /** Maps a submit queue to the matching explicit-transition pipeline. */
        [[nodiscard]] rhi::EArdaRHIPipeline GetTransitionPipeline(
            rhi::EArdaRHIQueueType Queue) noexcept
        {
            switch (Queue)
            {
            case rhi::EArdaRHIQueueType::Compute:
                return rhi::EArdaRHIPipeline::AsyncCompute;
            case rhi::EArdaRHIQueueType::Copy:
                return rhi::EArdaRHIPipeline::Copy;
            case rhi::EArdaRHIQueueType::Graphics:
            default:
                return rhi::EArdaRHIPipeline::Graphics;
            }
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

        /** Places compatible transient resources and orders every overlapping reuse. */
        void MaterializeTransientHeaps(
            FARDGBuilder::FImpl& Graph,
            rhi::IArdaRHIDevice& Device,
            FARDGExecutionResult& Result,
            eastl::vector<FARDGAliasingTransition>& OutAliases)
        {
            struct FARDGPlacedResource
            {
                FARDGResourceLifetime mLifetime;
                rhi::FArdaRHIMemoryRequirements mRequirements;
                rhi::FArdaRHITextureRef mTexture;
                rhi::FArdaRHIBufferRef mBuffer;
                eastl::array<FARDGPassHandle, rhi::ArdaRHIQueueTypeCount> mLastUses;
            };
            struct FARDGHeapGroup
            {
                EARDGResourceType mType;
                uint32_t mMemoryTypeBits;
                eastl::vector<FARDGPlacedResource> mResources;
            };

            eastl::vector<FARDGHeapGroup> Groups;
            const auto& Capabilities = Device.GetCapabilities();
            for (const FARDGResourceLifetime& Lifetime :
                 Graph.mCompileResult.mResourceLifetimes)
            {
                if (!Lifetime.mbTransient)
                    continue;
                FARDGPlacedResource Resource;
                Resource.mLifetime = Lifetime;
                if (Capabilities.mbVirtualResources && Capabilities.mbHeaps &&
                    Capabilities.mbAliasingBarriers)
                {
                    if (Lifetime.mType == EARDGResourceType::Texture)
                    {
                        auto Desc = Graph.mTextures.Get(
                            FARDGTextureHandle(Lifetime.mResourceIndex)).GetDesc();
                        Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                        Desc.mbKeepInitialState = false;
                        Desc.mbVirtual = true;
                        auto Created = Device.CreateTexture(Desc);
                        if (Created)
                        {
                            auto Memory = Device.GetTextureMemoryRequirements(Created.mValue);
                            if (Memory)
                            {
                                Resource.mTexture = eastl::move(Created.mValue);
                                Resource.mRequirements = Memory.mValue;
                            }
                        }
                    }
                    else if (Lifetime.mType == EARDGResourceType::Buffer)
                    {
                        auto Desc = Graph.mBuffers.Get(
                            FARDGBufferHandle(Lifetime.mResourceIndex)).GetDesc();
                        if (Desc.mCpuAccess == rhi::EArdaRHICpuAccess::None)
                        {
                            Desc.mInitialState = NormalizeInitialState(Desc.mInitialState);
                            Desc.mbKeepInitialState = false;
                            Desc.mbVirtual = true;
                            auto Created = Device.CreateBuffer(Desc);
                            if (Created)
                            {
                                auto Memory = Device.GetBufferMemoryRequirements(Created.mValue);
                                if (Memory)
                                {
                                    Resource.mBuffer = eastl::move(Created.mValue);
                                    Resource.mRequirements = Memory.mValue;
                                }
                            }
                        }
                    }
                }
                if ((!Resource.mTexture && !Resource.mBuffer) ||
                    !Resource.mRequirements.mSize ||
                    !Resource.mRequirements.mAlignment ||
                    !Resource.mRequirements.mMemoryTypeBits)
                {
                    Result.mbUsedTransientFallback = true;
                    continue;
                }

                for (uint32_t Index = Lifetime.mFirstUse; Index <= Lifetime.mLastUse; ++Index)
                {
                    const auto Handle = Graph.mCompileResult.mExecutionOrder[Index];
                    const auto& State = Graph.mPasses.Get(Handle).GetState();
                    const bool bUsesResource = Resource.mTexture
                        ? eastl::any_of(State.mTextureStates.begin(), State.mTextureStates.end(),
                            [&](const auto& Access)
                            { return Access.mTexture.GetIndex() == Lifetime.mResourceIndex; })
                        : eastl::any_of(State.mBufferStates.begin(), State.mBufferStates.end(),
                            [&](const auto& Access)
                            { return Access.mBuffer.GetIndex() == Lifetime.mResourceIndex; });
                    if (bUsesResource)
                        Resource.mLastUses[rhi::GetArdaRHIQueueIndex(
                            GetCommandQueue(State.mPipeline))] = Handle;
                }

                // Separate buffers from optimal images, so Vulkan's buffer/image
                // granularity cannot turn adjacent live placements into aliases.
                auto Group = eastl::find_if(Groups.begin(), Groups.end(),
                    [&](const auto& Entry)
                    {
                        return Entry.mType == Lifetime.mType &&
                            (Entry.mMemoryTypeBits & Resource.mRequirements.mMemoryTypeBits);
                    });
                if (Group == Groups.end())
                {
                    Groups.push_back({Lifetime.mType, Resource.mRequirements.mMemoryTypeBits, {}});
                    Group = Groups.end() - 1;
                }
                Group->mMemoryTypeBits &= Resource.mRequirements.mMemoryTypeBits;
                Group->mResources.push_back(eastl::move(Resource));
            }

            for (auto& Group : Groups)
            {
                eastl::vector<FARDGTransientAllocationRequest> Requests;
                for (uint32_t Index = 0; Index < Group.mResources.size(); ++Index)
                {
                    const auto& Resource = Group.mResources[Index];
                    Requests.push_back({Index, Resource.mLifetime.mFirstUse,
                        Resource.mLifetime.mLastUse, Resource.mRequirements.mSize,
                        Resource.mRequirements.mAlignment});
                }
                const auto Layout = FARDGTransientHeapAllocator::Allocate(Requests, true);
                rhi::FArdaRHIHeapDesc HeapDesc;
                HeapDesc.mCapacity = Layout.mCapacity;
                HeapDesc.mType = rhi::EArdaRHIHeapType::DeviceLocal;
                HeapDesc.mMemoryTypeBits = Group.mMemoryTypeBits;
                HeapDesc.mDebugName = "ARDG transient alias heap";
                auto Heap = Device.CreateHeap(HeapDesc);
                bool bBound = static_cast<bool>(Heap);
                for (const auto& Allocation : Layout.mAllocations)
                {
                    if (!bBound)
                        break;
                    auto& Resource = Group.mResources[Allocation.mIdentifier];
                    bBound = static_cast<bool>(Resource.mTexture
                        ? Device.BindTextureMemory(Resource.mTexture, Heap.mValue, Allocation.mOffset)
                        : Device.BindBufferMemory(Resource.mBuffer, Heap.mValue, Allocation.mOffset));
                }
                if (!bBound)
                {
                    // Publish only fully bound groups; one unsupported resource
                    // never disables placement for the remaining compatible groups.
                    Result.mbUsedTransientFallback = true;
                    continue;
                }

                for (const auto& Allocation : Layout.mAllocations)
                {
                    auto& Resource = Group.mResources[Allocation.mIdentifier];
                    if (Resource.mTexture)
                        Graph.mTextures.Get(FARDGTextureHandle(Resource.mLifetime.mResourceIndex))
                            .BindTexture(Resource.mTexture);
                    else
                        Graph.mBuffers.Get(FARDGBufferHandle(Resource.mLifetime.mResourceIndex))
                            .BindBuffer(Resource.mBuffer);
                    if (!Allocation.mbReusedMemory)
                        continue;

                    FARDGAliasingTransition Alias{Resource.mLifetime.mFirstUse,
                        Resource.mLifetime.mType, Resource.mLifetime.mResourceIndex, {}};
                    for (const auto& Previous : Layout.mAllocations)
                    {
                        const auto& Prior = Group.mResources[Previous.mIdentifier];
                        if (Prior.mLifetime.mLastUse >= Resource.mLifetime.mFirstUse ||
                            Previous.mOffset >= Allocation.mOffset + Allocation.mSize ||
                            Allocation.mOffset >= Previous.mOffset + Previous.mSize)
                            continue;
                        for (auto Use : Prior.mLastUses)
                        {
                            if (Use.IsValid() && eastl::find(Alias.mProducers.begin(),
                                    Alias.mProducers.end(), Use) == Alias.mProducers.end())
                                Alias.mProducers.push_back(Use);
                        }
                    }
                    OutAliases.push_back(eastl::move(Alias));
                    ++Result.mAliasingBarrierCount;
                }
                Result.mbUsedVirtualHeaps = true;
                Result.mbUsedTransientAliasing |= Layout.mbContainsAliases;
                Result.mTransientHeapBytes += Layout.mCapacity;
                Result.mTransientAliasedBytes +=
                    FARDGTransientHeapAllocator::Allocate(Requests, false).mCapacity - Layout.mCapacity;
            }
        }

        void CaptureTextureState(
            FARDGRecordedPass& Recorded,
            const FARDGPass& Pass,
            FARDGPassHandle PassHandle,
            FARDGTexture& Texture,
            FARDGTextureHandle TextureHandle,
            const rhi::FArdaRHITextureSubresourceRange& Subresources,
            EARDGStateCheckpoint Checkpoint,
            rhi::EArdaRHIResourceState ExpectedState,
            bool bValidateQueueOwnership = false,
            rhi::EArdaRHIQueueType ExpectedQueueOwner =
                rhi::EArdaRHIQueueType::Graphics,
            uint32_t ExpectedQueueFamily =
                rhi::ArdaRHIInvalidQueueFamily)
        {
            FARDGStateConformanceRecord Record;
            Record.mPass = PassHandle;
            Record.mPassName = Pass.GetName();
            Record.mResourceType = EARDGResourceType::Texture;
            Record.mResourceIndex = TextureHandle.GetIndex();
            Record.mResourceName = Texture.GetName();
            Record.mTextureSubresources = Subresources;
            Record.mCheckpoint = Checkpoint;
            Record.mExpectedState = ExpectedState;
            Record.mbValidateQueueOwnership = bValidateQueueOwnership;
            Record.mExpectedQueueOwner = ExpectedQueueOwner;
            Record.mExpectedQueueFamily = ExpectedQueueFamily;
            auto Snapshot = Recorded.mCommandList->QueryTextureState(
                *Texture.GetTexture(), Subresources);
            Record.mStatus = Snapshot.mStatus;
            if (Snapshot)
                Record.mObserved = eastl::move(Snapshot.mValue);
            Recorded.mStateConformanceRecords.push_back(eastl::move(Record));
        }

        void CaptureBufferState(
            FARDGRecordedPass& Recorded,
            const FARDGPass& Pass,
            FARDGPassHandle PassHandle,
            FARDGBuffer& Buffer,
            FARDGBufferHandle BufferHandle,
            EARDGStateCheckpoint Checkpoint,
            rhi::EArdaRHIResourceState ExpectedState,
            bool bValidateQueueOwnership = false,
            rhi::EArdaRHIQueueType ExpectedQueueOwner =
                rhi::EArdaRHIQueueType::Graphics,
            uint32_t ExpectedQueueFamily =
                rhi::ArdaRHIInvalidQueueFamily)
        {
            FARDGStateConformanceRecord Record;
            Record.mPass = PassHandle;
            Record.mPassName = Pass.GetName();
            Record.mResourceType = EARDGResourceType::Buffer;
            Record.mResourceIndex = BufferHandle.GetIndex();
            Record.mResourceName = Buffer.GetName();
            Record.mCheckpoint = Checkpoint;
            Record.mExpectedState = ExpectedState;
            Record.mbValidateQueueOwnership = bValidateQueueOwnership;
            Record.mExpectedQueueOwner = ExpectedQueueOwner;
            Record.mExpectedQueueFamily = ExpectedQueueFamily;
            auto Snapshot = Recorded.mCommandList->QueryBufferState(
                *Buffer.GetBuffer());
            Record.mStatus = Snapshot.mStatus;
            if (Snapshot)
                Record.mObserved = eastl::move(Snapshot.mValue);
            Recorded.mStateConformanceRecords.push_back(eastl::move(Record));
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
                    rhi::GetArdaRHIQueueIndex(
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
                    rhi::GetArdaRHIQueueIndex(
                        GetCommandQueue(Pass.GetState().mPipeline)));
                if (Domain >= 0 && Domain != PassDomain)
                {
                    return -1;
                }
                Domain = PassDomain;
            }
            return Domain;
        }

        /** Adds one cross-queue edge to the execution plan without changing compilation. */
        void AddExecutionDependency(FARDGBuilder::FImpl& Graph,
            FARDGPassHandle Producer, FARDGPassHandle Consumer)
        {
            const auto Source = Graph.mPasses.Get(Producer).GetState().mPipeline;
            const auto Destination = Graph.mPasses.Get(Consumer).GetState().mPipeline;
            auto& Edges = Graph.mExecutionResult.mQueueDependencies;
            if (Source != Destination && eastl::none_of(Edges.begin(), Edges.end(),
                [Producer, Consumer](const auto& Edge)
                { return Edge.mProducer == Producer && Edge.mConsumer == Consumer; }))
                Edges.push_back({Producer, Consumer, Source, Destination});
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
            FARDGExecutionResult& Result,
            eastl::vector<FARDGAliasingTransition>& OutAliases)
        {
            MaterializeTransientHeaps(
                Graph, Device, Result, OutAliases);

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
                    if (Texture.GetTexture())
                        continue;
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
                    if (Buffer.GetBuffer())
                        continue;
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
            struct FTextureQueueHistory
            {
                eastl::vector<FARDGPassHandle> mPasses;
                eastl::vector<rhi::EArdaRHIQueueType> mQueues;
                eastl::vector<FARDGTextureHandle> mTextures;
            };
            struct FBufferQueueHistory
            {
                FARDGPassHandle mPass;
                rhi::EArdaRHIQueueType mQueue =
                    rhi::EArdaRHIQueueType::Graphics;
                FARDGBufferHandle mBuffer;
            };
            eastl::unordered_map<
                const void*,
                eastl::vector<rhi::EArdaRHIResourceState>>
                TextureStates;
            eastl::unordered_map<const void*, FTextureQueueHistory>
                TextureQueues;
            eastl::unordered_map<const void*, rhi::EArdaRHIResourceState>
                BufferStates;
            eastl::unordered_map<const void*, FBufferQueueHistory>
                BufferQueues;
            eastl::vector<FARDGRuntimePassTransitions> Runtime(
                Graph.mPasses.GetCount());
            for (FARDGPassHandle Handle :
                 Graph.mCompileResult.mExecutionOrder)
            {
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                auto& Out = Runtime[Handle.GetIndex()];
                const rhi::EArdaRHIQueueType Queue =
                    GetCommandQueue(Pass.GetState().mPipeline);
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
                        auto& History = TextureQueues[Physical];
                        History.mPasses.assign(
                            States.size(), Graph.mCompileResult.mPrologue);
                        History.mQueues.assign(
                            States.size(), Texture.IsExternal()
                                ? rhi::EArdaRHIQueueType::Graphics : Queue);
                        History.mTextures.assign(
                            States.size(), Compiled.mTexture);
                    }
                    auto& History = TextureQueues[Physical];
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
                            const rhi::FArdaRHITextureSubresourceRange Cell{
                                MipLevel, 1, ArraySlice, 1 };
                            if (!Texture.IsExternal() && Texture.GetTexture()->GetDesc().mbVirtual &&
                                History.mPasses[Index] == Graph.mCompileResult.mPrologue &&
                                (Compiled.mStateAfter == rhi::EArdaRHIResourceState::RenderTarget ||
                                 Compiled.mStateAfter == rhi::EArdaRHIResourceState::DepthWrite))
                            {
                                Out.mTextureInitializations.push_back(
                                    {Compiled.mTexture, Cell, Compiled.mStateAfter, true});
                            }
                            if (History.mQueues[Index] != Queue)
                            {
                                AddExecutionDependency(Graph,
                                    History.mPasses[Index], Handle);
                                const auto Transfer =
                                    FARDGRuntimePassTransitions::FTextureQueueTransfer{
                                        History.mTextures[Index],
                                        Cell,
                                        History.mQueues[Index],
                                        Queue};
                                Runtime[History.mPasses[Index].GetIndex()]
                                    .mTextureReleases.push_back(Transfer);
                                Out.mTextureAcquires.push_back(
                                    {Compiled.mTexture,
                                     Cell,
                                     History.mQueues[Index],
                                     Queue});
                                States[Index] =
                                    rhi::EArdaRHIResourceState::Common;
                            }
                            Out.mTextures.push_back(
                                {Compiled.mTexture,
                                 Cell,
                                 States[Index],
                                 Compiled.mStateAfter,
                                 States[Index] == Compiled.mStateAfter &&
                                     IsUAVState(Compiled.mStateAfter),
                                 Compiled.mbForceBarrier &&
                                     States[Index] == Compiled.mStateAfter});
                            States[Index] = Compiled.mStateAfter;
                            History.mPasses[Index] = Handle;
                            History.mQueues[Index] = Queue;
                            History.mTextures[Index] = Compiled.mTexture;
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
                        BufferQueues.emplace(
                            Physical,
                            FBufferQueueHistory{
                                Graph.mCompileResult.mPrologue,
                                Buffer.IsExternal() ? rhi::EArdaRHIQueueType::Graphics : Queue,
                                Compiled.mBuffer});
                    }
                    auto& History = BufferQueues.at(Physical);
                    if (History.mQueue != Queue)
                    {
                        AddExecutionDependency(Graph, History.mPass, Handle);
                        Runtime[History.mPass.GetIndex()]
                            .mBufferReleases.push_back(
                                {History.mBuffer, History.mQueue, Queue});
                        Out.mBufferAcquires.push_back(
                            {Compiled.mBuffer, History.mQueue, Queue});
                        Existing->second =
                            rhi::EArdaRHIResourceState::Common;
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
                    History = {Handle, Queue, Compiled.mBuffer};
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
            const FARDGRuntimePassTransitions& Transitions,
            bool bValidateResourceStates,
            FARDGRecordedPass Recorded = {},
            bool bClose = true)
        {
            FARDGPass& Pass = Graph.mPasses.Get(Handle);
            if (!Recorded.mCommandList)
            {
                Recorded.mQueue = GetCommandQueue(Pass.GetState().mPipeline);
                auto CommandListResult =
                    Graph.mContext.mDevice->CreateCommandList(
                        Recorded.mQueue,
                        Graph.mContext.mDebugOptions.mbImmediateMode);
                if (!CommandListResult)
                {
                    Recorded.mStatus = CommandListResult.mStatus;
                    return Recorded;
                }

                Recorded.mCommandList = eastl::move(CommandListResult.mValue);
                Recorded.mStatus = Recorded.mCommandList->Open();
                if (!Recorded.mStatus)
                    return Recorded;
                Recorded.mCommandList->SetAutomaticBarriers(false);
                for (const auto& Alias : Transitions.mAliasingResources)
                {
                    rhi::IArdaRHIResource* ResourceAfter = nullptr;
                    if (Alias.mType == EARDGResourceType::Texture)
                    {
                        ResourceAfter = Graph.mTextures
                            .Get(FARDGTextureHandle(Alias.mResourceIndex))
                            .GetTexture()
                            .Get();
                    }
                    else if (Alias.mType == EARDGResourceType::Buffer)
                    {
                        ResourceAfter = Graph.mBuffers
                            .Get(FARDGBufferHandle(Alias.mResourceIndex))
                            .GetBuffer()
                            .Get();
                    }
                    if (!ResourceAfter ||
                        !Recorded.mCommandList->AliasingBarrier(
                            nullptr, ResourceAfter))
                    {
                        ARDA_CHECK_MSG(
                            "The RHI failed to encode an RDG aliasing barrier.");
                    }
                }
                if (!Transitions.mAliasingResources.empty())
                    Recorded.mCommandList->CommitBarriers();
                // Queue handoffs use Common as the portable ownership state.
                // The producer records a release after its work; the matching
                // acquire executes here before the consumer's normal state
                // transition. This is required by D3D12 copy queues and maps
                // directly to Vulkan queue-family ownership barriers.
                for (const auto& Transfer : Transitions.mTextureAcquires)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(Transfer.mTexture);
                    Recorded.mCommandList->BeginTrackingTextureState(
                        *Texture.GetTexture(),
                        Transfer.mSubresources,
                        rhi::EArdaRHIResourceState::Common);
                    rhi::FArdaRHITextureTransitionDesc Acquire;
                    Acquire.mSubresources = Transfer.mSubresources;
                    Acquire.mStateBefore =
                        rhi::EArdaRHIResourceState::Common;
                    Acquire.mStateAfter =
                        rhi::EArdaRHIResourceState::Common;
                    Acquire.mSourcePipelines =
                        GetTransitionPipeline(Transfer.mSourceQueue);
                    Acquire.mDestinationPipelines =
                        GetTransitionPipeline(Transfer.mDestinationQueue);
                    Acquire.mFlags =
                        rhi::EArdaRHITransitionFlags::EndOnly;
                    Acquire.mSourceQueue = Transfer.mSourceQueue;
                    Acquire.mDestinationQueue = Transfer.mDestinationQueue;
                    Acquire.mbQueueOwnershipTransfer = true;
                    if (!Recorded.mCommandList->TransitionTexture(
                            *Texture.GetTexture(), Acquire))
                    {
                        ARDA_CHECK_MSG(
                            "The RHI failed to encode an RDG texture queue acquire.");
                    }
                    if (bValidateResourceStates)
                    {
                        CaptureTextureState(
                            Recorded,
                            Pass,
                            Handle,
                            Texture,
                            Transfer.mTexture,
                            Transfer.mSubresources,
                            EARDGStateCheckpoint::QueueAcquire,
                            rhi::EArdaRHIResourceState::Common,
                            true,
                            Transfer.mDestinationQueue,
                            Graph.mContext.mDevice->GetCapabilities()
                                .mQueues.GetFamily(
                                    Transfer.mDestinationQueue));
                    }
                }
                for (const auto& Transfer : Transitions.mBufferAcquires)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Transfer.mBuffer);
                    Recorded.mCommandList->BeginTrackingBufferState(
                        *Buffer.GetBuffer(),
                        rhi::EArdaRHIResourceState::Common);
                    rhi::FArdaRHIBufferTransitionDesc Acquire;
                    Acquire.mStateBefore =
                        rhi::EArdaRHIResourceState::Common;
                    Acquire.mStateAfter =
                        rhi::EArdaRHIResourceState::Common;
                    Acquire.mSourcePipelines =
                        GetTransitionPipeline(Transfer.mSourceQueue);
                    Acquire.mDestinationPipelines =
                        GetTransitionPipeline(Transfer.mDestinationQueue);
                    Acquire.mFlags =
                        rhi::EArdaRHITransitionFlags::EndOnly;
                    Acquire.mSourceQueue = Transfer.mSourceQueue;
                    Acquire.mDestinationQueue = Transfer.mDestinationQueue;
                    Acquire.mbQueueOwnershipTransfer = true;
                    if (!Recorded.mCommandList->TransitionBuffer(
                            *Buffer.GetBuffer(), Acquire))
                    {
                        ARDA_CHECK_MSG(
                            "The RHI failed to encode an RDG buffer queue acquire.");
                    }
                    if (bValidateResourceStates)
                    {
                        CaptureBufferState(
                            Recorded,
                            Pass,
                            Handle,
                            Buffer,
                            Transfer.mBuffer,
                            EARDGStateCheckpoint::QueueAcquire,
                            rhi::EArdaRHIResourceState::Common,
                            true,
                            Transfer.mDestinationQueue,
                            Graph.mContext.mDevice->GetCapabilities()
                                .mQueues.GetFamily(
                                    Transfer.mDestinationQueue));
                    }
                }
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
                    if (bValidateResourceStates)
                    {
                        CaptureTextureState(
                            Recorded,
                            Pass,
                            Handle,
                            Texture,
                            Transition.mTexture,
                            Transition.mSubresources,
                            EARDGStateCheckpoint::BeforeTransition,
                            Transition.mStateBefore);
                    }
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->SetTextureState(
                            *Texture.GetTexture(),
                            Transition.mSubresources,
                            rhi::EArdaRHIResourceState::Common);
                        Recorded.mCommandList->CommitBarriers();
                        if (bValidateResourceStates)
                        {
                            CaptureTextureState(
                                Recorded,
                                Pass,
                                Handle,
                                Texture,
                                Transition.mTexture,
                                Transition.mSubresources,
                                EARDGStateCheckpoint::ForcedCommon,
                                rhi::EArdaRHIResourceState::Common);
                        }
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
                    if (bValidateResourceStates)
                    {
                        CaptureTextureState(
                            Recorded,
                            Pass,
                            Handle,
                            Texture,
                            Transition.mTexture,
                            Transition.mSubresources,
                            EARDGStateCheckpoint::AfterTransition,
                            Transition.mStateAfter);
                    }
                }
                for (const FARDGBufferTransition& Transition :
                     Transitions.mBuffers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(Transition.mBuffer);
                    Recorded.mCommandList->BeginTrackingBufferState(
                        *Buffer.GetBuffer(),
                        Transition.mStateBefore);
                    if (bValidateResourceStates)
                    {
                        CaptureBufferState(
                            Recorded,
                            Pass,
                            Handle,
                            Buffer,
                            Transition.mBuffer,
                            EARDGStateCheckpoint::BeforeTransition,
                            Transition.mStateBefore);
                    }
                    if (Transition.mbForceBarrier)
                    {
                        Recorded.mCommandList->SetBufferState(
                            *Buffer.GetBuffer(),
                            rhi::EArdaRHIResourceState::Common);
                        Recorded.mCommandList->CommitBarriers();
                        if (bValidateResourceStates)
                        {
                            CaptureBufferState(
                                Recorded,
                                Pass,
                                Handle,
                                Buffer,
                                Transition.mBuffer,
                                EARDGStateCheckpoint::ForcedCommon,
                                rhi::EArdaRHIResourceState::Common);
                        }
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
                    if (bValidateResourceStates)
                    {
                        CaptureBufferState(
                            Recorded,
                            Pass,
                            Handle,
                            Buffer,
                            Transition.mBuffer,
                            EARDGStateCheckpoint::AfterTransition,
                            Transition.mStateAfter);
                    }
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

                for (const auto& Initialization : Transitions.mTextureInitializations)
                {
                    auto& Texture = Graph.mTextures.Get(Initialization.mTexture);
                    const auto& Desc = Texture.GetDesc();
                    const auto& Format = rhi::GetArdaRHIFormatInfo(Desc.mFormat);
                    const auto Status = Format.mbDepth || Format.mbStencil
                        ? Recorded.mCommandList->ClearDepthStencilTexture(*Texture.GetTexture(),
                            Initialization.mSubresources, Format.mbDepth,
                            Desc.mbUseClearValue ? Desc.mClearValue.mR : 1.0f,
                            Format.mbStencil, Desc.mbUseClearValue
                                ? static_cast<uint8_t>(Desc.mClearValue.mG) : 0)
                        : Recorded.mCommandList->ClearTexture(*Texture.GetTexture(),
                            Initialization.mSubresources, Desc.mbUseClearValue
                                ? Desc.mClearValue : rhi::FArdaRHIColor{0, 0, 0, 0});
                    if (!Status)
                    {
                        Recorded.mStatus = Status;
                        return Recorded;
                    }
                }

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
                if (Recorded.mStatus)
                    Recorded.mStatus = Context.GetStatus();
                Recorded.mCommandList->EndMarker();
            }
            if (bValidateResourceStates)
            {
                for (const FARDGTextureTransition& State :
                     Transitions.mTextures)
                {
                    FARDGTexture& Texture =
                        Graph.mTextures.Get(State.mTexture);
                    CaptureTextureState(
                        Recorded,
                        Pass,
                        Handle,
                        Texture,
                        State.mTexture,
                        State.mSubresources,
                        EARDGStateCheckpoint::AfterPass,
                        State.mStateAfter);
                }
                for (const FARDGBufferTransition& State :
                     Transitions.mBuffers)
                {
                    FARDGBuffer& Buffer =
                        Graph.mBuffers.Get(State.mBuffer);
                    CaptureBufferState(
                        Recorded,
                        Pass,
                        Handle,
                        Buffer,
                        State.mBuffer,
                        EARDGStateCheckpoint::AfterPass,
                        State.mStateAfter);
                }
            }
            for (const auto& Transfer : Transitions.mTextureReleases)
            {
                FARDGTexture& Texture =
                    Graph.mTextures.Get(Transfer.mTexture);
                if (!Recorded.mCommandList->SetTextureState(
                        *Texture.GetTexture(),
                        Transfer.mSubresources,
                        rhi::EArdaRHIResourceState::Common))
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to transition an RDG texture to its queue-release state.");
                }
                Recorded.mCommandList->CommitBarriers();
                rhi::FArdaRHITextureTransitionDesc Release;
                Release.mSubresources = Transfer.mSubresources;
                Release.mStateBefore =
                    rhi::EArdaRHIResourceState::Common;
                Release.mStateAfter =
                    rhi::EArdaRHIResourceState::Common;
                Release.mSourcePipelines =
                    GetTransitionPipeline(Transfer.mSourceQueue);
                Release.mDestinationPipelines =
                    GetTransitionPipeline(Transfer.mDestinationQueue);
                Release.mFlags =
                    rhi::EArdaRHITransitionFlags::BeginOnly;
                Release.mSourceQueue = Transfer.mSourceQueue;
                Release.mDestinationQueue = Transfer.mDestinationQueue;
                Release.mbQueueOwnershipTransfer = true;
                if (!Recorded.mCommandList->TransitionTexture(
                        *Texture.GetTexture(), Release))
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to encode an RDG texture queue release.");
                }
                if (bValidateResourceStates)
                {
                    CaptureTextureState(
                        Recorded,
                        Pass,
                        Handle,
                        Texture,
                        Transfer.mTexture,
                        Transfer.mSubresources,
                        EARDGStateCheckpoint::QueueRelease,
                        rhi::EArdaRHIResourceState::Common,
                        true,
                        Transfer.mDestinationQueue,
                        Graph.mContext.mDevice->GetCapabilities()
                            .mQueues.GetFamily(
                                Transfer.mDestinationQueue));
                }
            }
            for (const auto& Transfer : Transitions.mBufferReleases)
            {
                FARDGBuffer& Buffer =
                    Graph.mBuffers.Get(Transfer.mBuffer);
                if (!Recorded.mCommandList->SetBufferState(
                        *Buffer.GetBuffer(),
                        rhi::EArdaRHIResourceState::Common))
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to transition an RDG buffer to its queue-release state.");
                }
                Recorded.mCommandList->CommitBarriers();
                rhi::FArdaRHIBufferTransitionDesc Release;
                Release.mStateBefore =
                    rhi::EArdaRHIResourceState::Common;
                Release.mStateAfter =
                    rhi::EArdaRHIResourceState::Common;
                Release.mSourcePipelines =
                    GetTransitionPipeline(Transfer.mSourceQueue);
                Release.mDestinationPipelines =
                    GetTransitionPipeline(Transfer.mDestinationQueue);
                Release.mFlags =
                    rhi::EArdaRHITransitionFlags::BeginOnly;
                Release.mSourceQueue = Transfer.mSourceQueue;
                Release.mDestinationQueue = Transfer.mDestinationQueue;
                Release.mbQueueOwnershipTransfer = true;
                if (!Recorded.mCommandList->TransitionBuffer(
                        *Buffer.GetBuffer(), Release))
                {
                    ARDA_CHECK_MSG(
                        "The RHI failed to encode an RDG buffer queue release.");
                }
                if (bValidateResourceStates)
                {
                    CaptureBufferState(
                        Recorded,
                        Pass,
                        Handle,
                        Buffer,
                        Transfer.mBuffer,
                        EARDGStateCheckpoint::QueueRelease,
                        rhi::EArdaRHIResourceState::Common,
                        true,
                        Transfer.mDestinationQueue,
                        Graph.mContext.mDevice->GetCapabilities()
                            .mQueues.GetFamily(
                                Transfer.mDestinationQueue));
                }
            }
            if (bClose)
            {
                const auto CloseStatus = Recorded.mCommandList->Close();
                if (Recorded.mStatus)
                    Recorded.mStatus = CloseStatus;
            }
            return Recorded;
        }

        /**
         * Uploads frozen uniform-buffer bytes on one graphics command list.
         *
         * The returned submission instance is zero when there is no upload.
         * Non-graphics queues wait on a non-zero instance before their first
         * pass submission; graphics work is naturally ordered on its queue.
         */
        [[nodiscard]] rhi::TArdaRHIResult<uint64_t> UploadUniformBuffers(
            FARDGBuilder::FImpl& Graph)
        {
            if (Graph.mUniformBuffers.GetCount() == 0)
            {
                return {0, {}};
            }

            auto CommandListResult = Graph.mContext.mDevice->CreateCommandList(
                rhi::EArdaRHIQueueType::Graphics);
            if (!CommandListResult)
            {
                return {0, CommandListResult.mStatus};
            }
            rhi::FArdaRHICommandListRef CommandList = eastl::move(CommandListResult.mValue);
            if (auto Status = CommandList->Open(); !Status)
                return {0, Status};
            for (const FARDGUniformBuffer* UniformBuffer :
                 Graph.mUniformBuffers.GetEntries())
            {
                CommandList->BeginTrackingBufferState(
                    *UniformBuffer->GetBuffer(),
                    NormalizeInitialState(
                        UniformBuffer->GetDesc().mInitialState));
                if (auto Status = CommandList->WriteBuffer(
                    *UniformBuffer->GetBuffer(),
                    UniformBuffer->GetContents(),
                    UniformBuffer->GetDesc().mByteSize); !Status)
                    return {0, Status};
                CommandList->SetBufferState(
                    *UniformBuffer->GetBuffer(),
                    rhi::EArdaRHIResourceState::ConstantBuffer);
            }
            CommandList->CommitBarriers();
            if (auto Status = CommandList->Close(); !Status)
                return {0, Status};
            return Graph.mContext.mDevice->ExecuteCommandList(CommandList);
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
            for (const FARDGAccelStructExtraction& Extraction :
                 Graph.mAccelStructExtractions)
            {
                *Extraction.mOutput =
                    Extraction.mAccelStruct->GetAccelStruct();
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
        Graph.mExecutionResult.mQueueDependencies = Graph.mCompileResult.mQueueDependencies;
        Graph.mExecutionResult.mbUsedImmediateMode =
            Graph.mContext.mDebugOptions.mbImmediateMode;
        // Physical handles must exist before transition rebuilding and before
        // worker threads invoke validated pass resource getters.
        eastl::vector<FARDGAliasingTransition> AliasingTransitions;
        MaterializeResources(
            Graph,
            *Graph.mContext.mDevice,
            Graph.mExecutionResult,
            AliasingTransitions);
        eastl::vector<FARDGRuntimePassTransitions> RuntimeTransitions =
            BuildPhysicalTransitions(Graph);
        for (const FARDGAliasingTransition& Alias : AliasingTransitions)
        {
            if (Alias.mExecutionIndex >=
                Graph.mCompileResult.mExecutionOrder.size())
            {
                ARDA_CHECK_MSG(
                    "An RDG aliasing transition has an invalid execution index.");
            }
            const FARDGPassHandle Pass =
                Graph.mCompileResult.mExecutionOrder[Alias.mExecutionIndex];
            auto& Transitions = RuntimeTransitions[Pass.GetIndex()];
            Transitions.mAliasingResources.push_back({Alias.mType, Alias.mResourceIndex});
            for (auto Producer : Alias.mProducers)
                AddExecutionDependency(Graph, Producer, Pass);
        }

        // Merging is an execution decision: preserve callbacks, marker scopes,
        // dependency tokens, and per-pass state evidence. Split at any barrier
        // or queue edge that cannot remain inside the same render scope.
        eastl::vector<eastl::vector<FARDGPassHandle>> Batches(Graph.mPasses.GetCount());
        eastl::vector<FARDGPassHandle> BatchOwners(Graph.mPasses.GetCount());
        FARDGPassHandle BatchHead;
        FARDGPassHandle Previous;
        for (auto Handle : Graph.mCompileResult.mExecutionOrder)
        {
            const auto& Pass = Graph.mPasses.Get(Handle);
            const auto& Next = RuntimeTransitions[Handle.GetIndex()];
            bool bMerge = Options.mbMergeRasterPasses &&
                !Graph.mContext.mDebugOptions.mbImmediateMode && Previous.IsValid() &&
                Pass.GetState().mRasterGroup != UINT32_MAX &&
                Pass.GetState().mRasterGroup == Graph.mPasses.Get(Previous).GetState().mRasterGroup &&
                !HasAllFlags(Pass.GetFlags(), EARDGPassFlags::NeverParallel) &&
                !HasAllFlags(Graph.mPasses.Get(Previous).GetFlags(), EARDGPassFlags::NeverParallel);
            if (bMerge)
            {
                const auto& Prior = RuntimeTransitions[Previous.GetIndex()];
                bMerge = Next.mBuffers.empty() && Next.mAccelStructs.empty() &&
                    Next.mTextureAcquires.empty() && Next.mBufferAcquires.empty() &&
                    Next.mAliasingResources.empty() && Next.mTextureClobbers.empty() &&
                    Next.mTextureInitializations.empty() &&
                    Next.mBufferClobbers.empty() && Prior.mTextureReleases.empty() &&
                    Prior.mBufferReleases.empty() &&
                    eastl::none_of(Graph.mExecutionResult.mQueueDependencies.begin(),
                        Graph.mExecutionResult.mQueueDependencies.end(),
                        [Handle](const auto& Edge) { return Edge.mConsumer == Handle; }) &&
                    eastl::all_of(Next.mTextures.begin(), Next.mTextures.end(),
                        [&Prior](const auto& Transition)
                        {
                            return Transition.mStateBefore == Transition.mStateAfter &&
                                !Transition.mbUAVBarrier && !Transition.mbForceBarrier &&
                                eastl::any_of(Prior.mTextures.begin(), Prior.mTextures.end(),
                                    [&Transition](const auto& Earlier)
                                    {
                                        return Earlier.mTexture == Transition.mTexture &&
                                            Earlier.mSubresources == Transition.mSubresources &&
                                            Earlier.mStateAfter == Transition.mStateBefore;
                                    });
                        });
            }
            if (!bMerge)
                BatchHead = Handle;
            else
                ++Graph.mExecutionResult.mMergedRasterPassCount;
            BatchOwners[Handle.GetIndex()] = BatchHead;
            Batches[BatchHead.GetIndex()].push_back(Handle);
            Previous = Handle;
        }

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

        for (const auto& Batch : Batches)
        {
            if (!Batch.empty())
                Levels[Batch.front().GetIndex()] = Levels[Batch.back().GetIndex()];
        }
        const auto RecordBatch = [&](FARDGPassHandle Head)
        {
            auto& Output = Recorded[Head.GetIndex()];
            const auto& Batch = Batches[Head.GetIndex()];
            for (auto Member : Batch)
            {
                Output = RecordPass(Builder, Graph, Member,
                    RuntimeTransitions[Member.GetIndex()], Options.mbValidateResourceStates,
                    eastl::move(Output), Member == Batch.back());
                if (!Output.mStatus)
                    break;
            }
        };

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
                if (BatchOwners[Handle.GetIndex()] != Handle || Levels[Handle.GetIndex()] != Level)
                {
                    continue;
                }
                const FARDGPass& Pass = Graph.mPasses.Get(Handle);
                const bool bHasWork =
                    !Pass.GetState().mbSentinel ||
                    !RuntimeTransitions[Handle.GetIndex()].mTextures.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()].mBuffers.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()]
                         .mTextureAcquires.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()]
                         .mBufferAcquires.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()]
                         .mTextureReleases.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()]
                         .mBufferReleases.empty() ||
                    !RuntimeTransitions[Handle.GetIndex()]
                         .mAliasingResources.empty();
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
                        [&RecordBatch, Handle] { RecordBatch(Handle); }));
                }
                for (auto& Future : Futures)
                {
                    Future.get();
                }
            }
            for (FARDGPassHandle Handle : SerialPasses)
            {
                RecordBatch(Handle);
            }
        }

        // All recording finishes before submission. A callback failure must not
        // leave successful-looking outputs or submit an incomplete graph.
        for (const auto& Pass : Recorded)
        {
            if (!Pass.mStatus)
            {
                Graph.mExecutionResult.mStatus = Pass.mStatus;
                return Graph.mExecutionResult;
            }
        }
        const auto Upload = UploadUniformBuffers(Graph);
        if (!Upload)
        {
            Graph.mExecutionResult.mStatus = Upload.mStatus;
            return Graph.mExecutionResult;
        }
        const uint64_t UploadInstance = Upload.mValue;
        eastl::array<bool, rhi::ArdaRHIQueueTypeCount> bUploadWaited{};
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

            for (FARDGStateConformanceRecord& Record :
                 Pass.mStateConformanceRecords)
            {
                if (!Record.IsConsistent())
                    ++Graph.mExecutionResult.mStateConformanceFailureCount;
                Graph.mExecutionResult.mStateConformanceRecords.push_back(
                    eastl::move(Record));
            }
            if (Options.mbValidateResourceStates &&
                Graph.mExecutionResult.mStateConformanceFailureCount != 0)
            {
                Graph.mExecutionResult.mStatus = rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "RDG resource state differs from the facade/backend/native state.");
                break;
            }

            const size_t ConsumerQueueIndex =
                rhi::GetArdaRHIQueueIndex(Pass.mQueue);
            eastl::array<uint64_t, rhi::ArdaRHIQueueTypeCount> RequiredInstances{};
            if (UploadInstance != 0 &&
                Pass.mQueue != rhi::EArdaRHIQueueType::Graphics &&
                !bUploadWaited[ConsumerQueueIndex])
            {
                RequiredInstances[rhi::GetArdaRHIQueueIndex(
                    rhi::EArdaRHIQueueType::Graphics)] = UploadInstance;
                bUploadWaited[ConsumerQueueIndex] = true;
            }

            for (const FARDGQueueDependency& Dependency :
                 Graph.mExecutionResult.mQueueDependencies)
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
                auto& Required = RequiredInstances[rhi::GetArdaRHIQueueIndex(
                    GetCommandQueue(Dependency.mProducerPipeline))];
                Required = eastl::max(Required, ProducerInstance);
            }

            for (size_t QueueIndex = 0; QueueIndex < RequiredInstances.size(); ++QueueIndex)
            {
                if (!RequiredInstances[QueueIndex])
                    continue;
                auto Status = Graph.mContext.mDevice->QueueWait(Pass.mQueue,
                    static_cast<rhi::EArdaRHIQueueType>(QueueIndex), RequiredInstances[QueueIndex]);
                if (!Status)
                {
                    Graph.mExecutionResult.mStatus = eastl::move(Status);
                    break;
                }
                ++Graph.mExecutionResult.mQueueWaitCount;
            }
            if (!Graph.mExecutionResult.mStatus)
                break;

            const auto SubmitResult =
                Graph.mContext.mDevice->ExecuteCommandList(Pass.mCommandList);
            if (!SubmitResult)
            {
                Graph.mExecutionResult.mStatus = SubmitResult.mStatus;
                ++Graph.mExecutionResult.mSubmissionFailureCount;
                if (SubmitResult.mStatus.mCode ==
                    rhi::EArdaRHIResult::InvalidState)
                {
                    ++Graph.mExecutionResult.mStateConformanceFailureCount;
                }
                break;
            }
            const uint64_t Instance = SubmitResult.mValue;
            for (auto Member : Batches[Handle.GetIndex()])
                PassInstances[Member.GetIndex()] = Instance;
            Graph.mExecutionResult.mLastSubmittedInstances[
                ConsumerQueueIndex] = Instance;
            ++Graph.mExecutionResult.mSubmittedCommandListCount;
            Graph.mExecutionResult.mClobberedResourceCount +=
                Pass.mClobberedResourceCount;
        }

        if (Graph.mExecutionResult.mStatus)
            CompleteExtractions(Graph);
        Graph.mContext.mDevice->RunGarbageCollection();
        Graph.mbExecuted = Graph.mExecutionResult.mStatus.IsSuccess();
        FailureGuard.mbCompleted = Graph.mbExecuted;
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
