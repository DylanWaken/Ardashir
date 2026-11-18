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

namespace arda
{
	class FArdaTemporalModelGpu : public testing::TestWithParam<const char*>
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

	TEST_P(FArdaTemporalModelGpu, MultiportPackagePreservesTemporalStateAcrossInvocations)
	{
		const auto* PackagePath = std::getenv("ARDA_TEST_TEMPORAL_MODEL_PACKAGE");
		const auto* RawPath = std::getenv("ARDA_TEST_TEMPORAL_MODEL_RAW_DIR");
		if (!PackagePath || !RawPath)
		{
			GTEST_SKIP()
			    << "Set ARDA_TEST_TEMPORAL_MODEL_PACKAGE and ARDA_TEST_TEMPORAL_MODEL_RAW_DIR to the NPPD export fixtures.";
		}
		auto Package = FArdaGraphPackage::Load(PackagePath);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		if (!Package.mValue->HasStateDict())
		{
			const auto* StatePath = std::getenv("ARDA_TEST_TEMPORAL_MODEL_STATE_DICT");
			auto Checkpoint = std::filesystem::path(PackagePath).replace_extension(".ardastate");
			Package = Package.mValue->LoadStateDict(StatePath ? std::filesystem::path(StatePath) : Checkpoint);
			ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		}
		const std::filesystem::path Directory(RawPath);
		FArdaDependencyGraph Graph(mDevice);

		struct FArdaTemporalReference
		{
			eastl::string mName;
			eastl::shared_ptr<eastl::vector<uint8_t>> mActual;
			eastl::vector<uint8_t> mExpected;
		};

		eastl::vector<FArdaTemporalReference> References;
		{
			FArdaGraphEdit Edit(Graph);
			eastl::vector<FArdaGraphPackageBinding> Inputs;
			for (const auto& Tensor : Package.mValue->GetTensors())
			{
				ASSERT_EQ(Tensor.mDesc.mDataType, EArdaRHIDataType::Float32);
				if (Tensor.mRole & 1)
				{
					eastl::vector<uint8_t> Bytes;
					ASSERT_EQ(fileops::ReadBinaryFile(Directory / (Tensor.mName + ".bin").c_str(), Bytes),
					    fileops::EArdaFileReadResult::Success)
					    << Tensor.mName.c_str();
					const auto Desc = MakeArdaContiguousTensorDesc(Tensor.mDesc.mDataType, Tensor.mDesc.mDimensions);
					ASSERT_TRUE(Desc);
					const auto Range = GetArdaRHITensorByteRange(Desc.mValue);
					ASSERT_TRUE(Range);
					ASSERT_EQ(Bytes.size(), Range.mValue.mByteSize);
					FArdaRHIBufferDesc BufferDesc;
					BufferDesc.mByteSize = Bytes.size();
					BufferDesc.mbCudaInterop = true;
					BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
					const auto Buffer = Upload(Edit, AllocateBuffer(Edit, BufferDesc), eastl::move(Bytes));
					Inputs.push_back({Tensor.mName, {Buffer, Desc.mValue}});
				}
			}
			ASSERT_EQ(Inputs.size(), 8u);
			for (uint32_t Frame = 0; Frame < 2; ++Frame)
			{
				const auto Invocation = Package.mValue->Invoke(Edit, Inputs);
				ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
				for (const auto& Tensor : Package.mValue->GetTensors())
				{
					if (Tensor.mRole & 2)
					{
						const auto* Output = Invocation.mValue.Find(Tensor.mName);
						ASSERT_NE(Output, nullptr);
						const auto Range = GetArdaRHITensorByteRange(Output->mDesc);
						ASSERT_TRUE(Range);
						FArdaTemporalReference Reference;
						Reference.mName = Tensor.mName + (Frame ? "_frame2" : "");
						ASSERT_EQ(fileops::ReadBinaryFile(Directory / (Reference.mName + ".bin").c_str(),
						              Reference.mExpected),
						    fileops::EArdaFileReadResult::Success);
						Reference.mActual =
						    Readback(Edit, Output->mBuffer, Range.mValue.mByteOffset, Range.mValue.mByteSize);
						References.push_back(eastl::move(Reference));
					}
				}
				const auto* NextState = Invocation.mValue.Find("next_temporal");
				ASSERT_NE(NextState, nullptr);
				bool bStateRebound = false;
				for (auto& Input : Inputs)
				{
					if (Input.mName == "temporal")
					{
						// A typed view can retain the nonzero offset of a shared output arena.
						Input.mTensor = *NextState;
						bStateRebound = true;
					}
				}
				ASSERT_TRUE(bStateRebound);
			}
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(References.size(), 4u);
		for (const auto& Reference : References)
		{
			ASSERT_EQ(Reference.mActual->size(), Reference.mExpected.size());
			double MaximumError = 0;
			for (size_t Offset = 0; Offset < Reference.mExpected.size(); Offset += sizeof(float))
			{
				float Actual = 0;
				float Expected = 0;
				std::memcpy(&Actual, Reference.mActual->data() + Offset, sizeof(float));
				std::memcpy(&Expected, Reference.mExpected.data() + Offset, sizeof(float));
				ASSERT_TRUE(std::isfinite(Actual)) << Reference.mName.c_str() << ": " << Offset;
				MaximumError = std::max(MaximumError, double(std::abs(Actual - Expected)));
			}
			EXPECT_LT(MaximumError, 1e-4) << Reference.mName.c_str();
			RecordProperty(Reference.mName.c_str(), MaximumError);
		}
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaTemporalModelGpu, testing::Values("native-d3d12", "native-vulkan"));
}
