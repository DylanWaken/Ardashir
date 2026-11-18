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
	namespace
	{
		FArdaRuntimeNodeDesc MakeRuntimeTestDefinition(const char* Name)
		{
			constexpr char Ptx[] = R"ptx(.version 6.0
.target sm_50
.address_size 64
.visible .entry ArdaRuntimeAdd(.param .u64 input, .param .u64 output, .param .u32 count)
{
 .reg .pred p;
 .reg .b32 i, b, n;
 .reg .b64 src, dst, offset;
 .reg .f32 value;
 mov.u32 i, %ctaid.x;
 mov.u32 b, %ntid.x;
 mul.lo.u32 i, i, b;
 mov.u32 b, %tid.x;
 add.u32 i, i, b;
 ld.param.u32 n, [count];
 setp.ge.u32 p, i, n;
 @p bra done;
 ld.param.u64 src, [input];
 ld.param.u64 dst, [output];
 mul.wide.u32 offset, i, 4;
 add.u64 src, src, offset;
 add.u64 dst, dst, offset;
 ld.global.f32 value, [src];
 add.f32 value, value, 0f3f800000;
 st.global.f32 [dst], value;
done:
 ret;
}
)ptx";
			FArdaRuntimeNodeDesc Desc;
			Desc.mName = Name;
			Desc.mEntryPoint = "ArdaRuntimeAdd";
			Desc.mModule.mFormat = EArdaCudaModuleFormat::Ptx;
			Desc.mModule.mBuild.mIdentity = "runtime-test-add-v1";
			Desc.mModule.mBuild.mArchitectures.push_back({50, false});
			Desc.mModule.mImage.assign(Ptx, Ptx + sizeof(Ptx) - 1);
			Desc.mParameterSize = 24;
			Desc.mParameterAlignment = 8;
			Desc.mArguments = {{"Input", {0, 8, 8, EArdaCudaModuleArgumentKind::Buffer}, EArdaComputeAccess::Read},
			    {"Output", {8, 8, 8, EArdaCudaModuleArgumentKind::Buffer}, EArdaComputeAccess::Write},
			    {"Count", {16, 4, 4, EArdaCudaModuleArgumentKind::Value}, EArdaComputeAccess::Read}};
			return Desc;
		}

		FArdaRuntimeNodeParameters MakeRuntimeTestParameters(FArdaGraphBuffer Input, FArdaGraphBuffer Output)
		{
			FArdaRuntimeNodeParameters Parameters;
			Parameters.mBlock[0] = 32;
			Parameters.mArguments = {{Input, {0, 64}, {}}, {Output, {0, 64}, {}}, {{}, {}, {16, 0, 0, 0}}};
			return Parameters;
		}

		FArdaRHIBufferDesc RuntimeTestBufferDesc(uint64_t Bytes = 64)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = Bytes;
			Desc.mbCudaInterop = true;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			return Desc;
		}

		eastl::string MakeRuntimeTestPackage()
		{
			const auto Definition = MakeRuntimeTestDefinition("test.package.add");
			FArdaGraphArchiveWriter Archive;
			Archive.WriteBytes("ARDAGPU\0", 8);
			Archive.Fields(uint32_t{1}, eastl::string("test-package"), uint64_t{1});
			Archive.Fields(uint8_t{0}, uint32_t{50}, Definition.mModule.mBuild.mIdentity, Definition.mModule.mImage);
			Archive.Write(uint64_t{2});
			Archive.Fields(eastl::string("input"), uint64_t{64}, eastl::vector<uint8_t>{});
			Archive.Fields(eastl::string("output"), uint64_t{64}, eastl::vector<uint8_t>{});
			Archive.Write(uint64_t{2});
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				Archive.Fields(eastl::string(Index ? "result" : "source"),
				    Index,
				    uint8_t(Index ? 2 : 1),
				    uint16_t(EArdaRHIDataType::Float32),
				    eastl::vector<uint64_t>{16},
				    eastl::vector<int64_t>{1},
				    uint64_t{0});
			}
			Archive.Write(uint64_t{1});
			Archive.Fields(Definition.mName,
			    uint32_t{0},
			    Definition.mEntryPoint,
			    Definition.mParameterSize,
			    Definition.mParameterAlignment,
			    uint64_t{3});
			for (const auto& Argument : Definition.mArguments)
			{
				Archive.Fields(Argument.mName,
				    Argument.mAbi.mKind,
				    Argument.mAccess,
				    uint32_t(Argument.mAbi.mOffset),
				    uint32_t(Argument.mAbi.mSize),
				    uint32_t(Argument.mAbi.mAlignment));
			}
			Archive.Fields(uint64_t{1},
			    uint8_t{0},
			    uint32_t{0},
			    eastl::array<uint32_t, 3>{1, 1, 1},
			    eastl::array<uint32_t, 3>{32, 1, 1},
			    uint32_t{0},
			    uint64_t{3});
			Archive.Fields(uint32_t{0}, uint64_t{0}, uint64_t{64}, eastl::vector<uint8_t>{});
			Archive.Fields(uint32_t{1}, uint64_t{0}, uint64_t{64}, eastl::vector<uint8_t>{});
			Archive.Fields(UINT32_MAX, uint64_t{0}, uint64_t{0}, eastl::vector<uint8_t>{16, 0, 0, 0});
			return Archive.GetData();
		}
	}

	TEST(ArdaRuntimeNode, RegistrationIsImmutableAndLibraryPublicationIsAtomic)
	{
		auto Definition = MakeRuntimeTestDefinition("test.runtime.registration");
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		const auto Original = FArdaNodeRegistry::Get().Find(Definition.mName);
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		EXPECT_EQ(Original, FArdaNodeRegistry::Get().Find(Definition.mName));
		Definition.mArguments[0].mAccess = EArdaComputeAccess::ReadWrite;
		const auto NewDefinition = MakeRuntimeTestDefinition("test.runtime.atomic-new");
		EXPECT_FALSE(
		    FArdaRuntimeNodeDefinition::Register(eastl::vector<FArdaRuntimeNodeDesc>{NewDefinition, Definition}));
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find(NewDefinition.mName));
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, ArchiveRestoresCodeWithoutPriorRegistration)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.archive");
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Input =
			    Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc()), eastl::vector<uint8_t>(64, 0));
			const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
			ASSERT_TRUE(
			    FArdaRuntimeNodeDefinition::Invoke(Edit, Definition.mName, MakeRuntimeTestParameters(Input, Output)));
			Edit.Keep(Output);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
		EXPECT_FALSE(FArdaDependencyGraph::Deserialize(Encoded.mValue + "x"));
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find(Definition.mName));
		auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		EXPECT_TRUE(FArdaNodeRegistry::Get().Find(Definition.mName));
		const auto Reencoded = Decoded.mValue->Serialize();
		ASSERT_TRUE(Reencoded);
		EXPECT_EQ(Encoded.mValue, Reencoded.mValue);
		EXPECT_EQ(Decoded.mValue->GetTopology().GetNodeCount(), Graph.GetTopology().GetNodeCount());
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, InvalidInvocationPoisonsEditWithoutPublishingNodes)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.invalid");
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Input = AllocateBuffer(Edit, RuntimeTestBufferDesc());
		const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
		auto Parameters = MakeRuntimeTestParameters(Input, Output);
		Parameters.mArguments[1].mRange.mByteSize = 65;
		EXPECT_FALSE(FArdaRuntimeNodeDefinition::Invoke(Edit, Definition.mName, Parameters));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaGraphPackage, ExpandsWithTypedTensorBindingsAndRejectsCorruptTransport)
	{
		const auto Bytes = MakeRuntimeTestPackage();
		const auto Package = FArdaGraphPackage::Deserialize(Bytes);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		for (const size_t Size : {size_t{0}, size_t{8}, Bytes.size() / 2, Bytes.size() - 1})
		{
			EXPECT_FALSE(FArdaGraphPackage::Deserialize(Bytes.substr(0, Size)));
		}
		EXPECT_FALSE(FArdaGraphPackage::Deserialize(Bytes + "x"));
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Input = Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc()), eastl::vector<uint8_t>(64, 0));
		const auto Desc = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {16});
		ASSERT_TRUE(Desc);
		const auto Invocation = Package.mValue->Invoke(Edit, {{"source", {Input, Desc.mValue}}});
		ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
		ASSERT_NE(Invocation.mValue.Find("result"), nullptr);
		Edit.Keep(Invocation.mValue.Find("result")->mBuffer);
		ASSERT_TRUE(Edit.Commit());
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 2u);
		EXPECT_TRUE(Graph.Serialize());
	}

	TEST(ArdaGraphPackage, LoadedPackageRetainsItsImplementationAfterRegistryReplacement)
	{
		const auto Package = FArdaGraphPackage::Deserialize(MakeRuntimeTestPackage());
		ASSERT_TRUE(Package);
		const auto Original = FArdaNodeRegistry::Get().Find("test.package.add");
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister("test.package.add"));
		auto Replacement = MakeRuntimeTestDefinition("test.package.add");
		Replacement.mModule.mBuild.mIdentity = "different-implementation";
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Replacement));
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Input =
			    Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc()), eastl::vector<uint8_t>(64, 0));
			const auto Desc = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {16});
			ASSERT_TRUE(Desc);
			const auto Invocation = Package.mValue->Invoke(Edit, {{"source", {Input, Desc.mValue}}});
			ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
			Edit.Keep(Invocation.mValue.Find("result")->mBuffer);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Archive = Graph.Serialize();
		ASSERT_TRUE(Archive);
		EXPECT_NE(Archive.mValue.find(Original->mPortableDefinition), eastl::string::npos);
		EXPECT_EQ(Archive.mValue.find("different-implementation"), eastl::string::npos);
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister("test.package.add"));
	}

	class FArdaRuntimeGraphGpu : public testing::TestWithParam<const char*>
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

	TEST_P(FArdaRuntimeGraphGpu, ExecutesLoadedDefinitionBetweenGraphicsUploadAndReadback)
	{
		const auto Package = FArdaGraphPackage::Deserialize(MakeRuntimeTestPackage());
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph(mDevice);
		eastl::shared_ptr<eastl::vector<uint8_t>> Bytes;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Input =
			    Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc()), eastl::vector<uint8_t>(64, 0));
			const auto Desc = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {16});
			ASSERT_TRUE(Desc);
			const auto Invocation = Package.mValue->Invoke(Edit, {{"source", {Input, Desc.mValue}}});
			ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
			Bytes = Readback(Edit, Invocation.mValue.Find("result")->mBuffer);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Repeat = 0; Repeat < 2; ++Repeat)
		{
			const auto Executed = Graph.ExecuteAndWait();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			ASSERT_EQ(Bytes->size(), 64u);
			for (size_t Index = 0; Index < 16; ++Index)
			{
				float Value = 0;
				std::memcpy(&Value, Bytes->data() + Index * 4, 4);
				EXPECT_FLOAT_EQ(Value, 1.0f);
			}
		}
	}

	TEST_P(FArdaRuntimeGraphGpu, ArchiveRestoresKernelAndInitialDataWithoutThePackageOrRegistry)
	{
		eastl::string Bytes;
		{
			const auto Package = FArdaGraphPackage::Deserialize(MakeRuntimeTestPackage());
			ASSERT_TRUE(Package);
			FArdaDependencyGraph Source;
			FArdaGraphEdit Edit(Source);
			const auto Input = AllocateInitializedBuffer(Edit, RuntimeTestBufferDesc(), eastl::vector<uint8_t>(64, 0));
			const auto Desc = MakeArdaContiguousTensorDesc(EArdaRHIDataType::Float32, {16});
			ASSERT_TRUE(Desc);
			const auto Invocation = Package.mValue->Invoke(Edit, {{"source", {Input, Desc.mValue}}}, "model");
			ASSERT_TRUE(Invocation);
			Edit.Keep(Invocation.mValue.Find("result")->mBuffer);
			ASSERT_TRUE(Edit.Commit());
			const auto Archive = Source.Serialize();
			ASSERT_TRUE(Archive);
			Bytes = Archive.mValue;
		}
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister("test.package.add"));
		const auto Restored = FArdaDependencyGraph::Deserialize(Bytes, mDevice);
		ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
		auto& Graph = *Restored.mValue;
		eastl::shared_ptr<eastl::vector<uint8_t>> Output;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = Edit.UseBuffer(Graph.FindOutput(Graph.FindNode("model/command0"), "Output"));
			ASSERT_TRUE(Buffer);
			Output = Readback(Edit, Buffer);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			const auto Executed = Graph.ExecuteAndWait();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			ASSERT_EQ(Output->size(), 64u);
			for (size_t Index = 0; Index < 16; ++Index)
			{
				float Actual = 0;
				std::memcpy(&Actual, Output->data() + Index * sizeof(float), sizeof(float));
				EXPECT_FLOAT_EQ(Actual, 1.0f);
			}
		}
	}

	TEST_P(FArdaRuntimeGraphGpu, ExportedModelMatchesOfflineReference)
	{
		const auto* PackagePath = std::getenv("ARDA_TEST_MODEL_PACKAGE");
		const auto* InputPath = std::getenv("ARDA_TEST_MODEL_INPUT");
		const auto* ReferencePath = std::getenv("ARDA_TEST_MODEL_REFERENCE");
		if (!PackagePath || !InputPath || !ReferencePath)
		{
			GTEST_SKIP()
			    << "Set ARDA_TEST_MODEL_PACKAGE, ARDA_TEST_MODEL_INPUT and ARDA_TEST_MODEL_REFERENCE for exported model parity.";
		}
		auto Package = FArdaGraphPackage::Load(PackagePath);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		if (!Package.mValue->HasStateDict())
		{
			const auto* StatePath = std::getenv("ARDA_TEST_MODEL_STATE_DICT");
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
			if (Tensor.mRole == 1)
			{
				ASSERT_EQ(InputTensor, nullptr) << "This fixture supports one input.";
				InputTensor = &Tensor;
			}
			if (Tensor.mRole == 2)
			{
				ASSERT_EQ(OutputTensor, nullptr) << "This fixture supports one output.";
				OutputTensor = &Tensor;
			}
		}
		ASSERT_NE(InputTensor, nullptr);
		ASSERT_NE(OutputTensor, nullptr);
		ASSERT_EQ(InputTensor->mDesc.mDataType, EArdaRHIDataType::Float32);
		ASSERT_EQ(OutputTensor->mDesc.mDataType, EArdaRHIDataType::Float32);
		FArdaDependencyGraph Graph(mDevice);
		eastl::shared_ptr<eastl::vector<uint8_t>> Output;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc(Input.size())), Input);
			const auto Invocation = Package.mValue->Invoke(Edit, {{InputTensor->mName, {Buffer, InputTensor->mDesc}}});
			ASSERT_TRUE(Invocation) << Invocation.mStatus.mMessage.c_str();
			const auto* Result = Invocation.mValue.Find(OutputTensor->mName);
			ASSERT_NE(Result, nullptr);
			const auto Range = GetArdaRHITensorByteRange(Result->mDesc);
			ASSERT_TRUE(Range);
			Output = Readback(Edit, Result->mBuffer, Range.mValue.mByteOffset, Range.mValue.mByteSize);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_EQ(Output->size(), Reference.size());
		double MaxError = 0;
		for (size_t Offset = 0; Offset < Reference.size(); Offset += sizeof(float))
		{
			float Actual = 0;
			float Expected = 0;
			std::memcpy(&Actual, Output->data() + Offset, sizeof(float));
			std::memcpy(&Expected, Reference.data() + Offset, sizeof(float));
			ASSERT_TRUE(std::isfinite(Actual));
			MaxError = std::max(MaxError, double(std::abs(Actual - Expected)));
		}
		EXPECT_LT(MaxError, 1e-3);
		RecordProperty("maximum_absolute_error", MaxError);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaRuntimeGraphGpu, testing::Values("native-d3d12", "native-vulkan"));
}
