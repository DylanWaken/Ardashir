#include "ArdaARDGExamplePch.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"

#include "ArdaTerrainRenderer.h"
#include "ArdaTerrainValidation.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace arda
{

	bool FArdaTerrainRenderer::Initialize(arda::FArdaRHIDeviceRef device, arda::EArdaRHIFormat, bool bVerifyTerrain)
	{
		mDevice = eastl::move(device);
		if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
		{
			mError = "The initialized backend does not expose a graphics device.";
			return false;
		}
		mbVerifyTerrain = bVerifyTerrain;
		mbTerrainReadbackValidated = false;
		mError.clear();
		return true;
	}

	void FArdaTerrainRenderer::UpdateCamera(float forward, float right, float lookX, float lookY, float deltaSeconds)
	{

		// Integrate mouse orientation and normalize movement so diagonal input preserves speed.
		constexpr float LookSensitivity = 0.0025f;
		constexpr float MoveSpeed = 1.1f;
		constexpr float PitchLimit = 1.50f;
		mElapsedSeconds += eastl::min(deltaSeconds, 0.1f);
		mCameraYaw += lookX * LookSensitivity;
		mCameraPitch = eastl::clamp(mCameraPitch - lookY * LookSensitivity, -PitchLimit, PitchLimit);

		// Build the camera basis from the current view orientation.
		const float cosPitch = std::cos(mCameraPitch);
		const FArdaVector3f forwardVector = {std::cos(mCameraYaw) * cosPitch,
		    std::sin(mCameraYaw) * cosPitch,
		    std::sin(mCameraPitch)};
		const FArdaVector3f rightVector = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
		const float inputLength = FArdaVector2f(forward, right).norm();
		if (inputLength > 1.0f)
		{
			forward /= inputLength;
			right /= inputLength;
		}
		const float distance = MoveSpeed * eastl::min(deltaSeconds, 0.1f);
		mCameraPosition += (forwardVector * forward + rightVector * right) * distance;
	}

	struct FArdaTerrainRenderer::FArdaFrameGraph
	{
		FArdaRHITextureRef mColor;
		FArdaDependencyGraph mGraph;
		eastl::shared_ptr<FArdaTerrainFrameInputs> mInputs = eastl::make_shared<FArdaTerrainFrameInputs>();

		explicit FArdaFrameGraph(FArdaRHIDeviceRef Device)
		    : mGraph(eastl::move(Device))
		{
		}
	};

	FArdaTerrainRenderer::FArdaTerrainRenderer() = default;
	FArdaTerrainRenderer::~FArdaTerrainRenderer() = default;

	void FArdaTerrainRenderer::ReleaseFrameGraphs()
	{
		mFrameGraphs.clear();
	}

	bool FArdaTerrainRenderer::CreateFrameGraph(const FArdaRHITextureRef& Color, uint32_t Width, uint32_t Height)
	{
		const auto Existing = std::find_if(mFrameGraphs.begin(),
		    mFrameGraphs.end(),
		    [&](const auto& Cached)
		    {
			    return Cached->mColor == Color;
		    });
		std::unique_ptr<FArdaFrameGraph> Created;
		FArdaFrameGraph* Frame = nullptr;
		if (Existing == mFrameGraphs.end())
		{
			Created = std::make_unique<FArdaFrameGraph>(mDevice);
			Created->mColor = Color;
			Frame = Created.get();
		}
		else
		{
			Frame = Existing->get();
		}
		auto& Graph = Frame->mGraph;
		FArdaGraphEdit Edit(Graph);
		auto Status = Edit.GetStatus();
		if (!Status)
		{
			mError = Status.mMessage;
			return false;
		}

		// Each operation returns the typed resources consumed by subsequent operations.
		const auto ColorTarget = BindTexture(Edit, Color, "Swap-chain color");
		const auto SettingsUpload =
		    TerrainUploadSettings(Edit, {{}, Frame->mInputs}, "Upload settings bytes").GetBuffer();
		const auto Settings = Clone(Edit, SettingsUpload);
		const auto Camera = TerrainUploadCamera(Edit, {{}, Frame->mInputs}, "Upload camera").GetBuffer();
		const auto RawHeightmap = TerrainGenerate(Edit, {Settings}, "Generate heightmap").GetTexture("Heightmap");
		const auto Heightmap = TerrainErode(Edit, {RawHeightmap}, "Erode heightmap").GetTexture("Heightmap");
		const auto Mesh = TerrainTriangulate(Edit, {Heightmap}, "Triangulate terrain");
		const auto Vertices = Mesh.GetBuffer("Vertices");
		const auto Indices = Mesh.GetBuffer("Indices");
		const auto Scene = TerrainDraw(Edit,
		    {Heightmap, Vertices, Indices, Camera, {}, {}, Width, Height, Color->GetDesc().mFormat},
		    "Draw terrain");
		TerrainOverlay(Edit, {Scene.GetTexture("Color"), ColorTarget, Width, Height}, "Composite overlay");
		if (mbVerifyTerrain && !mbTerrainReadbackValidated)
		{
			ReadbackNode(Edit, {Vertices, Frame->mInputs->mVertexReadback}, "Validate vertices");
			ReadbackNode(Edit, {Indices, Frame->mInputs->mIndexReadback}, "Validate indices");
		}

		// Compile the completed dependency graph before publishing it in the frame cache.
		Status = Edit.Commit();
		if (Status)
		{
			Status = Graph.Compile();
		}
		if (!Status)
		{
			mError = Status.mMessage;
			return false;
		}
		if (Created)
		{
			mFrameGraphs.push_back(std::move(Created));
		}
		return true;
	}

	bool FArdaTerrainRenderer::RenderFrame(IArdaSwapChain& swapChain)
	{
		FArdaRHIFramebufferRef Framebuffer;
		if (!swapChain.AcquireFrame(Framebuffer))
		{
			mError = swapChain.GetError();
			return false;
		}
		if (Framebuffer->GetDesc().mColorAttachments.empty() || !Framebuffer->GetDesc().mColorAttachments[0].mTexture)
		{
			mError = "The acquired swap-chain framebuffer has no color attachment.";
			return false;
		}
		const auto Color = Framebuffer->GetDesc().mColorAttachments[0].mTexture;

		// Reuse one compiled graph for each swap-chain image.
		FArdaFrameGraph* Frame = nullptr;
		for (auto& Cached : mFrameGraphs)
		{
			if (Cached->mColor.Get() == Color.Get())
			{
				Frame = Cached.get();
			}
		}
		const uint32_t width = swapChain.GetWidth(), height = swapChain.GetHeight();
		if (!Frame)
		{
			if (!CreateFrameGraph(Color, width, height))
			{
				return false;
			}
			Frame = mFrameGraphs.back().get();
		}

		// Build the camera basis from the current view orientation.
		const float cosPitch = std::cos(mCameraPitch);
		const FArdaVector3f forward = {std::cos(mCameraYaw) * cosPitch,
		    std::sin(mCameraYaw) * cosPitch,
		    std::sin(mCameraPitch)};

		// Preserve the horizontal field of view and infinite reverse-depth projection.
		constexpr float NearPlane = 0.05f;
		constexpr float HorizontalHalfFov = 0.78539816f;
		const float Aspect = static_cast<float>(width) / static_cast<float>(height);
		const float VerticalFov = 2.0f * std::atan(std::tan(HorizontalHalfFov) / Aspect);
		FArdaCameraMatrices cameraSettings;
		cameraSettings.mWorldToView = MakeArdaViewMatrix(mCameraPosition, forward, FArdaVector3f::UnitZ());
		cameraSettings.mViewToClip = MakeArdaInfinitePerspectiveProjection(VerticalFov, Aspect, NearPlane, true);

		// Update retained frame inputs and execute the already-compiled operations.
		Frame->mInputs->mSettings.mTime = mElapsedSeconds;
		Frame->mInputs->mCamera = cameraSettings;
		swapChain.PrepareSubmit();
		const auto Result = Frame->mGraph.Execute();
		if (!Result.mStatus)
		{
			mError = Result.mStatus.mMessage;
			return false;
		}
		if (mbVerifyTerrain && !mbTerrainReadbackValidated)
		{
			mError = ValidateTerrainReadback(*Frame->mInputs->mVertexReadback,
			    *Frame->mInputs->mIndexReadback,
			    Frame->mInputs->mSettings);
			if (!mError.empty())
			{
				return false;
			}
			mbTerrainReadbackValidated = true;
			// The next construction omits the completed diagnostic operations.
			Frame->mInputs->mVertexReadback->clear();
			Frame->mInputs->mIndexReadback->clear();
			if (!CreateFrameGraph(Color, width, height))
			{
				return false;
			}
		}
		if (!Result.mSubmittedCommandListCount)
		{
			mError = "Terrain graph submitted no command lists.";
			return false;
		}
		if (!swapChain.Present())
		{
			mError = swapChain.GetError();
			return false;
		}
		mError.clear();
		return true;
	}
}
