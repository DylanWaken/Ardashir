#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphLog.h"
#include "ArdaRenderGraphValidation.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace arda::render_graph
{
    namespace
    {
        constexpr uint32_t WriteMask =
            static_cast<uint32_t>(nvrhi::ResourceStates::UnorderedAccess) |
            static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
            static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
            static_cast<uint32_t>(nvrhi::ResourceStates::CopyDest) |
            static_cast<uint32_t>(nvrhi::ResourceStates::ResolveDest) |
            static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructWrite) |
            static_cast<uint32_t>(nvrhi::ResourceStates::OpacityMicromapWrite) |
            static_cast<uint32_t>(
                nvrhi::ResourceStates::ConvertCoopVecMatrixOutput);

        constexpr uint32_t CopyMask =
            static_cast<uint32_t>(nvrhi::ResourceStates::CopySource) |
            static_cast<uint32_t>(nvrhi::ResourceStates::CopyDest);

        constexpr uint32_t GraphicsOnlyMask =
            static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
            static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
            static_cast<uint32_t>(nvrhi::ResourceStates::DepthRead) |
            static_cast<uint32_t>(nvrhi::ResourceStates::Present) |
            static_cast<uint32_t>(nvrhi::ResourceStates::ShadingRateSurface);

        constexpr uint32_t TextureForbiddenMask =
            static_cast<uint32_t>(nvrhi::ResourceStates::ConstantBuffer) |
            static_cast<uint32_t>(nvrhi::ResourceStates::VertexBuffer) |
            static_cast<uint32_t>(nvrhi::ResourceStates::IndexBuffer) |
            static_cast<uint32_t>(nvrhi::ResourceStates::IndirectArgument) |
            static_cast<uint32_t>(nvrhi::ResourceStates::StreamOut) |
            static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructRead) |
            static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructWrite) |
            static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructBuildInput) |
            static_cast<uint32_t>(nvrhi::ResourceStates::AccelStructBuildBlas) |
            static_cast<uint32_t>(nvrhi::ResourceStates::OpacityMicromapWrite) |
            static_cast<uint32_t>(
                nvrhi::ResourceStates::OpacityMicromapBuildInput) |
            static_cast<uint32_t>(
                nvrhi::ResourceStates::ConvertCoopVecMatrixInput) |
            static_cast<uint32_t>(
                nvrhi::ResourceStates::ConvertCoopVecMatrixOutput);

        constexpr uint32_t BufferForbiddenMask =
            static_cast<uint32_t>(nvrhi::ResourceStates::RenderTarget) |
            static_cast<uint32_t>(nvrhi::ResourceStates::DepthWrite) |
            static_cast<uint32_t>(nvrhi::ResourceStates::DepthRead) |
            static_cast<uint32_t>(nvrhi::ResourceStates::ResolveDest) |
            static_cast<uint32_t>(nvrhi::ResourceStates::ResolveSource) |
            static_cast<uint32_t>(nvrhi::ResourceStates::Present) |
            static_cast<uint32_t>(nvrhi::ResourceStates::ShadingRateSurface);

        [[nodiscard]] bool IsWriteState(nvrhi::ResourceStates State) noexcept
        {
            return (static_cast<uint32_t>(State) & WriteMask) != 0;
        }

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

        [[nodiscard]] bool HasMultipleWriteStates(
            nvrhi::ResourceStates State) noexcept
        {
            uint32_t Value = static_cast<uint32_t>(State) & WriteMask;
            return Value != 0 && (Value & (Value - 1u)) != 0;
        }

        [[nodiscard]] bool IsLegalStateCombination(
            nvrhi::ResourceStates State) noexcept
        {
            const uint32_t Value = static_cast<uint32_t>(State);
            const uint32_t Common =
                static_cast<uint32_t>(nvrhi::ResourceStates::Common);
            const uint32_t Present =
                static_cast<uint32_t>(nvrhi::ResourceStates::Present);
            return State != nvrhi::ResourceStates::Unknown &&
                !HasMultipleWriteStates(State) &&
                ((Value & WriteMask) == 0 || (Value & ~WriteMask) == 0) &&
                ((Value & Common) == 0 || Value == Common) &&
                ((Value & Present) == 0 || Value == Present);
        }

        [[noreturn]] void ReportPassError(
            const FARDGPass& Pass,
            const char* Message)
        {
            std::ostringstream Stream;
            Stream << "Render-graph pass \"" << Pass.GetName() << "\": "
                   << Message;
            ARDA_CHECK_MSG("%s", Stream.str().c_str());
        }

        void ValidateState(
            const FARDGPass& Pass,
            nvrhi::ResourceStates State,
            bool bTexture)
        {
            const uint32_t Value = static_cast<uint32_t>(State);
            if (State == nvrhi::ResourceStates::Unknown)
            {
                ReportPassError(Pass, "declares an unknown resource state.");
            }
            if (!IsLegalStateCombination(State))
            {
                ReportPassError(
                    Pass,
                    "combines a write state with another incompatible state.");
            }
            if (bTexture && (Value & TextureForbiddenMask) != 0)
            {
                ReportPassError(Pass, "declares a buffer-only state for a texture.");
            }
            if (!bTexture && (Value & BufferForbiddenMask) != 0)
            {
                ReportPassError(Pass, "declares a texture-only state for a buffer.");
            }
            if (HasAllFlags(Pass.GetFlags(), EARDGPassFlags::Copy) &&
                (Value & ~CopyMask) != 0)
            {
                ReportPassError(
                    Pass,
                    "declares a state unsupported by a copy queue.");
            }
            if (HasAllFlags(Pass.GetFlags(), EARDGPassFlags::AsyncCompute) &&
                (Value & GraphicsOnlyMask) != 0)
            {
                ReportPassError(
                    Pass,
                    "declares a graphics-only state for async compute.");
            }
        }

        void ValidateTextureAccess(
            const FARDGBuilder::FImpl& Graph,
            const FARDGPass& Pass,
            const FARDGPassTextureState& Access)
        {
            const FARDGTexture* Texture = Graph.mTextures.TryGet(Access.mTexture);
            if (Texture == nullptr)
            {
                ReportPassError(Pass, "references an invalid texture handle.");
            }
            ValidateState(Pass, Access.mState, true);
            const nvrhi::TextureDesc& Desc = Texture->GetDesc();
            const nvrhi::TextureSubresourceSet Resolved =
                Access.mSubresources.resolve(Desc, false);
            if (Resolved.numMipLevels == 0 || Resolved.numArraySlices == 0 ||
                Resolved.baseMipLevel >= Desc.mipLevels ||
                Resolved.baseArraySlice >= Desc.arraySize ||
                Resolved.baseMipLevel + Resolved.numMipLevels > Desc.mipLevels ||
                Resolved.baseArraySlice + Resolved.numArraySlices > Desc.arraySize)
            {
                ReportPassError(Pass, "declares an invalid texture subresource range.");
            }
            if ((Access.mState & nvrhi::ResourceStates::UnorderedAccess) !=
                    nvrhi::ResourceStates::Unknown &&
                !Desc.isUAV)
            {
                ReportPassError(Pass, "uses unordered access on a non-UAV texture.");
            }
            if ((Access.mState &
                 (nvrhi::ResourceStates::RenderTarget |
                  nvrhi::ResourceStates::DepthWrite |
                  nvrhi::ResourceStates::DepthRead)) !=
                    nvrhi::ResourceStates::Unknown &&
                !Desc.isRenderTarget)
            {
                ReportPassError(
                    Pass,
                    "uses an attachment state on a non-render-target texture.");
            }
        }

        void ValidateBufferAccess(
            const FARDGBuilder::FImpl& Graph,
            const FARDGPass& Pass,
            const FARDGPassBufferState& Access)
        {
            const FARDGBuffer* Buffer = Graph.mBuffers.TryGet(Access.mBuffer);
            if (Buffer == nullptr)
            {
                ReportPassError(Pass, "references an invalid buffer handle.");
            }
            ValidateState(Pass, Access.mState, false);
            const nvrhi::BufferDesc& Desc = Buffer->GetDesc();
            const nvrhi::BufferRange Resolved = Access.mRange.resolve(Desc);
            if (Resolved.byteSize == 0 || Resolved.byteOffset > Desc.byteSize ||
                Resolved.byteSize > Desc.byteSize - Resolved.byteOffset)
            {
                ReportPassError(Pass, "declares an invalid buffer range.");
            }
            if ((Access.mState & nvrhi::ResourceStates::UnorderedAccess) !=
                    nvrhi::ResourceStates::Unknown &&
                !Desc.canHaveUAVs)
            {
                ReportPassError(Pass, "uses unordered access on a non-UAV buffer.");
            }
        }

        void ValidateProducedBeforeRead(const FARDGBuilder::FImpl& Graph)
        {
            std::vector<std::vector<bool>> ProducedTextures;
            ProducedTextures.reserve(Graph.mTextures.GetCount());
            for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                const nvrhi::TextureDesc& Desc = Texture->GetDesc();
                ProducedTextures.emplace_back(
                    static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
                    Texture->IsExternal());
            }
            std::vector<bool> ProducedBuffers(Graph.mBuffers.GetCount(), false);
            for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                ProducedBuffers[Buffer->GetHandle().GetIndex()] =
                    Buffer->IsExternal();
            }

            for (const FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                if (Pass->GetState().mbSentinel)
                {
                    continue;
                }

                std::vector<std::vector<bool>> PassTextureWrites(
                    Graph.mTextures.GetCount());
                for (const FARDGPassTextureState& Access :
                     Pass->GetState().mTextureStates)
                {
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Access.mTexture);
                    const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                    auto& Writes = PassTextureWrites[Access.mTexture.GetIndex()];
                    if (Writes.empty())
                    {
                        Writes.resize(
                            static_cast<size_t>(Desc.mipLevels) * Desc.arraySize,
                            false);
                    }
                    if (!Access.mbWrite)
                    {
                        continue;
                    }
                    const auto Range = Access.mSubresources.resolve(Desc, false);
                    for (uint32_t Slice = Range.baseArraySlice;
                         Slice < Range.baseArraySlice + Range.numArraySlices;
                         ++Slice)
                    {
                        for (uint32_t Mip = Range.baseMipLevel;
                             Mip < Range.baseMipLevel + Range.numMipLevels;
                             ++Mip)
                        {
                            Writes[static_cast<size_t>(Slice) * Desc.mipLevels + Mip] =
                                true;
                        }
                    }
                }

                bool bPassWritesBuffer = false;
                std::unordered_set<uint32_t> PassBufferWrites;
                for (const FARDGPassBufferState& Access :
                     Pass->GetState().mBufferStates)
                {
                    if (Access.mbWrite)
                    {
                        PassBufferWrites.insert(Access.mBuffer.GetIndex());
                        bPassWritesBuffer = true;
                    }
                }

                for (const FARDGPassTextureState& Access :
                     Pass->GetState().mTextureStates)
                {
                    if (Access.mbWrite)
                    {
                        continue;
                    }
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Access.mTexture);
                    const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                    const auto Range = Access.mSubresources.resolve(Desc, false);
                    for (uint32_t Slice = Range.baseArraySlice;
                         Slice < Range.baseArraySlice + Range.numArraySlices;
                         ++Slice)
                    {
                        for (uint32_t Mip = Range.baseMipLevel;
                             Mip < Range.baseMipLevel + Range.numMipLevels;
                             ++Mip)
                        {
                            const size_t Index =
                                static_cast<size_t>(Slice) * Desc.mipLevels + Mip;
                            if (!ProducedTextures[Access.mTexture.GetIndex()][Index] &&
                                !PassTextureWrites[Access.mTexture.GetIndex()][Index])
                            {
                                ReportPassError(
                                    *Pass,
                                    "reads a texture subresource before it is produced.");
                            }
                        }
                    }
                }
                for (const FARDGPassBufferState& Access :
                     Pass->GetState().mBufferStates)
                {
                    const uint32_t Index = Access.mBuffer.GetIndex();
                    if (!Access.mbWrite && !ProducedBuffers[Index] &&
                        PassBufferWrites.find(Index) == PassBufferWrites.end())
                    {
                        ReportPassError(
                            *Pass,
                            "reads a buffer before it is produced.");
                    }
                }

                for (uint32_t TextureIndex = 0;
                     TextureIndex < PassTextureWrites.size();
                     ++TextureIndex)
                {
                    const auto& Writes = PassTextureWrites[TextureIndex];
                    for (size_t Index = 0; Index < Writes.size(); ++Index)
                    {
                        ProducedTextures[TextureIndex][Index] =
                            ProducedTextures[TextureIndex][Index] ||
                            Writes[Index];
                    }
                }
                if (bPassWritesBuffer)
                {
                    for (uint32_t Index : PassBufferWrites)
                    {
                        ProducedBuffers[Index] = true;
                    }
                }
            }
        }
    }

    void FARDGValidation::ValidateBeforeCompile(
        const FARDGBuilder::FImpl& Graph)
    {
        constexpr uint16_t KnownPassFlags =
            static_cast<uint16_t>(EARDGPassFlags::Raster) |
            static_cast<uint16_t>(EARDGPassFlags::Compute) |
            static_cast<uint16_t>(EARDGPassFlags::AsyncCompute) |
            static_cast<uint16_t>(EARDGPassFlags::Copy) |
            static_cast<uint16_t>(EARDGPassFlags::NeverCull) |
            static_cast<uint16_t>(EARDGPassFlags::SkipRenderPass) |
            static_cast<uint16_t>(EARDGPassFlags::NeverParallel);
        constexpr uint8_t KnownResourceFlags =
            static_cast<uint8_t>(EARDGResourceFlags::External) |
            static_cast<uint8_t>(EARDGResourceFlags::Extracted) |
            static_cast<uint8_t>(EARDGResourceFlags::Transient);

        for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
        {
            const uint8_t Flags = static_cast<uint8_t>(Texture->GetFlags());
            const uint32_t Initial =
                static_cast<uint32_t>(Texture->GetInitialState());
            const uint32_t Final =
                static_cast<uint32_t>(Texture->GetFinalState());
            if ((Flags & ~KnownResourceFlags) != 0 ||
                (Texture->IsExternal() &&
                 HasAllFlags(Texture->GetFlags(), EARDGResourceFlags::Transient)) ||
                (Texture->IsExternal() != static_cast<bool>(Texture->GetTexture())) ||
                (Texture->GetInitialState() != nvrhi::ResourceStates::Unknown &&
                 (!IsLegalStateCombination(Texture->GetInitialState()) ||
                  (Initial & TextureForbiddenMask) != 0)) ||
                ((Texture->IsExternal() || Texture->IsExtracted()) &&
                 (!IsLegalStateCombination(Texture->GetFinalState()) ||
                  (Final & TextureForbiddenMask) != 0)))
            {
                ARDA_CHECK_MSG(
                    "A render-graph texture has invalid ownership flags or backing.");
            }
        }
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            const uint8_t Flags = static_cast<uint8_t>(Buffer->GetFlags());
            const uint32_t Initial =
                static_cast<uint32_t>(Buffer->GetInitialState());
            const uint32_t Final =
                static_cast<uint32_t>(Buffer->GetFinalState());
            if ((Flags & ~KnownResourceFlags) != 0 ||
                (Buffer->IsExternal() &&
                 HasAllFlags(Buffer->GetFlags(), EARDGResourceFlags::Transient)) ||
                (Buffer->IsExternal() != static_cast<bool>(Buffer->GetBuffer())) ||
                (Buffer->GetInitialState() != nvrhi::ResourceStates::Unknown &&
                 (!IsLegalStateCombination(Buffer->GetInitialState()) ||
                  (Initial & BufferForbiddenMask) != 0)) ||
                ((Buffer->IsExternal() || Buffer->IsExtracted()) &&
                 (!IsLegalStateCombination(Buffer->GetFinalState()) ||
                  (Final & BufferForbiddenMask) != 0)))
            {
                ARDA_CHECK_MSG(
                    "A render-graph buffer has invalid ownership flags or backing.");
            }
        }

        for (const FARDGPass* Pass : Graph.mPasses.GetEntries())
        {
            if ((static_cast<uint16_t>(Pass->GetFlags()) & ~KnownPassFlags) != 0)
            {
                ReportPassError(*Pass, "contains unknown pass flags.");
            }
            for (const FARDGPassTextureState& Access :
                 Pass->GetState().mTextureStates)
            {
                ValidateTextureAccess(Graph, *Pass, Access);
            }
            for (const FARDGPassBufferState& Access :
                 Pass->GetState().mBufferStates)
            {
                ValidateBufferAccess(Graph, *Pass, Access);
            }
        }

        std::unordered_set<uint32_t> ExtractedTextures;
        std::unordered_set<const void*> TextureOutputs;
        for (const FARDGTextureExtraction& Extraction : Graph.mTextureExtractions)
        {
            if (Extraction.mTexture == nullptr || Extraction.mOutput == nullptr ||
                Graph.mTextures.TryGet(Extraction.mTexture->GetHandle()) !=
                    Extraction.mTexture ||
                !Extraction.mTexture->IsExtracted() ||
                !ExtractedTextures.insert(
                    Extraction.mTexture->GetHandle().GetIndex()).second ||
                !TextureOutputs.insert(Extraction.mOutput).second)
            {
                ARDA_CHECK_MSG(
                    "A render-graph texture extraction is invalid or duplicated.");
            }
        }
        std::unordered_set<uint32_t> ExtractedBuffers;
        std::unordered_set<const void*> BufferOutputs;
        for (const FARDGBufferExtraction& Extraction : Graph.mBufferExtractions)
        {
            if (Extraction.mBuffer == nullptr || Extraction.mOutput == nullptr ||
                Graph.mBuffers.TryGet(Extraction.mBuffer->GetHandle()) !=
                    Extraction.mBuffer ||
                !Extraction.mBuffer->IsExtracted() ||
                !ExtractedBuffers.insert(
                    Extraction.mBuffer->GetHandle().GetIndex()).second ||
                !BufferOutputs.insert(Extraction.mOutput).second)
            {
                ARDA_CHECK_MSG(
                    "A render-graph buffer extraction is invalid or duplicated.");
            }
        }

        ValidateProducedBeforeRead(Graph);
    }

    void FARDGValidation::ValidateTransitions(
        const FARDGBuilder::FImpl& Graph)
    {
        std::vector<std::vector<nvrhi::ResourceStates>> TextureStates;
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
        std::vector<nvrhi::ResourceStates> BufferStates;
        BufferStates.reserve(Graph.mBuffers.GetCount());
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            nvrhi::ResourceStates Initial = Buffer->GetInitialState();
            BufferStates.push_back(
                Initial == nvrhi::ResourceStates::Unknown
                    ? nvrhi::ResourceStates::Common
                    : Initial);
        }

        for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
        {
            const FARDGPass& Pass = Graph.mPasses.Get(Handle);
            for (const FARDGTextureTransition& Transition :
                 Pass.GetState().mTextureTransitions)
            {
                const FARDGTexture& Texture =
                    Graph.mTextures.Get(Transition.mTexture);
                const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                const auto Range =
                    Transition.mSubresources.resolve(Desc, false);
                for (uint32_t Slice = Range.baseArraySlice;
                     Slice < Range.baseArraySlice + Range.numArraySlices;
                     ++Slice)
                {
                    for (uint32_t Mip = Range.baseMipLevel;
                         Mip < Range.baseMipLevel + Range.numMipLevels;
                         ++Mip)
                    {
                        nvrhi::ResourceStates& Current =
                            TextureStates[Transition.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mipLevels +
                                          Mip];
                        if (Current != Transition.mStateBefore)
                        {
                            ReportPassError(
                                Pass,
                                "has a discontinuous compiled texture transition.");
                        }
                        Current = Transition.mStateAfter;
                    }
                }
            }
            for (const FARDGBufferTransition& Transition :
                 Pass.GetState().mBufferTransitions)
            {
                nvrhi::ResourceStates& Current =
                    BufferStates[Transition.mBuffer.GetIndex()];
                if (Current != Transition.mStateBefore)
                {
                    ReportPassError(
                        Pass,
                        "has a discontinuous compiled buffer transition.");
                }
                Current = Transition.mStateAfter;
            }

            for (const FARDGPassTextureState& Access :
                 Pass.GetState().mTextureStates)
            {
                const FARDGTexture& Texture =
                    Graph.mTextures.Get(Access.mTexture);
                const nvrhi::TextureDesc& Desc = Texture.GetDesc();
                const auto Range =
                    Access.mSubresources.resolve(Desc, false);
                for (uint32_t Slice = Range.baseArraySlice;
                     Slice < Range.baseArraySlice + Range.numArraySlices;
                     ++Slice)
                {
                    for (uint32_t Mip = Range.baseMipLevel;
                         Mip < Range.baseMipLevel + Range.numMipLevels;
                         ++Mip)
                    {
                        const nvrhi::ResourceStates Current =
                            TextureStates[Access.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mipLevels +
                                          Mip];
                        const nvrhi::ResourceStates Required =
                            NormalizeStateForPipeline(
                                Access.mState,
                                Pass.GetState().mPipeline);
                        const bool bSatisfied = Access.mbWrite
                            ? Current == Required
                            : (Current & Required) == Required;
                        if (!bSatisfied)
                        {
                            ReportPassError(
                                Pass,
                                "has an unsatisfied compiled texture state.");
                        }
                    }
                }
            }
            for (const FARDGPassBufferState& Access :
                 Pass.GetState().mBufferStates)
            {
                const nvrhi::ResourceStates Current =
                    BufferStates[Access.mBuffer.GetIndex()];
                const nvrhi::ResourceStates Required =
                    NormalizeStateForPipeline(
                        Access.mState,
                        Pass.GetState().mPipeline);
                const bool bSatisfied = Access.mbWrite
                    ? Current == Required
                    : (Current & Required) == Required;
                if (!bSatisfied)
                {
                    ReportPassError(
                        Pass,
                        "has an unsatisfied compiled buffer state.");
                }
            }
        }

        for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
        {
            if ((!Texture->IsExternal() && !Texture->IsExtracted()) ||
                !Texture->GetFirstUse().IsValid())
            {
                continue;
            }
            const auto& States =
                TextureStates[Texture->GetHandle().GetIndex()];
            if (std::any_of(
                    States.begin(),
                    States.end(),
                    [Texture](nvrhi::ResourceStates State)
                    {
                        return State != Texture->GetFinalState();
                    }))
            {
                ARDA_CHECK_MSG(
                    "A compiled texture does not reach its graph-exit state.");
            }
        }
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            if ((!Buffer->IsExternal() && !Buffer->IsExtracted()) ||
                !Buffer->GetFirstUse().IsValid())
            {
                continue;
            }
            if (BufferStates[Buffer->GetHandle().GetIndex()] !=
                Buffer->GetFinalState())
            {
                ARDA_CHECK_MSG(
                    "A compiled buffer does not reach its graph-exit state.");
            }
        }
    }
}
