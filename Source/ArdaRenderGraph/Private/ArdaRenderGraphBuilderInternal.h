#pragma once

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphBuilder.h"
#include "ArdaRenderGraphRegistry.h"

#include <mutex>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>

namespace arda::render_graph
{
    struct FARDGBuilder::FImpl final
    {
        /**
         * Initializes the graph's build-stage storage and entry sentinel.
         *
         * Every handle registry shares the graph arena, keeping record addresses
         * stable for the full builder lifetime. The prologue is appended first so
         * imported resources can treat it as their initial producer.
         */
        explicit FImpl(FARDGRenderGraphContext InContext)
            : mContext(eastl::move(InContext))
            , mPasses(mArena)
            , mTextures(mArena)
            , mBuffers(mArena)
            , mAccelStructs(mArena)
            , mViews(mArena)
            , mUniformBuffers(mArena)
        {
            mCompileResult.mPrologue =
                mPasses.Emplace<FARDGSentinelPass>("GraphPrologue");
        }

        /** Immutable-after-construction device, queue, and debug configuration shared by all graph stages. */
        FARDGRenderGraphContext mContext;

        /** Graph-lifetime owner of registry records and frozen parameter storage; declared before its users. */
        FARDGArena mArena;

        /** Arena-backed pass registry; handle indices are stable insertion-order positions for this graph. */
        TARDGHandleRegistry<FARDGPass, FARDGPassHandle> mPasses;

        /** Arena-backed logical texture registry indexed exclusively by FARDGTextureHandle values. */
        TARDGHandleRegistry<FARDGTexture, FARDGTextureHandle> mTextures;

        /** Arena-backed logical buffer registry indexed exclusively by FARDGBufferHandle values. */
        TARDGHandleRegistry<FARDGBuffer, FARDGBufferHandle> mBuffers;

        TARDGHandleRegistry<FARDGAccelStruct, FARDGAccelStructHandle> mAccelStructs;

        /** Arena-backed logical view registry spanning texture and buffer SRV/UAV records. */
        TARDGHandleRegistry<FARDGView, FARDGViewHandle> mViews;

        /** Arena-backed logical uniform-buffer registry indexed by graph-local uniform-buffer handles. */
        TARDGHandleRegistry<FARDGUniformBuffer, FARDGUniformBufferHandle> mUniformBuffers;

        /** Addresses already frozen into mArena; populated during building and valid until graph teardown. */
        eastl::unordered_set<const void*> mParameterStorage;

        /** Deduplication map from non-owning physical texture identity to its arena-owned logical wrapper. */
        eastl::unordered_map<const void*, FARDGTextureRef> mImportedTextures;

        /** Deduplication map from non-owning physical buffer identity to its arena-owned logical wrapper. */
        eastl::unordered_map<const void*, FARDGBufferRef> mImportedBuffers;
        eastl::unordered_map<const void*, FARDGAccelStructRef> mImportedAccelStructs;

        /** Build-order texture extraction requests; output pointers remain caller-owned through execution. */
        eastl::vector<FARDGTextureExtraction> mTextureExtractions;

        /** Build-order buffer extraction requests; output pointers remain caller-owned through execution. */
        eastl::vector<FARDGBufferExtraction> mBufferExtractions;

        /** Graph-scoped typed data store, mutable only while building and retained through execution. */
        FARDGBlackboard mBlackboard;

        /** Compiler output published once compilation succeeds; initially contains only the prologue handle. */
        FARDGCompileResult mCompileResult;

        /** Executor output published after execution; default state denotes that no result exists yet. */
        FARDGExecutionResult mExecutionResult;

        /** Guards active-pass access tracking and physical-resource resolution during parallel recording. */
        std::mutex mPassAccessMutex;

        /** Pass-handle indices whose execution access gates are open; empty outside callback recording. */
        eastl::unordered_set<uint32_t> mActivePassAccess;

        /** True after compiler results are successfully published; false throughout initial building. */
        bool mbCompiled = false;

        /** Re-entry guard set only while the compiler pipeline is actively mutating graph state. */
        bool mbCompiling = false;

        /** True once execution has begun, permanently closing all build-stage mutation APIs. */
        bool mbExecutionStarted = false;

        /** True only after execution and extraction publication complete successfully. */
        bool mbExecuted = false;

        /** Sticky lifecycle failure flag; false means no graph-stage failure has been recorded. */
        bool mbFailed = false;
    };
}
