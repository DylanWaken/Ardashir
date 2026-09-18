#include "ArdaARDGExamplePch.h"

#include "ArdaTerrainRenderer.h"
#include "ArdaTerrainValidation.h"
#include "ArdaDependencyGraphNodes.h"

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
		const float forwardVector[3] = {std::cos(mCameraYaw) * cosPitch,
		    std::sin(mCameraYaw) * cosPitch,
		    std::sin(mCameraPitch)};
		const float rightVector[3] = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
		const float inputLength = std::sqrt(forward * forward + right * right);
		if (inputLength > 1.0f)
		{
			forward /= inputLength;
			right /= inputLength;
		}
		const float distance = MoveSpeed * eastl::min(deltaSeconds, 0.1f);
		for (uint32_t component = 0; component < 3; ++component)
		{
			mCameraPosition[component] +=
			    (forwardVector[component] * forward + rightVector[component] * right) * distance;
		}
	}

	struct FArdaTerrainRenderer::FArdaFrameGraph
	{
		FArdaRHITextureRef mColor;
		FArdaDependencyGraph mGraph;
		eastl::shared_ptr<FArdaTerrainFrameInputs> mInputs = eastl::make_shared<FArdaTerrainFrameInputs>();
		FArdaGraphNodeHandle mVertexReadback, mIndexReadback;

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
		auto Frame = std::make_unique<FArdaFrameGraph>(mDevice);
		Frame->mColor = Color;
		auto& Graph = Frame->mGraph;
		auto Status = Graph.BeginGraphEdit();
		if (!Status)
		{
			mError = Status.mMessage;
			return false;
		}

		// Keep only logical resource handles while node declarations create intermediate storage.
		struct FArdaTerrainGraphResources
		{
			FArdaDependencyResourceHandle mSettingsUpload;
			FArdaDependencyResourceHandle mSettings;
			FArdaDependencyResourceHandle mCamera;
			FArdaDependencyResourceHandle mRawHeightmap;
			FArdaDependencyResourceHandle mHeightmap;
			FArdaDependencyResourceHandle mVertices;
			FArdaDependencyResourceHandle mIndices;
			FArdaDependencyResourceHandle mSceneColor;
			FArdaDependencyResourceHandle mColor;
			FArdaDependencyResourceHandle mDepth;
		} P;

		auto Save = [this](auto Result, auto& Destination)
		{
			if (!Result)
			{
				mError = Result.mStatus.mMessage;
				return false;
			}
			Destination = eastl::move(Result.mValue);
			return true;
		};
		if (!Save(Graph.ImportTexture("Swap-chain color", Color), P.mColor))
		{
			return false;
		}

		// Attach uploads and compute operations, forwarding each named output to its consumer.
		FArdaGraphNodeHandle Node;
		if (!Save(Graph.AttachOrFind<FArdaTerrainUploadSettingsNode>("Upload settings bytes", {{}, Frame->mInputs}),
		        Node))
		{
			return false;
		}
		P.mSettingsUpload = Graph.FindOutput(Node, "Destination");
		if (!Save(Graph.AttachOrFind<FArdaGraphCopyNode>("Copy settings",
		              {P.mSettingsUpload, {}, sizeof(FArdaTerrainSettings)}),
		        Node))
		{
			return false;
		}
		P.mSettings = Graph.FindOutput(Node, "Destination");
		if (!Save(Graph.AttachOrFind<FArdaTerrainUploadCameraNode>("Upload camera", {{}, Frame->mInputs}), Node))
		{
			return false;
		}
		P.mCamera = Graph.FindOutput(Node, "Destination");
		if (!Save(Graph.AttachOrFind<FArdaTerrainGenerateNode>("Generate heightmap", {P.mSettings}), Node))
		{
			return false;
		}
		P.mRawHeightmap = Graph.FindOutput(Node, "Heightmap");
		if (!Save(Graph.AttachOrFind<FArdaTerrainErodeNode>("Erode heightmap", {P.mRawHeightmap}), Node))
		{
			return false;
		}
		P.mHeightmap = Graph.FindOutput(Node, "Heightmap");
		if (!Save(Graph.AttachOrFind<FArdaTerrainTriangulateNode>("Triangulate terrain", {P.mHeightmap}), Node))
		{
			return false;
		}

		// Feed generated geometry into rasterization, then composite onto the imported back buffer.
		P.mVertices = Graph.FindOutput(Node, "Vertices");
		P.mIndices = Graph.FindOutput(Node, "Indices");
		if (!Save(Graph.AttachOrFind<FArdaTerrainDrawNode>("Draw terrain",
		              {P.mHeightmap,
		                  P.mVertices,
		                  P.mIndices,
		                  P.mCamera,
		                  {},
		                  {},
		                  Width,
		                  Height,
		                  Color->GetDesc().mFormat}),
		        Node))
		{
			return false;
		}
		P.mSceneColor = Graph.FindOutput(Node, "Color");
		P.mDepth = Graph.FindOutput(Node, "Depth");
		if (!Save(Graph.AttachOrFind<FArdaTerrainOverlayNode>("Composite overlay",
		              {P.mSceneColor, P.mColor, Width, Height}),
		        Node))
		{
			return false;
		}

		if (mbVerifyTerrain && !mbTerrainReadbackValidated)
		{
			if (!Save(Graph.AttachOrFind<FArdaGraphReadbackNode>("Validate vertices",
			              FArdaGraphReadbackParameters{P.mVertices, Frame->mInputs->mVertexReadback}),
			        Frame->mVertexReadback) ||
			    !Save(Graph.AttachOrFind<FArdaGraphReadbackNode>("Validate indices",
			              FArdaGraphReadbackParameters{P.mIndices, Frame->mInputs->mIndexReadback}),
			        Frame->mIndexReadback))
			{
				return false;
			}
		}

		// Compile the completed dependency graph before publishing it in the frame cache.
		Status = Graph.EndGraphEdit();
		if (!Status)
		{
			mError = Status.mMessage;
			return false;
		}
		mFrameGraphs.push_back(std::move(Frame));
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
		const float forward[3] = {std::cos(mCameraYaw) * cosPitch,
		    std::sin(mCameraYaw) * cosPitch,
		    std::sin(mCameraPitch)};
		const float right[3] = {-std::sin(mCameraYaw), std::cos(mCameraYaw), 0.0f};
		const float up[3] = {forward[1] * right[2] - forward[2] * right[1],
		    forward[2] * right[0] - forward[0] * right[2],
		    forward[0] * right[1] - forward[1] * right[0]};
		const float viewTranslation[3] = {
		    -(mCameraPosition[0] * right[0] + mCameraPosition[1] * right[1] + mCameraPosition[2] * right[2]),
		    -(mCameraPosition[0] * up[0] + mCameraPosition[1] * up[1] + mCameraPosition[2] * up[2]),
		    -(mCameraPosition[0] * forward[0] + mCameraPosition[1] * forward[1] + mCameraPosition[2] * forward[2])};

		// Use a reverse-depth projection while keeping CPU camera data separate from node setup.
		constexpr float NearPlane = 0.05f;
		constexpr float HorizontalHalfFov = 0.78539816f;
		const float xScale = 1.0f / std::tan(HorizontalHalfFov);
		const float yScale = xScale * static_cast<float>(width) / static_cast<float>(height);
		const FArdaTerrainCameraSettings cameraSettings = {{right[0],
		                                                       up[0],
		                                                       forward[0],
		                                                       0.0f,
		                                                       right[1],
		                                                       up[1],
		                                                       forward[1],
		                                                       0.0f,
		                                                       right[2],
		                                                       up[2],
		                                                       forward[2],
		                                                       0.0f,
		                                                       viewTranslation[0],
		                                                       viewTranslation[1],
		                                                       viewTranslation[2],
		                                                       1.0f},
		    {xScale, 0.0f, 0.0f, 0.0f, 0.0f, yScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, NearPlane, 0.0f}};

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
			// Validation is a one-time diagnostic. Remove its nodes transactionally before replay.
			auto Status = Frame->mGraph.BeginGraphEdit();
			if (Status)
			{
				Status = Frame->mGraph.RemoveNode(Frame->mVertexReadback);
			}
			if (Status)
			{
				Status = Frame->mGraph.RemoveNode(Frame->mIndexReadback);
			}
			if (Status)
			{
				Status = Frame->mGraph.EndGraphEdit();
			}
			if (!Status)
			{
				mError = Status.mMessage;
				return false;
			}
			Frame->mInputs->mVertexReadback->clear();
			Frame->mInputs->mIndexReadback->clear();
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
