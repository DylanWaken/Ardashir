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
	struct FArdaTriangleNodeParameters
	{
		FArdaDependencyResourceHandle mVertices, mIndices, mColor;
		uint32_t mWidth = 0, mHeight = 0;
	};

}
