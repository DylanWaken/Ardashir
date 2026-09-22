#include "ArdaRHITestPch.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"

#include "ArdaTriangleRenderer.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"

namespace arda
{
	struct FArdaTriangleRenderer::FArdaFrameGraph
	{
		FArdaRHITextureRef mColor;
		FArdaDependencyGraph mGraph;

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
			FArdaGraphEdit Edit(Graph);
			auto Status = Edit.GetStatus();
			if (!Status)
			{
				mError = Status.mMessage;
				return false;
			}

			// Intermediate values connect independently registered graphics stages.
			auto Color = BindTexture(Edit, Created->mColor, "Swap-chain color");
			auto V = BindBuffer(Edit, mVertexBuffer, "Triangle vertices");
			auto I = BindBuffer(Edit, mIndexBuffer, "Triangle indices");
			if (!Color || !V || !I)
			{
				mError = "Unable to import triangle frame resources.";
				return false;
			}
			const auto VertexResults = TriangleVS(Edit, V, I);
			const auto Fragments = TriangleRasterize(Edit, VertexResults, swapChain.GetWidth(), swapChain.GetHeight());
			const auto Pixels = TrianglePS(Edit, Fragments, Color);
			if (!Pixels)
			{
				mError = Edit.GetStatus().mMessage;
				return false;
			}
			if (!mbGeometryUploaded)
			{
				// Record independent uploads after their consumer. ArdaInductor orders them before drawing.
				auto Vertices = TriangleVertexUpload(Edit, {V.GetHandle()}, "Upload vertices");
				auto Indices = TriangleIndexUpload(Edit, {I.GetHandle()}, "Upload indices");
				if (!Vertices || !Indices)
				{
					mError = Edit.GetStatus().mMessage;
					return false;
				}
			}

			// Compile the graph before adding the new frame to the replay cache.
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
			Frame = Created.get();
			mFrameGraphs.push_back(std::move(Created));
		}

		// Execute compiled work before presenting and recording the next revision.
		swapChain.PrepareSubmit();
		const auto Result = Frame->mGraph.Execute();
		if (!Result.mStatus)
		{
			mError = Result.mStatus.mMessage;
			return false;
		}

		if (!mbGeometryUploaded)
		{
			// Describe the next revision using the geometry initialized by the first execution.
			auto& Graph = Frame->mGraph;
			FArdaGraphEdit Edit(Graph);
			const auto Color = BindTexture(Edit, Frame->mColor, "Swap-chain color");
			const auto Vertices = BindBuffer(Edit, mVertexBuffer, "Triangle vertices");
			const auto Indices = BindBuffer(Edit, mIndexBuffer, "Triangle indices");
			const auto VertexResults = TriangleVS(Edit, Vertices, Indices);
			const auto Fragments = TriangleRasterize(Edit, VertexResults, swapChain.GetWidth(), swapChain.GetHeight());
			TrianglePS(Edit, Fragments, Color);
			auto Status = Edit.Commit();
			if (Status)
			{
				Status = Graph.Compile();
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
