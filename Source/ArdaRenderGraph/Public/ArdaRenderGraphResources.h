#pragma once

#include "ArdaRenderGraphDefinitions.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <cstddef>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda::render_graph
{
    class FARDGParameterMetadata;

    /** Base record shared by every logical render-graph resource. */
    class FARDGResource
    {
    public:
        /**
         * Constructs a named logical resource.
         *
         * @param Name The diagnostic resource name.
         * @param Type The concrete resource kind.
         */
        FARDGResource(eastl::string Name, EARDGResourceType Type)
            : mName(eastl::move(Name))
            , mType(Type)
        {
        }

        /** Destroys the logical resource record. */
        virtual ~FARDGResource() = default;

        /** Returns the diagnostic resource name. */
        [[nodiscard]] const eastl::string& GetName() const noexcept
        {
            return mName;
        }

        /** Returns the concrete resource kind. */
        [[nodiscard]] EARDGResourceType GetType() const noexcept
        {
            return mType;
        }

    private:
        eastl::string mName;
        EARDGResourceType mType;
    };

    /** Base record for logical resources that own or import GPU memory. */
    class FARDGViewableResource : public FARDGResource
    {
    public:
        /**
         * Constructs a viewable logical resource.
         *
         * @param Name The diagnostic resource name.
         * @param Type The concrete resource kind.
         * @param InitialState The known RHI state on graph entry.
         * @param Flags The graph ownership flags.
         */
        FARDGViewableResource(
            eastl::string Name,
            EARDGResourceType Type,
            rhi::EArdaRHIResourceState InitialState,
            EARDGResourceFlags Flags)
            : FARDGResource(eastl::move(Name), Type)
            , mInitialState(InitialState)
            , mFinalState(InitialState)
            , mFlags(Flags)
        {
        }

        /** Returns the known RHI state on graph entry. */
        [[nodiscard]] rhi::EArdaRHIResourceState GetInitialState() const noexcept
        {
            return mInitialState;
        }

        /** Returns the requested RHI state on graph exit. */
        [[nodiscard]] rhi::EArdaRHIResourceState GetFinalState() const noexcept
        {
            return mFinalState;
        }

        /** Sets the requested RHI state on graph exit. */
        void SetFinalState(rhi::EArdaRHIResourceState FinalState) noexcept
        {
            mFinalState = FinalState;
        }

        /** Returns the ownership and lifetime flags. */
        [[nodiscard]] EARDGResourceFlags GetFlags() const noexcept
        {
            return mFlags;
        }

        /** Returns whether this record imports an externally owned RHI resource. */
        [[nodiscard]] bool IsExternal() const noexcept
        {
            return HasAllFlags(mFlags, EARDGResourceFlags::External);
        }

        /** Returns whether this resource is marked for extraction from the graph. */
        [[nodiscard]] bool IsExtracted() const noexcept
        {
            return HasAllFlags(mFlags, EARDGResourceFlags::Extracted);
        }

        /** Adds graph ownership or lifetime flags to this resource. */
        void AddFlags(EARDGResourceFlags Flags) noexcept
        {
            mFlags |= Flags;
        }

        /** Returns the latest pass that wrote this logical resource. */
        [[nodiscard]] FARDGPassHandle GetLastProducer() const noexcept
        {
            return mLastProducer;
        }

        /** Records the latest pass that wrote this logical resource. */
        void SetLastProducer(FARDGPassHandle Producer) noexcept
        {
            mLastProducer = Producer;
        }

        /** Returns readers since the latest write, in registration order. */
        [[nodiscard]] const eastl::vector<FARDGPassHandle>& GetReaders() const noexcept
        {
            return mReaders;
        }

        /** Records a reader since the latest write. */
        void AddReader(FARDGPassHandle Reader)
        {
            if (Reader.IsValid() &&
                eastl::find(mReaders.begin(), mReaders.end(), Reader) ==
                    mReaders.end())
            {
                mReaders.push_back(Reader);
            }
        }

        /** Clears readers superseded by a write. */
        void ClearReaders() noexcept
        {
            mReaders.clear();
        }

        /** Includes a pass in this logical resource's live-use interval. */
        void MarkUsed(FARDGPassHandle Pass) noexcept
        {
            if (!Pass.IsValid())
            {
                return;
            }
            if (!mFirstUse.IsValid() || Pass < mFirstUse)
            {
                mFirstUse = Pass;
            }
            if (!mLastUse.IsValid() || mLastUse < Pass)
            {
                mLastUse = Pass;
            }
        }

        /** Returns the first registered pass that refers to this resource. */
        [[nodiscard]] FARDGPassHandle GetFirstUse() const noexcept
        {
            return mFirstUse;
        }

        /** Returns the last registered pass that refers to this resource. */
        [[nodiscard]] FARDGPassHandle GetLastUse() const noexcept
        {
            return mLastUse;
        }

        /** Clears the compiled live-use interval before culling is resolved. */
        void ResetUsage() noexcept
        {
            mFirstUse = FARDGPassHandle();
            mLastUse = FARDGPassHandle();
        }

    private:
        rhi::EArdaRHIResourceState mInitialState;
        rhi::EArdaRHIResourceState mFinalState;
        EARDGResourceFlags mFlags;
        FARDGPassHandle mLastProducer;
        eastl::vector<FARDGPassHandle> mReaders;
        FARDGPassHandle mFirstUse;
        FARDGPassHandle mLastUse;
    };

    /** A logical texture record with deferred or imported RHI backing. */
    class FARDGTexture final : public FARDGViewableResource
    {
    public:
        /**
         * Constructs a logical texture.
         *
         * @param Handle The stable texture-registry handle.
         * @param Desc The RHI texture descriptor.
         * @param Flags The graph ownership flags.
         * @param Texture Optional imported physical texture.
         */
        FARDGTexture(
            FARDGTextureHandle Handle,
            rhi::FArdaRHITextureDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient,
            rhi::FArdaRHITextureRef Texture = nullptr)
            : FARDGViewableResource(
                  Desc.mDebugName,
                  EARDGResourceType::Texture,
                  Desc.mInitialState,
                  Flags)
            , mHandle(Handle)
            , mDesc(eastl::move(Desc))
            , mTexture(eastl::move(Texture))
        {
        }

        /** Returns the stable texture-registry handle. */
        [[nodiscard]] FARDGTextureHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the RHI descriptor used to allocate or validate the texture. */
        [[nodiscard]] const rhi::FArdaRHITextureDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

        /**
         * Returns the physical texture, or an empty handle before materialization.
         * Pass code should use FARDGPassExecutionContext::GetTexture so the
         * graph can validate that the resource was declared.
         */
        [[nodiscard]] const rhi::FArdaRHITextureRef& GetTexture() const noexcept
        {
            return mTexture;
        }

        /** Binds a materialized or imported RHI texture to this logical record. */
        void BindTexture(rhi::FArdaRHITextureRef Texture) noexcept
        {
            mTexture = eastl::move(Texture);
        }

    private:
        FARDGTextureHandle mHandle;
        rhi::FArdaRHITextureDesc mDesc;
        rhi::FArdaRHITextureRef mTexture;
    };

    /** A logical buffer record with deferred or imported RHI backing. */
    class FARDGBuffer final : public FARDGViewableResource
    {
    public:
        /**
         * Constructs a logical buffer.
         *
         * @param Handle The stable buffer-registry handle.
         * @param Desc The RHI buffer descriptor.
         * @param Flags The graph ownership flags.
         * @param Buffer Optional imported physical buffer.
         */
        FARDGBuffer(
            FARDGBufferHandle Handle,
            rhi::FArdaRHIBufferDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient,
            rhi::FArdaRHIBufferRef Buffer = nullptr)
            : FARDGViewableResource(
                  Desc.mDebugName,
                  EARDGResourceType::Buffer,
                  Desc.mInitialState,
                  Flags)
            , mHandle(Handle)
            , mDesc(eastl::move(Desc))
            , mBuffer(eastl::move(Buffer))
        {
        }

        /** Returns the stable buffer-registry handle. */
        [[nodiscard]] FARDGBufferHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the RHI descriptor used to allocate or validate the buffer. */
        [[nodiscard]] const rhi::FArdaRHIBufferDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

        /**
         * Returns the physical buffer, or an empty handle before materialization.
         * Pass code should use FARDGPassExecutionContext::GetBuffer so the
         * graph can validate that the resource was declared.
         */
        [[nodiscard]] const rhi::FArdaRHIBufferRef& GetBuffer() const noexcept
        {
            return mBuffer;
        }

        /** Binds a materialized or imported RHI buffer to this logical record. */
        void BindBuffer(rhi::FArdaRHIBufferRef Buffer) noexcept
        {
            mBuffer = eastl::move(Buffer);
        }

    private:
        FARDGBufferHandle mHandle;
        rhi::FArdaRHIBufferDesc mDesc;
        rhi::FArdaRHIBufferRef mBuffer;
    };

    /** A logical acceleration structure with deferred or imported RHI backing. */
    class FARDGAccelStruct final : public FARDGViewableResource
    {
    public:
        FARDGAccelStruct(
            FARDGAccelStructHandle Handle,
            rhi::FArdaRHIAccelStructDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::None,
            rhi::FArdaRHIAccelStructRef AccelStruct = nullptr,
            rhi::EArdaRHIResourceState InitialState =
                rhi::EArdaRHIResourceState::AccelStructWrite)
            : FARDGViewableResource(
                  Desc.mDebugName,
                  EARDGResourceType::AccelStruct,
                  InitialState,
                  Flags)
            , mHandle(Handle)
            , mDesc(eastl::move(Desc))
            , mAccelStruct(eastl::move(AccelStruct))
        {
        }

        [[nodiscard]] FARDGAccelStructHandle GetHandle() const noexcept { return mHandle; }
        [[nodiscard]] const rhi::FArdaRHIAccelStructDesc& GetDesc() const noexcept { return mDesc; }
        [[nodiscard]] const rhi::FArdaRHIAccelStructRef& GetAccelStruct() const noexcept { return mAccelStruct; }
        void BindAccelStruct(rhi::FArdaRHIAccelStructRef AccelStruct) noexcept
        {
            mAccelStruct = eastl::move(AccelStruct);
        }

    private:
        FARDGAccelStructHandle mHandle;
        rhi::FArdaRHIAccelStructDesc mDesc;
        rhi::FArdaRHIAccelStructRef mAccelStruct;
    };

    /** Describes a logical texture SRV or UAV. */
    struct FARDGTextureViewDesc
    {
        /** The parent logical texture. */
        FARDGTextureHandle mTexture;

        /** The texture subresources exposed by the view. */
        rhi::FArdaRHITextureSubresourceRange mSubresources;

        /** An optional format override for the view. */
        rhi::EArdaRHIFormat mFormat = rhi::EArdaRHIFormat::Unknown;

        /** An optional texture-dimension override for the view. */
        rhi::EArdaRHITextureDimension mDimension = rhi::EArdaRHITextureDimension::Unknown;
    };

    /** Describes a logical buffer SRV or UAV. */
    struct FARDGBufferViewDesc
    {
        /** The parent logical buffer. */
        FARDGBufferHandle mBuffer;

        /** The byte range exposed by the view. */
        rhi::FArdaRHIBufferRange mRange;

        /** An optional typed-buffer format override. */
        rhi::EArdaRHIFormat mFormat = rhi::EArdaRHIFormat::Unknown;
    };

    /** Base record for a logical texture or buffer view. */
    class FARDGView : public FARDGResource
    {
    public:
        /**
         * Constructs a logical resource view.
         *
         * @param Handle The stable view-registry handle.
         * @param Name The diagnostic view name.
         * @param Type The concrete view kind.
         */
        FARDGView(FARDGViewHandle Handle, eastl::string Name, EARDGResourceType Type)
            : FARDGResource(eastl::move(Name), Type)
            , mHandle(Handle)
        {
        }

        /** Returns the stable view-registry handle. */
        [[nodiscard]] FARDGViewHandle GetHandle() const noexcept
        {
            return mHandle;
        }

    private:
        FARDGViewHandle mHandle;
    };

    /** A logical shader-resource view of a texture. */
    class FARDGTextureSRV final : public FARDGView
    {
    public:
        /** Constructs a texture SRV from its stable handle, name, and descriptor. */
        FARDGTextureSRV(FARDGViewHandle Handle, eastl::string Name, FARDGTextureViewDesc Desc)
            : FARDGView(Handle, eastl::move(Name), EARDGResourceType::TextureShaderResourceView)
            , mDesc(eastl::move(Desc))
        {
        }

        /** Returns the logical texture-view descriptor. */
        [[nodiscard]] const FARDGTextureViewDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

    private:
        FARDGTextureViewDesc mDesc;
    };

    /** A logical unordered-access view of a texture. */
    class FARDGTextureUAV final : public FARDGView
    {
    public:
        /** Constructs a texture UAV from its stable handle, name, and descriptor. */
        FARDGTextureUAV(FARDGViewHandle Handle, eastl::string Name, FARDGTextureViewDesc Desc)
            : FARDGView(Handle, eastl::move(Name), EARDGResourceType::TextureUnorderedAccessView)
            , mDesc(eastl::move(Desc))
        {
        }

        /** Returns the logical texture-view descriptor. */
        [[nodiscard]] const FARDGTextureViewDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

    private:
        FARDGTextureViewDesc mDesc;
    };

    /** A logical shader-resource view of a buffer. */
    class FARDGBufferSRV final : public FARDGView
    {
    public:
        /** Constructs a buffer SRV from its stable handle, name, and descriptor. */
        FARDGBufferSRV(FARDGViewHandle Handle, eastl::string Name, FARDGBufferViewDesc Desc)
            : FARDGView(Handle, eastl::move(Name), EARDGResourceType::BufferShaderResourceView)
            , mDesc(eastl::move(Desc))
        {
        }

        /** Returns the logical buffer-view descriptor. */
        [[nodiscard]] const FARDGBufferViewDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

    private:
        FARDGBufferViewDesc mDesc;
    };

    /** A logical unordered-access view of a buffer. */
    class FARDGBufferUAV final : public FARDGView
    {
    public:
        /** Constructs a buffer UAV from its stable handle, name, and descriptor. */
        FARDGBufferUAV(FARDGViewHandle Handle, eastl::string Name, FARDGBufferViewDesc Desc)
            : FARDGView(Handle, eastl::move(Name), EARDGResourceType::BufferUnorderedAccessView)
            , mDesc(eastl::move(Desc))
        {
        }

        /** Returns the logical buffer-view descriptor. */
        [[nodiscard]] const FARDGBufferViewDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

    private:
        FARDGBufferViewDesc mDesc;
    };

    /** A logical uniform-buffer record and its optional RHI constant-buffer backing. */
    class FARDGUniformBuffer final : public FARDGResource
    {
    public:
        /**
         * Constructs a logical uniform buffer.
         *
         * @param Handle The stable uniform-buffer registry handle.
         * @param Name The diagnostic resource name.
         * @param Desc The RHI constant-buffer descriptor.
         * @param Metadata Metadata for the immutable parameter contents.
         * @param Contents A non-owning pointer to graph-arena parameter storage.
         */
        FARDGUniformBuffer(
            FARDGUniformBufferHandle Handle,
            eastl::string Name,
            rhi::FArdaRHIBufferDesc Desc,
            const FARDGParameterMetadata* Metadata,
            const void* Contents)
            : FARDGResource(eastl::move(Name), EARDGResourceType::UniformBuffer)
            , mHandle(Handle)
            , mDesc(eastl::move(Desc))
            , mMetadata(Metadata)
            , mContents(Contents)
        {
        }

        /** Returns the stable uniform-buffer registry handle. */
        [[nodiscard]] FARDGUniformBufferHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the RHI descriptor used for physical constant-buffer allocation. */
        [[nodiscard]] const rhi::FArdaRHIBufferDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

        /** Returns the static metadata for the immutable parameter contents. */
        [[nodiscard]] const FARDGParameterMetadata* GetMetadata() const noexcept
        {
            return mMetadata;
        }

        /** Returns the non-owning pointer to immutable graph-arena parameter contents. */
        [[nodiscard]] const void* GetContents() const noexcept
        {
            return mContents;
        }

        /**
         * Returns the physical constant buffer, or an empty handle before materialization.
         * Pass code should use FARDGPassExecutionContext::GetUniformBuffer so
         * the graph can validate that the resource was declared.
         */
        [[nodiscard]] const rhi::FArdaRHIBufferRef& GetBuffer() const noexcept
        {
            return mBuffer;
        }

        /** Binds a materialized RHI constant buffer to this logical record. */
        void BindBuffer(rhi::FArdaRHIBufferRef Buffer) noexcept
        {
            mBuffer = eastl::move(Buffer);
        }

    private:
        FARDGUniformBufferHandle mHandle;
        rhi::FArdaRHIBufferDesc mDesc;
        const FARDGParameterMetadata* mMetadata = nullptr;
        const void* mContents = nullptr;
        rhi::FArdaRHIBufferRef mBuffer;
    };

    /** Declares a direct texture access that does not require a logical view. */
    struct FARDGTextureAccess
    {
        /** The logical texture being accessed. */
        FARDGTexture* mTexture = nullptr;

        /** The RHI state required by the access. */
        rhi::EArdaRHIResourceState mState = rhi::EArdaRHIResourceState::Unknown;

        /** The texture subresources covered by the access. */
        rhi::FArdaRHITextureSubresourceRange mSubresources;
    };

    /** Declares a direct buffer access that does not require a logical view. */
    struct FARDGBufferAccess
    {
        /** The logical buffer being accessed. */
        FARDGBuffer* mBuffer = nullptr;

        /** The RHI state required by the access. */
        rhi::EArdaRHIResourceState mState = rhi::EArdaRHIResourceState::Unknown;

        /** The byte range covered by the access. */
        rhi::FArdaRHIBufferRange mRange;
    };

    /** Declares direct acceleration-structure access and its required state. */
    struct FARDGAccelStructAccess
    {
        FARDGAccelStruct* mAccelStruct = nullptr;
        rhi::EArdaRHIResourceState mState = rhi::EArdaRHIResourceState::Unknown;
    };

    /** Identifies one logical texture attachment and its selected subresources. */
    struct FARDGRenderTargetBinding
    {
        /** The logical attachment texture, or null for an unused slot. */
        FARDGTexture* mTexture = nullptr;

        /** The texture subresources attached to the framebuffer. */
        rhi::FArdaRHITextureSubresourceRange mSubresources;
    };

    /** Stores color and depth attachments declared by a raster pass. */
    struct FARDGRenderTargetBindingSlots
    {
        /** The logical color attachments, indexed by render-target slot. */
        eastl::array<FARDGRenderTargetBinding, rhi::ArdaRHIMaxRenderTargets> mColor;

        /** The logical depth-stencil attachment, or an empty binding when unused. */
        FARDGRenderTargetBinding mDepthStencil;
    };

    /** A nullable reference to a logical texture. */
    using FARDGTextureRef = FARDGTexture*;

    /** A nullable reference to a logical buffer. */
    using FARDGBufferRef = FARDGBuffer*;
    using FARDGAccelStructRef = FARDGAccelStruct*;

    /** A nullable reference to a logical texture SRV. */
    using FARDGTextureSRVRef = FARDGTextureSRV*;

    /** A nullable reference to a logical texture UAV. */
    using FARDGTextureUAVRef = FARDGTextureUAV*;

    /** A nullable reference to a logical buffer SRV. */
    using FARDGBufferSRVRef = FARDGBufferSRV*;

    /** A nullable reference to a logical buffer UAV. */
    using FARDGBufferUAVRef = FARDGBufferUAV*;

    /** A nullable reference to a logical uniform buffer. */
    using FARDGUniformBufferRef = FARDGUniformBuffer*;
}
