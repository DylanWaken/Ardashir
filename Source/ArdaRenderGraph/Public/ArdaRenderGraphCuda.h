/** CUDA operand scheduling using the operand's single parameter schema. */
#pragma once
#include "ArdaRenderGraphBuilder.h"
#include "Compute/ArdaComputeOperand.h"

namespace arda
{
    /** Logical RDG resource representation of a CUDA parameter declaration. */
    struct FARDGCudaParameterDomain
    {
        template<class T> using Value = T;
        template<class T, EArdaComputeAccess Access> using Buffer = FARDGBufferAccess;
        template<class T, EArdaComputeAccess Access> using Surface = FARDGTextureAccess;
    };
    struct FArdaCudaGraphHelpers
    {
        struct MetadataVisitor
        {
            eastl::vector<FARDGParameterMember> mMembers;
            template<class T> void Value(const char* Name, size_t Offset)
            { mMembers.push_back({Name, EARDGParameterType::Value, Offset, sizeof(T), alignof(T), 1, sizeof(T)}); }
            template<class T, EArdaComputeAccess A> void Buffer(const char* Name, size_t Offset)
            { mMembers.push_back({Name, EARDGParameterType::BufferAccess, Offset, sizeof(FARDGBufferAccess), alignof(FARDGBufferAccess),
                1, sizeof(FARDGBufferAccess), EArdaRHIResourceState::UnorderedAccess, nullptr, A == EArdaComputeAccess::Read}); }
            template<class T, EArdaComputeAccess A> void Surface(const char* Name, size_t Offset, EArdaRHIFormat)
            { mMembers.push_back({Name, EARDGParameterType::TextureAccess, Offset, sizeof(FARDGTextureAccess), alignof(FARDGTextureAccess),
                1, sizeof(FARDGTextureAccess), EArdaRHIResourceState::UnorderedAccess, nullptr, A == EArdaComputeAccess::Read}); }
        };
        struct NormalizeVisitor
        {
            uint8_t* mBytes;
            FArdaRHIStatus mStatus;
            template<class T> void Value(const char*, size_t) {}
            template<class T, EArdaComputeAccess A> void Buffer(const char*, size_t Offset)
            {
                if (!mStatus) return;
                auto& P = *reinterpret_cast<FARDGBufferAccess*>(mBytes + Offset);
                P.mState = EArdaRHIResourceState::UnorderedAccess;
                if (!P.mBuffer || !P.mBuffer->GetDesc().mbCudaInterop)
                    mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "RDG CUDA buffer must be declared with CUDA sharing.");
                else mStatus = ValidateArdaCudaBuffer(P.mBuffer->GetDesc());
            }
            template<class T, EArdaComputeAccess A> void Surface(const char*, size_t Offset, EArdaRHIFormat Format)
            {
                if (!mStatus) return;
                auto& P = *reinterpret_cast<FARDGTextureAccess*>(mBytes + Offset);
                P.mState = EArdaRHIResourceState::UnorderedAccess;
                if (!P.mTexture || !P.mTexture->GetDesc().mbCudaInterop || P.mTexture->GetDesc().mFormat != Format)
                    mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "RDG CUDA surface must use its declared format and CUDA sharing.");
                else mStatus = ValidateArdaCudaTexture(P.mTexture->GetDesc());
            }
        };
        struct ResolveVisitor
        {
            const uint8_t* mGraph;
            uint8_t* mHost;
            const eastl::vector<FArdaCudaParameterMember>& mMembers;
            FARDGPassExecutionContext& mContext;
            size_t mIndex = 0;
            template<class T> void Value(const char*, size_t Offset)
            { std::memcpy(mHost + mMembers[mIndex++].mHostOffset, mGraph + Offset, sizeof(T)); }
            template<class T, EArdaComputeAccess A> void Buffer(const char*, size_t Offset)
            {
                const auto& Source = *reinterpret_cast<const FARDGBufferAccess*>(mGraph + Offset);
                auto& Destination = *reinterpret_cast<FArdaComputeBufferParameter*>(mHost + mMembers[mIndex++].mHostOffset);
                Destination = {FArdaRHIBufferRef(mContext.GetBuffer(Source.mBuffer)), Source.mRange};
            }
            template<class T, EArdaComputeAccess A> void Surface(const char*, size_t Offset, EArdaRHIFormat)
            {
                const auto& Source = *reinterpret_cast<const FARDGTextureAccess*>(mGraph + Offset);
                auto& Destination = *reinterpret_cast<FArdaComputeTextureParameter*>(mHost + mMembers[mIndex++].mHostOffset);
                Destination = {FArdaRHITextureRef(mContext.GetTexture(Source.mTexture)), Source.mSubresources};
            }
        };
    };
    /** The same named fields as Parameters, storing logical graph resources instead of physical RHI references. */
    template<class Parameters>
    struct TARDGCudaParameters : Parameters::template Rebind<FARDGCudaParameterDomain>
    {
        using FStorage = typename Parameters::template Rebind<FARDGCudaParameterDomain>;
        /** Graph metadata preserves schema read/write hazards even when CUDA requires UAV resource state. */
        static const FARDGParameterMetadata& GetStaticMetadata()
        {
            static const FARDGParameterMetadata Metadata = [] {
                FArdaCudaGraphHelpers::MetadataVisitor Visitor;
                FStorage::VisitMembers(Visitor);
                return FARDGParameterMetadata(Parameters::GetStaticMetadata().GetName(), sizeof(FStorage), alignof(FStorage), eastl::move(Visitor.mMembers));
            }();
            return Metadata;
        }
    };
    /** Adds a single-kernel operand pass, deriving accesses before allocation and resolving them at execution.
     * Retains Operand and a typed parameter copy. Runtime schema/build errors return before adding a pass;
     * physical-resource/selection/launch failures propagate through the graph's execution status.
     */
    template<class OperandType>
    TArdaRHIResult<FARDGPassHandle> AddArdaCudaPass(FARDGBuilder& Graph, eastl::string Name,
        eastl::shared_ptr<OperandType> Operand, TARDGCudaParameters<typename OperandType::FParameters> Parameters,
        EARDGPassFlags Flags = EARDGPassFlags::Compute)
    {
        using P = typename OperandType::FParameters;
        using G = TARDGCudaParameters<P>;
        if (!Operand) return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "RDG CUDA pass requires an operand.")};
        if (auto S = Operand->GetOperandSupport(); !S) return {{}, S};
        if (Graph.GetContext().mDevice != Operand->GetDevice())
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "CUDA operand belongs to another graph device.")};
        const auto Allowed = EARDGPassFlags::Compute | EARDGPassFlags::AsyncCompute | EARDGPassFlags::NeverCull |
            EARDGPassFlags::NeverParallel | EARDGPassFlags::RecordAtSubmit;
        if (Name.empty() || !HasAllFlags(Flags, EARDGPassFlags::Compute) ||
            (static_cast<uint16_t>(Flags) & ~static_cast<uint16_t>(Allowed)))
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA pass requires a compute pipeline.")};
        const auto C = Operand->GetDevice()->GetCudaCapabilities();
        if (C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
        {
            if (HasAllFlags(Flags, EARDGPassFlags::AsyncCompute))
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "D3D12 CiG cannot use the async compute queue.")};
            Flags |= EARDGPassFlags::NeverParallel | EARDGPassFlags::RecordAtSubmit;
        }
        FArdaCudaGraphHelpers::NormalizeVisitor Normalize{reinterpret_cast<uint8_t*>(static_cast<typename G::FStorage*>(&Parameters))};
        G::FStorage::VisitMembers(Normalize);
        if (!Normalize.mStatus) return {{}, Normalize.mStatus};
        auto Pass = Graph.AddPass(eastl::move(Name), &Parameters, Flags,
            [Operand = eastl::move(Operand)](FARDGPassExecutionContext& Context, const G& Values) -> FArdaRHIStatus {
                P Host;
                FArdaCudaGraphHelpers::ResolveVisitor Resolve{reinterpret_cast<const uint8_t*>(static_cast<const typename G::FStorage*>(&Values)),
                    reinterpret_cast<uint8_t*>(&Host), P::GetCudaMetadata().GetMembers(), Context};
                G::FStorage::VisitMembers(Resolve);
                return Operand->DispatchDeferred(Context.mUnsafeRawCommandList, Host);
            });
        return {Pass, {}};
    }
}
