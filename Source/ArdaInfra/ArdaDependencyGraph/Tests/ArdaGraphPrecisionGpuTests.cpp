#include "ArdaDependencyGraph.h"
#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "FileOperations/ArdaFileOperations.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>

namespace arda
{
	namespace
	{
		float DecodePrecisionValue(const uint8_t* Bytes, EArdaRHIDataType DataType)
		{
			if (DataType == EArdaRHIDataType::Int8)
			{
				return float(Bytes[0] < 128 ? int(Bytes[0]) : int(Bytes[0]) - 256);
			}
			if (DataType == EArdaRHIDataType::UInt8)
			{
				return float(Bytes[0]);
			}
			if (DataType == EArdaRHIDataType::Int32 || DataType == EArdaRHIDataType::Float32)
			{
				const uint32_t Bits =
				    uint32_t(Bytes[0]) | uint32_t(Bytes[1]) << 8 | uint32_t(Bytes[2]) << 16 | uint32_t(Bytes[3]) << 24;
				if (DataType == EArdaRHIDataType::Int32)
				{
					int32_t Value = 0;
					std::memcpy(&Value, &Bits, sizeof(Value));
					return float(Value);
				}
				float Value = 0;
				std::memcpy(&Value, &Bits, sizeof(Value));
				return Value;
			}
			if (DataType == EArdaRHIDataType::BFloat16)
			{
				const uint32_t Bits = uint32_t(Bytes[0]) << 16 | uint32_t(Bytes[1]) << 24;
				float Value = 0;
				std::memcpy(&Value, &Bits, sizeof(Value));
				return Value;
			}
			uint32_t Bits = Bytes[0];
			uint32_t FractionBits = 0;
			uint32_t ExponentBits = 0;
			int Bias = 0;
			switch (DataType)
			{
			case EArdaRHIDataType::Float16:
				Bits |= uint32_t(Bytes[1]) << 8;
				FractionBits = 10;
				ExponentBits = 5;
				Bias = 15;
				break;
			case EArdaRHIDataType::Float8E4M3FN:
				FractionBits = 3;
				ExponentBits = 4;
				Bias = 7;
				break;
			case EArdaRHIDataType::Float8E5M2:
				FractionBits = 2;
				ExponentBits = 5;
				Bias = 15;
				break;
			default:
				return std::numeric_limits<float>::quiet_NaN();
			}
			const uint32_t FractionMask = (1u << FractionBits) - 1;
			const uint32_t ExponentMask = (1u << ExponentBits) - 1;
			const uint32_t Fraction = Bits & FractionMask;
			const uint32_t Exponent = Bits >> FractionBits & ExponentMask;
			const float Sign = Bits >> (FractionBits + ExponentBits) ? -1.0f : 1.0f;
			if (Exponent == ExponentMask)
			{
				if (DataType == EArdaRHIDataType::Float8E4M3FN)
				{
					if (Fraction == FractionMask)
					{
						return std::numeric_limits<float>::quiet_NaN();
					}
				}
				else
				{
					return Fraction ? std::numeric_limits<float>::quiet_NaN()
					                : std::copysign(std::numeric_limits<float>::infinity(), Sign);
				}
			}
			const float Magnitude = Exponent
			    ? std::ldexp(float((1u << FractionBits) + Fraction), int(Exponent) - Bias - int(FractionBits))
			    : std::ldexp(float(Fraction), 1 - Bias - int(FractionBits));
			return std::copysign(Magnitude, Sign);
		}

		uint64_t GetPrecisionElementByteOffset(const FArdaRHITensorDesc& Desc, uint64_t LinearIndex)
		{
			uint64_t Offset = Desc.mByteOffset;
			const uint64_t ElementSize = GetArdaRHIDataTypeInfo(Desc.mDataType).mByteSize;
			for (size_t Axis = Desc.mDimensions.size(); Axis-- > 0;)
			{
				const uint64_t Coordinate = LinearIndex % Desc.mDimensions[Axis];
				LinearIndex /= Desc.mDimensions[Axis];
				const int64_t Stride = Desc.mStrides[Axis];
				if (Stride < 0)
				{
					Offset -= Coordinate * (uint64_t(-(Stride + 1)) + 1) * ElementSize;
				}
				else
				{
					Offset += Coordinate * uint64_t(Stride) * ElementSize;
				}
			}
			return Offset;
		}

		bool ReadPrecisionTolerance(const char* Name, double Default, double& OutValue)
		{
			OutValue = Default;
			if (const auto* Text = std::getenv(Name))
			{
				char* End = nullptr;
				OutValue = std::strtod(Text, &End);
				return End != Text && !*End && std::isfinite(OutValue) && OutValue >= 0;
			}
			return true;
		}
	}

	TEST(ArdaGraphPrecisionDecode, DecodesStoredValuesIncludingFormatBoundaries)
	{
		const auto DecodeBits = [](uint32_t Bits, EArdaRHIDataType DataType)
		{
			const uint8_t Bytes[] = {uint8_t(Bits), uint8_t(Bits >> 8), uint8_t(Bits >> 16), uint8_t(Bits >> 24)};
			return DecodePrecisionValue(Bytes, DataType);
		};
		EXPECT_EQ(DecodeBits(0x3c00, EArdaRHIDataType::Float16), 1.0f);
		EXPECT_EQ(DecodeBits(0xc100, EArdaRHIDataType::Float16), -2.5f);
		EXPECT_EQ(DecodeBits(0x0001, EArdaRHIDataType::Float16), std::ldexp(1.0f, -24));
		EXPECT_EQ(DecodeBits(0x7bff, EArdaRHIDataType::Float16), 65504.0f);
		EXPECT_TRUE(std::isinf(DecodeBits(0x7c00, EArdaRHIDataType::Float16)));
		EXPECT_TRUE(std::isnan(DecodeBits(0x7e00, EArdaRHIDataType::Float16)));
		EXPECT_TRUE(std::signbit(DecodeBits(0x8000, EArdaRHIDataType::Float16)));
		EXPECT_EQ(DecodeBits(0x3f80, EArdaRHIDataType::BFloat16), 1.0f);
		EXPECT_EQ(DecodeBits(0xc020, EArdaRHIDataType::BFloat16), -2.5f);
		EXPECT_EQ(DecodeBits(0x0001, EArdaRHIDataType::BFloat16), std::ldexp(1.0f, -133));
		EXPECT_TRUE(std::isinf(DecodeBits(0x7f80, EArdaRHIDataType::BFloat16)));
		EXPECT_EQ(DecodeBits(0x38, EArdaRHIDataType::Float8E4M3FN), 1.0f);
		EXPECT_EQ(DecodeBits(0x01, EArdaRHIDataType::Float8E4M3FN), std::ldexp(1.0f, -9));
		EXPECT_EQ(DecodeBits(0x7e, EArdaRHIDataType::Float8E4M3FN), 448.0f);
		EXPECT_EQ(DecodeBits(0xfe, EArdaRHIDataType::Float8E4M3FN), -448.0f);
		EXPECT_TRUE(std::isnan(DecodeBits(0x7f, EArdaRHIDataType::Float8E4M3FN)));
		EXPECT_TRUE(std::signbit(DecodeBits(0x80, EArdaRHIDataType::Float8E4M3FN)));
		EXPECT_EQ(DecodeBits(0x3c, EArdaRHIDataType::Float8E5M2), 1.0f);
		EXPECT_EQ(DecodeBits(0x01, EArdaRHIDataType::Float8E5M2), std::ldexp(1.0f, -16));
		EXPECT_EQ(DecodeBits(0x7b, EArdaRHIDataType::Float8E5M2), 57344.0f);
		EXPECT_TRUE(std::isinf(DecodeBits(0x7c, EArdaRHIDataType::Float8E5M2)));
		EXPECT_TRUE(std::isnan(DecodeBits(0x7d, EArdaRHIDataType::Float8E5M2)));
		EXPECT_EQ(DecodeBits(0x80, EArdaRHIDataType::Int8), -128.0f);
		EXPECT_EQ(DecodeBits(0xff, EArdaRHIDataType::UInt8), 255.0f);
		EXPECT_EQ(DecodeBits(0xfffffffe, EArdaRHIDataType::Int32), -2.0f);
		EXPECT_EQ(DecodeBits(0xc0200000, EArdaRHIDataType::Float32), -2.5f);
	}

	TEST(ArdaGraphPrecisionDecode, LogicalOrderIncludesOffsetReversedAndBroadcastViews)
	{
		FArdaRHITensorDesc Desc{EArdaRHIDataType::Float16, {2, 3}, {3, -1}, 4};
		ASSERT_TRUE(ValidateArdaRHITensorDesc(Desc, 12));
		constexpr uint64_t Offsets[] = {4, 2, 0, 10, 8, 6};
		for (uint64_t Index = 0; Index < 6; ++Index)
		{
			EXPECT_EQ(GetPrecisionElementByteOffset(Desc, Index), Offsets[Index]);
		}
		Desc.mStrides = {0, 1};
		Desc.mByteOffset = 2;
		ASSERT_TRUE(ValidateArdaRHITensorDesc(Desc, 8));
		EXPECT_EQ(GetPrecisionElementByteOffset(Desc, 0), 2u);
		EXPECT_EQ(GetPrecisionElementByteOffset(Desc, 3), 2u);
		EXPECT_EQ(GetPrecisionElementByteOffset(Desc, 5), 6u);
	}

	class FArdaGraphPrecisionGpu : public testing::TestWithParam<const char*>
	{
	protected:
		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;

		void SetUp() override
		{
			ShutdownBackend();
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mCudaExecutionMode = EArdaCudaExecutionMode::ContextSwitch;
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
			if (!mDevice->GetCudaCapabilities())
			{
				GTEST_SKIP() << mDevice->GetCudaCapabilities().mUnavailableReason.c_str();
			}
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mDevice.Reset();
			ShutdownBackend();
			EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
			EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
		}
	};

	TEST_P(FArdaGraphPrecisionGpu, ExportedPrecisionModelMatchesOfflineReference)
	{
		const auto* PackagePath = std::getenv("ARDA_TEST_PRECISION_PACKAGE");
		const auto* InputPath = std::getenv("ARDA_TEST_PRECISION_INPUT");
		const auto* ReferencePath = std::getenv("ARDA_TEST_PRECISION_REFERENCE");
		if (!PackagePath || !InputPath || !ReferencePath)
		{
			GTEST_SKIP()
			    << "Set ARDA_TEST_PRECISION_PACKAGE, ARDA_TEST_PRECISION_INPUT and ARDA_TEST_PRECISION_REFERENCE for typed model parity.";
		}
		double RelativeTolerance = 0;
		double AbsoluteTolerance = 0;
		ASSERT_TRUE(ReadPrecisionTolerance("ARDA_TEST_PRECISION_RTOL", 1e-4, RelativeTolerance));
		ASSERT_TRUE(ReadPrecisionTolerance("ARDA_TEST_PRECISION_ATOL", 1e-5, AbsoluteTolerance));
		auto Package = FArdaGraphPackage::Load(PackagePath);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		if (!Package.mValue->HasStateDict())
		{
			const auto* StatePath = std::getenv("ARDA_TEST_PRECISION_STATE_DICT");
			auto Checkpoint = std::filesystem::path(PackagePath).replace_extension(".ardastate");
			Package = Package.mValue->LoadStateDict(StatePath ? std::filesystem::path(StatePath) : Checkpoint);
			ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		}
		eastl::vector<uint8_t> Input;
		eastl::vector<uint8_t> Reference;
		ASSERT_EQ(fileops::ReadBinaryFile(InputPath, Input), fileops::EArdaFileReadResult::Success);
		ASSERT_EQ(fileops::ReadBinaryFile(ReferencePath, Reference), fileops::EArdaFileReadResult::Success);
		const FArdaGraphPackageTensor* InputTensor = nullptr;
		const FArdaGraphPackageTensor* OutputTensor = nullptr;
		for (const auto& Tensor : Package.mValue->GetTensors())
		{
			if (Tensor.mRole & 1)
			{
				ASSERT_EQ(InputTensor, nullptr) << "This fixture supports one input.";
				InputTensor = &Tensor;
			}
			if (Tensor.mRole & 2)
			{
				ASSERT_EQ(OutputTensor, nullptr) << "This fixture supports one output.";
				OutputTensor = &Tensor;
			}
		}
		ASSERT_NE(InputTensor, nullptr);
		ASSERT_NE(OutputTensor, nullptr);
		ASSERT_TRUE(ValidateArdaRHITensorDesc(InputTensor->mDesc, Input.size()));
		ASSERT_LE(Input.size(), InputTensor->mStorageByteSize);
		Input.resize(size_t(InputTensor->mStorageByteSize), 0);
		const auto ElementCount = GetArdaRHITensorElementCount(OutputTensor->mDesc);
		ASSERT_TRUE(ElementCount);
		ASSERT_GT(ElementCount.mValue, 0u);
		ASSERT_EQ(Reference.size() % sizeof(float), 0u);
		ASSERT_EQ(ElementCount.mValue, Reference.size() / sizeof(float));
		const auto DataType = OutputTensor->mDesc.mDataType;
		ASSERT_TRUE(DataType == EArdaRHIDataType::Float16 || DataType == EArdaRHIDataType::BFloat16 ||
		    DataType == EArdaRHIDataType::Float8E4M3FN || DataType == EArdaRHIDataType::Float8E5M2 ||
		    DataType == EArdaRHIDataType::Int8 || DataType == EArdaRHIDataType::UInt8 ||
		    DataType == EArdaRHIDataType::Int32 || DataType == EArdaRHIDataType::Float32);
		FArdaDependencyGraph Graph(mDevice);
		eastl::shared_ptr<eastl::vector<uint8_t>> Output;
		FArdaRHITensorDesc OutputDesc;
		FArdaRHIBufferRange OutputRange;
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc BufferDesc;
			BufferDesc.mByteSize = Input.size();
			BufferDesc.mbCudaInterop = true;
			BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			const auto Buffer = Upload(Edit, AllocateBuffer(Edit, BufferDesc), Input);
			const auto Invocation = Package.mValue->Invoke(Edit, {{InputTensor->mName, {Buffer, InputTensor->mDesc}}});
			ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
			const auto* Result = Invocation.mValue.Find(OutputTensor->mName);
			ASSERT_NE(Result, nullptr);
			OutputDesc = Result->mDesc;
			const auto Range = GetArdaRHITensorByteRange(OutputDesc);
			ASSERT_TRUE(Range);
			OutputRange = Range.mValue;
			Output = Readback(Edit, Result->mBuffer, OutputRange.mByteOffset, OutputRange.mByteSize);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		double MaximumError = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			SCOPED_TRACE(Frame);
			const auto Executed = Graph.ExecuteAndWait();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			ASSERT_EQ(Output->size(), OutputRange.mByteSize);
			for (uint64_t Index = 0; Index < ElementCount.mValue; ++Index)
			{
				const auto Offset = GetPrecisionElementByteOffset(OutputDesc, Index) - OutputRange.mByteOffset;
				const float Actual = DecodePrecisionValue(Output->data() + Offset, DataType);
				const float Expected =
				    DecodePrecisionValue(Reference.data() + Index * sizeof(float), EArdaRHIDataType::Float32);
				ASSERT_TRUE(std::isfinite(Actual)) << "Element " << Index;
				ASSERT_TRUE(std::isfinite(Expected)) << "Reference element " << Index;
				const double Error = std::abs(double(Actual) - double(Expected));
				EXPECT_LE(Error, AbsoluteTolerance + RelativeTolerance * std::abs(double(Expected)))
				    << "Element " << Index;
				MaximumError = std::max(MaximumError, Error);
			}
		}
		RecordProperty("input_dtype", GetArdaRHIDataTypeInfo(InputTensor->mDesc.mDataType).mName);
		RecordProperty("output_dtype", GetArdaRHIDataTypeInfo(DataType).mName);
		RecordProperty("maximum_absolute_error", MaximumError);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphPrecisionGpu, testing::Values("native-d3d12", "native-vulkan"));
}
