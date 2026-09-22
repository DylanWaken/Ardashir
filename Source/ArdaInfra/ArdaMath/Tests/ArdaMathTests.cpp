#include "ArdaMath.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace arda
{
	namespace
	{
		FArdaVector3f Project(const FArdaMatrix4f& Matrix, const FArdaVector3f& Position)
		{
			const FArdaVector4f Clip = Matrix * FArdaVector4f(Position.x(), Position.y(), Position.z(), 1.f);
			return Clip.head<3>() / Clip.w();
		}
	}

	TEST(ArdaMath, DefaultMatricesAreEigenRowMajorAndWorkInProjectContainers)
	{
		static_assert(Eigen::Matrix4f::IsRowMajor);
		static_assert(FArdaMatrix2f::IsRowMajor && FArdaMatrix3f::IsRowMajor && FArdaMatrix4f::IsRowMajor);
		static_assert(FArdaMatrix3d::IsRowMajor && FArdaMatrix4d::IsRowMajor);
		static_assert(FArdaAffineTransform3f::MatrixType::IsRowMajor);
		static_assert(std::is_same_v<FArdaModelMatrix, FArdaMatrix4f>);
		eastl::vector<FArdaMatrix4f> Matrices(19, FArdaMatrix4f::Identity());
		for (size_t I = 0; I < Matrices.size(); ++I)
		{
			EXPECT_EQ(reinterpret_cast<uintptr_t>(&Matrices[I]) % alignof(FArdaMatrix4f), 0u);
			Matrices[I](0, 3) = float(I);
			EXPECT_FLOAT_EQ((Matrices[I] * FArdaVector4f(1, 2, 3, 1)).x(), float(I) + 1.f);
		}
		Matrices.reserve(41);
		for (size_t I = 0; I < Matrices.size(); ++I)
		{
			EXPECT_EQ(reinterpret_cast<uintptr_t>(&Matrices[I]) % alignof(FArdaMatrix4f), 0u);
			EXPECT_FLOAT_EQ(Matrices[I](0, 3), float(I));
		}
		const auto Camera = eastl::make_shared<FArdaCameraMatrices>();
		EXPECT_EQ(reinterpret_cast<uintptr_t>(Camera.get()) % alignof(FArdaCameraMatrices), 0u);
		EXPECT_TRUE(Camera->GetWorldToClip().isIdentity());
	}

	TEST(ArdaMath, ViewMapsEyeToOriginAndForwardToPositiveZ)
	{
		const FArdaVector3f Eye(3, -4, 7), Forward(2, 3, 1), Up(0, 0, 1);
		const auto View = MakeArdaViewMatrix(Eye, Forward, Up);
		EXPECT_TRUE(Project(View, Eye).isZero(1.e-6f));
		EXPECT_TRUE(Project(View, Eye + Forward.normalized() * 5.f).isApprox(FArdaVector3f(0, 0, 5), 1.e-6f));
		const FArdaMatrix3f Rotation = View.block<3, 3>(0, 0);
		EXPECT_TRUE((Rotation * Rotation.transpose()).isApprox(FArdaMatrix3f::Identity(), 1.e-6f));
		EXPECT_NEAR(Rotation.determinant(), 1.f, 1.e-6f);
	}

	TEST(ArdaMath, PerspectiveDefaultsToUnrealDeviceDepthAndVerticalFov)
	{
		constexpr float Near = 0.1f, Far = 1000.f;
		const auto Projection = MakeArdaPerspectiveProjection(1.57079632679f, 2.f, Near, Far);
		EXPECT_NEAR(Project(Projection, {0, 0, Near}).z(), ArdaDeviceZNear, 1.e-6f);
		EXPECT_NEAR(Project(Projection, {0, 0, Far}).z(), ArdaDeviceZFar, 1.e-6f);
		EXPECT_NEAR(Project(Projection, {0, 1, 1}).y(), 1.f, 1.e-6f);
		EXPECT_NEAR(Project(Projection, {2, 0, 1}).x(), 1.f, 1.e-6f);
		EXPECT_GT(Project(Projection, {0, 0, 1}).z(), Project(Projection, {0, 0, 10}).z());
		const auto Conventional = MakeArdaPerspectiveProjection(1.f, 1.f, Near, Far, false);
		EXPECT_NEAR(Project(Conventional, {0, 0, Near}).z(), 0.f, 1.e-6f);
		EXPECT_NEAR(Project(Conventional, {0, 0, Far}).z(), 1.f, 1.e-6f);
	}

	TEST(ArdaMath, InfinitePerspectiveRetainsNearOverViewDepth)
	{
		const auto Projection = MakeArdaInfinitePerspectiveProjection(1.f, 1.5f, 0.05f);
		for (float Z : {0.05f, 1.f, 100.f, 100000.f})
		{
			EXPECT_FLOAT_EQ(Project(Projection, {0, 0, Z}).z(), 0.05f / Z);
		}
		EXPECT_FLOAT_EQ(Projection(2, 2), 0.f);
		EXPECT_FLOAT_EQ(ArdaDepthClearValue, 0.f);
	}

	TEST(ArdaMath, OrthographicMapsAsymmetricBoundsAndReversedDepth)
	{
		const auto Projection = MakeArdaOrthographicProjection(-2, 6, -3, 1, 2, 12);
		EXPECT_TRUE(Project(Projection, {-2, -3, 2}).isApprox(FArdaVector3f(-1, -1, 1)));
		EXPECT_TRUE(Project(Projection, {6, 1, 12}).isApprox(FArdaVector3f(1, 1, 0), 1.e-6f));
		const auto Conventional = MakeArdaOrthographicProjection(-2, 6, -3, 1, 2, 12, false);
		EXPECT_NEAR(Project(Conventional, {2, -1, 2}).z(), 0.f, 1.e-6f);
		EXPECT_NEAR(Project(Conventional, {2, -1, 12}).z(), 1.f, 1.e-6f);
	}

	TEST(ArdaMath, CameraComposesProjectionAfterView)
	{
		FArdaCameraMatrices Camera;
		EXPECT_TRUE(Camera.GetWorldToClip().isIdentity());
		Camera.mWorldToView = MakeArdaViewMatrix({3, 4, 5}, {1, 0, 0}, {0, 0, 1});
		Camera.mViewToClip = MakeArdaInfinitePerspectiveProjection(1.f, 2.f, 0.1f);
		EXPECT_TRUE(Project(Camera.GetWorldToClip(), {5, 4, 5}).isApprox(FArdaVector3f(0, 0, 0.05f), 1.e-6f));
	}

	TEST(ArdaMath, NormalMatrixPreservesPerpendicularityUnderNonuniformScaleAndShear)
	{
		FArdaModelMatrix Model = FArdaModelMatrix::Identity();
		Model.block<3, 3>(0, 0) << 2, 1, 0, 0, 3, 1, 0, 0, 4;
		Model.block<3, 1>(0, 3) = FArdaVector3f(7, 8, 9);
		const FArdaVector3f Tangent(1, 1, 0), Normal(1, -1, 0);
		const FArdaVector3f TransformedTangent = Model.block<3, 3>(0, 0) * Tangent;
		const FArdaVector3f TransformedNormal = MakeArdaNormalMatrix(Model) * Normal;
		EXPECT_NEAR(TransformedTangent.dot(TransformedNormal), 0.f, 1.e-6f);
	}

	TEST(ArdaMath, ShaderPackingUsesRowsAndPadsThreeByThreeRows)
	{
		FArdaMatrix4f Matrix;
		Matrix << 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16;
		const auto Packed = PackArdaShaderMatrix(Matrix);
		for (int I = 0; I < 16; ++I)
		{
			EXPECT_FLOAT_EQ(Packed.mValues[I], float(I + 1));
		}
		const FArdaMatrix3f Normal = Matrix.block<3, 3>(0, 0);
		const auto PackedNormal = PackArdaShaderMatrix(Normal);
		const float Expected[] = {1, 2, 3, 0, 5, 6, 7, 0, 9, 10, 11, 0};
		for (int I = 0; I < 12; ++I)
		{
			EXPECT_FLOAT_EQ(PackedNormal.mValues[I], Expected[I]);
		}
		FArdaCameraMatrices Camera;
		Camera.mWorldToView = Matrix;
		const auto PackedCamera = PackArdaShaderCameraMatrices(Camera);
		EXPECT_EQ(offsetof(FArdaShaderCameraMatrices, mViewToClip), 64u);
		EXPECT_FLOAT_EQ(PackedCamera.mWorldToView.mValues[3], 4.f);
		EXPECT_FLOAT_EQ(PackedCamera.mViewToClip.mValues[15], 1.f);
	}

	TEST(ArdaMath, InvalidCamerasAndSingularNormalTransformsAreRejected)
	{
		EXPECT_THROW(MakeArdaViewMatrix({0, 0, 0}, {0, 0, 0}, {0, 1, 0}), std::invalid_argument);
		EXPECT_THROW(MakeArdaViewMatrix({0, 0, 0}, {0, 1, 0}, {0, 2, 0}), std::invalid_argument);
		EXPECT_THROW(MakeArdaPerspectiveProjection(0, 1, 0.1f, 10), std::invalid_argument);
		EXPECT_THROW(MakeArdaPerspectiveProjection(1, 0, 0.1f, 10), std::invalid_argument);
		EXPECT_THROW(MakeArdaPerspectiveProjection(1, 1, 1, 1), std::invalid_argument);
		EXPECT_THROW(MakeArdaInfinitePerspectiveProjection(1, 1, -1), std::invalid_argument);
		EXPECT_THROW(MakeArdaOrthographicProjection(1, 1, 0, 1, 0, 1), std::invalid_argument);
		EXPECT_THROW(MakeArdaInfinitePerspectiveProjection(std::numeric_limits<float>::quiet_NaN(), 1, 1),
		    std::invalid_argument);
		EXPECT_THROW(MakeArdaNormalMatrix(FArdaModelMatrix::Zero()), std::invalid_argument);
		FArdaModelMatrix Singular = FArdaModelMatrix::Identity();
		Singular(0, 0) = 0;
		EXPECT_THROW(MakeArdaNormalMatrix(Singular), std::invalid_argument);
	}
}
