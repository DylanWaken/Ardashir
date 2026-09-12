#pragma once

#include "ArdaSwapChain.h"
#include "Nodes/ArdaTriangleNodes.h"
#include <memory>
#include <vector>

#include <filesystem>
#include <EASTL/string.h>

namespace arda
{
	class FArdaTriangleRenderer
	{
	public:
		FArdaTriangleRenderer();
		~FArdaTriangleRenderer();
		void ReleaseFrameGraphs();
		bool Initialize(arda::FArdaRHIDeviceRef device, arda::EArdaRHIFormat swapChainFormat);
		bool RenderFrame(arda::IArdaSwapChain& swapChain);

		[[nodiscard]] const eastl::string& GetError() const
		{
			return mError;
		}

	private:
		struct FFrameGraph;

		arda::FArdaRHIDeviceRef mDevice;
		bool mbGeometryUploaded = false;
		arda::FArdaRHIBufferRef mVertexBuffer;
		arda::FArdaRHIBufferRef mIndexBuffer;
		std::vector<std::unique_ptr<FFrameGraph>> mFrameGraphs;
		eastl::string mError;
	};
}
