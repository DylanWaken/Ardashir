#pragma once

#include "ArdaRenderGraphDefinitions.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/functional.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include <nvrhi/nvrhi.h>

namespace arda::render_graph
{
    class FARDGParameterMetadata;
    class FARDGBuilder;
    class FARDGTexture;
    class FARDGBuffer;
    class FARDGTextureSRV;
    class FARDGTextureUAV;
    class FARDGBufferSRV;
    class FARDGBufferUAV;
    class FARDGUniformBuffer;

    /** Records one texture-state requirement contributed by a pass. */
    struct FARDGPassTextureState
    {
        /** The logical texture whose state is required. */
        FARDGTextureHandle mTexture;

        /** The affected texture subresources. */
        nvrhi::TextureSubresourceSet mSubresources = nvrhi::AllSubresources;

        /** The NVRHI state required while the pass executes. */
        nvrhi::ResourceStates mState = nvrhi::ResourceStates::Unknown;

        /** Whether the state permits the pass to modify the resource. */
        bool mbWrite = false;
    };

    /** Records one buffer-state requirement contributed by a pass. */
    struct FARDGPassBufferState
    {
        /** The logical buffer whose state is required. */
        FARDGBufferHandle mBuffer;

        /** The affected byte range. */
        nvrhi::BufferRange mRange = nvrhi::EntireBuffer;

        /** The NVRHI state required while the pass executes. */
        nvrhi::ResourceStates mState = nvrhi::ResourceStates::Unknown;

        /** Whether the state permits the pass to modify the resource. */
        bool mbWrite = false;
    };

    /** Describes one compiled texture transition before a pass executes. */
    struct FARDGTextureTransition
    {
        /** The logical texture being transitioned. */
        FARDGTextureHandle mTexture;

        /** The texture subresources covered by the transition. */
        nvrhi::TextureSubresourceSet mSubresources = nvrhi::AllSubresources;

        /** The state known before the transition. */
        nvrhi::ResourceStates mStateBefore = nvrhi::ResourceStates::Unknown;

        /** The state required after the transition. */
        nvrhi::ResourceStates mStateAfter = nvrhi::ResourceStates::Unknown;

        /** Whether equal UAV states still require an ordering barrier. */
        bool mbUAVBarrier = false;

        /** Whether debug validation requests an ordering barrier for equal states. */
        bool mbForceBarrier = false;
    };

    /** Describes one compiled whole-buffer transition before a pass executes. */
    struct FARDGBufferTransition
    {
        /** The logical buffer being transitioned. */
        FARDGBufferHandle mBuffer;

        /** The state known before the transition. */
        nvrhi::ResourceStates mStateBefore = nvrhi::ResourceStates::Unknown;

        /** The state required after the transition. */
        nvrhi::ResourceStates mStateAfter = nvrhi::ResourceStates::Unknown;

        /** Whether equal UAV states still require an ordering barrier. */
        bool mbUAVBarrier = false;

        /** Whether debug validation requests an ordering barrier for equal states. */
        bool mbForceBarrier = false;
    };

    /** Identifies raster attachments used for compatibility grouping. */
    struct FARDGRasterBindingSignature
    {
        /** Color attachment handles indexed by render-target slot. */
        eastl::array<FARDGTextureHandle, nvrhi::c_MaxRenderTargets> mColor;

        /** Color attachment subresources indexed by render-target slot. */
        eastl::array<nvrhi::TextureSubresourceSet, nvrhi::c_MaxRenderTargets>
            mColorSubresources;

        /** The depth-stencil attachment handle. */
        FARDGTextureHandle mDepthStencil;

        /** Depth-stencil attachment subresources. */
        nvrhi::TextureSubresourceSet mDepthStencilSubresources =
            nvrhi::AllSubresources;

        /** Returns whether both signatures bind the same logical attachments. */
        friend bool operator==(
            const FARDGRasterBindingSignature& Left,
            const FARDGRasterBindingSignature& Right) noexcept
        {
            return Left.mColor == Right.mColor &&
                Left.mColorSubresources == Right.mColorSubresources &&
                Left.mDepthStencil == Right.mDepthStencil &&
                Left.mDepthStencilSubresources == Right.mDepthStencilSubresources;
        }

        /** Returns whether two signatures bind different logical attachments. */
        friend bool operator!=(
            const FARDGRasterBindingSignature& Left,
            const FARDGRasterBindingSignature& Right) noexcept
        {
            return !(Left == Right);
        }
    };

    /** Stores graph state collected for a pass before compilation. */
    struct FARDGPassState
    {
        /** Handles of passes that must execute before this pass. */
        eastl::vector<FARDGPassHandle> mProducers;

        /** Handles of passes that directly consume this pass. */
        eastl::vector<FARDGPassHandle> mConsumers;

        /** Ordering-only predecessors that do not affect culling reachability. */
        eastl::vector<FARDGPassHandle> mSynchronizationProducers;

        /** Ordering-only successors that do not affect culling reachability. */
        eastl::vector<FARDGPassHandle> mSynchronizationConsumers;

        /** Texture-state requirements derived from parameter metadata. */
        eastl::vector<FARDGPassTextureState> mTextureStates;

        /** Buffer-state requirements derived from parameter metadata. */
        eastl::vector<FARDGPassBufferState> mBufferStates;

        /** Uniform buffers declared through parameter metadata. */
        eastl::vector<FARDGUniformBufferHandle> mUniformBuffers;

        /** Logical views declared directly through parameter metadata. */
        eastl::vector<FARDGViewHandle> mViews;

        /** Texture transitions lowered by graph compilation. */
        eastl::vector<FARDGTextureTransition> mTextureTransitions;

        /** Whole-buffer transitions lowered by graph compilation. */
        eastl::vector<FARDGBufferTransition> mBufferTransitions;

        /** The pipeline selected from the pass flags or by later compilation. */
        EARDGPipeline mPipeline = EARDGPipeline::Graphics;

        /** Nearest graphics synchronization point before an async-compute pass. */
        FARDGPassHandle mAsyncFork;

        /** Nearest graphics synchronization point after an async-compute pass. */
        FARDGPassHandle mAsyncJoin;

        /** Raster compatibility-group index, or UINT32_MAX when not grouped. */
        uint32_t mRasterGroup = eastl::numeric_limits<uint32_t>::max();

        /** Logical attachments used to form raster compatibility groups. */
        FARDGRasterBindingSignature mRasterBindings;

        /** Whether graph compilation has removed this pass. */
        bool mbCulled = false;

        /** Whether this is a synthetic graph-boundary pass. */
        bool mbSentinel = false;
    };

    /** Supplies the active graph and NVRHI command list to a pass lambda. */
    class FARDGPassExecutionContext final
    {
    public:
        /**
         * Constructs a context for one pass recording operation.
         *
         * @param Graph The graph whose resources are materialized.
         * @param Pass The pass being recorded.
         * @param CommandList The open NVRHI command list.
         * @param Pipeline The queue pipeline selected by compilation.
         */
        FARDGPassExecutionContext(
            FARDGBuilder& Graph,
            FARDGPassHandle Pass,
            nvrhi::ICommandList& CommandList,
            EARDGPipeline Pipeline);

        /** Closes the active physical-access gate for this pass. */
        ~FARDGPassExecutionContext() noexcept;

        FARDGPassExecutionContext(const FARDGPassExecutionContext&) = delete;
        FARDGPassExecutionContext& operator=(const FARDGPassExecutionContext&) = delete;

        /** Returns the graph being executed. */
        [[nodiscard]] FARDGBuilder& GetGraph() const noexcept
        {
            return mGraph;
        }

        /** Returns the stable handle of the pass being recorded. */
        [[nodiscard]] FARDGPassHandle GetPass() const noexcept
        {
            return mPass;
        }

        /** Returns a declared texture's physical handle during this pass. */
        [[nodiscard]] nvrhi::ITexture* GetTexture(FARDGTexture* Texture) const;

        /** Returns a declared texture SRV's parent physical texture. */
        [[nodiscard]] nvrhi::ITexture* GetTexture(FARDGTextureSRV* View) const;

        /** Returns a declared texture UAV's parent physical texture. */
        [[nodiscard]] nvrhi::ITexture* GetTexture(FARDGTextureUAV* View) const;

        /** Returns a declared buffer's physical handle during this pass. */
        [[nodiscard]] nvrhi::IBuffer* GetBuffer(FARDGBuffer* Buffer) const;

        /** Returns a declared buffer SRV's parent physical buffer. */
        [[nodiscard]] nvrhi::IBuffer* GetBuffer(FARDGBufferSRV* View) const;

        /** Returns a declared buffer UAV's parent physical buffer. */
        [[nodiscard]] nvrhi::IBuffer* GetBuffer(FARDGBufferUAV* View) const;

        /** Returns a declared uniform buffer's physical constant buffer. */
        [[nodiscard]] nvrhi::IBuffer* GetUniformBuffer(
            FARDGUniformBuffer* UniformBuffer) const;

        /**
         * Creates an NVRHI binding set from this pass's parameter descriptors.
         *
         * Shader-resource, unordered-access, and uniform-buffer parameters are
         * matched to the supplied layout by register class and declaration order.
         * Direct access and render-target parameters are intentionally not bindings.
         */
        [[nodiscard]] nvrhi::BindingSetHandle CreateBindingSet(
            nvrhi::IBindingLayout* BindingLayout) const;

        /**
         * The command list currently recording the pass.
         *
         * Physical graph resources should be obtained through the validated
         * getters above. Calls made with independently retained NVRHI handles
         * cannot be proven against parameter declarations.
         */
        nvrhi::ICommandList& mCommandList;

        /** The command pipeline selected by graph compilation. */
        EARDGPipeline mPipeline = EARDGPipeline::Graphics;

    private:
        FARDGBuilder& mGraph;
        FARDGPassHandle mPass;
        bool mbAccessGateOpen = false;
    };

    /** Type-erased execution body stored by a lambda pass. */
    using FARDGPassExecuteFunction = eastl::function<void(FARDGPassExecutionContext&)>;

    /** Base record for a registered render-graph pass. */
    class FARDGPass
    {
    public:
        /**
         * Constructs a pass record without implementing execution dispatch.
         *
         * @param Handle The stable pass-registry handle.
         * @param Name The diagnostic pass name.
         * @param Flags The pass behavior and pipeline flags.
         * @param Parameters A non-owning pointer to immutable parameter storage.
         * @param ParameterMetadata Static metadata describing Parameters.
         */
        FARDGPass(
            FARDGPassHandle Handle,
            eastl::string Name,
            EARDGPassFlags Flags,
            const void* Parameters = nullptr,
            const FARDGParameterMetadata* ParameterMetadata = nullptr)
            : mHandle(Handle)
            , mName(eastl::move(Name))
            , mFlags(Flags)
            , mParameters(Parameters)
            , mParameterMetadata(ParameterMetadata)
        {
            if (HasAllFlags(Flags, EARDGPassFlags::AsyncCompute))
            {
                mState.mPipeline = EARDGPipeline::AsyncCompute;
            }
            else if (HasAllFlags(Flags, EARDGPassFlags::Copy))
            {
                mState.mPipeline = EARDGPipeline::Copy;
            }
        }

        /** Destroys the pass record. */
        virtual ~FARDGPass() = default;

        /** Returns the stable pass-registry handle. */
        [[nodiscard]] FARDGPassHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the diagnostic pass name. */
        [[nodiscard]] const eastl::string& GetName() const noexcept
        {
            return mName;
        }

        /** Returns the pass behavior and pipeline flags. */
        [[nodiscard]] EARDGPassFlags GetFlags() const noexcept
        {
            return mFlags;
        }

        /** Returns the immutable parameter-storage pointer supplied at registration. */
        [[nodiscard]] const void* GetParameters() const noexcept
        {
            return mParameters;
        }

        /** Returns the static metadata describing the parameter storage. */
        [[nodiscard]] const FARDGParameterMetadata* GetParameterMetadata() const noexcept
        {
            return mParameterMetadata;
        }

        /** Returns graph state collected for this pass. */
        [[nodiscard]] const FARDGPassState& GetState() const noexcept
        {
            return mState;
        }

        /** Returns mutable graph state for setup and compilation support. */
        [[nodiscard]] FARDGPassState& GetState() noexcept
        {
            return mState;
        }

        /**
         * Adds an incoming producer edge if it is valid and not already present.
         *
         * @param Producer The pass that must execute first.
         */
        void AddProducer(FARDGPassHandle Producer)
        {
            if (!Producer.IsValid())
            {
                return;
            }

            const auto Existing = eastl::find(
                mState.mProducers.begin(),
                mState.mProducers.end(),
                Producer);
            if (Existing == mState.mProducers.end())
            {
                mState.mProducers.push_back(Producer);
            }
        }

        /** Adds an outgoing consumer edge if it is valid and not already present. */
        void AddConsumer(FARDGPassHandle Consumer)
        {
            if (!Consumer.IsValid())
            {
                return;
            }

            const auto Existing = eastl::find(
                mState.mConsumers.begin(),
                mState.mConsumers.end(),
                Consumer);
            if (Existing == mState.mConsumers.end())
            {
                mState.mConsumers.push_back(Consumer);
            }
        }

        /** Adds an ordering-only predecessor without making it a culling edge. */
        void AddSynchronizationProducer(FARDGPassHandle Producer)
        {
            if (!Producer.IsValid())
            {
                return;
            }
            const auto Existing = eastl::find(
                mState.mSynchronizationProducers.begin(),
                mState.mSynchronizationProducers.end(),
                Producer);
            if (Existing == mState.mSynchronizationProducers.end())
            {
                mState.mSynchronizationProducers.push_back(Producer);
            }
        }

        /** Adds an ordering-only successor without making it a culling edge. */
        void AddSynchronizationConsumer(FARDGPassHandle Consumer)
        {
            if (!Consumer.IsValid())
            {
                return;
            }
            const auto Existing = eastl::find(
                mState.mSynchronizationConsumers.begin(),
                mState.mSynchronizationConsumers.end(),
                Consumer);
            if (Existing == mState.mSynchronizationConsumers.end())
            {
                mState.mSynchronizationConsumers.push_back(Consumer);
            }
        }

        /** Invokes this pass's execution body when it has one. */
        virtual void Execute(FARDGPassExecutionContext&)
        {
        }

        /** Adds one texture-state requirement to this pass. */
        void AddTextureState(FARDGPassTextureState State)
        {
            mState.mTextureStates.push_back(eastl::move(State));
        }

        /** Adds one buffer-state requirement to this pass. */
        void AddBufferState(FARDGPassBufferState State)
        {
            mState.mBufferStates.push_back(eastl::move(State));
        }

    private:
        FARDGPassHandle mHandle;
        eastl::string mName;
        EARDGPassFlags mFlags = EARDGPassFlags::None;
        const void* mParameters = nullptr;
        const FARDGParameterMetadata* mParameterMetadata = nullptr;
        FARDGPassState mState;
    };

    /** A registered pass whose execution body is a type-erased user lambda. */
    class FARDGLambdaPass final : public FARDGPass
    {
    public:
        /**
         * Constructs a lambda pass.
         *
         * @param Handle The stable pass-registry handle.
         * @param Name The diagnostic pass name.
         * @param Flags The pass behavior and requested pipeline.
         * @param Parameters Immutable graph-arena parameter storage.
         * @param ParameterMetadata Static metadata describing Parameters.
         * @param ExecuteFunction The body invoked during command recording.
         */
        FARDGLambdaPass(
            FARDGPassHandle Handle,
            eastl::string Name,
            EARDGPassFlags Flags,
            const void* Parameters,
            const FARDGParameterMetadata* ParameterMetadata,
            FARDGPassExecuteFunction ExecuteFunction)
            : FARDGPass(
                  Handle,
                  eastl::move(Name),
                  Flags,
                  Parameters,
                  ParameterMetadata)
            , mExecuteFunction(eastl::move(ExecuteFunction))
        {
        }

        /** Invokes the stored pass body. */
        void Execute(FARDGPassExecutionContext& Context) override
        {
            if (mExecuteFunction)
            {
                mExecuteFunction(Context);
            }
        }

    private:
        FARDGPassExecuteFunction mExecuteFunction;
    };

    /** Synthetic pass used to anchor graph-entry and graph-exit dependencies. */
    class FARDGSentinelPass final : public FARDGPass
    {
    public:
        /** Constructs a named synthetic boundary pass. */
        FARDGSentinelPass(FARDGPassHandle Handle, eastl::string Name)
            : FARDGPass(
                  Handle,
                  eastl::move(Name),
                  EARDGPassFlags::NeverCull)
        {
            GetState().mbSentinel = true;
        }
    };
}
