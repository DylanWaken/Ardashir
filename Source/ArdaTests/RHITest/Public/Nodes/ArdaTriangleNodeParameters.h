#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"
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

	/** Physical geometry inputs consumed by the vertex stage. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTriangleVertexParameters)
		/** Uploaded geometry read as a vertex buffer. */
		ARDA_NODE_BUFFER(mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer)
		/** Uploaded triangle indices read as an index buffer. */
		ARDA_NODE_BUFFER(mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Rasterization consumes logical vertex results without allocating an intermediate buffer. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTriangleRasterParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
		/** Viewport and scissor width. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 0)
		/** Viewport and scissor height. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** The pixel stage consumes interpolated fragments and writes the acquired color target. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTrianglePixelParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_COLOR_TARGET(mColor)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Destination retained only by the triangle vertex upload node. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTriangleVertexUploadParameters)
		/** Buffer receiving the triangle vertex data. */
		ARDA_NODE_BUFFER(mVertices, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Destination retained only by the triangle index upload node. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaTriangleIndexUploadParameters)
		/** Buffer receiving the triangle index data. */
		ARDA_NODE_BUFFER(mIndices, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
	ARDA_END_NODE_PARAMETER_STRUCT()

}
