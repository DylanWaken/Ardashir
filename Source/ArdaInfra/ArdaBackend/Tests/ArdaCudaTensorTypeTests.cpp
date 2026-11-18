#include "RHI/CUDA/ArdaCudaModule.h"
#include "RHI/CUDA/ArdaRHICuda.h"

#include <gtest/gtest.h>

#include <cstring>

namespace arda
{
	namespace
	{
		FArdaCudaCapabilities MakeTensorTypeCapabilities(uint32_t Architecture)
		{
			FArdaCudaCapabilities Capabilities;
			Capabilities.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
			Capabilities.mComputeCapability = Architecture;
			Capabilities.mMaxThreadsPerBlock = 1024;
			for (uint32_t Axis = 0; Axis < 3; ++Axis)
			{
				Capabilities.mMaxBlockSize[Axis] = 1024;
				Capabilities.mMaxGridSize[Axis] = 65535;
			}
			return Capabilities;
		}

		TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>> MakeTensorTypeKernel(const char* Source,
		    uint32_t Architecture,
		    const char* Identity)
		{
			FArdaCudaModuleDesc ModuleDesc;
			ModuleDesc.mFormat = EArdaCudaModuleFormat::Ptx;
			ModuleDesc.mBuild.mIdentity = Identity;
			ModuleDesc.mBuild.mArchitectures.push_back({Architecture, false});
			ModuleDesc.mImage.assign(Source, Source + std::strlen(Source));
			auto Module = CreateArdaCudaModule(ModuleDesc);
			if (!Module)
			{
				return {{}, Module.mStatus};
			}
			FArdaCudaModuleKernelDesc KernelDesc;
			KernelDesc.mEntryPoint = "ArdaPrecision";
			KernelDesc.mParameterSize = 8;
			KernelDesc.mParameterAlignment = 8;
			KernelDesc.mArguments = {{0, 8, 8, EArdaCudaModuleArgumentKind::Buffer}};
			return CreateArdaCudaModuleKernel(Module.mValue, KernelDesc);
		}
	}

	TEST(ArdaCudaTensorType, HalfAndBfloatInstructionFamiliesHaveDistinctBoundaries)
	{
		struct FArdaExpectedPrecision
		{
			uint32_t mArchitecture;
			bool mbHalfArithmetic;
			bool mbHalfMatrix;
			bool mbBfloatConversionAndFma;
			bool mbBfloatAddMultiply;
		};

		constexpr FArdaExpectedPrecision Cases[] = {{50, false, false, false, false},
		    {53, true, false, false, false},
		    {61, true, false, false, false},
		    {70, true, true, false, false},
		    {75, true, true, false, false},
		    {80, true, true, true, false},
		    {89, true, true, true, false},
		    {90, true, true, true, true},
		    {120, true, true, true, true}};
		for (const auto& Expected : Cases)
		{
			SCOPED_TRACE(Expected.mArchitecture);
			const auto Capabilities = MakeTensorTypeCapabilities(Expected.mArchitecture);
			const auto Half = GetArdaCudaTensorTypeSupport(Capabilities, EArdaRHIDataType::Float16);
			EXPECT_TRUE(Half.mbStorage);
			EXPECT_TRUE(Half.mbInstructionProfileKnown);
			EXPECT_TRUE(Half.mbFloat32Conversion);
			EXPECT_EQ(Half.mbScalarAddMultiply, Expected.mbHalfArithmetic);
			EXPECT_EQ(Half.mbScalarFma, Expected.mbHalfArithmetic);
			EXPECT_EQ(Half.mbTensorCore, Expected.mbHalfMatrix);
			EXPECT_FALSE(Half.mbPackedInt8DotProduct);
			const auto Bfloat = GetArdaCudaTensorTypeSupport(Capabilities, EArdaRHIDataType::BFloat16);
			EXPECT_TRUE(Bfloat.mbStorage);
			EXPECT_TRUE(Bfloat.mbInstructionProfileKnown);
			EXPECT_EQ(Bfloat.mbFloat32Conversion, Expected.mbBfloatConversionAndFma);
			EXPECT_EQ(Bfloat.mbScalarFma, Expected.mbBfloatConversionAndFma);
			EXPECT_EQ(Bfloat.mbTensorCore, Expected.mbBfloatConversionAndFma);
			EXPECT_EQ(Bfloat.mbScalarAddMultiply, Expected.mbBfloatAddMultiply);
			EXPECT_FALSE(Bfloat.mbPackedInt8DotProduct);
		}
	}

	TEST(ArdaCudaTensorType, Float8StorageDoesNotImplyScalarArithmetic)
	{
		for (const auto DataType : {EArdaRHIDataType::Float8E4M3FN, EArdaRHIDataType::Float8E5M2})
		{
			for (const auto Architecture : {60u, 80u, 86u, 89u, 90u, 120u})
			{
				SCOPED_TRACE(Architecture);
				const auto Support = GetArdaCudaTensorTypeSupport(MakeTensorTypeCapabilities(Architecture), DataType);
				EXPECT_TRUE(Support.mbStorage);
				EXPECT_TRUE(Support.mbInstructionProfileKnown);
				EXPECT_EQ(Support.mbFloat32Conversion, Architecture >= 89);
				EXPECT_EQ(Support.mbTensorCore, Architecture >= 89);
				EXPECT_FALSE(Support.mbScalarAddMultiply);
				EXPECT_FALSE(Support.mbScalarFma);
				EXPECT_FALSE(Support.mbPackedInt8DotProduct);
			}
		}
	}

	TEST(ArdaCudaTensorType, IntegerDotProductPrecedesIntegerMatrixInstructions)
	{
		for (const auto DataType : {EArdaRHIDataType::Int8, EArdaRHIDataType::UInt8})
		{
			for (const auto Architecture : {50u, 60u, 61u, 70u, 72u, 75u, 80u, 89u, 120u})
			{
				SCOPED_TRACE(Architecture);
				const auto Support = GetArdaCudaTensorTypeSupport(MakeTensorTypeCapabilities(Architecture), DataType);
				EXPECT_TRUE(Support.mbStorage);
				EXPECT_TRUE(Support.mbInstructionProfileKnown);
				EXPECT_TRUE(Support.mbFloat32Conversion);
				EXPECT_EQ(Support.mbPackedInt8DotProduct, Architecture >= 61);
				EXPECT_EQ(Support.mbTensorCore, Architecture >= 72);
				EXPECT_FALSE(Support.mbScalarAddMultiply);
				EXPECT_FALSE(Support.mbScalarFma);
			}
		}
	}

	TEST(ArdaCudaTensorType, UnknownProfilesAndDisabledDevicesDoNotClaimInstructionSupport)
	{
		const auto CheckNoInstructions = [](const FArdaCudaTensorTypeSupport& Support)
		{
			EXPECT_FALSE(Support.mbInstructionProfileKnown);
			EXPECT_FALSE(Support.mbFloat32Conversion);
			EXPECT_FALSE(Support.mbScalarAddMultiply);
			EXPECT_FALSE(Support.mbScalarFma);
			EXPECT_FALSE(Support.mbPackedInt8DotProduct);
			EXPECT_FALSE(Support.mbTensorCore);
		};
		auto Capabilities = MakeTensorTypeCapabilities(120);
		for (const auto DataType : {EArdaRHIDataType::Float64, EArdaRHIDataType::Complex64})
		{
			const auto Support = GetArdaCudaTensorTypeSupport(Capabilities, DataType);
			EXPECT_TRUE(Support.mbStorage);
			CheckNoInstructions(Support);
		}
		const auto Unknown = GetArdaCudaTensorTypeSupport(Capabilities, static_cast<EArdaRHIDataType>(65535));
		EXPECT_FALSE(Unknown.mbStorage);
		CheckNoInstructions(Unknown);
		Capabilities.mComputeCapability = 0;
		const auto UnknownArchitecture = GetArdaCudaTensorTypeSupport(Capabilities, EArdaRHIDataType::Float16);
		EXPECT_FALSE(UnknownArchitecture.mbStorage);
		CheckNoInstructions(UnknownArchitecture);
		Capabilities = MakeTensorTypeCapabilities(120);
		Capabilities.mLaunchMode = EArdaCudaLaunchMode::None;
		const auto Disabled = GetArdaCudaTensorTypeSupport(Capabilities, EArdaRHIDataType::Float16);
		EXPECT_FALSE(Disabled.mbStorage);
		CheckNoInstructions(Disabled);
		const auto Float = GetArdaCudaTensorTypeSupport(MakeTensorTypeCapabilities(120), EArdaRHIDataType::Float32);
		EXPECT_TRUE(Float.mbStorage);
		EXPECT_TRUE(Float.mbInstructionProfileKnown);
		EXPECT_TRUE(Float.mbScalarFma);
		EXPECT_FALSE(Float.mbTensorCore); // TF32 rounds the mantissa and is a separate instruction operand type.
	}

	TEST(ArdaCudaTensorType, NativeInstructionArtifactsRejectEarlierTargets)
	{
		constexpr char Source[] = R"ptx(.version 7.0
.target sm_80
.address_size 64
.visible .entry ArdaPrecision(.param .u64 output)
{
 .reg .b64 ptr;
 .reg .b16 value;
 ld.param.u64 ptr, [output];
 cvt.rn.bf16.f32 value, 0f3f800000;
 st.global.b16 [ptr], value;
 ret;
}
)ptx";
		auto Entry = MakeTensorTypeKernel(Source, 80, "precision-test-native-bfloat16");
		ASSERT_TRUE(Entry);
		FArdaCudaKernel Kernel;
		Kernel.mEntry = Entry.mValue;
		Kernel.mParameters.resize(8);
		Kernel.mPatches.push_back({0, 0, 8});
		EXPECT_FALSE(ValidateArdaCudaKernels({Kernel}, 1, MakeTensorTypeCapabilities(75)));
		EXPECT_TRUE(ValidateArdaCudaKernels({Kernel}, 1, MakeTensorTypeCapabilities(80)));
	}

	TEST(ArdaCudaTensorType, StorageOnlyFloat8ArtifactsDoNotRequireNativeFloat8Instructions)
	{
		constexpr char Source[] = R"ptx(.version 6.0
.target sm_60
.address_size 64
.visible .entry ArdaPrecision(.param .u64 output)
{
 .reg .b64 ptr;
 .reg .b16 value;
 ld.param.u64 ptr, [output];
 mov.b16 value, 0x38;
 st.global.u8 [ptr], value;
 ret;
}
)ptx";
		const auto Capabilities = MakeTensorTypeCapabilities(60);
		const auto Support = GetArdaCudaTensorTypeSupport(Capabilities, EArdaRHIDataType::Float8E4M3FN);
		ASSERT_TRUE(Support.mbStorage);
		ASSERT_FALSE(Support.mbFloat32Conversion);
		ASSERT_FALSE(Support.mbTensorCore);
		auto Entry = MakeTensorTypeKernel(Source, 60, "precision-test-float8-byte-store");
		ASSERT_TRUE(Entry);
		FArdaCudaKernel Kernel;
		Kernel.mEntry = Entry.mValue;
		Kernel.mParameters.resize(8);
		Kernel.mPatches.push_back({0, 0, 8});
		EXPECT_TRUE(ValidateArdaCudaKernels({Kernel}, 1, Capabilities));
	}
}
