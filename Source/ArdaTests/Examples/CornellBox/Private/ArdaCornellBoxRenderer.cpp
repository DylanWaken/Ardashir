#include "ArdaCornellBoxPch.h"

#include "ArdaCornellBoxRenderer.h"

#include <EASTL/shared_ptr.h>

namespace arda
{
	namespace
	{
		template <typename T>
		bool TakeResult(arda::TArdaRHIResult<T>& Result, T& Output, eastl::string& Error)
		{
			if (!Result)
			{
				Error = Result.mStatus.mMessage;
				return false;
			}
			Output = eastl::move(Result.mValue);
			return true;
		}

		arda::FArdaRHIRayTracingGeometryDesc MakeGeometryDesc(const arda::FArdaRHIBufferRef& Vertices,
		    const arda::FArdaRHIBufferRef& Indices)
		{
			arda::FArdaRHIRayTracingGeometryDesc Geometry;
			Geometry.mType = arda::EArdaRHIRayTracingGeometryType::Triangles;
			Geometry.mFlags = arda::EArdaRHIRayTracingGeometryFlags::Opaque;
			Geometry.mVertexOrAABBBuffer = Vertices;
			Geometry.mIndexBuffer = Indices;
			Geometry.mVertexFormat = arda::EArdaRHIFormat::RGB32Float;
			Geometry.mIndexFormat = arda::EArdaRHIFormat::R32UInt;
			Geometry.mVertexOrAABBCount = ArdaCornellVertexCount;
			Geometry.mIndexCount = ArdaCornellIndexCount;
			Geometry.mStride = sizeof(FArdaCornellVertex);
			return Geometry;
		}

		arda::EArdaRHIAccelStructBuildFlags GetStaticBuildFlags(bool bCompact)
		{
			arda::EArdaRHIAccelStructBuildFlags Flags = arda::EArdaRHIAccelStructBuildFlags::PreferFastTrace;
			if (bCompact)
			{
				Flags |= arda::EArdaRHIAccelStructBuildFlags::AllowCompaction;
			}
			return Flags;
		}
	}

	bool FArdaCornellBoxRenderer::Initialize(arda::FArdaRHIDeviceRef Device,
	    arda::EArdaRHIFormat SwapChainFormat,
	    const FArdaCornellBoxSettings& Settings)
	{
		mDevice = eastl::move(Device);
		mSettings = Settings;
		if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
		{
			mError = "The initialized backend does not expose a graphics device.";
			return false;
		}

		const arda::FArdaRHIRayTracingCapabilities& RayTracing = mDevice->GetCapabilities().mRayTracing;
		if (!RayTracing.mbHardwareAccelerated || !RayTracing.mbPipelineShaders ||
		    !RayTracing.mbAccelerationStructures || !RayTracing.mbBottomLevel || !RayTracing.mbTopLevel)
		{
			mError = "CornellBox requires hardware ray tracing, pipeline shaders, and BLAS/TLAS support.";
			return false;
		}
		if (RayTracing.mMaxRecursionDepth < 1)
		{
			mError = "The ray-tracing device reports no supported recursion depth.";
			return false;
		}

		(void)SwapChainFormat;
		if (!CreateSceneGeometryResources() || !BuildSceneAccelerationStructures() || !CreateFrameTlasResource())
		{
			return false;
		}
		mbSceneReady = true;
		NotifyResize();
		mError.clear();
		return true;
	}

	bool FArdaCornellBoxRenderer::CreateSceneGeometryResources()
	{
		arda::FArdaRHIBufferDesc VertexDesc;
		VertexDesc.mDebugName = "Cornell GPU-generated vertices";
		VertexDesc.mByteSize = uint64_t(ArdaCornellVertexCount) * sizeof(FArdaCornellVertex);
		VertexDesc.mStructureStride = sizeof(FArdaCornellVertex);
		VertexDesc.mUsage = arda::EArdaRHIBufferUsage::Structured | arda::EArdaRHIBufferUsage::ShaderResource |
		    arda::EArdaRHIBufferUsage::UnorderedAccess | arda::EArdaRHIBufferUsage::Vertex |
		    arda::EArdaRHIBufferUsage::AccelStructBuildInput;
		auto Vertices = mDevice->CreateBuffer(VertexDesc);
		if (!TakeResult(Vertices, mVertexBuffer, mError))
		{
			return false;
		}

		arda::FArdaRHIBufferDesc IndexDesc;
		IndexDesc.mDebugName = "Cornell GPU-generated indices";
		IndexDesc.mByteSize = uint64_t(ArdaCornellIndexCount) * sizeof(uint32_t);
		IndexDesc.mStructureStride = sizeof(uint32_t);
		IndexDesc.mFormat = arda::EArdaRHIFormat::R32UInt;
		IndexDesc.mUsage = arda::EArdaRHIBufferUsage::Structured | arda::EArdaRHIBufferUsage::ShaderResource |
		    arda::EArdaRHIBufferUsage::UnorderedAccess | arda::EArdaRHIBufferUsage::Index |
		    arda::EArdaRHIBufferUsage::AccelStructBuildInput;
		auto Indices = mDevice->CreateBuffer(IndexDesc);
		if (!TakeResult(Indices, mIndexBuffer, mError))
		{
			return false;
		}

		arda::FArdaRHIBufferDesc MaterialDesc;
		MaterialDesc.mDebugName = "Cornell GPU-generated materials";
		MaterialDesc.mByteSize = uint64_t(ArdaCornellMaterialCount) * sizeof(FArdaCornellMaterial);
		MaterialDesc.mStructureStride = sizeof(FArdaCornellMaterial);
		MaterialDesc.mUsage = arda::EArdaRHIBufferUsage::Structured | arda::EArdaRHIBufferUsage::ShaderResource |
		    arda::EArdaRHIBufferUsage::UnorderedAccess;
		auto Materials = mDevice->CreateBuffer(MaterialDesc);
		if (!TakeResult(Materials, mMaterialBuffer, mError))
		{
			return false;
		}

		return true;
	}

	bool FArdaCornellBoxRenderer::BuildSceneAccelerationStructures()
	{
		const bool bCanCompact = mSettings.mbCompactStaticBlas && mDevice->GetCapabilities().mRayTracing.mbCompaction;
		if (!BuildUncompactedSceneAccelerationStructures())
		{
			return false;
		}
		if (!bCanCompact)
		{
			return true;
		}

		// The setup graph has completed; the compaction size is now available to the CPU.
		auto CompactedSize = mDevice->GetAccelStructCompactedSize(mBlas);
		if (!CompactedSize)
		{
			mError = CompactedSize.mStatus.mMessage;
			return false;
		}
		return CompactSceneBlas(CompactedSize.mValue);
	}

	bool FArdaCornellBoxRenderer::BuildUncompactedSceneAccelerationStructures()
	{
		const bool Compact = mSettings.mbCompactStaticBlas && mDevice->GetCapabilities().mRayTracing.mbCompaction;
		FArdaRHIAccelStructDesc Desc;
		Desc.mBottomLevelGeometries = {MakeGeometryDesc(mVertexBuffer, mIndexBuffer)};
		Desc.mBuildFlags = GetStaticBuildFlags(Compact);
		Desc.mDebugName = "Cornell static triangle BLAS";
		auto Blas = mDevice->CreateAccelStruct(Desc);
		if (!TakeResult(Blas, mBlas, mError))
		{
			return false;
		}
		auto Requirements = mDevice->GetAccelStructBuildMemoryRequirements(Desc);
		if (!Requirements)
		{
			mError = Requirements.mStatus.mMessage;
			return false;
		}
		FArdaDependencyGraph Graph(mDevice);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			mError = S.mMessage;
			return false;
		}
		auto V = Graph.ImportBuffer("vertices", mVertexBuffer), I = Graph.ImportBuffer("indices", mIndexBuffer);
		auto B = Graph.ImportAccelerationStructure("BLAS", mBlas);
		auto M = Graph.ImportBuffer("materials", mMaterialBuffer);
		if (!V || !I || !B || !M)
		{
			mError = "Failed to import Cornell BLAS resources.";
			return false;
		}
		FArdaCornellNodeParameters P;
		P.mResources = {V.mValue, I.mValue, B.mValue};
		P.mBuildFlags = Desc.mBuildFlags;
		P.mVertexCount = ArdaCornellVertexCount;
		P.mIndexCount = ArdaCornellIndexCount;
		P.mVertexStride = sizeof(FArdaCornellVertex);
		P.mWorkspaceBytes = Requirements.mValue.mBuildScratchSize;
		auto Build = Graph.AttachOrFind<FArdaCornellBuildBlasNode>("BuildCornellBLAS", P);
		if (!Build)
		{
			mError = Build.mStatus.mMessage;
			return false;
		}
		// Add the producer after its consumers; the compiler derives geometry -> BLAS.
		FArdaCornellNodeParameters Geometry;
		Geometry.mResources = {V.mValue, I.mValue, M.mValue};
		Geometry.mWidth = (ArdaCornellTriangleCount + 63) / 64;
		auto Generate = Graph.AttachOrFind<FArdaCornellGeometryNode>("GenerateCornellGeometry", Geometry);
		if (!Generate)
		{
			mError = Generate.mStatus.mMessage;
			return false;
		}
		return ExecuteGraph(Graph, "Cornell geometry and acceleration-structure build");
	}

	bool FArdaCornellBoxRenderer::CompactSceneBlas(uint64_t CompactedSize)
	{
		FArdaRHIAccelStructDesc Desc;
		Desc.mBottomLevelGeometries = {MakeGeometryDesc(mVertexBuffer, mIndexBuffer)};
		Desc.mBuildFlags = GetStaticBuildFlags(true);
		Desc.mResultSizeOverride = CompactedSize;
		Desc.mDebugName = "Cornell compacted BLAS";
		auto Compact = mDevice->CreateAccelStruct(Desc);
		if (!Compact)
		{
			mError = Compact.mStatus.mMessage;
			return false;
		}
		FArdaDependencyGraph Graph(mDevice);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			mError = S.mMessage;
			return false;
		}
		auto Source = Graph.ImportAccelerationStructure("source BLAS", mBlas);
		auto Destination = Graph.ImportAccelerationStructure("compacted BLAS", Compact.mValue);
		if (!Source || !Destination)
		{
			mError = "Failed to import Cornell compaction resources.";
			return false;
		}
		FArdaCornellNodeParameters P;
		P.mResources = {Source.mValue, Destination.mValue};
		auto Copy = Graph.AttachOrFind<FArdaCornellCompactBlasNode>("CompactCornellBLAS", P);
		if (!Copy)
		{
			mError = Copy.mStatus.mMessage;
			return false;
		}
		if (!ExecuteGraph(Graph, "Cornell BLAS compaction"))
		{
			return false;
		}
		mBlas = eastl::move(Compact.mValue);
		return true;
	}

	bool FArdaCornellBoxRenderer::CreateFrameTlasResource()
	{
		FArdaRHIAccelStructDesc Desc;
		Desc.mbTopLevel = true;
		Desc.mTopLevelMaxInstances = 1;
		Desc.mBuildFlags = GetStaticBuildFlags(false);
		Desc.mDebugName = "Cornell per-frame TLAS";
		auto Tlas = mDevice->CreateAccelStruct(Desc);
		if (!TakeResult(Tlas, mTlas, mError))
		{
			return false;
		}
		auto Requirements = mDevice->GetAccelStructBuildMemoryRequirements(Desc);
		if (!Requirements)
		{
			mError = Requirements.mStatus.mMessage;
			return false;
		}
		mTlasWorkspaceBytes = Requirements.mValue.mBuildScratchSize + 65536;
		return true;
	}

	bool FArdaCornellBoxRenderer::ExecuteGraph(FArdaDependencyGraph& Graph, const char* Description)
	{
		if (Graph.IsEditing())
		{
			if (auto Status = Graph.EndGraphEdit(); !Status)
			{
				mError = eastl::string(Description) + " compilation failed: " + Status.mMessage;
				return false;
			}
		}
		const auto Result = Graph.Execute();
		if (!Result.mStatus)
		{
			mError = eastl::string(Description) + " failed: " + Result.mStatus.mMessage;
			return false;
		}
		if (Result.mSubmittedCommandListCount == 0)
		{
			mError = eastl::string(Description) + " submitted no command lists.";
			return false;
		}
		return true;
	}

	void FArdaCornellBoxRenderer::UpdateCamera(float Forward, float Right, float LookX, float LookY, float DeltaSeconds)
	{
		const bool bMoved =
		    std::abs(Forward) > 0.0f || std::abs(Right) > 0.0f || std::abs(LookX) > 0.0f || std::abs(LookY) > 0.0f;
		if (!bMoved)
		{
			return;
		}

		constexpr float LookSensitivity = 0.0025f;
		constexpr float MoveSpeed = 1.1f;
		constexpr float PitchLimit = 1.50f;
		mCameraYaw += LookX * LookSensitivity;
		mCameraPitch = eastl::clamp(mCameraPitch - LookY * LookSensitivity, -PitchLimit, PitchLimit);
		const float CosPitch = std::cos(mCameraPitch);
		const float ForwardVector[3] = {std::cos(mCameraYaw) * CosPitch,
		    std::sin(mCameraYaw) * CosPitch,
		    std::sin(mCameraPitch)};
		const float RightVector[3] = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
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
			    (ForwardVector[Component] * Forward + RightVector[Component] * Right) * Distance;
		}
		ResetAccumulation();
	}

	void FArdaCornellBoxRenderer::NotifyResize()
	{
		mFrames.clear();
		mAccumulationTexture.Reset();
		ResetAccumulation();
	}

	void FArdaCornellBoxRenderer::ResetAccumulation()
	{
		// Sample zero overwrites the accumulation image; camera motion keeps compiled frame pools.
		mAccumulatedSamples = 0;
		mFrameIndex = 0;
	}

	bool FArdaCornellBoxRenderer::RenderFrame(arda::IArdaSwapChain& SwapChain)
	{
		if (!mbSceneReady)
		{
			mError = "Cornell scene acceleration structures are not ready.";
			return false;
		}

		arda::FArdaRHIFramebufferRef Framebuffer;
		if (!SwapChain.AcquireFrame(Framebuffer))
		{
			mError = SwapChain.GetError();
			return false;
		}
		const auto& FramebufferDesc = Framebuffer->GetDesc();
		if (FramebufferDesc.mColorAttachments.empty() || !FramebufferDesc.mColorAttachments[0].mTexture)
		{
			mError = "The acquired swap-chain framebuffer has no color attachment.";
			return false;
		}
		const auto ColorAttachment = FramebufferDesc.mColorAttachments[0];
		const uint32_t Width = SwapChain.GetWidth();
		const uint32_t Height = SwapChain.GetHeight();
		const uint64_t PixelCount = uint64_t(Width) * uint64_t(Height);
		if (!PixelCount)
		{
			mError = "Cornell frame dimensions must be nonzero.";
			return false;
		}
		const bool CreateAccumulation = !mAccumulationTexture || mAccumulationTexture->GetDesc().mWidth != Width ||
		    mAccumulationTexture->GetDesc().mHeight != Height;
		if (CreateAccumulation)
		{
			NotifyResize();
		}
		const uint32_t RemainingSamples =
		    mAccumulatedSamples < mSettings.mMaxSamples ? mSettings.mMaxSamples - mAccumulatedSamples : 0;
		const uint32_t DeviceInvocationLimit = mDevice->GetCapabilities().mRayTracing.mMaxRayDispatchInvocations;
		const uint64_t MaxSamplesByDevice =
		    DeviceInvocationLimit == 0 ? uint64_t(UINT32_MAX) : DeviceInvocationLimit / PixelCount;
		const uint64_t BytesPerSample = PixelCount * sizeof(float) * 4ull;
		const uint64_t MaxSamplesByScratch =
		    eastl::max<uint64_t>(1, ArdaCornellMaxSampleRadianceScratchBytes / BytesPerSample);
		const uint64_t MaxSamplesByAddressing = UINT32_MAX / PixelCount;
		const uint64_t PathSegmentsPerSample = PixelCount * eastl::max<uint64_t>(mSettings.mMaxBounces, 1);
		const uint64_t MaxSamplesByPathWork =
		    eastl::max<uint64_t>(1, ArdaCornellMaxPathSegmentsPerDispatch / PathSegmentsPerSample);
		const uint32_t DispatchSamples = static_cast<uint32_t>(eastl::min<uint64_t>(
		    eastl::min<uint64_t>(eastl::min<uint64_t>(mSettings.mSamplesPerDispatch, RemainingSamples),
		        MaxSamplesByDevice),
		    eastl::min(eastl::min(MaxSamplesByScratch, MaxSamplesByAddressing), MaxSamplesByPathWork)));
		if (RemainingSamples > 0 && DispatchSamples == 0)
		{
			mError = "The image dimensions exceed the device ray-dispatch or structured-buffer addressing limit.";
			return false;
		}

		if (CreateAccumulation)
		{
			FArdaRHITextureDesc Desc;
			Desc.mDebugName = "Cornell progressive accumulation";
			Desc.mInitialState = EArdaRHIResourceState::Common;
			Desc.mWidth = Width;
			Desc.mHeight = Height;
			Desc.mFormat = EArdaRHIFormat::RGBA32Float;
			Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
			auto Created = mDevice->CreateTexture(Desc);
			if (!TakeResult(Created, mAccumulationTexture, mError))
			{
				return false;
			}
		}
		auto Found = eastl::find_if(mFrames.begin(),
		    mFrames.end(),
		    [&](const auto& F)
		    {
			    return F->mBackBuffer.Get() == ColorAttachment.mTexture.Get();
		    });
		eastl::shared_ptr<FCachedFrame> Frame = Found == mFrames.end() ? eastl::make_shared<FCachedFrame>() : *Found;
		if (!Frame->mGraph || Frame->mDispatchSamples != DispatchSamples)
		{
			Frame->mBackBuffer = ColorAttachment.mTexture;
			Frame->mDispatchSamples = DispatchSamples;
			Frame->mInput = eastl::make_shared<FArdaCornellFrameInput>();
			Frame->mGraph = std::make_unique<FArdaDependencyGraph>(mDevice);
			auto& Graph = *Frame->mGraph;
			if (auto S = Graph.BeginGraphEdit(); !S)
			{
				mError = S.mMessage;
				return false;
			}
			auto T = Graph.ImportAccelerationStructure("Frame TLAS", mTlas);
			auto B = Graph.ImportAccelerationStructure("Static BLAS", mBlas);
			auto Back = Graph.ImportTexture("back buffer", ColorAttachment.mTexture);
			auto Accum = Graph.ImportTexture("accumulation", mAccumulationTexture);
			FArdaRHIBufferDesc ConstantsDesc;
			ConstantsDesc.mByteSize = sizeof(FArdaCornellFrameConstants);
			ConstantsDesc.mUsage = EArdaRHIBufferUsage::Constant;
			auto Constants = Graph.CreateBuffer("frame constants", ConstantsDesc);
			if (!Back || !Accum || !Constants || !T || !B)
			{
				mError = "Failed to declare Cornell frame resources.";
				return false;
			}
			const auto Attached = [&](const TArdaRHIResult<FArdaGraphNodeHandle>& Result)
			{
				if (!Result)
				{
					mError = Result.mStatus.mMessage;
				}
				return bool(Result);
			};
			FArdaCornellNodeParameters P;
			P.mFrame = Frame->mInput;
			P.mResources = {Accum.mValue, Constants.mValue, Back.mValue};
			P.mWidth = Width;
			P.mHeight = Height;
			if (!Attached(Graph.AttachOrFind<FArdaCornellPresentNode>("ToneMapAndPresentCornellBox", P)))
			{
				return false;
			}
			P.mResources = {Constants.mValue};
			if (!Attached(Graph.AttachOrFind<FArdaCornellUploadFrameNode>("UpdateCornellFrameConstants", P)))
			{
				return false;
			}
			if (DispatchSamples)
			{
				auto V = Graph.ImportBuffer("vertices", mVertexBuffer), I = Graph.ImportBuffer("indices", mIndexBuffer),
				     M = Graph.ImportBuffer("materials", mMaterialBuffer);
				FArdaRHIBufferDesc SamplesDesc;
				SamplesDesc.mByteSize = BytesPerSample * DispatchSamples;
				SamplesDesc.mStructureStride = sizeof(float) * 4;
				SamplesDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
				    EArdaRHIBufferUsage::UnorderedAccess;
				auto Samples = Graph.CreateBuffer("sample radiance", SamplesDesc);
				if (!V || !I || !M || !T || !B || !Samples)
				{
					mError = "Failed to declare Cornell ray resources.";
					return false;
				}
				P.mResources = {T.mValue, V.mValue, I.mValue, M.mValue, Samples.mValue, Constants.mValue, B.mValue};
				P.mWidth = Width;
				P.mHeight = Height;
				P.mSamples = DispatchSamples;
				if (!Attached(Graph.AttachOrFind<FArdaCornellTraceNode>("PathTraceCornellBox", P)))
				{
					return false;
				}
				P.mResources = {Samples.mValue, Accum.mValue, Constants.mValue};
				P.mWidth = (Width + 7) / 8;
				P.mHeight = (Height + 7) / 8;
				if (!Attached(Graph.AttachOrFind<FArdaCornellAccumulateNode>("ReduceCornellSampleBatch", P)))
				{
					return false;
				}
			}
			// A real frame operation, even after sampling finishes. The TLAS write orders all ray reads.
			FArdaCornellNodeParameters BuildTlas;
			BuildTlas.mResources = {B.mValue, T.mValue};
			BuildTlas.mBuildFlags = GetStaticBuildFlags(false);
			BuildTlas.mWorkspaceBytes = mTlasWorkspaceBytes;
			if (!Attached(Graph.AttachOrFind<FArdaCornellBuildTlasNode>("RebuildCornellFrameTLAS", BuildTlas)))
			{
				return false;
			}
			if (auto S = Graph.EndGraphEdit(); !S)
			{
				mError = S.mMessage;
				return false;
			}
			if (Found == mFrames.end())
			{
				mFrames.push_back(Frame);
			}
		}

		const float CosPitch = std::cos(mCameraPitch);
		const float Forward[3] = {std::cos(mCameraYaw) * CosPitch,
		    std::sin(mCameraYaw) * CosPitch,
		    std::sin(mCameraPitch)};
		const float Right[3] = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
		const float Up[3] = {Forward[1] * Right[2] - Forward[2] * Right[1],
		    Forward[2] * Right[0] - Forward[0] * Right[2],
		    Forward[0] * Right[1] - Forward[1] * Right[0]};
		constexpr float HorizontalHalfFov = 0.6981317008f;
		const float TanHalfFovX = std::tan(HorizontalHalfFov);
		const float TanHalfFovY = TanHalfFovX * static_cast<float>(Height) / static_cast<float>(Width);
		FArdaCornellFrameConstants Constants;
		Constants.mCameraPositionAndTanHalfFovX = {mCameraPosition[0],
		    mCameraPosition[1],
		    mCameraPosition[2],
		    TanHalfFovX};
		Constants.mCameraForwardAndTanHalfFovY = {Forward[0], Forward[1], Forward[2], TanHalfFovY};
		Constants.mCameraRightAndExposure = {Right[0], Right[1], Right[2], mSettings.mExposure};
		Constants.mCameraUpAndLightArea = {Up[0], Up[1], Up[2], 0.7f * 0.6f};
		Constants.mImageAndSampling = {Width, Height, mAccumulatedSamples, DispatchSamples};
		Constants.mPathAndSeed = {mSettings.mMaxBounces, mSettings.mSeed, mFrameIndex, mSettings.mMaxSamples};
		Frame->mInput->mConstants = Constants;
		SwapChain.PrepareSubmit();
		if (!ExecuteGraph(*Frame->mGraph, "Cornell frame graph"))
		{
			return false;
		}

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

}
