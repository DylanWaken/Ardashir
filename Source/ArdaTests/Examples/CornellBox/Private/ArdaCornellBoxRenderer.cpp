#include "ArdaCornellBoxPch.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"

#include "ArdaCornellBoxRenderer.h"
#include "Nodes/ArdaCornellTensorNodes.h"

#include <EASTL/shared_ptr.h>
#include <fstream>

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
		if (!mDevice || !mDevice->GetFeatures()->Supports("queue.graphics"))
		{
			mError = "The initialized backend does not expose a graphics device.";
			return false;
		}
		if (!mSettings.mDenoisingPackage.empty())
		{
			if (!mDevice->GetCudaCapabilities())
			{
				mError = "The denoising example requires CUDA graphics interoperability: ";
				mError += mDevice->GetCudaCapabilities().mUnavailableReason;
				return false;
			}
			auto Loaded = FArdaGraphPackage::Load(mSettings.mDenoisingPackage);
			if (Loaded && (!Loaded.mValue->HasStateDict() || !mSettings.mDenoisingStateDict.empty()))
			{
				auto StatePath = mSettings.mDenoisingStateDict;
				if (StatePath.empty())
				{
					StatePath = mSettings.mDenoisingPackage;
					StatePath.replace_extension(".ardastate");
				}
				Loaded = Loaded.mValue->LoadStateDict(StatePath);
			}
			if (!Loaded)
			{
				mError = Loaded.mStatus.mMessage;
				return false;
			}
			mDenoisingPackage = eastl::move(Loaded.mValue);
			const FArdaGraphPackageTensor* Input = nullptr;
			const FArdaGraphPackageTensor* Output = nullptr;
			for (const auto& Tensor : mDenoisingPackage->GetTensors())
			{
				if (Tensor.mName == "noisy_hdr" && Tensor.mRole == 1)
				{
					Input = &Tensor;
				}
				if (Tensor.mName == "denoised_hdr" && Tensor.mRole == 2)
				{
					Output = &Tensor;
				}
			}
			if (!Output || Output->mDesc.mDataType != EArdaRHIDataType::Float32 ||
			    Output->mDesc.mDimensions.size() != 4 || Output->mDesc.mDimensions[0] != 1 ||
			    Output->mDesc.mDimensions[1] != 3 || Output->mDesc.mByteOffset != 0)
			{
				mError = "Denoising requires a contiguous float32 denoised_hdr [1,3,H,W] output at byte origin zero.";
				return false;
			}
			auto Contiguous = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, Output->mDesc.mDimensions);
			if (!Contiguous || Contiguous.mValue != Output->mDesc)
			{
				mError = "Cornell denoising tensor ports must use contiguous NCHW storage.";
				return false;
			}
			mDenoisingTensorDesc = Output->mDesc;
			mbTemporalDenoising = !Input;
			if (mbTemporalDenoising)
			{
				const auto Height = Output->mDesc.mDimensions[2], Width = Output->mDesc.mDimensions[3];
				const auto Matches = [&](const char* Name, uint32_t Role, eastl::vector<uint64_t> Shape)
				{
					for (const auto& Tensor : mDenoisingPackage->GetTensors())
					{
						auto Expected = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32,
						    Shape,
						    Role == 2 ? Tensor.mDesc.mByteOffset : 0);
						if (Tensor.mName == Name && Tensor.mRole == Role && Expected && Tensor.mDesc == Expected.mValue)
						{
							return true;
						}
					}
					return false;
				};
				if (mDenoisingPackage->GetTensors().size() != 10 || !Matches("color", 1, {1, 3, Height, Width, 2}) ||
				    !Matches("depth", 1, {1, 1, Height, Width, 2}) || !Matches("normal", 1, {1, 3, Height, Width, 2}) ||
				    !Matches("diffuse", 1, {1, 3, Height, Width, 2}) ||
				    !Matches("motion", 1, {1, 2, Height, Width, 2}) || !Matches("crop_offset", 1, {1, 2}) ||
				    !Matches("previous_crop_offset", 1, {1, 2}) || !Matches("temporal", 1, {1, 38, Height, Width}) ||
				    !Matches("next_temporal", 2, {1, 38, Height, Width}))
				{
					mError =
					    "Temporal Cornell denoising requires the NPPD two-sample input and 38-channel history contract.";
					return false;
				}
				mSettings.mSamplesPerDispatch = 2;
				if (mSettings.mMaxSamples < 2 || mSettings.mMaxSamples % 2)
				{
					mError = "NPPD requires an even --max-samples of at least two.";
					return false;
				}
			}
			else if (Input->mDesc != Output->mDesc || mDenoisingPackage->GetTensors().size() != 2)
			{
				mError = "Spatial Cornell denoising requires matching noisy_hdr and denoised_hdr ports.";
				return false;
			}
			if (!mSettings.mDenoisingDumpPrefix.empty())
			{
				mNoisySnapshot = eastl::make_shared<eastl::vector<uint8_t>>();
				mDenoisedSnapshot = eastl::make_shared<eastl::vector<uint8_t>>();
				if (mbTemporalDenoising)
				{
					for (const auto& Tensor : mDenoisingPackage->GetTensors())
					{
						mTensorSnapshots.push_back({Tensor.mName, eastl::make_shared<eastl::vector<uint8_t>>()});
					}
				}
			}
		}

		const auto RaySupport = mDevice->RequireFeatures({{"ray-tracing.hardware"}, {"ray-tracing.pipeline"},
		    {"ray-tracing.acceleration-structure"}, {"ray-tracing.bottom-level-acceleration-structure"},
		    {"ray-tracing.top-level-acceleration-structure"}, {"ray-tracing.recursion-depth", 1}});
		if (!RaySupport)
		{
			mError = RaySupport.mMessage;
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
		const bool bCanCompact = mSettings.mbCompactStaticBlas && mDevice->GetFeatures()->Supports("ray-tracing.acceleration-structure-compaction");
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
		const bool Compact = mSettings.mbCompactStaticBlas && mDevice->GetFeatures()->Supports("ray-tracing.acceleration-structure-compaction");
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
		FArdaGraphEdit Edit(Graph);
		if (auto S = Edit.GetStatus(); !S)
		{
			mError = S.mMessage;
			return false;
		}
		auto V = BindBuffer(Edit, mVertexBuffer, "vertices"), I = BindBuffer(Edit, mIndexBuffer, "indices");
		auto B = BindAccelerationStructure(Edit, mBlas, "BLAS");
		auto M = BindBuffer(Edit, mMaterialBuffer, "materials");
		if (!V || !I || !B || !M)
		{
			mError = "Failed to import Cornell BLAS resources.";
			return false;
		}
		FArdaCornellBuildBlasParameters P;
		P.mVertices = V;
		P.mIndices = I;
		P.mBlas = B;
		P.mBuildFlags = Desc.mBuildFlags;
		P.mVertexCount = ArdaCornellVertexCount;
		P.mIndexCount = ArdaCornellIndexCount;
		P.mVertexStride = sizeof(FArdaCornellVertex);
		P.mWorkspaceBytes = Requirements.mValue.mBuildScratchSize;
		auto Build = CornellBuildBlas(Edit, P, "BuildCornellBLAS");
		if (!Build)
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		// Add the producer after its consumers; the compiler derives geometry -> BLAS.
		FArdaCornellGeometryParameters Geometry;
		Geometry.mVertices = V;
		Geometry.mIndices = I;
		Geometry.mMaterials = M;
		Geometry.mGroupCountX = (ArdaCornellTriangleCount + 63) / 64;
		auto Generate = CornellGeometry(Edit, Geometry, "GenerateCornellGeometry");
		if (!Generate)
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		if (auto Status = Edit.Commit(); !Status)
		{
			mError = Status.mMessage;
			return false;
		}
		if (auto Status = Graph.CompileAndWait(); !Status)
		{
			mError = Status.mMessage;
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
		FArdaGraphEdit Edit(Graph);
		if (auto S = Edit.GetStatus(); !S)
		{
			mError = S.mMessage;
			return false;
		}
		auto Source = BindAccelerationStructure(Edit, mBlas, "source BLAS");
		auto Destination = BindAccelerationStructure(Edit, Compact.mValue, "compacted BLAS");
		if (!Source || !Destination)
		{
			mError = "Failed to import Cornell compaction resources.";
			return false;
		}
		FArdaCornellCompactBlasParameters P;
		P.mSource = Source;
		P.mDestination = Destination;
		auto Copy = CornellCompactBlas(Edit, P, "CompactCornellBLAS");
		if (!Copy)
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		if (auto Status = Edit.Commit(); !Status)
		{
			mError = Status.mMessage;
			return false;
		}
		if (auto Status = Graph.CompileAndWait(); !Status)
		{
			mError = Status.mMessage;
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
		const auto Result = Graph.ExecuteAndWait();
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

		// Derive the current camera basis before updating retained frame constants.
		const float CosPitch = std::cos(mCameraPitch);
		const FArdaVector3f ForwardVector = {std::cos(mCameraYaw) * CosPitch,
		    std::sin(mCameraYaw) * CosPitch,
		    std::sin(mCameraPitch)};
		const FArdaVector3f RightVector = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
		const float InputLength = FArdaVector2f(Forward, Right).norm();
		if (InputLength > 1.0f)
		{
			Forward /= InputLength;
			Right /= InputLength;
		}
		const float Distance = MoveSpeed * eastl::min(DeltaSeconds, 0.1f);
		mCameraPosition += (ForwardVector * Forward + RightVector * Right) * Distance;
		ResetAccumulation();
	}

	void FArdaCornellBoxRenderer::NotifyResize()
	{
		mFrames.clear();
		mAccumulationTexture.Reset();
		mDenoisedTexture.Reset();
		mTemporalHistory[0].Reset();
		mTemporalHistory[1].Reset();
		ResetAccumulation();
	}

	void FArdaCornellBoxRenderer::ResetAccumulation()
	{
		// Sample zero overwrites the accumulation image; camera motion keeps compiled frame pools.
		mAccumulatedSamples = 0;
		mFrameIndex = 0;
		if (mbTemporalDenoising)
		{
			// A moving camera is a cut in this static-scene demonstration: discard stale history.
			mFrames.clear();
		}
	}

	bool FArdaCornellBoxRenderer::AppendDenoising(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Accumulation,
	    FArdaGraphBuffer Constants,
	    uint32_t Width,
	    uint32_t Height,
	    FArdaGraphTexture& OutImage)
	{
		FArdaRHIBufferDesc Storage;
		Storage.mByteSize = uint64_t(Width) * Height * 3 * sizeof(float);
		Storage.mStructureStride = sizeof(float);
		Storage.mbCudaInterop = true;
		Storage.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
		    EArdaRHIBufferUsage::UnorderedAccess;
		const auto Input = AllocateBuffer(Edit, Storage, "Cornell noisy HDR tensor");
		const auto Output = AllocateBuffer(Edit, Storage, "Cornell denoised HDR tensor");
		if (!Input || !Output)
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		FArdaCornellTensorPackParameters Pack;
		Pack.mImage = Accumulation;
		Pack.mTensor = Input;
		Pack.mConstants = Constants;
		Pack.mWidth = Width;
		Pack.mHeight = Height;
		if (!CornellTensorPack(Edit, Pack, "PackCornellLinearHdr"))
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		auto Inference = mDenoisingPackage->Invoke(Edit,
		    {{"noisy_hdr", {Input, mDenoisingTensorDesc}}, {"denoised_hdr", {Output, mDenoisingTensorDesc}}},
		    "PretrainedCornellDenoising");
		if (!Inference)
		{
			mError = Inference.mStatus.mMessage;
			return false;
		}
		FArdaCornellTensorUnpackParameters Unpack;
		Unpack.mTensor = Output;
		Unpack.mImage = OutImage;
		Unpack.mConstants = Constants;
		Unpack.mWidth = Width;
		Unpack.mHeight = Height;
		if (!CornellTensorUnpack(Edit, Unpack, "UnpackCornellLinearHdr"))
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		if (mNoisySnapshot)
		{
			if (!ReadbackNode(Edit, FArdaGraphReadbackParameters{Input, mNoisySnapshot}, "ReadNoisyHdrForValidation") ||
			    !ReadbackNode(Edit,
			        FArdaGraphReadbackParameters{Output, mDenoisedSnapshot},
			        "ReadDenoisedHdrForValidation"))
			{
				mError = Edit.GetStatus().mMessage;
				return false;
			}
		}
		return true;
	}

	bool FArdaCornellBoxRenderer::AppendTemporalDenoising(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Radiance,
	    FArdaGraphBuffer Guides,
	    FArdaGraphBuffer Constants,
	    uint32_t Width,
	    uint32_t Height,
	    FArdaGraphTexture OutImage)
	{
		eastl::vector<FArdaGraphPackageBinding> Bindings;
		FArdaRHIBufferDesc Storage;
		Storage.mStructureStride = sizeof(float);
		Storage.mbCudaInterop = true;
		Storage.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
		    EArdaRHIBufferUsage::UnorderedAccess;
		// Both model outputs are views in one exported allocation. Preserve that storage identity.
		uint64_t OutputBytes = 0;
		for (const auto& Tensor : mDenoisingPackage->GetTensors())
		{
			if (Tensor.mRole == 2)
			{
				auto Range = GetArdaRHITensorByteRange(Tensor.mDesc);
				if (!Range)
				{
					mError = Range.mStatus.mMessage;
					return false;
				}
				OutputBytes = eastl::max(OutputBytes, Tensor.mStorageByteSize);
			}
		}
		Storage.mByteSize = OutputBytes;
		const auto OutputArena = AllocateBuffer(Edit, Storage, "Cornell temporal model output arena");
		for (const auto& Tensor : mDenoisingPackage->GetTensors())
		{
			auto Range = GetArdaRHITensorByteRange(Tensor.mDesc);
			if (!Range)
			{
				mError = Range.mStatus.mMessage;
				return false;
			}
			Storage.mByteSize = Tensor.mStorageByteSize;
			FArdaGraphBuffer Buffer;
			if (Tensor.mRole == 2)
			{
				Buffer = OutputArena;
			}
			else if (Tensor.mName == "temporal")
			{
				const uint32_t Slot = mFrameIndex % 2;
				Buffer = BindBuffer(Edit, mTemporalHistory[Slot], Tensor.mName);
				if (Tensor.mName == "temporal" && mFrameIndex == 0 &&
				    !ClearBuffer(Edit, FArdaMemoryClearBufferParameters{Buffer, 0}, "ResetCornellTemporalHistory"))
				{
					mError = Edit.GetStatus().mMessage;
					return false;
				}
			}
			else if (Tensor.mName == "crop_offset" || Tensor.mName == "previous_crop_offset")
			{
				Buffer = AllocateInitializedBuffer(Edit, Storage, eastl::vector<uint8_t>(8, 0), Tensor.mName);
			}
			else
			{
				Buffer = AllocateBuffer(Edit, Storage, Tensor.mName);
			}
			if (!Buffer)
			{
				mError = Edit.GetStatus().mMessage;
				return false;
			}
			Bindings.push_back({Tensor.mName, {Buffer, Tensor.mDesc}});
		}
		const auto FindBuffer = [&](const char* Name)
		{
			for (const auto& Binding : Bindings)
			{
				if (Binding.mName == Name)
				{
					return Binding.mTensor.mBuffer;
				}
			}
			return FArdaGraphBuffer{};
		};
		Storage.mByteSize = uint64_t(Width) * Height * 3 * sizeof(float);
		const auto Mean = AllocateBuffer(Edit, Storage, "Cornell current two-sample mean");
		FArdaCornellNppdPackParameters Pack;
		Pack.mRadiance = Radiance;
		Pack.mGuides = Guides;
		Pack.mColor = FindBuffer("color");
		Pack.mDepth = FindBuffer("depth");
		Pack.mNormal = FindBuffer("normal");
		Pack.mDiffuse = FindBuffer("diffuse");
		Pack.mMotion = FindBuffer("motion");
		Pack.mMeanColor = Mean;
		Pack.mConstants = Constants;
		Pack.mWidth = Width;
		Pack.mHeight = Height;
		if (!Mean || !CornellNppdPack(Edit, Pack, "PackCornellSampleFeatures"))
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		auto Inference = mDenoisingPackage->Invoke(Edit, Bindings, "PretrainedCornellTemporalDenoising");
		if (!Inference)
		{
			mError = Inference.mStatus.mMessage;
			return false;
		}
		const auto* NextState = Inference.mValue.Find("next_temporal");
		const auto NextHistory = BindBuffer(Edit, mTemporalHistory[(mFrameIndex + 1) % 2], "next frame history");
		if (!NextState || !NextHistory ||
		    !CopyTo(Edit,
		        NextState->mBuffer,
		        NextHistory,
		        uint64_t(Width) * Height * 38 * sizeof(float),
		        NextState->mDesc.mByteOffset))
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		FArdaCornellTensorUnpackParameters Unpack;
		Unpack.mTensor = FindBuffer("denoised_hdr");
		Unpack.mImage = OutImage;
		Unpack.mConstants = Constants;
		Unpack.mWidth = Width;
		Unpack.mHeight = Height;
		if (!CornellTensorUnpack(Edit, Unpack, "UnpackCornellTemporalHdr"))
		{
			mError = Edit.GetStatus().mMessage;
			return false;
		}
		if (mNoisySnapshot)
		{
			if (!ReadbackNode(Edit, FArdaGraphReadbackParameters{Mean, mNoisySnapshot}, "ReadNoisyHdrForValidation") ||
			    !ReadbackNode(Edit,
			        FArdaGraphReadbackParameters{Unpack.mTensor, mDenoisedSnapshot},
			        "ReadDenoisedHdrForValidation"))
			{
				mError = Edit.GetStatus().mMessage;
				return false;
			}
			for (const auto& Snapshot : mTensorSnapshots)
			{
				if (!ReadbackNode(Edit,
				        FArdaGraphReadbackParameters{FindBuffer(Snapshot.mName.c_str()), Snapshot.mBytes},
				        "ReadModelTensor_" + Snapshot.mName))
				{
					mError = Edit.GetStatus().mMessage;
					return false;
				}
			}
		}
		return true;
	}

	bool FArdaCornellBoxRenderer::SaveDenoisingImages()
	{
		if (mSettings.mDenoisingDumpPrefix.empty())
		{
			return true;
		}
		if (!mDenoisingPackage || !mNoisySnapshot || !mDenoisedSnapshot)
		{
			mError = "Denoising image output requires an executed denoising package.";
			return false;
		}
		const auto Height = mDenoisingTensorDesc.mDimensions[2];
		const auto Width = mDenoisingTensorDesc.mDimensions[3];
		const auto Plane = Width * Height;
		if (mNoisySnapshot->size() != Plane * 12 || mDenoisedSnapshot->size() < Plane * 12)
		{
			mError = "Denoising readback has not completed or has an unexpected size.";
			return false;
		}
		std::error_code Error;
		const auto Parent = mSettings.mDenoisingDumpPrefix.parent_path();
		if (!Parent.empty())
		{
			std::filesystem::create_directories(Parent, Error);
		}
		if (Error)
		{
			mError = "Cannot create the denoising image output directory.";
			return false;
		}
		const auto Save = [&](const eastl::vector<uint8_t>& Bytes, const char* Suffix)
		{
			auto Path = mSettings.mDenoisingDumpPrefix;
			Path += Suffix;
			std::ofstream Stream(Path, std::ios::binary);
			Stream << "PF\n" << Width << ' ' << Height << "\n-1.0\n";
			for (uint64_t Row = Height; Row > 0; --Row)
			{
				for (uint64_t X = 0; X < Width; ++X)
				{
					for (uint64_t Channel = 0; Channel < 3; ++Channel)
					{
						const auto Offset = (Channel * Plane + (Row - 1) * Width + X) * sizeof(float);
						Stream.write(reinterpret_cast<const char*>(Bytes.data() + Offset), sizeof(float));
					}
				}
			}
			return Stream.good();
		};
		if (!Save(*mNoisySnapshot, ".noisy.pfm") || !Save(*mDenoisedSnapshot, ".denoised.pfm"))
		{
			mError = "Failed to save denoising HDR images.";
			return false;
		}
		if (mbTemporalDenoising)
		{
			auto ManifestPath = mSettings.mDenoisingDumpPrefix;
			ManifestPath += ".tensors.json";
			std::ofstream Manifest(ManifestPath);
			Manifest << "[\n";
			for (size_t Index = 0; Index < mTensorSnapshots.size(); ++Index)
			{
				const auto& Snapshot = mTensorSnapshots[Index];
				const auto& Tensor = mDenoisingPackage->GetTensors()[Index];
				auto Path = mSettings.mDenoisingDumpPrefix;
				Path += "." + std::string(Snapshot.mName.c_str()) + ".bin";
				std::ofstream Stream(Path, std::ios::binary);
				auto Range = GetArdaRHITensorByteRange(Tensor.mDesc);
				if (!Range || Range.mValue.mByteOffset + Range.mValue.mByteSize > Snapshot.mBytes->size())
				{
					mError = "A temporal tensor readback does not cover its declared view.";
					return false;
				}
				Stream.write(reinterpret_cast<const char*>(Snapshot.mBytes->data() + Range.mValue.mByteOffset),
				    Range.mValue.mByteSize);
				if (!Stream.good())
				{
					mError = "Failed to save a temporal denoising tensor.";
					return false;
				}
				// Tensor names are from the validated, fixed demonstration contract above.
				Manifest << "{\"name\":\"" << Snapshot.mName.c_str() << "\",\"shape\":[";
				for (size_t Dimension = 0; Dimension < Tensor.mDesc.mDimensions.size(); ++Dimension)
				{
					if (Dimension)
					{
						Manifest << ',';
					}
					Manifest << Tensor.mDesc.mDimensions[Dimension];
				}
				Manifest << "]}" << (Index + 1 == mTensorSnapshots.size() ? "\n" : ",\n");
			}
			Manifest << "]\n";
			if (!Manifest.good())
			{
				mError = "Failed to save the temporal tensor manifest.";
				return false;
			}
		}
		return true;
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
		if (mDenoisingPackage &&
		    (mDenoisingTensorDesc.mDimensions[2] != Height || mDenoisingTensorDesc.mDimensions[3] != Width))
		{
			mError =
			    "The window dimensions must match the denoising package's static H/W shape; export another shape before resizing.";
			return false;
		}
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

		// Bound the dispatch by device limits, scratch storage, addressing, and the per-frame path-work budget.
		const uint32_t RemainingSamples =
		    mAccumulatedSamples < mSettings.mMaxSamples ? mSettings.mMaxSamples - mAccumulatedSamples : 0;
		const uint64_t DeviceInvocationLimit = mDevice->GetFeatures()->GetValueOr("ray-tracing.max-dispatch-invocations", 0);
		const uint64_t MaxSamplesByDevice =
		    DeviceInvocationLimit == 0 ? uint64_t(UINT32_MAX) : DeviceInvocationLimit / PixelCount;
		const uint64_t BytesPerSample = PixelCount * sizeof(float) * (mbTemporalDenoising ? 12ull : 4ull);
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
		if (mbTemporalDenoising && DispatchSamples != 0 && DispatchSamples != 2)
		{
			mError = "The current device and image dimensions cannot admit the model's complete two-sample batch.";
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
			if (mDenoisingPackage)
			{
				Desc.mDebugName = "Cornell denoised linear HDR";
				auto Denoised = mDevice->CreateTexture(Desc);
				if (!TakeResult(Denoised, mDenoisedTexture, mError))
				{
					return false;
				}
				if (mbTemporalDenoising)
				{
					FArdaRHIBufferDesc History;
					History.mDebugName = "Cornell temporal denoising history";
					History.mByteSize = PixelCount * 38 * sizeof(float);
					History.mStructureStride = sizeof(float);
					History.mbCudaInterop = true;
					History.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
					    EArdaRHIBufferUsage::UnorderedAccess;
					for (auto& Slot : mTemporalHistory)
					{
						auto CreatedHistory = mDevice->CreateBuffer(History);
						if (!TakeResult(CreatedHistory, Slot, mError))
						{
							return false;
						}
					}
				}
			}
		}

		// Reuse the frame graph until its back buffer or sample-batch shape changes.
		auto Found = eastl::find_if(mFrames.begin(),
		    mFrames.end(),
		    [&](const auto& F)
		    {
			    return F->mBackBuffer.Get() == ColorAttachment.mTexture.Get() &&
			        (!mbTemporalDenoising ||
			            (F->mHistorySlot == mFrameIndex % 2 && F->mbResetHistory == (mFrameIndex == 0)));
		    });
		eastl::shared_ptr<FArdaCachedFrame> Frame =
		    Found == mFrames.end() ? eastl::make_shared<FArdaCachedFrame>() : *Found;
		if (!Frame->mGraph || Frame->mDispatchSamples != DispatchSamples)
		{
			Frame->mBackBuffer = ColorAttachment.mTexture;
			Frame->mDispatchSamples = DispatchSamples;
			Frame->mHistorySlot = mFrameIndex % 2;
			Frame->mbResetHistory = mFrameIndex == 0;
			Frame->mInput = eastl::make_shared<FArdaCornellFrameInput>();
			Frame->mGraph = std::make_unique<FArdaDependencyGraph>(mDevice);
			auto& Graph = *Frame->mGraph;
			FArdaGraphEdit Edit(Graph);
			if (auto S = Edit.GetStatus(); !S)
			{
				mError = S.mMessage;
				return false;
			}

			// Import persistent scene and accumulation resources; nodes own transient intermediate outputs.
			auto T = BindAccelerationStructure(Edit, mTlas, "Frame TLAS");
			auto B = BindAccelerationStructure(Edit, mBlas, "Static BLAS");
			auto Back = BindTexture(Edit, ColorAttachment.mTexture, "back buffer");
			auto Accum = BindTexture(Edit, mAccumulationTexture, "accumulation");

			if (!Back || !Accum || !T || !B)
			{
				mError = "Failed to declare Cornell frame resources.";
				return false;
			}
			const auto Attached = [&](const FArdaGraphOperation& Result)
			{
				if (!Result)
				{
					mError = Edit.GetStatus().mMessage;
				}
				return bool(Result);
			};
			FArdaCornellUploadFrameParameters UploadParameters;
			UploadParameters.mFrame = Frame->mInput;
			auto Upload = CornellUploadFrame(Edit, UploadParameters, "UpdateCornellFrameConstants");
			if (!Attached(Upload))
			{
				return false;
			}
			const auto Constants = Upload.GetBuffer("Constants");
			FArdaGraphBuffer SampleRadiance, SampleGuides;

			// Attach ray generation and accumulation only while progressive sampling has work remaining.
			if (DispatchSamples)
			{
				auto V = BindBuffer(Edit, mVertexBuffer, "vertices"), I = BindBuffer(Edit, mIndexBuffer, "indices"),
				     M = BindBuffer(Edit, mMaterialBuffer, "materials");

				if (!V || !I || !M || !T || !B)
				{
					mError = "Failed to declare Cornell ray resources.";
					return false;
				}
				FArdaCornellTraceParameters TraceParameters;
				TraceParameters.mTlas = T;
				TraceParameters.mVertices = V;
				TraceParameters.mIndices = I;
				TraceParameters.mMaterials = M;
				TraceParameters.mConstants = Constants;
				TraceParameters.mBlas = B;
				TraceParameters.mWidth = Width;
				TraceParameters.mHeight = Height;
				TraceParameters.mSamples = DispatchSamples;
				TraceParameters.mbWriteGuides = mbTemporalDenoising;
				auto Trace = CornellTrace(Edit, TraceParameters, "PathTraceCornellBox");
				if (!Attached(Trace))
				{
					return false;
				}
				SampleRadiance = Trace.GetBuffer("Radiance");
				SampleGuides = Trace.GetBuffer("Guides");
				FArdaCornellAccumulateParameters AccumulateParameters;
				AccumulateParameters.mRadiance = Trace.GetBuffer("Radiance");
				AccumulateParameters.mAccumulation = Accum;
				AccumulateParameters.mConstants = Constants;
				AccumulateParameters.mGroupCountX = (Width + 7) / 8;
				AccumulateParameters.mGroupCountY = (Height + 7) / 8;
				if (!Attached(CornellAccumulate(Edit, AccumulateParameters, "ReduceCornellSampleBatch")))
				{
					return false;
				}
			}
			auto PresentedImage = Accum;
			if (mDenoisingPackage)
			{
				PresentedImage = BindTexture(Edit, mDenoisedTexture, "denoised HDR");
				if (!PresentedImage)
				{
					return false;
				}
				if (DispatchSamples &&
				    !(mbTemporalDenoising ? AppendTemporalDenoising(Edit,
				                                SampleRadiance,
				                                SampleGuides,
				                                Constants,
				                                Width,
				                                Height,
				                                PresentedImage)
				                          : AppendDenoising(Edit, Accum, Constants, Width, Height, PresentedImage)))
				{
					return false;
				}
			}
			FArdaCornellPresentParameters PresentParameters;
			PresentParameters.mAccumulation = PresentedImage;
			PresentParameters.mConstants = Constants;
			PresentParameters.mColor = Back;
			PresentParameters.mWidth = Width;
			PresentParameters.mHeight = Height;
			if (!Attached(CornellPresent(Edit, PresentParameters, "ToneMapAndPresentCornellBox")))
			{
				return false;
			}
			// A real frame operation, even after sampling finishes. The TLAS write orders all ray reads.
			FArdaCornellBuildTlasParameters BuildTlas;
			BuildTlas.mBlas = B;
			BuildTlas.mTlas = T;
			BuildTlas.mBuildFlags = GetStaticBuildFlags(false);
			BuildTlas.mWorkspaceBytes = mTlasWorkspaceBytes;
			if (!Attached(CornellBuildTlas(Edit, BuildTlas, "RebuildCornellFrameTLAS")))
			{
				return false;
			}
			if (auto S = Edit.Commit(); !S)
			{
				mError = S.mMessage;
				return false;
			}
			if (auto S = Graph.CompileAndWait(); !S)
			{
				mError = S.mMessage;
				return false;
			}
			if (Found == mFrames.end())
			{
				mFrames.push_back(Frame);
			}
		}

		// Derive the current camera basis before updating retained frame constants.
		const float CosPitch = std::cos(mCameraPitch);
		const FArdaVector3f Forward = {std::cos(mCameraYaw) * CosPitch,
		    std::sin(mCameraYaw) * CosPitch,
		    std::sin(mCameraPitch)};
		const FArdaViewMatrix WorldToView = MakeArdaViewMatrix(mCameraPosition, Forward, FArdaVector3f::UnitZ());
		const FArdaVector3f Right = WorldToView.block<1, 3>(0, 0).transpose();
		const FArdaVector3f Up = WorldToView.block<1, 3>(1, 0).transpose();
		constexpr float HorizontalHalfFov = 0.6981317008f;
		const float TanHalfFovX = std::tan(HorizontalHalfFov);
		const float TanHalfFovY = TanHalfFovX * static_cast<float>(Height) / static_cast<float>(Width);

		// Capture camera, sampling, and material-independent controls for this frame's upload node.
		FArdaCornellFrameConstants Constants;
		Constants.mCameraPositionAndTanHalfFovX = {mCameraPosition.x(),
		    mCameraPosition.y(),
		    mCameraPosition.z(),
		    TanHalfFovX};
		Constants.mCameraForwardAndTanHalfFovY = {Forward.x(), Forward.y(), Forward.z(), TanHalfFovY};
		Constants.mCameraRightAndExposure = {Right.x(), Right.y(), Right.z(), mSettings.mExposure};
		Constants.mCameraUpAndLightArea = {Up.x(), Up.y(), Up.z(), 0.7f * 0.6f};
		Constants.mImageAndSampling = {Width, Height, mAccumulatedSamples, DispatchSamples};
		Constants.mPathAndSeed = {mSettings.mMaxBounces, mSettings.mSeed, mFrameIndex, mbTemporalDenoising ? 1u : 0u};
		Frame->mInput->mConstants = Constants;

		// Execute the compiled graph, then advance sampling only after successful presentation.
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
