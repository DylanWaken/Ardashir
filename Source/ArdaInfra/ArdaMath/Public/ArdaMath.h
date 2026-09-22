/** Project-wide Eigen CPU linear algebra and explicit graphics upload layouts. */
#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <type_traits>

namespace arda
{
	inline constexpr float ArdaDeviceZNear = 1.f;
	inline constexpr float ArdaDeviceZFar = 0.f;
	inline constexpr float ArdaDepthClearValue = ArdaDeviceZFar;

	/** CPU matrices multiply column vectors; transforms compose Projection * View * Model.
	 * Storage is explicitly row-major, independent of Eigen's global default options.
	 * Fixed-size Eigen values must be initialized explicitly (Identity/Zero or coefficients).
	 */
	template <class Scalar, int Rows, int Columns>
	using TArdaMatrix =
	    Eigen::Matrix<Scalar, Rows, Columns, (Columns == 1 && Rows != 1) ? Eigen::ColMajor : Eigen::RowMajor>;
	using FArdaVector2f = TArdaMatrix<float, 2, 1>;
	using FArdaVector3f = TArdaMatrix<float, 3, 1>;
	using FArdaVector4f = TArdaMatrix<float, 4, 1>;
	using FArdaVector2d = TArdaMatrix<double, 2, 1>;
	using FArdaVector3d = TArdaMatrix<double, 3, 1>;
	using FArdaVector4d = TArdaMatrix<double, 4, 1>;
	using FArdaMatrix2f = TArdaMatrix<float, 2, 2>;
	using FArdaMatrix3f = TArdaMatrix<float, 3, 3>;
	using FArdaMatrix4f = TArdaMatrix<float, 4, 4>;
	using FArdaMatrix3d = TArdaMatrix<double, 3, 3>;
	using FArdaMatrix4d = TArdaMatrix<double, 4, 4>;
	/** Row-major 3x4 affine coefficients used at native ray-tracing instance boundaries. */
	using FArdaMatrix3x4f = Eigen::Matrix<float, 3, 4, Eigen::RowMajor>;
	using FArdaQuaternionf = Eigen::Quaternion<float>;
	using FArdaQuaterniond = Eigen::Quaternion<double>;
	using FArdaAffineTransform3f = Eigen::Transform<float, 3, Eigen::Affine, Eigen::RowMajor>;
	using FArdaAffineTransform3d = Eigen::Transform<double, 3, Eigen::Affine, Eigen::RowMajor>;

	using FArdaModelMatrix = FArdaMatrix4f;
	using FArdaViewMatrix = FArdaMatrix4f;
	using FArdaProjectionMatrix = FArdaMatrix4f;
	using FArdaViewProjectionMatrix = FArdaMatrix4f;
	using FArdaNormalMatrix = FArdaMatrix3f;

	/** A positive-Z-forward view and a [0,1] clip-depth projection. World up is chosen by the caller.
	 * Projection helpers default to Unreal-style reversed device Z: near=1, far=0, clear depth=0.
	 * Use a greater/greater-equal depth comparison with reversed projections.
	 */
	struct FArdaCameraMatrices
	{
		FArdaViewMatrix mWorldToView = FArdaViewMatrix::Identity();
		FArdaProjectionMatrix mViewToClip = FArdaProjectionMatrix::Identity();

		FArdaViewProjectionMatrix GetWorldToClip() const
		{
			return mViewToClip * mWorldToView;
		}
	};

	/** Invalid/nonfinite inputs to the construction helpers throw std::invalid_argument. */
	FArdaViewMatrix MakeArdaViewMatrix(const FArdaVector3f& Position,
	    const FArdaVector3f& Forward,
	    const FArdaVector3f& Up);
	/** Vertical FOV in radians, width/height aspect, 0 < Near < Far. Reverse depth maps Near to 1. */
	FArdaProjectionMatrix MakeArdaPerspectiveProjection(float VerticalFovRadians,
	    float Aspect,
	    float Near,
	    float Far,
	    bool ReverseDepth = true);
	/** Infinite far plane; reverse depth is the default and maps infinite distance to zero. */
	FArdaProjectionMatrix MakeArdaInfinitePerspectiveProjection(float VerticalFovRadians,
	    float Aspect,
	    float Near,
	    bool ReverseDepth = true);
	/** Positive-Z orthographic volume, 0 <= Near < Far. No backend-specific Y flip is applied. */
	FArdaProjectionMatrix MakeArdaOrthographicProjection(float Left,
	    float Right,
	    float Bottom,
	    float Top,
	    float Near,
	    float Far,
	    bool ReverseDepth = true);
	/** Inverse transpose of the model's linear part; rejects non-affine or singular transforms. */
	FArdaNormalMatrix MakeArdaNormalMatrix(const FArdaModelMatrix& Model);

	/** POD constant-buffer representation of HLSL row_major float4x4 (64 bytes). */
	struct alignas(16) FArdaShaderMatrix4f
	{
		float mValues[16]{};
	};

	/** POD constant-buffer representation of HLSL row_major float3x3 (three padded float4 rows). */
	struct alignas(16) FArdaShaderMatrix3f
	{
		float mValues[12]{};
	};

	struct alignas(16) FArdaShaderCameraMatrices
	{
		FArdaShaderMatrix4f mWorldToView;
		FArdaShaderMatrix4f mViewToClip;
	};

	/** Copy coefficients, never the Eigen object's representation, to the shader ABI. */
	FArdaShaderMatrix4f PackArdaShaderMatrix(const FArdaMatrix4f& Matrix);
	FArdaShaderMatrix3f PackArdaShaderMatrix(const FArdaMatrix3f& Matrix);
	FArdaShaderCameraMatrices PackArdaShaderCameraMatrices(const FArdaCameraMatrices& Camera);

	static_assert(sizeof(FArdaShaderMatrix4f) == 64 && alignof(FArdaShaderMatrix4f) == 16);
	static_assert(sizeof(FArdaShaderMatrix3f) == 48 && alignof(FArdaShaderMatrix3f) == 16);
	static_assert(sizeof(FArdaShaderCameraMatrices) == 128);
	static_assert(std::is_trivially_copyable_v<FArdaShaderCameraMatrices>);
}
