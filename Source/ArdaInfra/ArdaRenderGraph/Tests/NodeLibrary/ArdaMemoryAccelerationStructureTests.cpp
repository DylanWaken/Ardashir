#include "NodeLibrary/ArdaMemoryAccelerationStructureNodes.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"

#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
	using namespace arda;

	class FArdaMemoryAccelerationStructuresGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
			const auto& Ray = mDevice->GetCapabilities().mRayTracing;
			if (!Ray.mbAccelerationStructures || !Ray.mbBottomLevel || !Ray.mbTopLevel || !Ray.mbCompaction)
			{
				GTEST_SKIP() << "Native BLAS/TLAS compaction unavailable";
			}
			FArdaRHIBufferDesc Vertices;
			Vertices.mByteSize = sizeof(mVertices);
			Vertices.mUsage = EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::AccelStructBuildInput;
			Vertices.mCpuAccess = EArdaRHICpuAccess::Write;
			Vertices.mInitialState = EArdaRHIResourceState::AccelStructBuildInput;
			const auto Buffer = mDevice->CreateBuffer(Vertices);
			ASSERT_TRUE(Buffer);
			mVertexBuffer = Buffer.mValue;
			FArdaRHIRayTracingGeometryDesc Geometry;
			Geometry.mType = EArdaRHIRayTracingGeometryType::Triangles;
			Geometry.mFlags = EArdaRHIRayTracingGeometryFlags::Opaque;
			Geometry.mVertexOrAABBBuffer = mVertexBuffer;
			Geometry.mVertexFormat = EArdaRHIFormat::RGB32Float;
			Geometry.mVertexOrAABBCount = 3;
			Geometry.mStride = sizeof(float) * 3;
			mBlasDesc.mBottomLevelGeometries = {Geometry};
			mBlasDesc.mBuildFlags = EArdaRHIAccelStructBuildFlags::AllowCompaction |
			    EArdaRHIAccelStructBuildFlags::AllowUpdate | EArdaRHIAccelStructBuildFlags::PreferFastTrace;
			mTlasDesc.mbTopLevel = true;
			mTlasDesc.mTopLevelMaxInstances = 1;
			mTlasDesc.mBuildFlags = mBlasDesc.mBuildFlags;
		}

		void TearDown() override
		{
			const bool MissingValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			mBlasDesc.mBottomLevelGeometries.clear();
			mVertexBuffer.Reset();
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
			if (!MissingValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaRHIAccelStructRef Create(const FArdaRHIAccelStructDesc& Description)
		{
			const auto Result = mDevice->CreateAccelStruct(Description);
			EXPECT_TRUE(Result) << Result.mStatus.mMessage.c_str();
			return Result.mValue;
		}

		void Build(const FArdaRHIAccelStructRef& Blas, const FArdaRHIAccelStructRef& Tlas = {})
		{
			const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*mVertexBuffer, mVertices, sizeof(mVertices)));
			ASSERT_TRUE(Commands.mValue->BuildBottomLevelAccelStruct(*Blas,
			    mBlasDesc.mBottomLevelGeometries,
			    mBlasDesc.mBuildFlags));
			if (Tlas)
			{
				FArdaRHIRayTracingInstanceDesc Instance;
				Instance.mBottomLevelAccelStruct = Blas;
				ASSERT_TRUE(Commands.mValue->BuildTopLevelAccelStruct(*Tlas, {Instance}, mTlasDesc.mBuildFlags));
			}
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
		}

		void Trace(const FArdaRHIAccelStructRef& Tlas)
		{
			static const std::string Source =
			    (std::filesystem::path(ARDA_BACKEND_TEST_SHADER_SOURCE_DIR) / "ArdaComputeConformance.hlsl").string();
			static FArdaShaderTypeRegistration Registration("ArdaMemoryInlineRayQuery",
			    Source.c_str(),
			    "ArdaMemoryInlineRayQuery",
			    "InlineRayQueryCS",
			    EArdaRHIShaderStage::Compute,
			    nullptr);
			ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
			const auto Previous = GetShaderCompilerConfiguration();
			auto Configuration = Previous;
			Configuration.mbCompileMissingArtifacts = true;
			Configuration.mbCompileOutdatedArtifacts = true;
			Configuration.mCommonArguments = {"-T", "cs_6_6", "-HV", "2021", "-enable-16bit-types"};
			ConfigureShaderCompiler(Configuration);
			const auto Compiled =
			    EnsureRegisteredShaderArtifact(Registration.GetType(), GetParam(), 0, ARDA_BACKEND_TEST_SHADER_DIR);
			ConfigureShaderCompiler(Previous);
			ASSERT_TRUE(Compiled) << (Compiled.mDiagnostics.empty() ? "Shader compilation failed"
			                                                        : Compiled.mDiagnostics.front().mMessage.c_str());
			const auto File = std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
			    (eastl::string("ArdaMemoryInlineRayQuery") + GetShaderArtifactExtension(GetParam())).c_str();
			std::ifstream Input(File, std::ios::binary);
			ASSERT_TRUE(Input.is_open()) << File.string();
			const std::vector<uint8_t> Bytes((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
			ASSERT_FALSE(Bytes.empty());
			FArdaRHIShaderDesc ShaderDesc;
			ShaderDesc.mStage = EArdaRHIShaderStage::Compute;
			ShaderDesc.mEntryPoint = "InlineRayQueryCS";
			ShaderDesc.mBytecode = Bytes.data();
			ShaderDesc.mBytecodeSize = Bytes.size();
			const auto Shader = mDevice->CreateShader(ShaderDesc);
			ASSERT_TRUE(Shader);
			FArdaRHIBindingLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
			LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::RayTracingAccelStruct},
			    {0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
			const auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
			ASSERT_TRUE(Layout);
			FArdaRHIBufferDesc OutputDesc;
			OutputDesc.mByteSize = sizeof(uint32_t) * 2;
			OutputDesc.mStructureStride = sizeof(uint32_t);
			OutputDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			const auto Output = mDevice->CreateBuffer(OutputDesc);
			ASSERT_TRUE(Output);
			FArdaRHIBindingSetDesc SetDesc;
			SetDesc.mLayout = Layout.mValue;
			SetDesc.mItems = {{0, 0, EArdaRHIBindingType::RayTracingAccelStruct, FArdaRHIResourceRef(Tlas.Get()), {}},
			    {0, 0, EArdaRHIBindingType::StructuredBufferUAV, FArdaRHIResourceRef(Output.mValue.Get()), {}}};
			const auto Set = mDevice->CreateBindingSet(SetDesc);
			ASSERT_TRUE(Set);
			FArdaRHIComputePipelineDesc PipelineDesc;
			PipelineDesc.mComputeShader = Shader.mValue;
			PipelineDesc.mBindingLayouts = {Layout.mValue};
			const auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
			const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->SetAccelStructState(*Tlas, EArdaRHIResourceState::AccelStructRead));
			ASSERT_TRUE(Commands.mValue->SetBufferState(*Output.mValue, EArdaRHIResourceState::UnorderedAccess));
			FArdaRHIComputeState State;
			State.mPipeline = Pipeline.mValue;
			State.mBindings = {Set.mValue};
			ASSERT_TRUE(Commands.mValue->SetComputeState(State));
			Commands.mValue->Dispatch(2, 1, 1);
			eastl::vector<uint8_t> Readback;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Readback));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_EQ(Readback.size(), sizeof(uint32_t) * 2);
			uint32_t Actual[2]{};
			std::memcpy(Actual, Readback.data(), sizeof(Actual));
			EXPECT_EQ(Actual[0], 0xC105E57u);
			EXPECT_EQ(Actual[1], 0xB055u);
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
		FArdaRHIBufferRef mVertexBuffer;
		FArdaRHIAccelStructDesc mBlasDesc, mTlasDesc;
		const float mVertices[9] = {-1.f, -1.f, 0.f, 0.f, 1.f, 0.f, 1.f, -1.f, 0.f};
	};

	TEST_P(FArdaMemoryAccelerationStructuresGpu, CloneBothKindsPreservesTriangleContentsAndWholeObjectHazards)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbInlineRayQueries)
		{
			GTEST_SKIP() << "Inline ray queries unavailable";
		}
		const auto Blas = Create(mBlasDesc);
		const auto FirstClone = Create(mBlasDesc);
		const auto SecondClone = Create(mBlasDesc);
		ASSERT_TRUE(Blas && FirstClone && SecondClone);
		Build(Blas);
		ASSERT_FALSE(HasFatalFailure());
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Source = Graph.ImportAccelerationStructure("source BLAS", Blas);
		const auto First = Graph.ImportAccelerationStructure("first clone", FirstClone);
		const auto Second = Graph.ImportAccelerationStructure("second clone", SecondClone);
		ASSERT_TRUE(Source && First && Second);
		const auto CopyOne =
		    Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("clone BLAS", {Source.mValue, First.mValue});
		const auto CopyTwo =
		    Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("clone again", {First.mValue, Second.mValue});
		ASSERT_TRUE(CopyOne && CopyTwo);
		ASSERT_TRUE(Graph.MarkOutput(Second.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetTopology().IsReachable(CopyOne.mValue, CopyTwo.mValue));
		for (auto Queue : Graph.GetCompileResult().mQueues)
		{
			EXPECT_EQ(Queue, EArdaRHIQueueType::Graphics);
		}
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(SecondClone->GetBuildState(), EArdaRHIAccelStructBuildState::Built);
		const auto Tlas = Create(mTlasDesc);
		const auto TlasClone = Create(mTlasDesc);
		ASSERT_TRUE(Tlas && TlasClone);
		const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		FArdaRHIRayTracingInstanceDesc Instance;
		Instance.mBottomLevelAccelStruct = SecondClone;
		ASSERT_TRUE(Commands.mValue->BuildTopLevelAccelStruct(*Tlas, {Instance}, mTlasDesc.mBuildFlags));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		FArdaDependencyGraph TlasGraph(mDevice);
		ASSERT_TRUE(TlasGraph.BeginGraphEdit());
		const auto TlasSource = TlasGraph.ImportAccelerationStructure("TLAS", Tlas);
		const auto TlasDestination = TlasGraph.ImportAccelerationStructure("TLAS clone", TlasClone);
		ASSERT_TRUE(TlasSource && TlasDestination);
		ASSERT_TRUE(TlasGraph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("clone TLAS",
		    {TlasSource.mValue, TlasDestination.mValue}));
		ASSERT_TRUE(TlasGraph.MarkOutput(TlasDestination.mValue));
		ASSERT_TRUE(TlasGraph.EndGraphEdit());
		ASSERT_TRUE(TlasGraph.Execute().mStatus);
		EXPECT_EQ(TlasClone->GetBuildState(), EArdaRHIAccelStructBuildState::Built);
		Trace(TlasClone);
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, CompactsBothKindsAndClonesCompactedState)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbInlineRayQueries)
		{
			GTEST_SKIP() << "Inline ray queries unavailable";
		}
		const auto Blas = Create(mBlasDesc);
		const auto Tlas = Create(mTlasDesc);
		ASSERT_TRUE(Blas && Tlas);
		Build(Blas, Tlas);
		ASSERT_FALSE(HasFatalFailure());
		for (const auto& Source : {Blas, Tlas})
		{
			SCOPED_TRACE(Source->GetDesc().mbTopLevel ? "TLAS" : "BLAS");
			const auto Size = mDevice->GetAccelStructCompactedSize(Source);
			ASSERT_TRUE(Size);
			auto Desc = Source->GetDesc();
			Desc.mResultSizeOverride = Size.mValue;
			const auto Compact = Create(Desc);
			const auto Clone = Create(Desc);
			ASSERT_TRUE(Compact && Clone);
			FArdaDependencyGraph Graph(mDevice);
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Input = Graph.ImportAccelerationStructure("source", Source);
			const auto Packed = Graph.ImportAccelerationStructure("compact", Compact);
			const auto Copied = Graph.ImportAccelerationStructure("cloned compact", Clone);
			ASSERT_TRUE(Input && Packed && Copied);
			ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCompactAccelerationStructureNode>("compact",
			    {Input.mValue, Packed.mValue}));
			ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("clone compact",
			    {Packed.mValue, Copied.mValue}));
			ASSERT_TRUE(Graph.MarkOutput(Copied.mValue));
			ASSERT_TRUE(Graph.EndGraphEdit());
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_TRUE(Compact->IsCompacted());
			EXPECT_TRUE(Clone->IsCompacted());
			const auto CloneSize = mDevice->GetAccelStructCompactedSize(Clone);
			ASSERT_TRUE(CloneSize) << CloneSize.mStatus.mMessage.c_str();
			EXPECT_LE(CloneSize.mValue, Size.mValue);
			if (Desc.mbTopLevel)
			{
				Trace(Clone);
			}
			else
			{
				const auto Scene = Create(mTlasDesc);
				ASSERT_TRUE(Scene);
				const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				FArdaRHIRayTracingInstanceDesc Instance;
				Instance.mBottomLevelAccelStruct = Clone;
				ASSERT_TRUE(Commands.mValue->BuildTopLevelAccelStruct(*Scene, {Instance}, mTlasDesc.mBuildFlags));
				ASSERT_TRUE(Commands.mValue->Close());
				ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
				Trace(Scene);
			}
		}
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, RejectsInvalidResourceKindsFlagsAndCompactionDescriptors)
	{
		const auto Blas = Create(mBlasDesc);
		const auto OtherBlas = Create(mBlasDesc);
		const auto Tlas = Create(mTlasDesc);
		auto OtherFlags = mBlasDesc;
		OtherFlags.mBuildFlags = EArdaRHIAccelStructBuildFlags::PreferFastTrace;
		const auto Incompatible = Create(OtherFlags);
		ASSERT_TRUE(Blas && OtherBlas && Tlas && Incompatible);
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Source = Graph.ImportAccelerationStructure("BLAS", Blas).mValue;
		const auto Destination = Graph.ImportAccelerationStructure("other BLAS", OtherBlas).mValue;
		const auto WrongKind = Graph.ImportAccelerationStructure("TLAS", Tlas).mValue;
		const auto WrongFlags = Graph.ImportAccelerationStructure("flags", Incompatible).mValue;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 64;
		const auto Buffer = Graph.CreateBuffer("buffer", BufferDesc).mValue;
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("empty", {}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("buffer", {Buffer, Destination}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("self", {Source, Source}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("kind", {Source, WrongKind}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("flags", {Source, WrongFlags}));
		EXPECT_FALSE(
		    Graph.AttachOrFind<FArdaMemoryCompactAccelerationStructureNode>("no compact size", {Source, Destination}));
		const auto Copy = Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("valid", {Source, Destination});
		ASSERT_TRUE(Copy);
		const auto Again = Graph.AttachOrFind<FArdaMemoryCopyAccelerationStructureNode>("valid", {Source, Destination});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Copy.mValue, Again.mValue);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, RejectsUnbuiltUndersizedAndCopyQueueClones)
	{
		const auto Source = Create(mBlasDesc);
		const auto Destination = Create(mBlasDesc);
		ASSERT_TRUE(Source && Destination);
		const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		EXPECT_EQ(Commands.mValue->CopyAccelStruct(*Destination, *Source).mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(Commands.mValue->CopyAccelStruct(*Source, *Source).mCode, EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(Commands.mValue->Close());
		Build(Source);
		ASSERT_FALSE(HasFatalFailure());
		auto SmallDesc = mBlasDesc;
		SmallDesc.mResultSizeOverride = 1;
		const auto SourceRequirements = mDevice->GetAccelStructBuildMemoryRequirements(mBlasDesc);
		ASSERT_TRUE(SourceRequirements);
		ASSERT_LT(SmallDesc.mResultSizeOverride, SourceRequirements.mValue.mResultSize);
		const auto Small = Create(SmallDesc);
		ASSERT_TRUE(Small);
		ASSERT_TRUE(Commands.mValue->Open());
		EXPECT_EQ(Commands.mValue->CopyAccelStruct(*Small, *Source).mCode, EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(Commands.mValue->Close());
		const auto CopyQueue = mDevice->CreateCommandList(EArdaRHIQueueType::Copy);
		ASSERT_TRUE(CopyQueue);
		ASSERT_TRUE(CopyQueue.mValue->Open());
		EXPECT_EQ(CopyQueue.mValue->CopyAccelStruct(*Destination, *Source).mCode, EArdaRHIResult::InvalidArgument);
		ASSERT_TRUE(CopyQueue.mValue->Close());
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, RejectsCompactionBeforeBuildAndUndersizedCompactionStorage)
	{
		const auto Source = Create(mBlasDesc);
		auto SmallDesc = mBlasDesc;
		SmallDesc.mResultSizeOverride = 1;
		const auto Small = Create(SmallDesc);
		ASSERT_TRUE(Source && Small);
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Input = Graph.ImportAccelerationStructure("source", Source);
		const auto Destination = Graph.ImportAccelerationStructure("small", Small);
		ASSERT_TRUE(Input && Destination);
		const auto Unbuilt = Graph.AttachOrFind<FArdaMemoryCompactAccelerationStructureNode>("unbuilt",
		    {Input.mValue, Destination.mValue});
		EXPECT_FALSE(Unbuilt);
		EXPECT_EQ(Unbuilt.mStatus.mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		Build(Source);
		ASSERT_FALSE(HasFatalFailure());
		const auto Size = mDevice->GetAccelStructCompactedSize(Source);
		ASSERT_TRUE(Size);
		ASSERT_LT(SmallDesc.mResultSizeOverride, Size.mValue);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto BuiltInput = Graph.ImportAccelerationStructure("built source", Source);
		const auto BuiltDestination = Graph.ImportAccelerationStructure("small destination", Small);
		ASSERT_TRUE(BuiltInput && BuiltDestination);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCompactAccelerationStructureNode>("undersized",
		    {BuiltInput.mValue, BuiltDestination.mValue}));
		ASSERT_TRUE(Graph.MarkOutput(BuiltDestination.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto Executed = Graph.Execute();
		EXPECT_FALSE(Executed.mStatus);
		EXPECT_EQ(Executed.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Small->GetBuildState(), EArdaRHIAccelStructBuildState::Unbuilt);
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, ClonesAnUpdatedBuildInTheSameCommandList)
	{
		const auto Source = Create(mBlasDesc);
		const auto Destination = Create(mBlasDesc);
		ASSERT_TRUE(Source && Destination);
		Build(Source);
		ASSERT_FALSE(HasFatalFailure());
		const auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->BuildBottomLevelAccelStruct(*Source,
		    mBlasDesc.mBottomLevelGeometries,
		    mBlasDesc.mBuildFlags | EArdaRHIAccelStructBuildFlags::PerformUpdate));
		ASSERT_TRUE(Commands.mValue->CopyAccelStruct(*Destination, *Source));
		const auto Snapshot = Commands.mValue->QueryAccelStructState(*Destination);
		ASSERT_TRUE(Snapshot);
		EXPECT_TRUE(Snapshot.mValue.IsConsistent());
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Destination->GetBuildState(), EArdaRHIAccelStructBuildState::Updated);
	}

	TEST_P(FArdaMemoryAccelerationStructuresGpu, DiscardedCloneDoesNotCommitLifecycleOrCompactionQueryState)
	{
		const auto Source = Create(mBlasDesc);
		const auto Destination = Create(mBlasDesc);
		ASSERT_TRUE(Source && Destination);
		Build(Source);
		ASSERT_FALSE(HasFatalFailure());
		{
			const auto Discarded = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Discarded);
			ASSERT_TRUE(Discarded.mValue->Open());
			ASSERT_TRUE(Discarded.mValue->CopyAccelStruct(*Destination, *Source));
			ASSERT_TRUE(Discarded.mValue->Close());
		}
		EXPECT_EQ(Destination->GetBuildState(), EArdaRHIAccelStructBuildState::Unbuilt);
		EXPECT_EQ(mDevice->GetAccelStructCompactedSize(Destination).mStatus.mCode, EArdaRHIResult::InvalidState);
		const auto Submitted = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(Submitted.mValue->Open());
		ASSERT_TRUE(Submitted.mValue->CopyAccelStruct(*Destination, *Source));
		ASSERT_TRUE(Submitted.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Submitted.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Destination->GetBuildState(), EArdaRHIAccelStructBuildState::Built);
		const auto SourceSize = mDevice->GetAccelStructCompactedSize(Source);
		const auto DestinationSize = mDevice->GetAccelStructCompactedSize(Destination);
		ASSERT_TRUE(SourceSize && DestinationSize);
		EXPECT_EQ(SourceSize.mValue, DestinationSize.mValue);
	}

	INSTANTIATE_TEST_SUITE_P(Native,
	    FArdaMemoryAccelerationStructuresGpu,
	    testing::Values("native-d3d12", "native-vulkan"));
}
