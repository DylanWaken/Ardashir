#pragma once

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphBuilder.h"
#include "ArdaRenderGraphRegistry.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace arda::render_graph
{
    struct FARDGBuilder::FImpl final
    {
        explicit FImpl(FARDGRenderGraphContext InContext)
            : mContext(std::move(InContext))
            , mPasses(mArena)
            , mTextures(mArena)
            , mBuffers(mArena)
            , mViews(mArena)
            , mUniformBuffers(mArena)
        {
            mCompileResult.mPrologue =
                mPasses.Emplace<FARDGSentinelPass>("GraphPrologue");
        }

        FARDGRenderGraphContext mContext;
        FARDGArena mArena;
        TARDGHandleRegistry<FARDGPass, FARDGPassHandle> mPasses;
        TARDGHandleRegistry<FARDGTexture, FARDGTextureHandle> mTextures;
        TARDGHandleRegistry<FARDGBuffer, FARDGBufferHandle> mBuffers;
        TARDGHandleRegistry<FARDGView, FARDGViewHandle> mViews;
        TARDGHandleRegistry<FARDGUniformBuffer, FARDGUniformBufferHandle> mUniformBuffers;
        std::unordered_set<const void*> mParameterStorage;
        std::unordered_map<nvrhi::ITexture*, FARDGTextureRef> mImportedTextures;
        std::unordered_map<nvrhi::IBuffer*, FARDGBufferRef> mImportedBuffers;
        std::vector<FARDGTextureExtraction> mTextureExtractions;
        std::vector<FARDGBufferExtraction> mBufferExtractions;
        FARDGBlackboard mBlackboard;
        FARDGCompileResult mCompileResult;
        FARDGExecutionResult mExecutionResult;
        std::mutex mPassAccessMutex;
        std::unordered_set<uint32_t> mActivePassAccess;
        bool mbCompiled = false;
        bool mbCompiling = false;
        bool mbExecutionStarted = false;
        bool mbExecuted = false;
        bool mbFailed = false;
    };
}
