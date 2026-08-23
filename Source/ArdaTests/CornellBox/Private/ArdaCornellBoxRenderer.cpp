#include "ArdaCornellBoxPch.h"

#include "ArdaCornellBoxRenderer.h"

#include <EASTL/shared_ptr.h>

namespace arda::tests::cornell_box
{
    namespace
    {
        constexpr uint32_t SphereFaceCount = 6;
        constexpr uint32_t SphereFaceResolution = 16;
        constexpr uint32_t RoomAndBoxQuadCount = 18;
        constexpr uint32_t SphereTriangleCount =
            SphereFaceCount * SphereFaceResolution *
            SphereFaceResolution * 2;
        constexpr uint32_t TriangleCount =
            RoomAndBoxQuadCount * 2 + SphereTriangleCount * 2;
        constexpr uint32_t VertexCount = TriangleCount * 3;
        constexpr uint32_t IndexCount = TriangleCount * 3;
        constexpr uint32_t MaterialCount = 7;
        constexpr uint64_t MaxSampleRadianceScratchBytes =
            256ull * 1024ull * 1024ull;
        // Keeps one ray dispatch responsive enough for window-close handling
        // and below common desktop watchdog thresholds. Millions of paths are
        // still available to the hardware scheduler concurrently.
        constexpr uint64_t MaxPathSegmentsPerDispatch =
            96ull * 1024ull * 1024ull;

        struct alignas(16) FCornellVertex
        {
            float mPosition[3];
            float mPadding = 0.0f;
            float mNormal[3];
            uint32_t mMaterialId = 0;
        };

        struct alignas(16) FCornellMaterial
        {
            float mBaseColor[3];
            float mRoughness = 0.0f;
            float mEmission[3];
            float mMetallic = 0.0f;
            float mTransmission = 0.0f;
            float mIor = 1.0f;
            float mPadding[2]{};
        };

        static_assert(sizeof(FCornellVertex) == 32,
            "Cornell vertex layout must match HLSL.");
        static_assert(sizeof(FCornellMaterial) == 48,
            "Cornell material layout must match HLSL.");

        class FGenerateCornellGeometryShader final :
            public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_BUFFER_UAV(
                    mVertices, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_BUFFER_UAV(
                    mIndices, 1, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_BUFFER_UAV(
                    mMaterials, 2, 0, rhi::EArdaRHIShaderStage::Compute)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FGenerateCornellGeometryShader);
        };

#define ARDA_CORNELL_RAY_PARAMETERS()                                                  \
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)                           \
                ARDA_SHADER_ACCELERATION_STRUCTURE(                                    \
                    mScene, 0, 0, rhi::EArdaRHIShaderStage::AllRayTracing)             \
                ARDA_SHADER_BUFFER_SRV(                                                \
                    mVertices, 1, 0, rhi::EArdaRHIShaderStage::AllRayTracing)          \
                ARDA_SHADER_BUFFER_SRV(                                                \
                    mIndices, 2, 0, rhi::EArdaRHIShaderStage::AllRayTracing)           \
                ARDA_SHADER_BUFFER_SRV(                                                \
                    mMaterials, 3, 0, rhi::EArdaRHIShaderStage::AllRayTracing)         \
                ARDA_SHADER_BUFFER_UAV(                                                \
                    mSampleRadiance, 0, 0, rhi::EArdaRHIShaderStage::AllRayTracing)    \
                ARDA_SHADER_UNIFORM_BUFFER(                                            \
                    mFrame, 0, 0, rhi::EArdaRHIShaderStage::AllRayTracing)             \
            ARDA_END_SHADER_PARAMETER_STRUCT()

        class FCornellRayGenerationShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_CORNELL_RAY_PARAMETERS()
            ARDA_DECLARE_GLOBAL_SHADER(FCornellRayGenerationShader);
        };

        class FCornellMissShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_CORNELL_RAY_PARAMETERS()
            ARDA_DECLARE_GLOBAL_SHADER(FCornellMissShader);
        };

        class FCornellClosestHitShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_CORNELL_RAY_PARAMETERS()
            ARDA_DECLARE_GLOBAL_SHADER(FCornellClosestHitShader);
        };
#undef ARDA_CORNELL_RAY_PARAMETERS

        class FAccumulateCornellSamplesShader final :
            public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_BUFFER_SRV(
                    mSampleRadiance, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_TEXTURE_UAV(
                    mAccumulation, 0, 0, rhi::EArdaRHIShaderStage::Compute)
                ARDA_SHADER_UNIFORM_BUFFER(
                    mFrame, 0, 0, rhi::EArdaRHIShaderStage::Compute)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FAccumulateCornellSamplesShader);
        };

        class FCornellPresentVertexShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_DECLARE_GLOBAL_SHADER(FCornellPresentVertexShader);
        };

        class FCornellPresentPixelShader final : public backend::FArdaGlobalShader
        {
        public:
            ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
                ARDA_SHADER_TEXTURE_SRV(
                    mAccumulation, 0, 0, rhi::EArdaRHIShaderStage::Pixel)
                ARDA_SHADER_UNIFORM_BUFFER(
                    mFrame, 0, 0, rhi::EArdaRHIShaderStage::Pixel)
            ARDA_END_SHADER_PARAMETER_STRUCT()
            ARDA_DECLARE_GLOBAL_SHADER(FCornellPresentPixelShader);
        };

        ARDG_BEGIN_PARAMETER_STRUCT(FGenerateGeometryParameters)
            ARDG_BUFFER_UAV(mVertices)
            ARDG_BUFFER_UAV(mIndices)
            ARDG_BUFFER_UAV(mMaterials)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FBuildBlasParameters)
            ARDG_BUFFER_ACCESS(mVertices)
            ARDG_BUFFER_ACCESS(mIndices)
            ARDG_ACCEL_STRUCT_ACCESS(mBlas)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FCompactBlasParameters)
            ARDG_ACCEL_STRUCT_ACCESS(mSource)
            ARDG_ACCEL_STRUCT_ACCESS(mDestination)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FBuildTlasParameters)
            ARDG_ACCEL_STRUCT_ACCESS(mBlas)
            ARDG_ACCEL_STRUCT_ACCESS(mTlas)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FFrameConstants)
            ARDG_PARAMETER_ARRAY(float, mCameraPositionAndTanHalfFovX, 4)
            ARDG_PARAMETER_ARRAY(float, mCameraForwardAndTanHalfFovY, 4)
            ARDG_PARAMETER_ARRAY(float, mCameraRightAndExposure, 4)
            ARDG_PARAMETER_ARRAY(float, mCameraUpAndLightArea, 4)
            ARDG_PARAMETER_ARRAY(uint32_t, mImageAndSampling, 4)
            ARDG_PARAMETER_ARRAY(uint32_t, mPathAndSeed, 4)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FPathTraceParameters)
            ARDG_ACCEL_STRUCT_ACCESS(mScene)
            ARDG_BUFFER_SRV(mVertices)
            ARDG_BUFFER_SRV(mIndices)
            ARDG_BUFFER_SRV(mMaterials)
            ARDG_BUFFER_UAV(mSampleRadiance)
            ARDG_UNIFORM_BUFFER(mFrame)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FAccumulateParameters)
            ARDG_BUFFER_SRV(mSampleRadiance)
            ARDG_TEXTURE_UAV(mAccumulation)
            ARDG_UNIFORM_BUFFER(mFrame)
        ARDG_END_PARAMETER_STRUCT()

        ARDG_BEGIN_PARAMETER_STRUCT(FPresentParameters)
            ARDG_TEXTURE_SRV(mAccumulation)
            ARDG_UNIFORM_BUFFER(mFrame)
            ARDG_RENDER_TARGET_BINDING_SLOTS(mTargets)
        ARDG_END_PARAMETER_STRUCT()

        class FPassErrors final
        {
        public:
            void Record(const char* Prefix, const eastl::string& Detail)
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (!mFirst.empty())
                    return;
                mFirst = Prefix ? Prefix : "Cornell Box pass failed";
                if (!Detail.empty())
                {
                    mFirst += ": ";
                    mFirst += Detail;
                }
            }

            [[nodiscard]] eastl::string GetFirst() const
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return mFirst;
            }

        private:
            mutable std::mutex mMutex;
            eastl::string mFirst;
        };

        template <typename T>
        bool TakeResult(
            rhi::TArdaRHIResult<T>& Result,
            T& Output,
            eastl::string& Error)
        {
            if (!Result)
            {
                Error = Result.mStatus.mMessage;
                return false;
            }
            Output = eastl::move(Result.mValue);
            return true;
        }

        rhi::FArdaRHIRayTracingGeometryDesc MakeGeometryDesc(
            const rhi::FArdaRHIBufferRef& Vertices,
            const rhi::FArdaRHIBufferRef& Indices)
        {
            rhi::FArdaRHIRayTracingGeometryDesc Geometry;
            Geometry.mType = rhi::EArdaRHIRayTracingGeometryType::Triangles;
            Geometry.mFlags = rhi::EArdaRHIRayTracingGeometryFlags::Opaque;
            Geometry.mVertexOrAABBBuffer = Vertices;
            Geometry.mIndexBuffer = Indices;
            Geometry.mVertexFormat = rhi::EArdaRHIFormat::RGB32Float;
            Geometry.mIndexFormat = rhi::EArdaRHIFormat::R32UInt;
            Geometry.mVertexOrAABBCount = VertexCount;
            Geometry.mIndexCount = IndexCount;
            Geometry.mStride = sizeof(FCornellVertex);
            return Geometry;
        }

        rhi::EArdaRHIAccelStructBuildFlags GetStaticBuildFlags(bool bCompact)
        {
            rhi::EArdaRHIAccelStructBuildFlags Flags =
                rhi::EArdaRHIAccelStructBuildFlags::PreferFastTrace;
            if (bCompact)
                Flags |= rhi::EArdaRHIAccelStructBuildFlags::AllowCompaction;
            return Flags;
        }
    }

    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FGenerateCornellGeometryShader,
        "/ArdaTests/CornellBox/CornellGeometry.hlsl",
        "CornellGeometryCS",
        "GenerateCornellGeometryCS",
        rhi::EArdaRHIShaderStage::Compute)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FCornellRayGenerationShader,
        "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
        "CornellRayGen",
        "CornellRayGen",
        rhi::EArdaRHIShaderStage::RayGeneration)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FCornellMissShader,
        "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
        "CornellMiss",
        "CornellMiss",
        rhi::EArdaRHIShaderStage::Miss)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FCornellClosestHitShader,
        "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
        "CornellClosestHit",
        "CornellClosestHit",
        rhi::EArdaRHIShaderStage::ClosestHit)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FAccumulateCornellSamplesShader,
        "/ArdaTests/CornellBox/CornellAccumulate.hlsl",
        "CornellAccumulateCS",
        "CornellAccumulateCS",
        rhi::EArdaRHIShaderStage::Compute)
    ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(
        FCornellPresentVertexShader,
        "/ArdaTests/CornellBox/CornellPresent.hlsl",
        "CornellPresentVS",
        "CornellPresentVS",
        rhi::EArdaRHIShaderStage::Vertex)
    ARDA_IMPLEMENT_GLOBAL_SHADER(
        FCornellPresentPixelShader,
        "/ArdaTests/CornellBox/CornellPresent.hlsl",
        "CornellPresentPS",
        "CornellPresentPS",
        rhi::EArdaRHIShaderStage::Pixel)

    bool FArdaCornellBoxRenderer::Initialize(
        rhi::FArdaRHIDeviceRef Device,
        rhi::EArdaRHIFormat SwapChainFormat,
        const FArdaCornellBoxSettings& Settings)
    {
        mDevice = eastl::move(Device);
        mSettings = Settings;
        if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
        {
            mError = "The initialized backend does not expose a graphics device.";
            return false;
        }

        const rhi::FArdaRHIRayTracingCapabilities& RayTracing =
            mDevice->GetCapabilities().mRayTracing;
        if (!RayTracing.mbHardwareAccelerated ||
            !RayTracing.mbPipelineShaders ||
            !RayTracing.mbAccelerationStructures ||
            !RayTracing.mbBottomLevel ||
            !RayTracing.mbTopLevel)
        {
            mError = "CornellBox requires hardware ray tracing, pipeline shaders, and BLAS/TLAS support.";
            return false;
        }
        if (RayTracing.mMaxRecursionDepth < 1)
        {
            mError = "The ray-tracing device reports no supported recursion depth.";
            return false;
        }

        mPipelineStateCache =
            std::make_unique<backend::FArdaPipelineStateCache>(mDevice);
        if (!CreateShadersAndPipelines(SwapChainFormat) ||
            !GenerateSceneGeometry() ||
            !BuildSceneAccelerationStructures())
        {
            return false;
        }
        mbSceneReady = true;
        ResetAccumulation();
        mError.clear();
        return true;
    }

    bool FArdaCornellBoxRenderer::CreateShadersAndPipelines(
        rhi::EArdaRHIFormat SwapChainFormat)
    {
        static_cast<void>(SwapChainFormat);
        if (!mShaderMap.Initialize(mDevice))
        {
            const auto Diagnostics = mShaderMap.GetDiagnostics();
            mError = Diagnostics.empty() ?
                "Global shader map initialization failed." :
                Diagnostics.back().mMessage;
            return false;
        }
        mGenerateGeometryShader = mShaderMap.Find(
            FGenerateCornellGeometryShader::GetStaticType());
        mRayGenerationShader = mShaderMap.Find(
            FCornellRayGenerationShader::GetStaticType());
        mMissShader = mShaderMap.Find(FCornellMissShader::GetStaticType());
        mClosestHitShader = mShaderMap.Find(
            FCornellClosestHitShader::GetStaticType());
        mAccumulateShader = mShaderMap.Find(
            FAccumulateCornellSamplesShader::GetStaticType());
        mPresentVertexShader = mShaderMap.Find(
            FCornellPresentVertexShader::GetStaticType());
        mPresentPixelShader = mShaderMap.Find(
            FCornellPresentPixelShader::GetStaticType());
        if (!mGenerateGeometryShader || !mRayGenerationShader ||
            !mMissShader || !mClosestHitShader || !mAccumulateShader ||
            !mPresentVertexShader || !mPresentPixelShader)
        {
            const auto Diagnostics = mShaderMap.GetDiagnostics();
            mError = Diagnostics.empty() ?
                "A registered Cornell Box shader is absent from the shader map." :
                Diagnostics.back().mMessage;
            return false;
        }

        mGenerateGeometryPipelineInitializer =
            backend::FArdaComputePipelineStateInitializer::FromGlobalShader(
                *mGenerateGeometryShader, "Cornell geometry generation");
        mAccumulatePipelineInitializer =
            backend::FArdaComputePipelineStateInitializer::FromGlobalShader(
                *mAccumulateShader, "Cornell sample reduction");

        rhi::FArdaRHIGraphicsPipelineDesc PresentFixedState;
        PresentFixedState.mRasterState.mCullMode = rhi::EArdaRHICullMode::None;
        PresentFixedState.mDepthStencilState.mbDepthTest = false;
        PresentFixedState.mDepthStencilState.mbDepthWrite = false;
        PresentFixedState.mSampleCount = 0;
        PresentFixedState.mDebugName = "Cornell tone-map presentation";
        mPresentPipelineInitializer =
            backend::FArdaGraphicsPipelineStateInitializer::FromGlobalShaders(
                *mPresentVertexShader,
                mPresentPixelShader,
                {},
                PresentFixedState);

        if (mRayGenerationShader->GetBindingLayouts().empty())
        {
            mError = "The Cornell ray-generation shader has no global binding layout.";
            return false;
        }
        rhi::FArdaRHIRayTracingPipelineDesc PipelineDesc;
        PipelineDesc.mDebugName = "Cornell Monte Carlo path tracer";
        PipelineDesc.mShaders.push_back({
            "CornellRayGen", mRayGenerationShader->GetShader(), {}});
        PipelineDesc.mShaders.push_back({
            "CornellMiss", mMissShader->GetShader(), {}});
        rhi::FArdaRHIRayTracingHitGroupDesc HitGroup;
        HitGroup.mExportName = "CornellHitGroup";
        HitGroup.mClosestHitShader = mClosestHitShader->GetShader();
        PipelineDesc.mHitGroups.push_back(eastl::move(HitGroup));
        PipelineDesc.mGlobalBindingLayouts.push_back(
            mRayGenerationShader->GetBindingLayouts()[0]);
        PipelineDesc.mMaxPayloadSize = 32;
        PipelineDesc.mMaxAttributeSize = sizeof(float) * 2;
        // The ray-generation shader iterates path segments. Native recursion
        // remains one, minimizing traversal live state and payload pressure.
        PipelineDesc.mMaxRecursionDepth = 1;
        auto Pipeline = mDevice->CreateRayTracingPipeline(PipelineDesc);
        if (!TakeResult(Pipeline, mRayTracingPipeline, mError))
            return false;

        rhi::FArdaRHIShaderTableDesc TableDesc;
        TableDesc.mMaxEntries = 3;
        TableDesc.mbPersistent = true;
        TableDesc.mDebugName = "Cornell persistent shader table";
        auto Table = mDevice->CreateShaderTable(mRayTracingPipeline, TableDesc);
        if (!TakeResult(Table, mShaderTable, mError))
            return false;
        rhi::FArdaRHIStatus Status = mDevice->SetShaderTableRayGeneration(
            mShaderTable, "CornellRayGen");
        if (!Status)
        {
            mError = Status.mMessage;
            return false;
        }
        auto Miss = mDevice->AddShaderTableMiss(mShaderTable, "CornellMiss");
        if (!Miss)
        {
            mError = Miss.mStatus.mMessage;
            return false;
        }
        auto Hit = mDevice->AddShaderTableHitGroup(
            mShaderTable, "CornellHitGroup");
        if (!Hit)
        {
            mError = Hit.mStatus.mMessage;
            return false;
        }
        Status = mDevice->CommitShaderTable(mShaderTable);
        if (!Status)
        {
            mError = Status.mMessage;
            return false;
        }
        return true;
    }

    bool FArdaCornellBoxRenderer::GenerateSceneGeometry()
    {
        render_graph::FARDGBuilder Graph(CreateGraphContext());

        rhi::FArdaRHIBufferDesc VertexDesc;
        VertexDesc.mDebugName = "Cornell GPU-generated vertices";
        VertexDesc.mByteSize = uint64_t(VertexCount) * sizeof(FCornellVertex);
        VertexDesc.mStructureStride = sizeof(FCornellVertex);
        VertexDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::ShaderResource |
            rhi::EArdaRHIBufferUsage::UnorderedAccess |
            rhi::EArdaRHIBufferUsage::Vertex |
            rhi::EArdaRHIBufferUsage::AccelStructBuildInput;
        auto* Vertices = Graph.CreateBuffer(VertexDesc);

        rhi::FArdaRHIBufferDesc IndexDesc;
        IndexDesc.mDebugName = "Cornell GPU-generated indices";
        IndexDesc.mByteSize = uint64_t(IndexCount) * sizeof(uint32_t);
        IndexDesc.mStructureStride = sizeof(uint32_t);
        IndexDesc.mFormat = rhi::EArdaRHIFormat::R32UInt;
        IndexDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::ShaderResource |
            rhi::EArdaRHIBufferUsage::UnorderedAccess |
            rhi::EArdaRHIBufferUsage::Index |
            rhi::EArdaRHIBufferUsage::AccelStructBuildInput;
        auto* Indices = Graph.CreateBuffer(IndexDesc);

        rhi::FArdaRHIBufferDesc MaterialDesc;
        MaterialDesc.mDebugName = "Cornell GPU-generated materials";
        MaterialDesc.mByteSize = uint64_t(MaterialCount) * sizeof(FCornellMaterial);
        MaterialDesc.mStructureStride = sizeof(FCornellMaterial);
        MaterialDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::ShaderResource |
            rhi::EArdaRHIBufferUsage::UnorderedAccess;
        auto* Materials = Graph.CreateBuffer(MaterialDesc);

        render_graph::FARDGBufferViewDesc VertexView;
        VertexView.mBuffer = Vertices->GetHandle();
        render_graph::FARDGBufferViewDesc IndexView;
        IndexView.mBuffer = Indices->GetHandle();
        render_graph::FARDGBufferViewDesc MaterialView;
        MaterialView.mBuffer = Materials->GetHandle();

        FGenerateGeometryParameters Parameters;
        Parameters.mVertices = Graph.CreateBufferUAV(
            "Cornell vertices UAV", VertexView);
        Parameters.mIndices = Graph.CreateBufferUAV(
            "Cornell indices UAV", IndexView);
        Parameters.mMaterials = Graph.CreateBufferUAV(
            "Cornell materials UAV", MaterialView);
        auto Errors = eastl::make_shared<FPassErrors>();
        (void)Graph.AddDispatchPass(
            "GenerateCornellGeometry",
            &Parameters,
            {(TriangleCount + 63) / 64, 1, 1},
            [this, Errors](
                render_graph::FARDGPassExecutionContext& Context,
                const FGenerateGeometryParameters&)
            {
                rhi::FArdaRHIComputeState State;
                State.mBindings.push_back(
                    Context.CreateBindingSet(*mGenerateGeometryShader));
                const rhi::FArdaRHIStatus Status =
                    mPipelineStateCache->SetComputePipelineState(
                        Context.mUnsafeRawCommandList,
                        mGenerateGeometryPipelineInitializer,
                        eastl::move(State));
                if (!Status)
                    Errors->Record("GenerateCornellGeometry bind failed", Status.mMessage);
            },
            render_graph::EARDGPassFlags::Compute |
                render_graph::EARDGPassFlags::NeverParallel);

        Graph.QueueBufferExtraction(
            Vertices, mVertexBuffer,
            rhi::EArdaRHIResourceState::AccelStructBuildInput);
        Graph.QueueBufferExtraction(
            Indices, mIndexBuffer,
            rhi::EArdaRHIResourceState::AccelStructBuildInput);
        Graph.QueueBufferExtraction(
            Materials, mMaterialBuffer,
            rhi::EArdaRHIResourceState::ShaderResource);

        if (!ExecuteGraph(Graph, "Cornell geometry generation"))
            return false;
        mError = Errors->GetFirst();
        return mError.empty() && mVertexBuffer && mIndexBuffer && mMaterialBuffer;
    }

    bool FArdaCornellBoxRenderer::BuildSceneAccelerationStructures()
    {
        const bool bCanCompact =
            mSettings.mbCompactStaticBlas &&
            mDevice->GetCapabilities().mRayTracing.mbCompaction;
        if (!BuildUncompactedSceneAccelerationStructures())
            return false;
        if (!bCanCompact)
            return mTlas != nullptr;

        rhi::FArdaRHIStatus Idle = mDevice->WaitForIdle();
        if (!Idle)
        {
            mError = Idle.mMessage;
            return false;
        }
        auto CompactedSize = mDevice->GetAccelStructCompactedSize(mBlas);
        if (!CompactedSize)
        {
            mError = CompactedSize.mStatus.mMessage;
            return false;
        }
        return CompactBlasAndBuildTlas(CompactedSize.mValue);
    }

    bool FArdaCornellBoxRenderer::BuildUncompactedSceneAccelerationStructures()
    {
        const bool bCompact = mSettings.mbCompactStaticBlas &&
            mDevice->GetCapabilities().mRayTracing.mbCompaction;
        render_graph::FARDGBuilder Graph(CreateGraphContext());
        auto* Vertices = Graph.RegisterExternalBuffer(
            mVertexBuffer, rhi::EArdaRHIResourceState::AccelStructBuildInput,
            "Cornell vertices");
        auto* Indices = Graph.RegisterExternalBuffer(
            mIndexBuffer, rhi::EArdaRHIResourceState::AccelStructBuildInput,
            "Cornell indices");

        const rhi::FArdaRHIRayTracingGeometryDesc Geometry =
            MakeGeometryDesc(mVertexBuffer, mIndexBuffer);
        rhi::FArdaRHIAccelStructDesc BlasDesc;
        BlasDesc.mBottomLevelGeometries.push_back(Geometry);
        BlasDesc.mBuildFlags = GetStaticBuildFlags(bCompact);
        BlasDesc.mDebugName = "Cornell static triangle BLAS";
        auto* Blas = Graph.CreateAccelStruct(BlasDesc);
        Graph.QueueAccelStructExtraction(
            Blas, mBlas, rhi::EArdaRHIResourceState::AccelStructRead);

        FBuildBlasParameters BlasParameters;
        BlasParameters.mVertices = {
            Vertices, rhi::EArdaRHIResourceState::AccelStructBuildInput, {}};
        BlasParameters.mIndices = {
            Indices, rhi::EArdaRHIResourceState::AccelStructBuildInput, {}};
        BlasParameters.mBlas = {
            Blas, rhi::EArdaRHIResourceState::AccelStructWrite};
        auto Errors = eastl::make_shared<FPassErrors>();
        const render_graph::FARDGPassHandle BuildBlas = Graph.AddPass(
            "BuildCornellBLAS",
            &BlasParameters,
            render_graph::EARDGPassFlags::Compute |
                render_graph::EARDGPassFlags::NeverParallel,
            [Geometry, Flags = BlasDesc.mBuildFlags, Errors](
                render_graph::FARDGPassExecutionContext& Context,
                const FBuildBlasParameters& Frozen)
            {
                eastl::vector<rhi::FArdaRHIRayTracingGeometryDesc> Geometries;
                Geometries.push_back(Geometry);
                const rhi::FArdaRHIStatus Status =
                    Context.mUnsafeRawCommandList.BuildBottomLevelAccelStruct(
                        *Context.GetAccelStruct(Frozen.mBlas.mAccelStruct),
                        Geometries,
                        Flags);
                if (!Status)
                    Errors->Record("BuildCornellBLAS failed", Status.mMessage);
            });

        if (!bCompact)
        {
            rhi::FArdaRHIAccelStructDesc TlasDesc;
            TlasDesc.mbTopLevel = true;
            TlasDesc.mTopLevelMaxInstances = 1;
            TlasDesc.mBuildFlags =
                rhi::EArdaRHIAccelStructBuildFlags::PreferFastTrace;
            TlasDesc.mDebugName = "Cornell TLAS";
            auto* Tlas = Graph.CreateAccelStruct(TlasDesc);
            Graph.QueueAccelStructExtraction(
                Tlas, mTlas, rhi::EArdaRHIResourceState::AccelStructRead);

            FBuildTlasParameters TlasParameters;
            TlasParameters.mBlas = {
                Blas, rhi::EArdaRHIResourceState::AccelStructRead};
            TlasParameters.mTlas = {
                Tlas, rhi::EArdaRHIResourceState::AccelStructWrite};
            const render_graph::FARDGPassHandle BuildTlas = Graph.AddPass(
                "BuildCornellTLAS",
                &TlasParameters,
                render_graph::EARDGPassFlags::Compute |
                    render_graph::EARDGPassFlags::NeverParallel,
                [Flags = TlasDesc.mBuildFlags, Errors](
                    render_graph::FARDGPassExecutionContext& Context,
                    const FBuildTlasParameters& Frozen)
                {
                    rhi::FArdaRHIRayTracingInstanceDesc Instance;
                    Instance.mBottomLevelAccelStruct.Reset(
                        Context.GetAccelStruct(Frozen.mBlas.mAccelStruct));
                    eastl::vector<rhi::FArdaRHIRayTracingInstanceDesc> Instances;
                    Instances.push_back(eastl::move(Instance));
                    const rhi::FArdaRHIStatus Status =
                        Context.mUnsafeRawCommandList.BuildTopLevelAccelStruct(
                            *Context.GetAccelStruct(Frozen.mTlas.mAccelStruct),
                            Instances,
                            Flags);
                    if (!Status)
                        Errors->Record("BuildCornellTLAS failed", Status.mMessage);
                });
            Graph.AddDependency(BuildBlas, BuildTlas);
        }

        if (!ExecuteGraph(Graph, "Cornell acceleration-structure build"))
            return false;
        mError = Errors->GetFirst();
        return mError.empty() && mBlas && (bCompact || mTlas);
    }

    bool FArdaCornellBoxRenderer::CompactBlasAndBuildTlas(uint64_t CompactedSize)
    {
        render_graph::FARDGBuilder Graph(CreateGraphContext());
        auto* SourceBlas = Graph.RegisterExternalAccelStruct(
            mBlas, rhi::EArdaRHIResourceState::AccelStructRead,
            "Cornell uncompacted BLAS");

        rhi::FArdaRHIAccelStructDesc CompactDesc;
        CompactDesc.mBottomLevelGeometries.push_back(
            MakeGeometryDesc(mVertexBuffer, mIndexBuffer));
        CompactDesc.mBuildFlags = GetStaticBuildFlags(true);
        CompactDesc.mResultSizeOverride = CompactedSize;
        CompactDesc.mDebugName = "Cornell compacted static BLAS";
        auto* CompactBlas = Graph.CreateAccelStruct(CompactDesc);
        Graph.QueueAccelStructExtraction(
            CompactBlas, mBlas, rhi::EArdaRHIResourceState::AccelStructRead);

        rhi::FArdaRHIAccelStructDesc TlasDesc;
        TlasDesc.mbTopLevel = true;
        TlasDesc.mTopLevelMaxInstances = 1;
        TlasDesc.mBuildFlags =
            rhi::EArdaRHIAccelStructBuildFlags::PreferFastTrace;
        TlasDesc.mDebugName = "Cornell TLAS";
        auto* Tlas = Graph.CreateAccelStruct(TlasDesc);
        Graph.QueueAccelStructExtraction(
            Tlas, mTlas, rhi::EArdaRHIResourceState::AccelStructRead);

        auto Errors = eastl::make_shared<FPassErrors>();
        FCompactBlasParameters CompactParameters;
        CompactParameters.mSource = {
            SourceBlas, rhi::EArdaRHIResourceState::AccelStructRead};
        CompactParameters.mDestination = {
            CompactBlas, rhi::EArdaRHIResourceState::AccelStructWrite};
        const render_graph::FARDGPassHandle Compact = Graph.AddPass(
            "CompactCornellBLAS",
            &CompactParameters,
            render_graph::EARDGPassFlags::Compute |
                render_graph::EARDGPassFlags::NeverParallel,
            [Errors](
                render_graph::FARDGPassExecutionContext& Context,
                const FCompactBlasParameters& Frozen)
            {
                const rhi::FArdaRHIStatus Status =
                    Context.mUnsafeRawCommandList.CompactAccelStruct(
                        *Context.GetAccelStruct(Frozen.mDestination.mAccelStruct),
                        *Context.GetAccelStruct(Frozen.mSource.mAccelStruct));
                if (!Status)
                    Errors->Record("CompactCornellBLAS failed", Status.mMessage);
            });

        FBuildTlasParameters TlasParameters;
        TlasParameters.mBlas = {
            CompactBlas, rhi::EArdaRHIResourceState::AccelStructRead};
        TlasParameters.mTlas = {
            Tlas, rhi::EArdaRHIResourceState::AccelStructWrite};
        const render_graph::FARDGPassHandle BuildTlas = Graph.AddPass(
            "BuildCornellTLASFromCompactedBLAS",
            &TlasParameters,
            render_graph::EARDGPassFlags::Compute |
                render_graph::EARDGPassFlags::NeverParallel,
            [Flags = TlasDesc.mBuildFlags, Errors](
                render_graph::FARDGPassExecutionContext& Context,
                const FBuildTlasParameters& Frozen)
            {
                rhi::FArdaRHIRayTracingInstanceDesc Instance;
                Instance.mBottomLevelAccelStruct.Reset(
                    Context.GetAccelStruct(Frozen.mBlas.mAccelStruct));
                eastl::vector<rhi::FArdaRHIRayTracingInstanceDesc> Instances;
                Instances.push_back(eastl::move(Instance));
                const rhi::FArdaRHIStatus Status =
                    Context.mUnsafeRawCommandList.BuildTopLevelAccelStruct(
                        *Context.GetAccelStruct(Frozen.mTlas.mAccelStruct),
                        Instances,
                        Flags);
                if (!Status)
                    Errors->Record("BuildCornellTLAS failed", Status.mMessage);
            });
        Graph.AddDependency(Compact, BuildTlas);

        if (!ExecuteGraph(Graph, "Cornell BLAS compaction and TLAS build"))
            return false;
        mError = Errors->GetFirst();
        return mError.empty() && mBlas && mTlas;
    }

    bool FArdaCornellBoxRenderer::ExecuteGraph(
        render_graph::FARDGBuilder& Graph,
        const char* Description)
    {
        const render_graph::FARDGExecutionResult& Result = Graph.Execute();
        if (!Result.mStatus)
        {
            mError = Description ? Description : "Cornell graph";
            mError += " failed: ";
            mError += Result.mStatus.mMessage;
            return false;
        }
        if (Result.mSubmittedCommandListCount == 0)
        {
            mError = Description ? Description : "Cornell graph";
            mError += " submitted no command lists.";
            return false;
        }
        return true;
    }

    void FArdaCornellBoxRenderer::UpdateCamera(
        float Forward,
        float Right,
        float LookX,
        float LookY,
        float DeltaSeconds)
    {
        const bool bMoved = std::abs(Forward) > 0.0f ||
            std::abs(Right) > 0.0f ||
            std::abs(LookX) > 0.0f ||
            std::abs(LookY) > 0.0f;
        if (!bMoved)
            return;

        constexpr float LookSensitivity = 0.0025f;
        constexpr float MoveSpeed = 1.1f;
        constexpr float PitchLimit = 1.50f;
        mCameraYaw += LookX * LookSensitivity;
        mCameraPitch = eastl::clamp(
            mCameraPitch - LookY * LookSensitivity, -PitchLimit, PitchLimit);
        const float CosPitch = std::cos(mCameraPitch);
        const float ForwardVector[3] = {
            std::cos(mCameraYaw) * CosPitch,
            std::sin(mCameraYaw) * CosPitch,
            std::sin(mCameraPitch)};
        const float RightVector[3] = {
            -std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
        const float InputLength = std::sqrt(Forward * Forward + Right * Right);
        if (InputLength > 1.0f)
        {
            Forward /= InputLength;
            Right /= InputLength;
        }
        const float Distance = MoveSpeed * eastl::min(DeltaSeconds, 0.1f);
        for (uint32_t Component = 0; Component < 3; ++Component)
        {
            mCameraPosition[Component] +=
                (ForwardVector[Component] * Forward +
                 RightVector[Component] * Right) * Distance;
        }
        ResetAccumulation();
    }

    void FArdaCornellBoxRenderer::NotifyResize()
    {
        ResetAccumulation();
    }

    void FArdaCornellBoxRenderer::ResetAccumulation()
    {
        mAccumulationTexture.Reset();
        mAccumulatedSamples = 0;
        mFrameIndex = 0;
    }

    bool FArdaCornellBoxRenderer::RenderFrame(
        backend::IArdaSwapChain& SwapChain)
    {
        if (!mbSceneReady)
        {
            mError = "Cornell scene acceleration structures are not ready.";
            return false;
        }

        rhi::FArdaRHIFramebufferRef Framebuffer;
        if (!SwapChain.AcquireFrame(Framebuffer))
        {
            mError = SwapChain.GetError();
            return false;
        }
        const auto& FramebufferDesc = Framebuffer->GetDesc();
        if (FramebufferDesc.mColorAttachments.empty() ||
            !FramebufferDesc.mColorAttachments[0].mTexture)
        {
            mError = "The acquired swap-chain framebuffer has no color attachment.";
            return false;
        }
        const auto ColorAttachment = FramebufferDesc.mColorAttachments[0];
        const uint32_t Width = SwapChain.GetWidth();
        const uint32_t Height = SwapChain.GetHeight();
        const uint64_t PixelCount = uint64_t(Width) * uint64_t(Height);
        const uint32_t RemainingSamples =
            mAccumulatedSamples < mSettings.mMaxSamples ?
            mSettings.mMaxSamples - mAccumulatedSamples : 0;
        const uint32_t DeviceInvocationLimit =
            mDevice->GetCapabilities().mRayTracing.
                mMaxRayDispatchInvocations;
        const uint64_t MaxSamplesByDevice = DeviceInvocationLimit == 0 ?
            uint64_t(UINT32_MAX) : DeviceInvocationLimit / PixelCount;
        const uint64_t BytesPerSample =
            PixelCount * sizeof(float) * 4ull;
        const uint64_t MaxSamplesByScratch = eastl::max<uint64_t>(
            1, MaxSampleRadianceScratchBytes / BytesPerSample);
        const uint64_t MaxSamplesByAddressing = UINT32_MAX / PixelCount;
        const uint64_t PathSegmentsPerSample = PixelCount *
            eastl::max<uint64_t>(mSettings.mMaxBounces, 1);
        const uint64_t MaxSamplesByPathWork = eastl::max<uint64_t>(
            1, MaxPathSegmentsPerDispatch / PathSegmentsPerSample);
        const uint32_t DispatchSamples = static_cast<uint32_t>(eastl::min<uint64_t>(
            eastl::min<uint64_t>(
                eastl::min<uint64_t>(
                    mSettings.mSamplesPerDispatch, RemainingSamples),
                MaxSamplesByDevice),
            eastl::min(
                eastl::min(MaxSamplesByScratch, MaxSamplesByAddressing),
                MaxSamplesByPathWork)));
        if (RemainingSamples > 0 && DispatchSamples == 0)
        {
            mError = "The image dimensions exceed the device ray-dispatch or structured-buffer addressing limit.";
            return false;
        }

        render_graph::FARDGBuilder Graph(CreateGraphContext());
        auto* BackBuffer = Graph.RegisterExternalTexture(
            ColorAttachment.mTexture,
            rhi::EArdaRHIResourceState::Present,
            "Cornell back buffer");
        auto* Vertices = Graph.RegisterExternalBuffer(
            mVertexBuffer,
            rhi::EArdaRHIResourceState::AccelStructBuildInput,
            "Cornell vertices");
        auto* Indices = Graph.RegisterExternalBuffer(
            mIndexBuffer,
            rhi::EArdaRHIResourceState::AccelStructBuildInput,
            "Cornell indices");
        auto* Materials = Graph.RegisterExternalBuffer(
            mMaterialBuffer,
            rhi::EArdaRHIResourceState::ShaderResource,
            "Cornell materials");
        auto* Tlas = Graph.RegisterExternalAccelStruct(
            mTlas,
            rhi::EArdaRHIResourceState::AccelStructRead,
            "Cornell TLAS");

        render_graph::FARDGTextureRef Accumulation = nullptr;
        const bool bCreateAccumulation = !mAccumulationTexture ||
            mAccumulationTexture->GetDesc().mWidth != Width ||
            mAccumulationTexture->GetDesc().mHeight != Height;
        if (bCreateAccumulation)
        {
            rhi::FArdaRHITextureDesc Desc;
            Desc.mDebugName = "Cornell progressive accumulation";
            Desc.mWidth = Width;
            Desc.mHeight = Height;
            Desc.mFormat = rhi::EArdaRHIFormat::RGBA32Float;
            Desc.mUsage = rhi::EArdaRHITextureUsage::ShaderResource |
                rhi::EArdaRHITextureUsage::UnorderedAccess;
            Accumulation = Graph.CreateTexture(Desc);
            Graph.QueueTextureExtraction(
                Accumulation,
                mAccumulationTexture,
                rhi::EArdaRHIResourceState::ShaderResource);
            mAccumulatedSamples = 0;
        }
        else
        {
            Accumulation = Graph.RegisterExternalTexture(
                mAccumulationTexture,
                rhi::EArdaRHIResourceState::ShaderResource,
                "Cornell progressive accumulation");
        }

        render_graph::FARDGBufferViewDesc VertexView;
        VertexView.mBuffer = Vertices->GetHandle();
        render_graph::FARDGBufferViewDesc IndexView;
        IndexView.mBuffer = Indices->GetHandle();
        render_graph::FARDGBufferViewDesc MaterialView;
        MaterialView.mBuffer = Materials->GetHandle();
        render_graph::FARDGTextureViewDesc AccumulationView;
        AccumulationView.mTexture = Accumulation->GetHandle();
        AccumulationView.mFormat = rhi::EArdaRHIFormat::RGBA32Float;

        auto* VertexSrv = Graph.CreateBufferSRV(
            "Cornell vertices SRV", VertexView);
        auto* IndexSrv = Graph.CreateBufferSRV(
            "Cornell indices SRV", IndexView);
        auto* MaterialSrv = Graph.CreateBufferSRV(
            "Cornell materials SRV", MaterialView);
        auto* AccumulationUav = Graph.CreateTextureUAV(
            "Cornell accumulation UAV", AccumulationView);
        auto* AccumulationSrv = Graph.CreateTextureSRV(
            "Cornell accumulation SRV", AccumulationView);

        render_graph::FARDGBufferUAVRef SampleRadianceUav = nullptr;
        render_graph::FARDGBufferSRVRef SampleRadianceSrv = nullptr;
        if (DispatchSamples > 0)
        {
            rhi::FArdaRHIBufferDesc SampleRadianceDesc;
            SampleRadianceDesc.mDebugName =
                "Cornell parallel sample radiance";
            SampleRadianceDesc.mByteSize =
                BytesPerSample * DispatchSamples;
            SampleRadianceDesc.mStructureStride = sizeof(float) * 4;
            SampleRadianceDesc.mUsage =
                rhi::EArdaRHIBufferUsage::Structured |
                rhi::EArdaRHIBufferUsage::ShaderResource |
                rhi::EArdaRHIBufferUsage::UnorderedAccess;
            auto* SampleRadiance = Graph.CreateBuffer(SampleRadianceDesc);
            render_graph::FARDGBufferViewDesc SampleRadianceView;
            SampleRadianceView.mBuffer = SampleRadiance->GetHandle();
            SampleRadianceUav = Graph.CreateBufferUAV(
                "Cornell parallel sample radiance UAV", SampleRadianceView);
            SampleRadianceSrv = Graph.CreateBufferSRV(
                "Cornell parallel sample radiance SRV", SampleRadianceView);
        }

        const float CosPitch = std::cos(mCameraPitch);
        const float Forward[3] = {
            std::cos(mCameraYaw) * CosPitch,
            std::sin(mCameraYaw) * CosPitch,
            std::sin(mCameraPitch)};
        const float Right[3] = {
            -std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
        const float Up[3] = {
            Forward[1] * Right[2] - Forward[2] * Right[1],
            Forward[2] * Right[0] - Forward[0] * Right[2],
            Forward[0] * Right[1] - Forward[1] * Right[0]};
        constexpr float HorizontalHalfFov = 0.6981317008f;
        const float TanHalfFovX = std::tan(HorizontalHalfFov);
        const float TanHalfFovY = TanHalfFovX *
            static_cast<float>(Height) / static_cast<float>(Width);
        FFrameConstants Constants;
        Constants.mCameraPositionAndTanHalfFovX = {
            mCameraPosition[0], mCameraPosition[1], mCameraPosition[2], TanHalfFovX};
        Constants.mCameraForwardAndTanHalfFovY = {
            Forward[0], Forward[1], Forward[2], TanHalfFovY};
        Constants.mCameraRightAndExposure = {
            Right[0], Right[1], Right[2], mSettings.mExposure};
        Constants.mCameraUpAndLightArea = {
            Up[0], Up[1], Up[2], 0.7f * 0.6f};
        Constants.mImageAndSampling = {
            Width, Height, mAccumulatedSamples, DispatchSamples};
        Constants.mPathAndSeed = {
            mSettings.mMaxBounces, mSettings.mSeed, mFrameIndex,
            mSettings.mMaxSamples};
        auto* Frame = Graph.CreateUniformBuffer(
            "Cornell frame constants", &Constants);

        auto Errors = eastl::make_shared<FPassErrors>();
        if (DispatchSamples > 0)
        {
            FPathTraceParameters TraceParameters;
            TraceParameters.mScene = {
                Tlas, rhi::EArdaRHIResourceState::AccelStructRead};
            TraceParameters.mVertices = VertexSrv;
            TraceParameters.mIndices = IndexSrv;
            TraceParameters.mMaterials = MaterialSrv;
            TraceParameters.mSampleRadiance = SampleRadianceUav;
            TraceParameters.mFrame = Frame;
            (void)Graph.AddRayDispatchPass(
                "PathTraceCornellBox",
                &TraceParameters,
                {Width, Height, DispatchSamples},
                [this, Errors](
                    render_graph::FARDGPassExecutionContext& Context,
                    const FPathTraceParameters&)
                {
                    rhi::FArdaRHIRayTracingState State;
                    State.mShaderTable = mShaderTable;
                    State.mBindings.push_back(
                        Context.CreateBindingSet(*mRayGenerationShader));
                    const rhi::FArdaRHIStatus Status =
                        Context.mUnsafeRawCommandList.SetRayTracingState(State);
                    if (!Status)
                        Errors->Record("PathTraceCornellBox bind failed", Status.mMessage);
                },
                render_graph::EARDGPassFlags::Compute |
                    render_graph::EARDGPassFlags::NeverParallel);

            FAccumulateParameters AccumulateParameters;
            AccumulateParameters.mSampleRadiance = SampleRadianceSrv;
            AccumulateParameters.mAccumulation = AccumulationUav;
            AccumulateParameters.mFrame = Frame;
            (void)Graph.AddDispatchPass(
                "ReduceCornellSampleBatch",
                &AccumulateParameters,
                {(Width + 7) / 8, (Height + 7) / 8, 1},
                [this, Errors](
                    render_graph::FARDGPassExecutionContext& Context,
                    const FAccumulateParameters&)
                {
                    rhi::FArdaRHIComputeState State;
                    State.mBindings.push_back(
                        Context.CreateBindingSet(*mAccumulateShader));
                    const rhi::FArdaRHIStatus Status =
                        mPipelineStateCache->SetComputePipelineState(
                            Context.mUnsafeRawCommandList,
                            mAccumulatePipelineInitializer,
                            eastl::move(State));
                    if (!Status)
                    {
                        Errors->Record(
                            "ReduceCornellSampleBatch bind failed",
                            Status.mMessage);
                    }
                },
                render_graph::EARDGPassFlags::Compute |
                    render_graph::EARDGPassFlags::NeverParallel);
        }

        FPresentParameters PresentParameters;
        PresentParameters.mAccumulation = AccumulationSrv;
        PresentParameters.mFrame = Frame;
        PresentParameters.mTargets.mColor[0] = {
            BackBuffer, ColorAttachment.mAttachment.mSubresources};
        (void)Graph.AddPass(
            "ToneMapAndPresentCornellBox",
            &PresentParameters,
            render_graph::EARDGPassFlags::Raster |
                render_graph::EARDGPassFlags::NeverParallel,
            [this, Framebuffer, Width, Height, Errors](
                render_graph::FARDGPassExecutionContext& Context,
                const FPresentParameters& Frozen)
            {
                (void)Context.GetTexture(Frozen.mTargets.mColor[0].mTexture);
                rhi::FArdaRHIGraphicsState State;
                State.mFramebuffer = Framebuffer;
                State.mBindings.push_back(
                    Context.CreateBindingSet(*mPresentPixelShader));
                State.mViewports.push_back({
                    0.0f, static_cast<float>(Width),
                    0.0f, static_cast<float>(Height), 0.0f, 1.0f});
                State.mScissors.push_back({
                    0, static_cast<int32_t>(Width),
                    0, static_cast<int32_t>(Height)});
                const rhi::FArdaRHIStatus Status =
                    mPipelineStateCache->SetGraphicsPipelineState(
                        Context.mUnsafeRawCommandList,
                        mPresentPipelineInitializer,
                        eastl::move(State));
                if (!Status)
                {
                    Errors->Record(
                        "ToneMapAndPresentCornellBox bind failed",
                        Status.mMessage);
                    return;
                }
                Context.mUnsafeRawCommandList.Draw({3});
            });

        SwapChain.PrepareSubmit();
        if (!ExecuteGraph(Graph, "Cornell frame graph"))
            return false;
        mError = Errors->GetFirst();
        if (!mError.empty())
            return false;
        if (!SwapChain.Present())
        {
            mError = SwapChain.GetError();
            return false;
        }
        mAccumulatedSamples += DispatchSamples;
        ++mFrameIndex;
        mError.clear();
        return true;
    }

    render_graph::FARDGRenderGraphContext
    FArdaCornellBoxRenderer::CreateGraphContext() const
    {
        return render_graph::MakeRenderGraphContext(mDevice);
    }
}
