#pragma once

#include "ArdaDependencyNode.h"
#include <EASTL/vector.h>
#include <EASTL/array.h>

namespace arda
{
	constexpr uint32_t ArdaCornellSphereFaceCount = 6;
	constexpr uint32_t ArdaCornellSphereFaceResolution = 16;
	constexpr uint32_t ArdaCornellRoomAndBoxQuadCount = 18;
	constexpr uint32_t ArdaCornellSphereTriangleCount =
	    ArdaCornellSphereFaceCount * ArdaCornellSphereFaceResolution * ArdaCornellSphereFaceResolution * 2;
	constexpr uint32_t ArdaCornellTriangleCount =
	    ArdaCornellRoomAndBoxQuadCount * 2 + ArdaCornellSphereTriangleCount * 2;
	constexpr uint32_t ArdaCornellVertexCount = ArdaCornellTriangleCount * 3;
	constexpr uint32_t ArdaCornellIndexCount = ArdaCornellTriangleCount * 3;
	constexpr uint32_t ArdaCornellMaterialCount = 7;
	constexpr uint64_t ArdaCornellMaxSampleRadianceScratchBytes = 256ull * 1024ull * 1024ull;

	// Keeps one ray dispatch responsive enough for window-close handling
	// and below common desktop watchdog thresholds. Millions of paths are
	// still available to the hardware scheduler concurrently.
	constexpr uint64_t ArdaCornellMaxPathSegmentsPerDispatch = 96ull * 1024ull * 1024ull;

	struct alignas(16) FArdaCornellVertex
	{
		float mPosition[3];
		float mPadding = 0.0f;
		float mNormal[3];
		uint32_t mMaterialId = 0;
	};

	struct alignas(16) FArdaCornellMaterial
	{
		float mBaseColor[3];
		float mRoughness = 0.0f;
		float mEmission[3];
		float mMetallic = 0.0f;
		float mTransmission = 0.0f;
		float mIor = 1.0f;
		float mPadding[2]{};
	};

	static_assert(sizeof(FArdaCornellVertex) == 32, "Cornell vertex layout must match HLSL.");
	static_assert(sizeof(FArdaCornellMaterial) == 48, "Cornell material layout must match HLSL.");

	/** Constant layout shared with the Cornell shaders. */
	struct alignas(16) FArdaCornellFrameConstants
	{
		eastl::array<float, 4> mCameraPositionAndTanHalfFovX{};
		eastl::array<float, 4> mCameraForwardAndTanHalfFovY{};
		eastl::array<float, 4> mCameraRightAndExposure{};
		eastl::array<float, 4> mCameraUpAndLightArea{};
		eastl::array<uint32_t, 4> mImageAndSampling{};
		eastl::array<uint32_t, 4> mPathAndSeed{};
	};

	/** Frame input changes before synchronous Execute; the compiled node identity remains stable. */
	struct FArdaCornellFrameInput
	{
		FArdaCornellFrameConstants mConstants;
	};

	struct FArdaCornellNodeParameters
	{
		eastl::array<FArdaDependencyResourceHandle, 7> mResources{};
		eastl::shared_ptr<FArdaCornellFrameInput> mFrame;
		uint32_t mWidth = 1, mHeight = 1, mSamples = 1;
		uint32_t mVertexCount = 0, mIndexCount = 0, mVertexStride = 0;
		EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::PreferFastTrace;
		uint64_t mWorkspaceBytes = 0;
	};

}
