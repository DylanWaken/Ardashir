#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphBuilderInternal.h"
#include "ArdaRenderGraphLog.h"
#include "ArdaRenderGraphValidation.h"

#include <EASTL/algorithm.h>
#include <sstream>
#include <EASTL/unordered_set.h>

namespace arda::render_graph
{
    namespace
    {
        constexpr uint32_t WriteMask =
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::UnorderedAccess) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopyDest) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::ResolveDest) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::AccelStructWrite);

        constexpr uint32_t CopyMask =
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopySource) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::CopyDest);

        constexpr uint32_t GraphicsOnlyMask =
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthRead) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::Present);

        constexpr uint32_t TextureForbiddenMask =
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::ConstantBuffer) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::VertexBuffer) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::IndexBuffer) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::IndirectArgument);

        constexpr uint32_t BufferForbiddenMask =
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::RenderTarget) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthWrite) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::DepthRead) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::ResolveDest) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::ResolveSource) |
            static_cast<uint32_t>(rhi::EArdaRHIResourceState::Present);

        /**
         * Returns whether a state contains any bit that permits resource writes.
         *
         * The classification uses the same write mask as the declaration
         * legality checks, covering UAV, attachment, copy, and acceleration writes.
         */
        [[nodiscard]] bool IsWriteState(rhi::EArdaRHIResourceState State) noexcept
        {
            return (static_cast<uint32_t>(State) & WriteMask) != 0;
        }

        /**
         * Converts a declared state to the requirement for the selected pipeline.
         *
         * Async compute drops the pixel bit only from a combined pixel/non-pixel
         * shader-read declaration. This exactly mirrors compiler lowering so
         * transition replay tests the state the selected queue actually uses.
         */
        [[nodiscard]] rhi::EArdaRHIResourceState NormalizeStateForPipeline(
            rhi::EArdaRHIResourceState State,
            EARDGPipeline Pipeline) noexcept
        {
            if (Pipeline != EARDGPipeline::AsyncCompute ||
                (State & rhi::EArdaRHIResourceState::PixelShaderResource) ==
                    rhi::EArdaRHIResourceState::Unknown ||
                (State & rhi::EArdaRHIResourceState::NonPixelShaderResource) ==
                    rhi::EArdaRHIResourceState::Unknown)
            {
                return State;
            }

            return static_cast<rhi::EArdaRHIResourceState>(
                static_cast<uint32_t>(State) &
                ~static_cast<uint32_t>(
                    rhi::EArdaRHIResourceState::PixelShaderResource));
        }

        /**
         * Returns whether more than one independently writable state bit is set.
         *
         * The bit-clearing test is applied only to the masked write bits; zero
         * and a single write state are legal candidates.
         */
        [[nodiscard]] bool HasMultipleWriteStates(
            rhi::EArdaRHIResourceState State) noexcept
        {
            uint32_t Value = static_cast<uint32_t>(State) & WriteMask;
            return Value != 0 && (Value & (Value - 1u)) != 0;
        }

        /**
         * Checks the resource-state combinations accepted by graph declarations.
         *
         * A legal state is known, has at most one write bit, never combines a
         * write with reads, and uses Common or Present only as a standalone state.
         * Resource-kind and queue-specific restrictions are checked separately.
         */
        [[nodiscard]] bool IsLegalStateCombination(
            rhi::EArdaRHIResourceState State) noexcept
        {
            const uint32_t Value = static_cast<uint32_t>(State);
            const uint32_t Common =
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::Common);
            const uint32_t Present =
                static_cast<uint32_t>(rhi::EArdaRHIResourceState::Present);
            return State != rhi::EArdaRHIResourceState::Unknown &&
                !HasMultipleWriteStates(State) &&
                ((Value & WriteMask) == 0 || (Value & ~WriteMask) == 0) &&
                ((Value & Common) == 0 || Value == Common) &&
                ((Value & Present) == 0 || Value == Present);
        }

        /**
         * Reports a fatal validation failure prefixed with the pass's diagnostic name.
         *
         * Centralizing this formatting keeps all pass-local declaration and
         * transition errors attributable to the pass that introduced them.
         */
        [[noreturn]] void ReportPassError(
            const FARDGPass& Pass,
            const char* Message)
        {
            std::ostringstream Stream;
            Stream << "Render-graph pass \"" << Pass.GetName().c_str() << "\": "
                   << Message;
            ARDA_CHECK_MSG("%s", Stream.str().c_str());
        }

        /**
         * Validates one declared state against general, resource-kind, and queue rules.
         *
         * This pre-compile helper rejects unknown or internally incompatible
         * combinations, texture/buffer domain mismatches, and states forbidden
         * by explicit Copy or AsyncCompute pass requests.
         */
        void ValidateState(
            const FARDGPass& Pass,
            rhi::EArdaRHIResourceState State,
            bool bTexture)
        {
            const uint32_t Value = static_cast<uint32_t>(State);
            if (State == rhi::EArdaRHIResourceState::Unknown)
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

        /**
         * Validates one pass texture access against the graph and texture descriptor.
         *
         * The handle must resolve in this graph, its state must be legal, its
         * resolved mip/slice range must be nonempty and in bounds, and UAV or
         * attachment states require the corresponding descriptor capability.
         * This stage reads declarations only.
         */
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
            const rhi::FArdaRHITextureDesc& Desc = Texture->GetDesc();
            const rhi::FArdaRHITextureSubresourceRange Resolved =
                Access.mSubresources.Resolve(Desc);
            if (Resolved.mMipLevelCount == 0 || Resolved.mArraySliceCount == 0 ||
                Resolved.mBaseMipLevel >= Desc.mMipLevels ||
                Resolved.mBaseArraySlice >= Desc.mArraySize ||
                Resolved.mBaseMipLevel + Resolved.mMipLevelCount > Desc.mMipLevels ||
                Resolved.mBaseArraySlice + Resolved.mArraySliceCount > Desc.mArraySize)
            {
                ReportPassError(Pass, "declares an invalid texture subresource range.");
            }
            if ((Access.mState & rhi::EArdaRHIResourceState::UnorderedAccess) !=
                    rhi::EArdaRHIResourceState::Unknown &&
                !rhi::HasAnyFlags(Desc.mUsage, rhi::EArdaRHITextureUsage::UnorderedAccess))
            {
                ReportPassError(Pass, "uses unordered access on a non-UAV texture.");
            }
            if ((Access.mState &
                 (rhi::EArdaRHIResourceState::RenderTarget |
                  rhi::EArdaRHIResourceState::DepthWrite |
                  rhi::EArdaRHIResourceState::DepthRead)) !=
                    rhi::EArdaRHIResourceState::Unknown &&
                !rhi::HasAnyFlags(Desc.mUsage,
                    rhi::EArdaRHITextureUsage::RenderTarget | rhi::EArdaRHITextureUsage::DepthStencil))
            {
                ReportPassError(
                    Pass,
                    "uses an attachment state on a non-render-target texture.");
            }
        }

        /**
         * Validates one pass buffer access against the graph and buffer descriptor.
         *
         * The handle and state must be valid, the resolved byte range must be
         * nonempty and in bounds, and unordered access requires UAV-capable
         * backing. Buffer state tracking remains whole-resource after this check.
         */
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
            const rhi::FArdaRHIBufferDesc& Desc = Buffer->GetDesc();
            const rhi::FArdaRHIBufferRange Resolved = Access.mRange.Resolve(Desc);
            const bool bWholeRemainingBuffer =
                Access.mRange.mByteSize == rhi::ArdaRHIWholeBuffer;
            const bool bRequestedRangeOverflows =
                Access.mRange.mByteOffset > Desc.mByteSize ||
                (!bWholeRemainingBuffer &&
                 Access.mRange.mByteOffset <= Desc.mByteSize &&
                 Access.mRange.mByteSize >
                     Desc.mByteSize - Access.mRange.mByteOffset);
            if (Resolved.mByteSize == 0 ||
                Resolved.mByteOffset > Desc.mByteSize ||
                Resolved.mByteSize > Desc.mByteSize - Resolved.mByteOffset ||
                bRequestedRangeOverflows)
            {
                ReportPassError(Pass, "declares an invalid buffer range.");
            }
            if ((Access.mState & rhi::EArdaRHIResourceState::UnorderedAccess) !=
                    rhi::EArdaRHIResourceState::Unknown &&
                !rhi::HasAnyFlags(Desc.mUsage, rhi::EArdaRHIBufferUsage::UnorderedAccess))
            {
                ReportPassError(Pass, "uses unordered access on a non-UAV buffer.");
            }
        }

        void ValidateAccelStructAccess(
            const FARDGBuilder::FImpl& Graph,
            const FARDGPass& Pass,
            const FARDGPassAccelStructState& Access)
        {
            if (Graph.mAccelStructs.TryGet(Access.mAccelStruct) == nullptr)
            {
                ReportPassError(
                    Pass, "references an invalid acceleration-structure handle.");
            }
            const auto Allowed =
                rhi::EArdaRHIResourceState::AccelStructRead |
                rhi::EArdaRHIResourceState::AccelStructWrite;
            if (Access.mState == rhi::EArdaRHIResourceState::Unknown ||
                static_cast<uint32_t>(Access.mState & Allowed) !=
                    static_cast<uint32_t>(Access.mState) ||
                HasAllFlags(Pass.GetFlags(), EARDGPassFlags::Copy))
            {
                ReportPassError(
                    Pass, "declares an invalid acceleration-structure state.");
            }
        }

        /**
         * Replays registration order to reject graph-created reads before production.
         *
         * External resources start produced. Texture production is tracked per
         * mip/slice, while buffers use one whole-resource bit. Writes are first
         * collected for a pass, reads may then rely on earlier production or a
         * same-pass write, and finally those writes are committed globally.
         * Sentinels are ignored and no graph state is mutated.
         */
        void ValidateProducedBeforeRead(const FARDGBuilder::FImpl& Graph)
        {
            eastl::vector<eastl::vector<bool>> ProducedTextures;
            ProducedTextures.reserve(Graph.mTextures.GetCount());
            for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
            {
                const rhi::FArdaRHITextureDesc& Desc = Texture->GetDesc();
                ProducedTextures.emplace_back(
                    static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                    Texture->IsExternal());
            }
            eastl::vector<bool> ProducedBuffers(Graph.mBuffers.GetCount(), false);
            for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
            {
                ProducedBuffers[Buffer->GetHandle().GetIndex()] =
                    Buffer->IsExternal();
            }
            eastl::vector<bool> ProducedAccelStructs(
                Graph.mAccelStructs.GetCount(), false);
            for (const FARDGAccelStruct* AccelStruct :
                 Graph.mAccelStructs.GetEntries())
            {
                ProducedAccelStructs[AccelStruct->GetHandle().GetIndex()] =
                    AccelStruct->IsExternal();
            }

            for (const FARDGPass* Pass : Graph.mPasses.GetEntries())
            {
                if (Pass->GetState().mbSentinel)
                {
                    continue;
                }

                // Gather writes first so a pass may initialize and read the same
                // declared state unit without exposing it to earlier passes.
                eastl::vector<eastl::vector<bool>> PassTextureWrites(
                    Graph.mTextures.GetCount());
                for (const FARDGPassTextureState& Access :
                     Pass->GetState().mTextureStates)
                {
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Access.mTexture);
                    const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                    auto& Writes = PassTextureWrites[Access.mTexture.GetIndex()];
                    if (Writes.empty())
                    {
                        Writes.resize(
                            static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                            false);
                    }
                    if (!Access.mbWrite)
                    {
                        continue;
                    }
                    const auto Range = Access.mSubresources.Resolve(Desc);
                    for (uint32_t Slice = Range.mBaseArraySlice;
                         Slice < Range.mBaseArraySlice + Range.mArraySliceCount;
                         ++Slice)
                    {
                        for (uint32_t Mip = Range.mBaseMipLevel;
                             Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                             ++Mip)
                        {
                            Writes[static_cast<size_t>(Slice) * Desc.mMipLevels + Mip] =
                                true;
                        }
                    }
                }

                bool bPassWritesBuffer = false;
                eastl::unordered_set<uint32_t> PassBufferWrites;
                for (const FARDGPassBufferState& Access :
                     Pass->GetState().mBufferStates)
                {
                    if (Access.mbWrite)
                    {
                        PassBufferWrites.insert(Access.mBuffer.GetIndex());
                        bPassWritesBuffer = true;
                    }
                }
                eastl::unordered_set<uint32_t> PassAccelStructWrites;
                for (const FARDGPassAccelStructState& Access :
                     Pass->GetState().mAccelStructStates)
                {
                    if (Access.mbWrite)
                    {
                        PassAccelStructWrites.insert(
                            Access.mAccelStruct.GetIndex());
                    }
                }

                // Validate reads against prior production or the pass-local write set.
                for (const FARDGPassTextureState& Access :
                     Pass->GetState().mTextureStates)
                {
                    if (Access.mbWrite)
                    {
                        continue;
                    }
                    const FARDGTexture& Texture =
                        Graph.mTextures.Get(Access.mTexture);
                    const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                    const auto Range = Access.mSubresources.Resolve(Desc);
                    for (uint32_t Slice = Range.mBaseArraySlice;
                         Slice < Range.mBaseArraySlice + Range.mArraySliceCount;
                         ++Slice)
                    {
                        for (uint32_t Mip = Range.mBaseMipLevel;
                             Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                             ++Mip)
                        {
                            const size_t Index =
                                static_cast<size_t>(Slice) * Desc.mMipLevels + Mip;
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
                for (const FARDGPassAccelStructState& Access :
                     Pass->GetState().mAccelStructStates)
                {
                    const uint32_t Index = Access.mAccelStruct.GetIndex();
                    if (!Access.mbWrite && !ProducedAccelStructs[Index] &&
                        PassAccelStructWrites.find(Index) ==
                            PassAccelStructWrites.end())
                    {
                        ReportPassError(
                            *Pass,
                            "reads an acceleration structure before it is produced.");
                    }
                }

                // Publish this pass's writes only after all of its reads are checked.
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
                for (uint32_t Index : PassAccelStructWrites)
                {
                    ProducedAccelStructs[Index] = true;
                }
            }
        }
    }

    /**
     * Validates the complete build-time representation before compiler mutation.
     *
     * Resource loops enforce known flags, ownership/backing consistency, and
     * legal entry/exit states. Pass loops validate every declared access,
     * including work later eligible for culling. Extraction records must refer
     * to unique registered resources and unique output addresses. A final
     * production replay proves graph-created reads have an initialization path.
     */
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
                (Texture->GetInitialState() != rhi::EArdaRHIResourceState::Unknown &&
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
                (Buffer->GetInitialState() != rhi::EArdaRHIResourceState::Unknown &&
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
        for (const FARDGAccelStruct* AccelStruct :
             Graph.mAccelStructs.GetEntries())
        {
            const uint8_t Flags = static_cast<uint8_t>(AccelStruct->GetFlags());
            if ((Flags & ~KnownResourceFlags) != 0 ||
                (AccelStruct->IsExternal() !=
                 static_cast<bool>(AccelStruct->GetAccelStruct())) ||
                HasAllFlags(AccelStruct->GetFlags(), EARDGResourceFlags::Transient))
            {
                ARDA_CHECK_MSG(
                    "A render-graph acceleration structure has invalid ownership flags or backing.");
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
            for (const FARDGPassAccelStructState& Access :
                 Pass->GetState().mAccelStructStates)
            {
                ValidateAccelStructAccess(Graph, *Pass, Access);
            }
        }

        eastl::unordered_set<uint32_t> ExtractedTextures;
        eastl::unordered_set<const void*> TextureOutputs;
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
        eastl::unordered_set<uint32_t> ExtractedBuffers;
        eastl::unordered_set<const void*> BufferOutputs;
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

    /**
     * Independently replays compiled transitions and verifies all live contracts.
     *
     * The replay starts from declared initial states, treating Unknown as Common.
     * Every transition must begin at the tracked state before advancing it.
     * Writes require exact normalized states, reads allow a containing read-state
     * mask, and used external/extracted resources must reach their final state.
     * Texture tracking is per mip/slice; buffer tracking is whole-resource.
     */
    void FARDGValidation::ValidateTransitions(
        const FARDGBuilder::FImpl& Graph)
    {
        eastl::vector<eastl::vector<rhi::EArdaRHIResourceState>> TextureStates;
        TextureStates.reserve(Graph.mTextures.GetCount());
        for (const FARDGTexture* Texture : Graph.mTextures.GetEntries())
        {
            const rhi::FArdaRHITextureDesc& Desc = Texture->GetDesc();
            rhi::EArdaRHIResourceState Initial = Texture->GetInitialState();
            if (Initial == rhi::EArdaRHIResourceState::Unknown)
            {
                Initial = rhi::EArdaRHIResourceState::Common;
            }
            TextureStates.emplace_back(
                static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                Initial);
        }
        eastl::vector<rhi::EArdaRHIResourceState> BufferStates;
        BufferStates.reserve(Graph.mBuffers.GetCount());
        for (const FARDGBuffer* Buffer : Graph.mBuffers.GetEntries())
        {
            rhi::EArdaRHIResourceState Initial = Buffer->GetInitialState();
            BufferStates.push_back(
                Initial == rhi::EArdaRHIResourceState::Unknown
                    ? rhi::EArdaRHIResourceState::Common
                    : Initial);
        }
        eastl::vector<rhi::EArdaRHIResourceState> AccelStructStates;
        for (const FARDGAccelStruct* AccelStruct :
             Graph.mAccelStructs.GetEntries())
        {
            AccelStructStates.push_back(AccelStruct->GetInitialState());
        }

        for (FARDGPassHandle Handle : Graph.mCompileResult.mExecutionOrder)
        {
            const FARDGPass& Pass = Graph.mPasses.Get(Handle);
            // Replay compiler output first, then test declarations against the
            // resulting state visible while this pass executes.
            for (const FARDGTextureTransition& Transition :
                 Pass.GetState().mTextureTransitions)
            {
                const FARDGTexture& Texture =
                    Graph.mTextures.Get(Transition.mTexture);
                const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                const auto Range =
                    Transition.mSubresources.Resolve(Desc);
                for (uint32_t Slice = Range.mBaseArraySlice;
                     Slice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++Slice)
                {
                    for (uint32_t Mip = Range.mBaseMipLevel;
                         Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++Mip)
                    {
                        rhi::EArdaRHIResourceState& Current =
                            TextureStates[Transition.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mMipLevels +
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
                rhi::EArdaRHIResourceState& Current =
                    BufferStates[Transition.mBuffer.GetIndex()];
                if (Current != Transition.mStateBefore)
                {
                    ReportPassError(
                        Pass,
                        "has a discontinuous compiled buffer transition.");
                }
                Current = Transition.mStateAfter;
            }
            for (const FARDGAccelStructTransition& Transition :
                 Pass.GetState().mAccelStructTransitions)
            {
                auto& Current =
                    AccelStructStates[Transition.mAccelStruct.GetIndex()];
                if (Current != Transition.mStateBefore)
                {
                    ReportPassError(
                        Pass,
                        "has a discontinuous compiled acceleration-structure transition.");
                }
                Current = Transition.mStateAfter;
            }

            for (const FARDGPassTextureState& Access :
                 Pass.GetState().mTextureStates)
            {
                const FARDGTexture& Texture =
                    Graph.mTextures.Get(Access.mTexture);
                const rhi::FArdaRHITextureDesc& Desc = Texture.GetDesc();
                const auto Range =
                    Access.mSubresources.Resolve(Desc);
                for (uint32_t Slice = Range.mBaseArraySlice;
                     Slice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++Slice)
                {
                    for (uint32_t Mip = Range.mBaseMipLevel;
                         Mip < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++Mip)
                    {
                        const rhi::EArdaRHIResourceState Current =
                            TextureStates[Access.mTexture.GetIndex()]
                                         [static_cast<size_t>(Slice) *
                                              Desc.mMipLevels +
                                          Mip];
                        const rhi::EArdaRHIResourceState Required =
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
                const rhi::EArdaRHIResourceState Current =
                    BufferStates[Access.mBuffer.GetIndex()];
                const rhi::EArdaRHIResourceState Required =
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
            for (const FARDGPassAccelStructState& Access :
                 Pass.GetState().mAccelStructStates)
            {
                const auto Current =
                    AccelStructStates[Access.mAccelStruct.GetIndex()];
                const auto Required = NormalizeStateForPipeline(
                    Access.mState, Pass.GetState().mPipeline);
                if (Access.mbWrite ? Current != Required :
                    (Current & Required) != Required)
                {
                    ReportPassError(
                        Pass,
                        "has an unsatisfied compiled acceleration-structure state.");
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
            if (eastl::any_of(
                    States.begin(),
                    States.end(),
                    // One final state contract applies to every texture cell.
                    [Texture](rhi::EArdaRHIResourceState State)
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
        for (const FARDGAccelStruct* AccelStruct :
             Graph.mAccelStructs.GetEntries())
        {
            if (!AccelStruct->IsExternal() ||
                !AccelStruct->GetFirstUse().IsValid()) continue;
            if (AccelStructStates[AccelStruct->GetHandle().GetIndex()] !=
                AccelStruct->GetFinalState())
            {
                ARDA_CHECK_MSG(
                    "A compiled acceleration structure does not reach its graph-exit state.");
            }
        }
    }
}
