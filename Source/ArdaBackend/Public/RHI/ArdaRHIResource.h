/** @file ArdaRHIResource.h
 * Declares the common intrusive-lifetime interface and resource type identifiers.
 */

#pragma once

#include "ArdaRHIFwd.h"

#include <cstdint>

namespace arda
{
    /** Representation the qualified allocation can supply to a CUDA kernel. */
    enum class EArdaCudaRepresentation : uint8_t
    {
        /** No compatible representation, including opaque acceleration-structure storage. */
        None,
        /** Linear device address for a buffer range. */
        LinearBuffer,
        /** CUDA surface object/storage-image handle for one texture mip. */
        Surface
    };
    /** Available API representations, independent of queue ownership and resource access state. */
    enum class EArdaResourceRepresentations : uint8_t
    {
        /** Only the graphics representation was created. */
        Graphics,
        /** Both representations exist and refer to the same native allocation. */
        GraphicsAndCuda
    };
    /** Representation availability is not permission for concurrent access. D3D12 CiG captures launches into
     * the graphics command list. Vulkan CiG and ordinary contexts launch on CUDA streams ordered with graphics
     * by GPU fence/semaphore handoffs. Retain shared storage until the final graphics consumer completes. */
    struct FArdaCudaResourceInfo
    {
        /** Allocation/device-qualified representation, not a promise for every resource of this type. */
        EArdaCudaRepresentation mSupportedRepresentation = EArdaCudaRepresentation::None;
        /** Whether this allocation actually owns a CUDA representation alongside graphics. */
        EArdaResourceRepresentations mRepresentations = EArdaResourceRepresentations::Graphics;
        /** True after successful native allocation/mapping; callers cannot toggle this state. */
        bool mbSharingEnabled = false;
    };

    /** Enumerates resource type values. */
    enum class EArdaRHIResourceType : uint8_t
    {
        Device,
        Texture,
        TextureReference,
        Buffer,
        UniformBuffer,
        Heap,
        StagingTexture,
        EventQuery,
        TimerQuery,
        GpuFence,
        ShaderResourceView,
        UnorderedAccessView,
        Sampler,
        Shader,
        ShaderLibrary,
        InputLayout,
        BindingLayout,
        BindingSet,
        DescriptorTable,
        ResourceCollection,
        Framebuffer,
        GraphicsPipeline,
        ComputePipeline,
        MeshletPipeline,
        AccelStruct,
        RayTracingPipeline,
        ShaderTable,
        WorkGraphPipeline,
        ShaderBundle,
        SamplerFeedbackTexture,
        OpacityMicromap,
        RasterState,
        BlendState,
        DepthStencilState,
        CommandList,
        Count
    };

    /** Device-affine intrusive lifetime shared by storage, views, pipelines and command objects.
     * Retained references preserve CPU object lifetime; submitted work additionally retains its
     * native dependencies until the owning queue completes. Resource kind is not a capability.
     */
    class IArdaRHIResource
    {
    public:
        /** Performs the add operation. */
        virtual void AddRef() noexcept = 0;
        /** Performs the release operation. */
        virtual void Release() noexcept = 0;
        /**
         * Returns the resource type.
         * @return The requested value.
         */
        [[nodiscard]] virtual EArdaRHIResourceType GetResourceType() const noexcept = 0;
        /**
         * Returns the debug name.
         * @return The requested object pointer.
         */
        [[nodiscard]] virtual const char* GetDebugName() const noexcept = 0;
        /** Reads CUDA representation facts qualified for this particular native allocation.
         * @return An owned value containing representation kind, sharing admission and available
         * domains. Opaque AS/pipeline objects and ordinary nonshared allocations do not become
         * CUDA-addressable merely because the device supports CUDA. The default is None/Graphics.
         * @ownership Does not import, map, transfer ownership, or return a CUDA pointer.
         * @threading Read-only allocation metadata; keep the resource alive during the call.
         * @errors Unsupported representations return the default value without failing or throwing.
         */
        [[nodiscard]] virtual FArdaCudaResourceInfo GetCudaResourceInfo() const noexcept { return {}; }

    protected:
        /** Releases the retained resource object. */
        virtual ~IArdaRHIResource() = default;
    };
}
