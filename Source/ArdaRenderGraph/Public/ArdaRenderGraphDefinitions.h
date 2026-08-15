#pragma once

#include <cstddef>
#include <cstdint>
#include <EASTL/functional.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/type_traits.h>

#include "RHIWrappers/ArdaRHI.h"

namespace arda::render_graph
{
    /**
     * A compact, type-safe index into an append-only render-graph registry.
     *
     * @tparam TagType An otherwise empty tag that makes handles from different registries incompatible.
     */
    template <typename TagType>
    class TARDGHandle
    {
    public:
        /** The value used to represent an invalid handle. */
        static constexpr uint32_t InvalidIndex = eastl::numeric_limits<uint32_t>::max();

        /** Constructs an invalid handle. */
        constexpr TARDGHandle() noexcept = default;

        /**
         * Constructs a handle for a registry index.
         *
         * @param Index The zero-based registry index.
         */
        explicit constexpr TARDGHandle(uint32_t Index) noexcept
            : mIndex(Index)
        {
        }

        /** Returns whether the handle names a registry entry. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return mIndex != InvalidIndex;
        }

        /** Returns the zero-based registry index, or InvalidIndex for an invalid handle. */
        [[nodiscard]] constexpr uint32_t GetIndex() const noexcept
        {
            return mIndex;
        }

        /** Returns whether the handle is valid. */
        explicit constexpr operator bool() const noexcept
        {
            return IsValid();
        }

        /** Returns whether two handles name the same registry entry. */
        friend constexpr bool operator==(TARDGHandle Left, TARDGHandle Right) noexcept
        {
            return Left.mIndex == Right.mIndex;
        }

        /** Returns whether two handles name different registry entries. */
        friend constexpr bool operator!=(TARDGHandle Left, TARDGHandle Right) noexcept
        {
            return !(Left == Right);
        }

        /** Orders handles by registry index. */
        friend constexpr bool operator<(TARDGHandle Left, TARDGHandle Right) noexcept
        {
            return Left.mIndex < Right.mIndex;
        }

    private:
        uint32_t mIndex = InvalidIndex;
    };

    /**
     * Hashes a typed render-graph handle by its registry index.
     *
     * @tparam TagType The handle tag.
     */
    template <typename TagType>
    struct TARDGHandleHasher
    {
        /** Returns the hash of Handle. */
        [[nodiscard]] size_t operator()(TARDGHandle<TagType> Handle) const noexcept
        {
            return eastl::hash<uint32_t>{}(Handle.GetIndex());
        }
    };

    /** Distinguishes pass handles from all other typed handles. */
    struct FARDGPassHandleTag final
    {
    };

    /** Distinguishes texture handles from all other typed handles. */
    struct FARDGTextureHandleTag final
    {
    };

    /** Distinguishes buffer handles from all other typed handles. */
    struct FARDGBufferHandleTag final
    {
    };

    /** Distinguishes acceleration-structure handles from all other typed handles. */
    struct FARDGAccelStructHandleTag final {};

    /** Distinguishes view handles from all other typed handles. */
    struct FARDGViewHandleTag final
    {
    };

    /** Distinguishes uniform-buffer handles from all other typed handles. */
    struct FARDGUniformBufferHandleTag final
    {
    };

    /** A stable index into the pass registry. */
    using FARDGPassHandle = TARDGHandle<FARDGPassHandleTag>;

    /** A stable index into the logical texture registry. */
    using FARDGTextureHandle = TARDGHandle<FARDGTextureHandleTag>;

    /** A stable index into the logical buffer registry. */
    using FARDGBufferHandle = TARDGHandle<FARDGBufferHandleTag>;
    using FARDGAccelStructHandle = TARDGHandle<FARDGAccelStructHandleTag>;

    /** A stable index into the logical view registry. */
    using FARDGViewHandle = TARDGHandle<FARDGViewHandleTag>;

    /** A stable index into the logical uniform-buffer registry. */
    using FARDGUniformBufferHandle = TARDGHandle<FARDGUniformBufferHandleTag>;

    /** Identifies the command pipeline on which a pass executes. */
    enum class EARDGPipeline : uint8_t
    {
        /** Graphics-capable command execution. */
        Graphics,

        /** Asynchronous compute command execution. */
        AsyncCompute,

        /** Copy-only command execution. */
        Copy
    };

    /** Describes which command queues may be selected by graph compilation. */
    struct FARDGQueueCapabilities
    {
        /** Whether graphics command execution is available. */
        bool mbGraphics = true;

        /** Whether asynchronous compute command execution is available. */
        bool mbCompute = false;

        /** Whether copy-only command execution is available. */
        bool mbCopy = false;
    };

    /** Selects correctness-oriented graph diagnostics and execution behavior. */
    struct FARDGDebugOptions
    {
        /**
         * Keeps every registered pass, assigns it to the graphics queue,
         * extends resource lifetimes, and records/submits passes serially as a
         * correctness oracle.
         */
        bool mbImmediateMode = false;

        /** Forces ordering barriers between otherwise identical pass states. */
        bool mbConservativeBarriers = false;

        /** Extends live resource intervals across the complete graph. */
        bool mbExtendResourceLifetimes = false;

        /** Clears safely supported resources immediately before their first write. */
        bool mbClobberFirstWrites = false;
    };

    /** Flags that describe a pass and constrain later graph compilation. */
    enum class EARDGPassFlags : uint16_t
    {
        /** A parameterless pass without an implied pipeline operation. */
        None = 0,

        /** A graphics pass that binds render targets. */
        Raster = 1u << 0u,

        /** A compute dispatch on a graphics-capable pipeline. */
        Compute = 1u << 1u,

        /** A compute dispatch eligible for the asynchronous compute pipeline. */
        AsyncCompute = 1u << 2u,

        /** A resource-copy pass. */
        Copy = 1u << 3u,

        /** A pass that remains live even when it has no observable outputs. */
        NeverCull = 1u << 4u,

        /** A raster pass that manages RHI framebuffer behavior explicitly. */
        SkipRenderPass = 1u << 5u,

        /** A pass that must not be recorded in parallel. */
        NeverParallel = 1u << 6u
    };

    /** Combines pass flags. */
    [[nodiscard]] constexpr EARDGPassFlags operator|(EARDGPassFlags Left, EARDGPassFlags Right) noexcept
    {
        return static_cast<EARDGPassFlags>(
            static_cast<uint16_t>(Left) | static_cast<uint16_t>(Right));
    }

    /** Intersects pass flags. */
    [[nodiscard]] constexpr EARDGPassFlags operator&(EARDGPassFlags Left, EARDGPassFlags Right) noexcept
    {
        return static_cast<EARDGPassFlags>(
            static_cast<uint16_t>(Left) & static_cast<uint16_t>(Right));
    }

    /** Adds flags to a pass-flag value. */
    constexpr EARDGPassFlags& operator|=(EARDGPassFlags& Left, EARDGPassFlags Right) noexcept
    {
        Left = Left | Right;
        return Left;
    }

    /** Returns whether Value contains every flag in Required. */
    [[nodiscard]] constexpr bool HasAllFlags(EARDGPassFlags Value, EARDGPassFlags Required) noexcept
    {
        return (Value & Required) == Required;
    }

    /** Describes how a logical resource participates in graph ownership. */
    enum class EARDGResourceFlags : uint8_t
    {
        /** No special ownership behavior. */
        None = 0,

        /** The physical RHI resource is owned outside the graph. */
        External = 1u << 0u,

        /** The physical RHI resource must survive graph completion. */
        Extracted = 1u << 1u,

        /** Physical storage may be created and recycled within graph execution. */
        Transient = 1u << 2u
    };

    /** Combines resource flags. */
    [[nodiscard]] constexpr EARDGResourceFlags operator|(
        EARDGResourceFlags Left,
        EARDGResourceFlags Right) noexcept
    {
        return static_cast<EARDGResourceFlags>(
            static_cast<uint8_t>(Left) | static_cast<uint8_t>(Right));
    }

    /** Intersects resource flags. */
    [[nodiscard]] constexpr EARDGResourceFlags operator&(
        EARDGResourceFlags Left,
        EARDGResourceFlags Right) noexcept
    {
        return static_cast<EARDGResourceFlags>(
            static_cast<uint8_t>(Left) & static_cast<uint8_t>(Right));
    }

    /** Adds flags to a resource-flag value. */
    constexpr EARDGResourceFlags& operator|=(
        EARDGResourceFlags& Left,
        EARDGResourceFlags Right) noexcept
    {
        Left = Left | Right;
        return Left;
    }

    /** Returns whether Value contains every flag in Required. */
    [[nodiscard]] constexpr bool HasAllFlags(
        EARDGResourceFlags Value,
        EARDGResourceFlags Required) noexcept
    {
        return (Value & Required) == Required;
    }

    /** Identifies the kind of a logical render-graph resource. */
    enum class EARDGResourceType : uint8_t
    {
        /** A logical texture. */
        Texture,

        /** A logical buffer. */
        Buffer,

        /** A logical texture shader-resource view. */
        TextureShaderResourceView,

        /** A logical texture unordered-access view. */
        TextureUnorderedAccessView,

        /** A logical buffer shader-resource view. */
        BufferShaderResourceView,

        /** A logical buffer unordered-access view. */
        BufferUnorderedAccessView,

        /** A logical uniform buffer. */
        UniformBuffer,

        /** A logical ray tracing acceleration structure. */
        AccelStruct
    };

    /** Supplies opaque RHI execution state to a render-graph builder and executor. */
    struct FARDGRenderGraphContext
    {
        /** The RHI device on which render-graph work is executed. */
        rhi::FArdaRHIDeviceRef mDevice;

        /** Queue capabilities used for deterministic pipeline fallback. */
        FARDGQueueCapabilities mQueueCapabilities;

        /** Optional correctness and diagnostic behavior for this graph. */
        FARDGDebugOptions mDebugOptions;
    };
}
