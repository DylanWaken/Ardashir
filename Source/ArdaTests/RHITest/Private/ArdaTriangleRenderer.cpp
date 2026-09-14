#include "ArdaRHITestPch.h"

#include "ArdaTriangleRenderer.h"
#include "ArdaDependencyGraphNodes.h"

namespace arda
{
	struct FArdaTriangleRenderer::FArdaFrameGraph
	{
		FArdaRHITextureRef mColor;
		FArdaDependencyGraph mGraph;
		FArdaTriangleDrawParameters mDrawParameters;
		FArdaGraphNodeHandle mVertexUpload;
		FArdaGraphNodeHandle mIndexUpload;

		explicit FArdaFrameGraph(FArdaRHIDeviceRef Device)
		    : mGraph(eastl::move(Device))
		{
		}
	};

	FArdaTriangleRenderer::FArdaTriangleRenderer() = default;
	FArdaTriangleRenderer::~FArdaTriangleRenderer() = default;

	void FArdaTriangleRenderer::ReleaseFrameGraphs()
	{
		mFrameGraphs.clear();
	}

	bool FArdaTriangleRenderer::Initialize(arda::FArdaRHIDeviceRef device, arda::EArdaRHIFormat swapChainFormat)
	{
		mDevice = eastl::move(device);
		if (!mDevice || !mDevice->GetCapabilities().mQueues.mbGraphics)
		{
			mError = "The initialized backend does not expose a graphics device.";
			return false;
		}

		(void)swapChainFormat;

		// Keep geometry in imported buffers so one-time uploads can be retired after the first frame.
		arda::FArdaRHIBufferDesc bufferDesc;
		bufferDesc.mByteSize = sizeof(ArdaTriangleVertices);
		bufferDesc.mUsage = arda::EArdaRHIBufferUsage::Vertex;
		bufferDesc.mInitialState = arda::EArdaRHIResourceState::VertexBuffer;
		bufferDesc.mbKeepInitialState = true;
		bufferDesc.mDebugName = "Triangle vertex buffer";
		auto vertexBuffer = mDevice->CreateBuffer(bufferDesc);
		bufferDesc.mByteSize = sizeof(ArdaTriangleIndices);
		bufferDesc.mUsage = arda::EArdaRHIBufferUsage::Index;
		bufferDesc.mInitialState = arda::EArdaRHIResourceState::IndexBuffer;
		bufferDesc.mDebugName = "Triangle index buffer";
		auto indexBuffer = mDevice->CreateBuffer(bufferDesc);
		if (!vertexBuffer || !indexBuffer)
		{
			mError = "RHI failed to create the triangle geometry buffers.";
			return false;
		}
		mVertexBuffer = eastl::move(vertexBuffer.mValue);
		mIndexBuffer = eastl::move(indexBuffer.mValue);

		mbGeometryUploaded = false;

		mError.clear();
		return true;
	}

	bool FArdaTriangleRenderer::RenderFrame(arda::IArdaSwapChain& swapChain)
	{
		arda::FArdaRHIFramebufferRef framebuffer;
		if (!swapChain.AcquireFrame(framebuffer))
		{
			mError = swapChain.GetError();
			return false;
		}
		const auto& framebufferDesc = framebuffer->GetDesc();
		if (framebufferDesc.mColorAttachments.empty() || !framebufferDesc.mColorAttachments[0].mTexture)
		{
			mError = "The acquired swap-chain framebuffer has no color attachment.";
			return false;
		}
		const auto colorAttachment = framebufferDesc.mColorAttachments[0];

		// Look up the persistent graph belonging to this swap-chain image.
		FArdaFrameGraph* Frame = nullptr;
		for (auto& Cached : mFrameGraphs)
		{
			if (Cached->mColor.Get() == colorAttachment.mTexture.Get())
			{
				Frame = Cached.get();
			}
		}
		if (!Frame)
		{
			auto Created = std::make_unique<FArdaFrameGraph>(mDevice);
			Created->mColor = colorAttachment.mTexture;
			auto& Graph = Created->mGraph;
			auto Status = Graph.BeginGraphEdit();
			if (!Status)
			{
				mError = Status.mMessage;
				return false;
			}

			// Wire imported geometry and the acquired color target into the registered draw node.
			auto Color = Graph.ImportTexture("Swap-chain color", Created->mColor);
			auto V = Graph.ImportBuffer("Triangle vertices", mVertexBuffer);
			auto I = Graph.ImportBuffer("Triangle indices", mIndexBuffer);
			if (!Color || !V || !I)
			{
				mError = "Unable to import triangle frame resources.";
				return false;
			}
			Created->mDrawParameters = {V.mValue, I.mValue, Color.mValue, swapChain.GetWidth(), swapChain.GetHeight()};
			auto Draw = Graph.AttachOrFind<FArdaTriangleDrawNode>("Render triangle", Created->mDrawParameters);
			if (!Draw)
			{
				mError = Draw.mStatus.mMessage;
				return false;
			}
			if (!mbGeometryUploaded)
			{
				// Attach independent uploads after their consumer. ArdaInductor orders them before drawing.
				auto Vertices = Graph.AttachOrFind<FArdaTriangleVertexUploadNode>("Upload vertices", {V.mValue});
				auto Indices = Graph.AttachOrFind<FArdaTriangleIndexUploadNode>("Upload indices", {I.mValue});
				if (!Vertices || !Indices)
				{
					mError = !Vertices ? Vertices.mStatus.mMessage : Indices.mStatus.mMessage;
					return false;
				}
				Created->mVertexUpload = Vertices.mValue;
				Created->mIndexUpload = Indices.mValue;
			}

			// Compile the graph before adding the new frame to the replay cache.
			Status = Graph.EndGraphEdit();
			if (!Status)
			{
				mError = Status.mMessage;
				return false;
			}
			Frame = Created.get();
			mFrameGraphs.push_back(std::move(Created));
		}

		// Execute compiled work before presenting or removing completed one-time operations.
		swapChain.PrepareSubmit();
		const auto Result = Frame->mGraph.Execute();
		if (!Result.mStatus)
		{
			mError = Result.mStatus.mMessage;
			return false;
		}

		if (!mbGeometryUploaded)
		{
			// Retire one-time uploads after successful execution. Removing a producer cascades to
			// its draw consumer, so reattach that draw using the now-initialized imported buffers.
			auto& Graph = Frame->mGraph;
			auto Status = Graph.BeginGraphEdit();
			if (Status)
			{
				Status = Graph.RemoveNode(Frame->mVertexUpload);
			}
			if (Status)
			{
				Status = Graph.RemoveNode(Frame->mIndexUpload);
			}
			if (Status)
			{
				Status = Graph.AttachOrFind<FArdaTriangleDrawNode>("Render triangle", Frame->mDrawParameters).mStatus;
			}
			if (Status)
			{

				// Compile the graph before adding the new frame to the replay cache.
				Status = Graph.EndGraphEdit();
			}
			if (!Status)
			{
				mError = Status.mMessage;
				return false;
			}
			mbGeometryUploaded = true;
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
