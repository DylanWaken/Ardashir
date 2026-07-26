#pragma once

#include "ArdaRenderGraphDefinitions.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <nvrhi/nvrhi.h>

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
        FARDGResource(std::string Name, EARDGResourceType Type)
            : mName(std::move(Name))
            , mType(Type)
        {
        }

        /** Destroys the logical resource record. */
        virtual ~FARDGResource() = default;

        /** Returns the diagnostic resource name. */
        [[nodiscard]] const std::string& GetName() const noexcept
        {
            return mName;
        }

        /** Returns the concrete resource kind. */
        [[nodiscard]] EARDGResourceType GetType() const noexcept
        {
            return mType;
        }

    private:
        std::string mName;
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
         * @param InitialState The known NVRHI state on graph entry.
         * @param Flags The graph ownership flags.
         */
        FARDGViewableResource(
            std::string Name,
            EARDGResourceType Type,
            nvrhi::ResourceStates InitialState,
            EARDGResourceFlags Flags)
            : FARDGResource(std::move(Name), Type)
            , mInitialState(InitialState)
            , mFinalState(InitialState)
            , mFlags(Flags)
        {
        }

        /** Returns the known NVRHI state on graph entry. */
        [[nodiscard]] nvrhi::ResourceStates GetInitialState() const noexcept
        {
            return mInitialState;
        }

        /** Returns the requested NVRHI state on graph exit. */
        [[nodiscard]] nvrhi::ResourceStates GetFinalState() const noexcept
        {
            return mFinalState;
        }

        /** Sets the requested NVRHI state on graph exit. */
        void SetFinalState(nvrhi::ResourceStates FinalState) noexcept
        {
            mFinalState = FinalState;
        }

        /** Returns the ownership and lifetime flags. */
        [[nodiscard]] EARDGResourceFlags GetFlags() const noexcept
        {
            return mFlags;
        }

        /** Returns whether this record imports an externally owned NVRHI resource. */
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
        [[nodiscard]] const std::vector<FARDGPassHandle>& GetReaders() const noexcept
        {
            return mReaders;
        }

        /** Records a reader since the latest write. */
        void AddReader(FARDGPassHandle Reader)
        {
            if (Reader.IsValid() &&
                std::find(mReaders.begin(), mReaders.end(), Reader) ==
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
        nvrhi::ResourceStates mInitialState;
        nvrhi::ResourceStates mFinalState;
        EARDGResourceFlags mFlags;
        FARDGPassHandle mLastProducer;
        std::vector<FARDGPassHandle> mReaders;
        FARDGPassHandle mFirstUse;
        FARDGPassHandle mLastUse;
    };

    /** A logical texture record with deferred or imported NVRHI backing. */
    class FARDGTexture final : public FARDGViewableResource
    {
    public:
        /**
         * Constructs a logical texture.
         *
         * @param Handle The stable texture-registry handle.
         * @param Desc The NVRHI texture descriptor.
         * @param Flags The graph ownership flags.
         * @param Texture Optional imported physical texture.
         */
        FARDGTexture(
            FARDGTextureHandle Handle,
            nvrhi::TextureDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient,
            nvrhi::TextureHandle Texture = nullptr)
            : FARDGViewableResource(
                  Desc.debugName,
                  EARDGResourceType::Texture,
                  Desc.initialState,
                  Flags)
            , mHandle(Handle)
            , mDesc(std::move(Desc))
            , mTexture(std::move(Texture))
        {
        }

        /** Returns the stable texture-registry handle. */
        [[nodiscard]] FARDGTextureHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the NVRHI descriptor used to allocate or validate the texture. */
        [[nodiscard]] const nvrhi::TextureDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

        /**
         * Returns the physical texture, or an empty handle before materialization.
         * Pass code should use FARDGPassExecutionContext::GetTexture so the
         * graph can validate that the resource was declared.
         */
        [[nodiscard]] const nvrhi::TextureHandle& GetTexture() const noexcept
        {
            return mTexture;
        }

        /** Binds a materialized or imported NVRHI texture to this logical record. */
        void BindTexture(nvrhi::TextureHandle Texture) noexcept
        {
            mTexture = std::move(Texture);
        }

    private:
        FARDGTextureHandle mHandle;
        nvrhi::TextureDesc mDesc;
        nvrhi::TextureHandle mTexture;
    };

    /** A logical buffer record with deferred or imported NVRHI backing. */
    class FARDGBuffer final : public FARDGViewableResource
    {
    public:
        /**
         * Constructs a logical buffer.
         *
         * @param Handle The stable buffer-registry handle.
         * @param Desc The NVRHI buffer descriptor.
         * @param Flags The graph ownership flags.
         * @param Buffer Optional imported physical buffer.
         */
        FARDGBuffer(
            FARDGBufferHandle Handle,
            nvrhi::BufferDesc Desc,
            EARDGResourceFlags Flags = EARDGResourceFlags::Transient,
            nvrhi::BufferHandle Buffer = nullptr)
            : FARDGViewableResource(
                  Desc.debugName,
                  EARDGResourceType::Buffer,
                  Desc.initialState,
                  Flags)
            , mHandle(Handle)
            , mDesc(std::move(Desc))
            , mBuffer(std::move(Buffer))
        {
        }

        /** Returns the stable buffer-registry handle. */
        [[nodiscard]] FARDGBufferHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the NVRHI descriptor used to allocate or validate the buffer. */
        [[nodiscard]] const nvrhi::BufferDesc& GetDesc() const noexcept
        {
            return mDesc;
        }

        /**
         * Returns the physical buffer, or an empty handle before materialization.
         * Pass code should use FARDGPassExecutionContext::GetBuffer so the
         * graph can validate that the resource was declared.
         */
        [[nodiscard]] const nvrhi::BufferHandle& GetBuffer() const noexcept
        {
            return mBuffer;
        }

        /** Binds a materialized or imported NVRHI buffer to this logical record. */
        void BindBuffer(nvrhi::BufferHandle Buffer) noexcept
        {
            mBuffer = std::move(Buffer);
        }

    private:
        FARDGBufferHandle mHandle;
        nvrhi::BufferDesc mDesc;
        nvrhi::BufferHandle mBuffer;
    };

    /** Describes a logical texture SRV or UAV. */
    struct FARDGTextureViewDesc
    {
        /** The parent logical texture. */
        FARDGTextureHandle mTexture;

        /** The texture subresources exposed by the view. */
        nvrhi::TextureSubresourceSet mSubresources = nvrhi::AllSubresources;

        /** An optional format override for the view. */
        nvrhi::Format mFormat = nvrhi::Format::UNKNOWN;

        /** An optional texture-dimension override for the view. */
        nvrhi::TextureDimension mDimension = nvrhi::TextureDimension::Unknown;
    };

    /** Describes a logical buffer SRV or UAV. */
    struct FARDGBufferViewDesc
    {
        /** The parent logical buffer. */
        FARDGBufferHandle mBuffer;

        /** The byte range exposed by the view. */
        nvrhi::BufferRange mRange = nvrhi::EntireBuffer;

        /** An optional typed-buffer format override. */
        nvrhi::Format mFormat = nvrhi::Format::UNKNOWN;
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
        FARDGView(FARDGViewHandle Handle, std::string Name, EARDGResourceType Type)
            : FARDGResource(std::move(Name), Type)
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
        FARDGTextureSRV(FARDGViewHandle Handle, std::string Name, FARDGTextureViewDesc Desc)
            : FARDGView(Handle, std::move(Name), EARDGResourceType::TextureShaderResourceView)
            , mDesc(std::move(Desc))
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
        FARDGTextureUAV(FARDGViewHandle Handle, std::string Name, FARDGTextureViewDesc Desc)
            : FARDGView(Handle, std::move(Name), EARDGResourceType::TextureUnorderedAccessView)
            , mDesc(std::move(Desc))
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
        FARDGBufferSRV(FARDGViewHandle Handle, std::string Name, FARDGBufferViewDesc Desc)
            : FARDGView(Handle, std::move(Name), EARDGResourceType::BufferShaderResourceView)
            , mDesc(std::move(Desc))
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
        FARDGBufferUAV(FARDGViewHandle Handle, std::string Name, FARDGBufferViewDesc Desc)
            : FARDGView(Handle, std::move(Name), EARDGResourceType::BufferUnorderedAccessView)
            , mDesc(std::move(Desc))
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

    /** A logical uniform-buffer record and its optional NVRHI constant-buffer backing. */
    class FARDGUniformBuffer final : public FARDGResource
    {
    public:
        /**
         * Constructs a logical uniform buffer.
         *
         * @param Handle The stable uniform-buffer registry handle.
         * @param Name The diagnostic resource name.
         * @param Desc The NVRHI constant-buffer descriptor.
         * @param Metadata Metadata for the immutable parameter contents.
         * @param Contents A non-owning pointer to graph-arena parameter storage.
         */
        FARDGUniformBuffer(
            FARDGUniformBufferHandle Handle,
            std::string Name,
            nvrhi::BufferDesc Desc,
            const FARDGParameterMetadata* Metadata,
            const void* Contents)
            : FARDGResource(std::move(Name), EARDGResourceType::UniformBuffer)
            , mHandle(Handle)
            , mDesc(std::move(Desc))
            , mMetadata(Metadata)
            , mContents(Contents)
        {
        }

        /** Returns the stable uniform-buffer registry handle. */
        [[nodiscard]] FARDGUniformBufferHandle GetHandle() const noexcept
        {
            return mHandle;
        }

        /** Returns the NVRHI descriptor used for physical constant-buffer allocation. */
        [[nodiscard]] const nvrhi::BufferDesc& GetDesc() const noexcept
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
        [[nodiscard]] const nvrhi::BufferHandle& GetBuffer() const noexcept
        {
            return mBuffer;
        }

        /** Binds a materialized NVRHI constant buffer to this logical record. */
        void BindBuffer(nvrhi::BufferHandle Buffer) noexcept
        {
            mBuffer = std::move(Buffer);
        }

    private:
        FARDGUniformBufferHandle mHandle;
        nvrhi::BufferDesc mDesc;
        const FARDGParameterMetadata* mMetadata = nullptr;
        const void* mContents = nullptr;
        nvrhi::BufferHandle mBuffer;
    };

    /** Declares a direct texture access that does not require a logical view. */
    struct FARDGTextureAccess
    {
        /** The logical texture being accessed. */
        FARDGTexture* mTexture = nullptr;

        /** The NVRHI state required by the access. */
        nvrhi::ResourceStates mState = nvrhi::ResourceStates::Unknown;

        /** The texture subresources covered by the access. */
        nvrhi::TextureSubresourceSet mSubresources = nvrhi::AllSubresources;
    };

    /** Declares a direct buffer access that does not require a logical view. */
    struct FARDGBufferAccess
    {
        /** The logical buffer being accessed. */
        FARDGBuffer* mBuffer = nullptr;

        /** The NVRHI state required by the access. */
        nvrhi::ResourceStates mState = nvrhi::ResourceStates::Unknown;

        /** The byte range covered by the access. */
        nvrhi::BufferRange mRange = nvrhi::EntireBuffer;
    };

    /** Identifies one logical texture attachment and its selected subresources. */
    struct FARDGRenderTargetBinding
    {
        /** The logical attachment texture, or null for an unused slot. */
        FARDGTexture* mTexture = nullptr;

        /** The texture subresources attached to the framebuffer. */
        nvrhi::TextureSubresourceSet mSubresources = nvrhi::AllSubresources;
    };

    /** Stores color and depth attachments declared by a raster pass. */
    struct FARDGRenderTargetBindingSlots
    {
        /** The logical color attachments, indexed by render-target slot. */
        std::array<FARDGRenderTargetBinding, nvrhi::c_MaxRenderTargets> mColor;

        /** The logical depth-stencil attachment, or an empty binding when unused. */
        FARDGRenderTargetBinding mDepthStencil;
    };

    /** A nullable reference to a logical texture. */
    using FARDGTextureRef = FARDGTexture*;

    /** A nullable reference to a logical buffer. */
    using FARDGBufferRef = FARDGBuffer*;

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
