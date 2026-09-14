#pragma once
#include "ArdaDependencyNode.h"
#include <filesystem>

namespace arda
{
	struct FArdaTriangleVertex
	{
		float mPosition[2];
		float mColor[3];
	};

	constexpr FArdaTriangleVertex ArdaTriangleVertices[] = {{{0.0f, 0.6f}, {1.0f, 0.1f, 0.1f}},
	    {{0.6f, -0.6f}, {0.1f, 1.0f, 0.1f}},
	    {{-0.6f, -0.6f}, {0.1f, 0.2f, 1.0f}}};
	constexpr uint16_t ArdaTriangleIndices[] = {0, 1, 2};

	/** Logical resources and dimensions of one draw; shader state belongs to the node library. */
	struct FArdaTriangleDrawParameters
	{
		/** Uploaded geometry read as a vertex buffer. */
		FArdaDependencyResourceHandle mVertices;
		/** Uploaded triangle indices read as an index buffer. */
		FArdaDependencyResourceHandle mIndices;
		/** Acquired swap-chain color target. */
		FArdaDependencyResourceHandle mColor;
		/** Viewport and scissor width. */
		uint32_t mWidth = 0;
		/** Viewport and scissor height. */
		uint32_t mHeight = 0;
	};

	/** Destination retained only by the triangle vertex upload node. */
	struct FArdaTriangleVertexUploadParameters
	{
		/** Buffer receiving the triangle vertex data. */
		FArdaDependencyResourceHandle mVertices;
	};

	/** Destination retained only by the triangle index upload node. */
	struct FArdaTriangleIndexUploadParameters
	{
		/** Buffer receiving the triangle index data. */
		FArdaDependencyResourceHandle mIndices;
	};

}
