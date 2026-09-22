#include "ArdaMath.h"
#include <Eigen/LU>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace arda
{
	namespace
	{
		void Require(bool Valid, const char* Message)
		{
			if (!Valid)
			{
				throw std::invalid_argument(Message);
			}
		}

		FArdaProjectionMatrix PerspectiveBase(float Fov, float Aspect, float Near)
		{
			Require(std::isfinite(Fov) && Fov > 0.f && Fov < 3.14159265358979323846 && std::isfinite(Aspect) &&
			        Aspect > 0.f && std::isfinite(Near) && Near > 0.f,
			    "Perspective requires a finite vertical FOV in (0,pi), positive aspect and near plane.");
			FArdaProjectionMatrix Result = FArdaProjectionMatrix::Zero();
			Result(1, 1) = float(1.0 / std::tan(double(Fov) * 0.5));
			Result(0, 0) = float(double(Result(1, 1)) / Aspect);
			Result(3, 2) = 1.f;
			Require(Result.allFinite(), "Perspective coefficients overflow float precision.");
			return Result;
		}
	}

	FArdaViewMatrix MakeArdaViewMatrix(const FArdaVector3f& Position,
	    const FArdaVector3f& Forward,
	    const FArdaVector3f& Up)
	{
		Require(Position.allFinite() && Forward.allFinite() && Up.allFinite(), "View inputs must be finite.");
		const float ForwardLength = Forward.stableNorm(), UpLength = Up.stableNorm();
		Require(std::isfinite(ForwardLength) && std::isfinite(UpLength) && ForwardLength > 0.f && UpLength > 0.f,
		    "View directions must be nonzero and representable.");
		const FArdaVector3f Z = Forward / ForwardLength;
		const FArdaVector3f Cross = (Up / UpLength).cross(Z);
		const float CrossLength = Cross.norm();
		Require(CrossLength > 1.e-6f, "View up and forward must not be parallel.");
		const FArdaVector3f X = Cross / CrossLength;
		const FArdaVector3f Y = Z.cross(X);
		FArdaViewMatrix Result = FArdaViewMatrix::Identity();
		Result.block<1, 3>(0, 0) = X.transpose();
		Result.block<1, 3>(1, 0) = Y.transpose();
		Result.block<1, 3>(2, 0) = Z.transpose();
		Result.block<3, 1>(0, 3) = -Result.block<3, 3>(0, 0) * Position;
		Require(Result.allFinite(), "View translation overflows float precision.");
		return Result;
	}

	FArdaProjectionMatrix MakeArdaPerspectiveProjection(float Fov, float Aspect, float Near, float Far, bool Reverse)
	{
		auto Result = PerspectiveBase(Fov, Aspect, Near);
		Require(std::isfinite(Far) && Far > Near, "Perspective far plane must be finite and greater than near.");
		const double Range = double(Far) - Near;
		Result(2, 2) = float(Reverse ? -double(Near) / Range : double(Far) / Range);
		Result(2, 3) = float((Reverse ? 1.0 : -1.0) * double(Near) * Far / Range);
		Require(Result.allFinite(), "Perspective depth coefficients overflow float precision.");
		return Result;
	}

	FArdaProjectionMatrix MakeArdaInfinitePerspectiveProjection(float Fov, float Aspect, float Near, bool Reverse)
	{
		auto Result = PerspectiveBase(Fov, Aspect, Near);
		Result(2, 2) = Reverse ? 0.f : 1.f;
		Result(2, 3) = Reverse ? Near : -Near;
		return Result;
	}

	FArdaProjectionMatrix MakeArdaOrthographicProjection(float Left,
	    float Right,
	    float Bottom,
	    float Top,
	    float Near,
	    float Far,
	    bool Reverse)
	{
		Require(std::isfinite(Left) && std::isfinite(Right) && std::isfinite(Bottom) && std::isfinite(Top) &&
		        std::isfinite(Near) && std::isfinite(Far) && Right > Left && Top > Bottom && Near >= 0.f && Far > Near,
		    "Orthographic bounds must be finite, nonempty, and satisfy 0 <= near < far.");
		FArdaProjectionMatrix Result = FArdaProjectionMatrix::Identity();
		Result(0, 0) = float(2.0 / (double(Right) - Left));
		Result(1, 1) = float(2.0 / (double(Top) - Bottom));
		Result(0, 3) = float(-(double(Right) + Left) / (double(Right) - Left));
		Result(1, 3) = float(-(double(Top) + Bottom) / (double(Top) - Bottom));
		Result(2, 2) = float((Reverse ? -1.0 : 1.0) / (double(Far) - Near));
		Result(2, 3) = float((Reverse ? double(Far) : -double(Near)) / (double(Far) - Near));
		Require(Result.allFinite(), "Orthographic coefficients overflow float precision.");
		return Result;
	}

	FArdaNormalMatrix MakeArdaNormalMatrix(const FArdaModelMatrix& Model)
	{
		Require(Model.allFinite() && Model.row(3).isApprox(FArdaVector4f(0, 0, 0, 1).transpose()),
		    "Normal matrices require a finite affine model transform.");
		const FArdaMatrix3f Linear = Model.block<3, 3>(0, 0);
		const Eigen::FullPivLU<FArdaMatrix3f> Factor(Linear);
		Require(Factor.isInvertible(), "Normal matrices require a nonsingular model transform.");
		const FArdaNormalMatrix Result = Factor.inverse().transpose();
		Require(Result.allFinite(), "Normal matrix coefficients overflow float precision.");
		return Result;
	}

	FArdaShaderMatrix4f PackArdaShaderMatrix(const FArdaMatrix4f& Matrix)
	{
		FArdaShaderMatrix4f Result;
		std::memcpy(Result.mValues, Matrix.data(), sizeof(Result.mValues));
		return Result;
	}

	FArdaShaderMatrix3f PackArdaShaderMatrix(const FArdaMatrix3f& Matrix)
	{
		FArdaShaderMatrix3f Result;
		for (int Row = 0; Row < 3; ++Row)
		{
			for (int Column = 0; Column < 3; ++Column)
			{
				Result.mValues[Row * 4 + Column] = Matrix(Row, Column);
			}
		}
		return Result;
	}

	FArdaShaderCameraMatrices PackArdaShaderCameraMatrices(const FArdaCameraMatrices& Camera)
	{
		return {PackArdaShaderMatrix(Camera.mWorldToView), PackArdaShaderMatrix(Camera.mViewToClip)};
	}
}
