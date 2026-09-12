#include "ArdaARDGExamplePch.h"

#include "ArdaTerrainRenderer.h"
#include "ArdaDependencyGraphNodes.h"

#include <cmath>
#include <memory>

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

		eastl::string ValidateTerrainReadback(const eastl::vector<uint8_t>& VertexBytes,
		    const eastl::vector<uint8_t>& IndexBytes)
		{
			if (VertexBytes.size() != static_cast<size_t>(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex) ||
			    IndexBytes.size() != static_cast<size_t>(ArdaTerrainIndexCount) * sizeof(uint32_t))
			{
				char Message[256]{};
				std::snprintf(Message,
				    sizeof(Message),
				    "Terrain GPU readback returned unexpected byte counts: vertices expected %zu, got %zu; indices expected %zu, got %zu.",
				    static_cast<size_t>(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex),
				    VertexBytes.size(),
				    static_cast<size_t>(ArdaTerrainIndexCount) * sizeof(uint32_t),
				    IndexBytes.size());
				return Message;
			}

			const auto* Vertices = reinterpret_cast<const FArdaTerrainVertex*>(VertexBytes.data());
			const auto* Indices = reinterpret_cast<const uint32_t*>(IndexBytes.data());
			constexpr uint32_t LocalIndices[6] = {0, 2, 1, 1, 2, 3};
			float MaximumGradient = 0.0f;
			uint32_t MaximumGradientCell = 0;
			for (uint32_t Cell = 0; Cell < ArdaTerrainCellCount; ++Cell)
			{
				const uint32_t VertexBase = Cell * 4;
				const uint32_t IndexBase = Cell * 6;
				for (uint32_t Index = 0; Index < 6; ++Index)
				{
					const uint32_t Expected = VertexBase + LocalIndices[Index];
					if (Indices[IndexBase + Index] != Expected)
					{
						char Message[192]{};
						std::snprintf(Message,
						    sizeof(Message),
						    "Terrain index readback diverged at cell %u index %u: expected %u, got %u.",
						    Cell,
						    Index,
						    Expected,
						    Indices[IndexBase + Index]);
						return Message;
					}
				}

				const uint32_t CellX = Cell % (ArdaTerrainHeightmapWidth - 1);
				const uint32_t CellY = Cell / (ArdaTerrainHeightmapWidth - 1);
				constexpr uint32_t CornerX[4] = {0, 1, 0, 1};
				constexpr uint32_t CornerY[4] = {0, 0, 1, 1};
				for (uint32_t Corner = 0; Corner < 4; ++Corner)
				{
					const FArdaTerrainVertex& Vertex = Vertices[VertexBase + Corner];
					const float ExpectedX = ((static_cast<float>(CellY + CornerY[Corner]) /
					                             static_cast<float>(ArdaTerrainHeightmapHeight - 1)) -
					                            0.5f) *
					    1.45f;
					const float ExpectedY = ((static_cast<float>(CellX + CornerX[Corner]) /
					                             static_cast<float>(ArdaTerrainHeightmapWidth - 1)) -
					                            0.5f) *
					    1.45f;
					if (!std::isfinite(Vertex.mPosition[0]) || !std::isfinite(Vertex.mPosition[1]) ||
					    !std::isfinite(Vertex.mPosition[2]) || !std::isfinite(Vertex.mHeight) ||
					    std::abs(Vertex.mPosition[0] - ExpectedX) > 0.00001f ||
					    std::abs(Vertex.mPosition[1] - ExpectedY) > 0.00001f ||
					    std::abs(Vertex.mPosition[2] - (Vertex.mHeight * 0.72f - 0.32f)) > 0.00002f)
					{
						char Message[192]{};
						std::snprintf(Message,
						    sizeof(Message),
						    "Terrain vertex readback diverged at cell %u corner %u: position=(%.6f, %.6f, %.6f), height=%.6f.",
						    Cell,
						    Corner,
						    Vertex.mPosition[0],
						    Vertex.mPosition[1],
						    Vertex.mPosition[2],
						    Vertex.mHeight);
						return Message;
					}
				}

				const auto HeightDiff = [](float Left, float Right)
				{
					return std::abs(Left - Right);
				};
				const float HorizontalGradient =
				    HeightDiff(Vertices[VertexBase + 0].mHeight, Vertices[VertexBase + 1].mHeight);
				const float VerticalGradient =
				    HeightDiff(Vertices[VertexBase + 0].mHeight, Vertices[VertexBase + 2].mHeight);
				const float Gradient = eastl::max(HorizontalGradient, VerticalGradient);
				if (Gradient > MaximumGradient)
				{
					MaximumGradient = Gradient;
					MaximumGradientCell = Cell;
				}
				if (CellX + 1 < ArdaTerrainHeightmapWidth - 1)
				{
					const FArdaTerrainVertex* Right = Vertices + VertexBase + 4;
					if (HeightDiff(Vertices[VertexBase + 1].mHeight, Right[0].mHeight) > 0.000001f ||
					    HeightDiff(Vertices[VertexBase + 3].mHeight, Right[2].mHeight) > 0.000001f)
					{
						char Message[160]{};
						std::snprintf(Message,
						    sizeof(Message),
						    "Terrain readback has a horizontal height seam after cell %u.",
						    Cell);
						return Message;
					}
				}
				if (CellY + 1 < ArdaTerrainHeightmapHeight - 1)
				{
					const FArdaTerrainVertex* Below = Vertices + VertexBase + (ArdaTerrainHeightmapWidth - 1) * 4;
					if (HeightDiff(Vertices[VertexBase + 2].mHeight, Below[0].mHeight) > 0.000001f ||
					    HeightDiff(Vertices[VertexBase + 3].mHeight, Below[1].mHeight) > 0.000001f)
					{
						char Message[160]{};
						std::snprintf(Message,
						    sizeof(Message),
						    "Terrain readback has a vertical height seam after cell %u.",
						    Cell);
						return Message;
					}
				}
			}
			if (MaximumGradient > 0.10f)
			{
				char Message[160]{};
				std::snprintf(Message,
				    sizeof(Message),
				    "Terrain readback has a discontinuous height gradient of %.6f at cell %u.",
				    MaximumGradient,
				    MaximumGradientCell);
				return Message;
			}
			return {};
		}
	}

	bool FArdaTerrainRenderer::Initialize(arda::FArdaRHIDeviceRef device, arda::EArdaRHIFormat)
	{
		mDevice = eastl::move(device);
		if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
		{
			mError = "The initialized backend does not expose a graphics device.";
			return false;
		}
		if (!CreateSettingsUploadBuffer() || !CreateCameraResources())
		{
			return false;
		}
		mError.clear();
		return true;
	}

	bool FArdaTerrainRenderer::CreateCameraResources()
	{
		arda::FArdaRHIBufferDesc desc;
		desc.mByteSize = sizeof(FArdaTerrainCameraSettings);
		desc.mUsage = arda::EArdaRHIBufferUsage::Constant;
		desc.mInitialState = arda::EArdaRHIResourceState::ConstantBuffer;
		desc.mbKeepInitialState = true;
		desc.mDebugName = "Terrain camera";
		auto buffer = mDevice->CreateBuffer(desc);
		if (!TakeResult(buffer, mCameraBuffer, mError))
		{
			return false;
		}
		return true;
	}

	bool FArdaTerrainRenderer::CreateSettingsUploadBuffer()
	{
		arda::FArdaRHIBufferDesc desc;
		desc.mByteSize = sizeof(FArdaTerrainSettings);
		desc.mStructureStride = sizeof(FArdaTerrainSettings);
		desc.mInitialState = arda::EArdaRHIResourceState::CopySource;
		desc.mbKeepInitialState = true;
		desc.mDebugName = "Terrain settings upload";
		auto buffer = mDevice->CreateBuffer(desc);
		return TakeResult(buffer, mSettingsUploadBuffer, mError);
	}

	void FArdaTerrainRenderer::UpdateCamera(float forward, float right, float lookX, float lookY, float deltaSeconds)
	{
		constexpr float LookSensitivity = 0.0025f;
		constexpr float MoveSpeed = 1.1f;
		constexpr float PitchLimit = 1.50f;
		mElapsedSeconds += eastl::min(deltaSeconds, 0.1f);
		mCameraYaw += lookX * LookSensitivity;
		mCameraPitch = eastl::clamp(mCameraPitch - lookY * LookSensitivity, -PitchLimit, PitchLimit);
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

	struct FArdaTerrainRenderer::FFrameGraph
	{
		FArdaRHITextureRef mColor;
		FArdaDependencyGraph mGraph;
		eastl::shared_ptr<FArdaTerrainFrameInputs> mInputs = eastl::make_shared<FArdaTerrainFrameInputs>();
		FArdaGraphNodeHandle mVertexReadback, mIndexReadback;

		explicit FFrameGraph(FArdaRHIDeviceRef Device)
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
		auto Frame = std::make_unique<FFrameGraph>(mDevice);
		Frame->mColor = Color;
		auto& Graph = Frame->mGraph;
		auto Status = Graph.BeginGraphEdit();
		if (!Status)
		{
			mError = Status.mMessage;
			return false;
		}

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
		if (!Save(Graph.ImportTexture("Swap-chain color", Color), P.mColor) ||
		    !Save(Graph.ImportBuffer("Settings upload", mSettingsUploadBuffer), P.mSettingsUpload) ||
		    !Save(Graph.ImportBuffer("Terrain camera", mCameraBuffer), P.mCamera))
		{
			return false;
		}
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = sizeof(FArdaTerrainSettings);
		Buffer.mStructureStride = sizeof(FArdaTerrainSettings);
		Buffer.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource;
		if (!Save(Graph.CreateBuffer("Terrain settings", Buffer), P.mSettings))
		{
			return false;
		}
		Buffer.mByteSize = uint64_t(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex);
		Buffer.mStructureStride = sizeof(FArdaTerrainVertex);
		Buffer.mUsage =
		    EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Vertex;
		if (!Save(Graph.CreateBuffer("Terrain vertices", Buffer), P.mVertices))
		{
			return false;
		}
		Buffer.mByteSize = uint64_t(ArdaTerrainIndexCount) * sizeof(uint32_t);
		Buffer.mStructureStride = sizeof(uint32_t);
		Buffer.mUsage =
		    EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Index;
		if (!Save(Graph.CreateBuffer("Terrain indices", Buffer), P.mIndices))
		{
			return false;
		}
		FArdaRHITextureDesc Texture;
		Texture.mWidth = ArdaTerrainHeightmapWidth;
		Texture.mHeight = ArdaTerrainHeightmapHeight;
		Texture.mFormat = EArdaRHIFormat::R32Float;
		Texture.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
		if (!Save(Graph.CreateTexture("Raw heightmap", Texture), P.mRawHeightmap) ||
		    !Save(Graph.CreateTexture("Eroded heightmap", Texture), P.mHeightmap))
		{
			return false;
		}
		Texture.mWidth = Width;
		Texture.mHeight = Height;
		Texture.mFormat = Color->GetDesc().mFormat;
		Texture.mUsage = EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::ShaderResource;
		if (!Save(Graph.CreateTexture("Terrain color before overlay", Texture), P.mSceneColor))
		{
			return false;
		}
		Texture.mFormat = EArdaRHIFormat::D32;
		Texture.mUsage = EArdaRHITextureUsage::DepthStencil;
		if (!Save(Graph.CreateTexture("Terrain depth", Texture), P.mDepth))
		{
			return false;
		}
		const auto Attached = [this](const TArdaRHIResult<FArdaGraphNodeHandle>& Node)
		{
			if (!Node)
			{
				mError = Node.mStatus.mMessage;
			}
			return bool(Node);
		};
		// Functional composition: each operation consumes named values and produces new ones.
		// Consumers are attached first. EndGraphEdit infers the order and all inter-node barriers.
		if (!Attached(Graph.AttachOrFind<FArdaTerrainOverlayNode>("Composite overlay",
		        FArdaTerrainOverlayParameters{P.mSceneColor, P.mColor, Width, Height})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainDrawNode>("Draw terrain",
		        FArdaTerrainDrawParameters{P.mHeightmap,
		            P.mVertices,
		            P.mIndices,
		            P.mCamera,
		            P.mSceneColor,
		            P.mDepth,
		            Width,
		            Height})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainTriangulateNode>("Triangulate terrain",
		        FArdaTerrainTriangulateParameters{P.mHeightmap, P.mVertices, P.mIndices})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainErodeNode>("Erode heightmap",
		        FArdaTerrainErodeParameters{P.mRawHeightmap, P.mHeightmap})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainGenerateNode>("Generate heightmap",
		        FArdaTerrainGenerateParameters{P.mSettings, P.mRawHeightmap})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainUploadCameraNode>("Upload camera",
		        FArdaTerrainUploadCameraParameters{P.mCamera, Frame->mInputs})) ||
		    !Attached(Graph.AttachOrFind<FArdaTerrainUploadSettingsNode>("Upload settings bytes",
		        FArdaTerrainUploadSettingsParameters{P.mSettingsUpload, Frame->mInputs})))
		{
			return false;
		}
		auto Copy = Graph.AttachOrFind<FArdaGraphCopyNode>("Upload settings",
		    FArdaGraphCopyParameters{P.mSettingsUpload, P.mSettings, sizeof(FArdaTerrainSettings)});
		if (!Copy)
		{
			mError = Copy.mStatus.mMessage;
			return false;
		}
		if (!mbTerrainReadbackValidated)
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
		FFrameGraph* Frame = nullptr;
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

		Frame->mInputs->mSettings.mTime = mElapsedSeconds;
		Frame->mInputs->mCamera = cameraSettings;
		swapChain.PrepareSubmit();
		const auto Result = Frame->mGraph.Execute();
		if (!Result.mStatus)
		{
			mError = Result.mStatus.mMessage;
			return false;
		}
		if (!mbTerrainReadbackValidated)
		{
			mError = ValidateTerrainReadback(*Frame->mInputs->mVertexReadback, *Frame->mInputs->mIndexReadback);
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
