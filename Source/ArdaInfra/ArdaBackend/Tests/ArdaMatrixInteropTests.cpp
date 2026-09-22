#include "RHI/Providers/ArdaProviderCommandTypes.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"

#include <gtest/gtest.h>

#include <cstring>
#include <type_traits>

TEST(ArdaRHI, DefaultDepthStateUsesReversedDeviceZ)
{
	const arda::FArdaRHIDepthStencilState Depth;
	EXPECT_EQ(Depth.mDepthFunc, arda::EArdaRHIComparisonFunc::GreaterOrEqual);
	EXPECT_FLOAT_EQ(arda::ArdaDepthClearValue, 0.f);
	EXPECT_FLOAT_EQ(arda::ArdaDeviceZNear, 1.f);
}

TEST(ArdaRHI, RayTracingInstancesUseIdentityAffineTransformsByDefault)
{
	const arda::FArdaRHIRayTracingInstanceDesc Instance;
	EXPECT_TRUE(Instance.mTransform.isApprox(arda::FArdaMatrix3x4f::Identity()));
}

TEST(ArdaRHI, RayTracingInstanceTransformHasExplicitNativeRowMajorLayout)
{
	using namespace arda;
	static_assert(FArdaMatrix3x4f::IsRowMajor);
	static_assert(std::is_same_v<decltype(FArdaProviderRayTracingInstance::mTransform), float[3][4]>);

	FArdaAffineTransform3f ObjectToWorld = FArdaAffineTransform3f::Identity();
	ObjectToWorld.matrix() << 2.f, 3.f, 5.f, 7.f, 11.f, 13.f, 17.f, 19.f, 23.f, 29.f, 31.f, 37.f, 0.f, 0.f, 0.f, 1.f;
	FArdaRHIRayTracingInstanceDesc Instance;
	Instance.mTransform = ObjectToWorld.matrix().topRows<3>();

	const float Expected[3][4] = {{2.f, 3.f, 5.f, 7.f}, {11.f, 13.f, 17.f, 19.f}, {23.f, 29.f, 31.f, 37.f}};
	FArdaProviderRayTracingInstance Native;
	std::memcpy(Native.mTransform, Instance.mTransform.data(), sizeof(Native.mTransform));
	EXPECT_EQ(std::memcmp(Native.mTransform, Expected, sizeof(Expected)), 0);
	EXPECT_FLOAT_EQ(Native.mTransform[0][3], 7.f);
	EXPECT_FLOAT_EQ(Native.mTransform[1][3], 19.f);
	EXPECT_FLOAT_EQ(Native.mTransform[2][3], 37.f);
}
