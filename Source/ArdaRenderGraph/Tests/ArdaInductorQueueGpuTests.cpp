#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaDependencyGraph.h"
#include "ArdaDependencyGraphNodes.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
	using namespace arda;
	constexpr uint32_t QueueTraceCount = 7;
	constexpr uint32_t QueueTestWordCount = 64;

	class FQueueGpuDiagnostics final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
			{
				mErrors.fetch_add(1, std::memory_order_relaxed);
				std::fprintf(stderr, "Inductor queue validation: %s\n", Text ? Text : "");
			}
		}

		std::atomic<uint32_t> mErrors{0};
	};

	struct FQueueGpuTrace
	{
		FQueueGpuTrace()
		{
			for (uint32_t Index = 0; Index < QueueTraceCount; ++Index)
			{
				mQueues[Index].store(UINT32_MAX, std::memory_order_relaxed);
				mCalls[Index].store(0, std::memory_order_relaxed);
			}
		}

		std::atomic<uint32_t> mQueues[QueueTraceCount];
		std::atomic<uint32_t> mCalls[QueueTraceCount];
	};

	struct FQueueGpuParameters
	{
		FArdaDependencyResourceHandle mSource, mDestination;
		uint32_t mValue = 0;
		uint32_t mCost = 1;
		uint32_t mTraceIndex = 0;
		eastl::shared_ptr<FQueueGpuTrace> mTrace;
	};

	FArdaRHIStatus RegisterQueueGpuNodes()
	{
		static const FArdaRHIStatus Status = []
		{
			for (const auto Kind :
			    {EArdaDependencyNodeKind::Compute, EArdaDependencyNodeKind::Graphics, EArdaDependencyNodeKind::Copy})
			{
				TArdaDependencyNodeDefinition<FQueueGpuParameters> Definition;
				Definition.mName = Kind == EArdaDependencyNodeKind::Compute ? "test.inductor.queue-compute"
				    : Kind == EArdaDependencyNodeKind::Graphics             ? "test.inductor.queue-graphics"
				                                                            : "test.inductor.queue-copy";
				Definition.mKind = Kind;
				Definition.mCanonicalKey = [](const FQueueGpuParameters& P)
				{
					eastl::string Key;
					for (const uint64_t Value : {P.mSource.mGraph,
					         uint64_t(P.mSource.mIndex),
					         P.mSource.mGeneration,
					         P.mDestination.mGraph,
					         uint64_t(P.mDestination.mIndex),
					         P.mDestination.mGeneration,
					         uint64_t(P.mValue),
					         uint64_t(P.mCost),
					         uint64_t(P.mTraceIndex),
					         uint64_t(reinterpret_cast<uintptr_t>(P.mTrace.get()))})
					{
						Key.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
					}
					return Key;
				};
				Definition.mDescribe = [Kind](const FQueueGpuParameters& P)
				{
					FArdaDependencyNodeDesc Desc;
					Desc.mEstimatedCost = P.mCost;
					Desc.mbSideEffect = true;
					if (P.mSource)
					{
						Desc.mAccesses.push_back(
						    {P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource});
					}
					if (P.mDestination)
					{
						Desc.mAccesses.push_back({P.mDestination,
						    EArdaDependencyAccess::Write,
						    Kind == EArdaDependencyNodeKind::Copy ? EArdaRHIResourceState::CopyDest
						                                          : EArdaRHIResourceState::UnorderedAccess});
					}
					return Desc;
				};
				Definition.mRecord = [Kind](FArdaDependencyExecutionContext& Context, const FQueueGpuParameters& P)
				{
					if (!P.mTrace || P.mTraceIndex >= QueueTraceCount)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Queue trace is invalid.");
					}
					P.mTrace->mQueues[P.mTraceIndex].store(uint32_t(Context.GetCommands().GetQueueType()),
					    std::memory_order_relaxed);
					P.mTrace->mCalls[P.mTraceIndex].fetch_add(1, std::memory_order_relaxed);
					if (!P.mDestination)
					{
						return FArdaRHIStatus{};
					}
					const auto Destination = Context.GetBuffer(P.mDestination);
					if (!Destination)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
						    "Queue test destination is unavailable.");
					}
					if (Kind == EArdaDependencyNodeKind::Copy)
					{
						const auto Source = Context.GetBuffer(P.mSource);
						if (!Source)
						{
							return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
							    "Queue test source is unavailable.");
						}
						return Context.GetCommands().CopyBuffer(*Destination,
						    0,
						    *Source,
						    0,
						    QueueTestWordCount * sizeof(uint32_t));
					}
					return Context.GetCommands().ClearBufferUInt(*Destination, P.mValue);
				};
				if (const auto Registered = FArdaNodeRegistry::Get().Register(eastl::move(Definition)); !Registered)
				{
					return Registered;
				}
			}
			return FArdaRHIStatus{};
		}();
		return Status;
	}

	class ArdaInductorQueueGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = true;
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration)) << GetBackendError().c_str();
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			EXPECT_EQ(mDiagnostics.mErrors.load(std::memory_order_relaxed), 0u);
		}

		FQueueGpuDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(ArdaInductorQueueGpu, AutomaticComputeAndCopyQueuesPreserveResultsAcrossFrames)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		ASSERT_TRUE(RegisterQueueGpuNodes());
		const auto Trace = eastl::make_shared<FQueueGpuTrace>();
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Efficiency;
		Options.mbEnableAsyncCompute = true;
		Options.mbEnableCopyQueue = true;
		Options.mMinimumAsyncChain = 4;
		Options.mMinimumAsyncSlack = 4;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = QueueTestWordCount * sizeof(uint32_t);
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		eastl::vector<FArdaDependencyResourceHandle> Buffers;
		for (uint32_t Index = 0; Index < 6; ++Index)
		{
			eastl::string Name = "queue-storage-";
			Name.push_back(char('0' + Index));
			const auto Buffer = Graph.CreateBuffer(Name, Desc);
			ASSERT_TRUE(Buffer);
			Buffers.push_back(Buffer.mValue);
		}
		eastl::vector<FArdaGraphNodeHandle> Nodes;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute-clear-";
			Name.push_back(char('0' + Index));
			FQueueGpuParameters Parameters;
			Parameters.mDestination = Buffers[Index];
			Parameters.mValue = 0xC0100000u + Index;
			Parameters.mTraceIndex = Index;
			Parameters.mTrace = Trace;
			const auto Node = Graph.AttachOrFind(Name, "test.inductor.queue-compute", Parameters);
			ASSERT_TRUE(Node);
			if (!Nodes.empty())
			{
				ASSERT_TRUE(Graph.AddDependency(Nodes.back(), Node.mValue));
			}
			Nodes.push_back(Node.mValue);
		}
		FQueueGpuParameters GraphicsParameters;
		GraphicsParameters.mDestination = Buffers[4];
		GraphicsParameters.mValue = 0x6A4F1234u;
		GraphicsParameters.mCost = 100;
		GraphicsParameters.mTraceIndex = 4;
		GraphicsParameters.mTrace = Trace;
		const auto Graphics =
		    Graph.AttachOrFind("independent-graphics", "test.inductor.queue-graphics", GraphicsParameters);
		ASSERT_TRUE(Graphics);
		Nodes.push_back(Graphics.mValue);
		FQueueGpuParameters JoinParameters;
		JoinParameters.mTraceIndex = 5;
		JoinParameters.mTrace = Trace;
		const auto Join = Graph.AttachOrFind("late-graphics-join", "test.inductor.queue-graphics", JoinParameters);
		ASSERT_TRUE(Join);
		ASSERT_TRUE(Graph.AddDependency(Nodes[3], Join.mValue));
		ASSERT_TRUE(Graph.AddDependency(Graphics.mValue, Join.mValue));
		Nodes.push_back(Join.mValue);
		FQueueGpuParameters CopyParameters;
		CopyParameters.mSource = Buffers[3];
		CopyParameters.mDestination = Buffers[5];
		CopyParameters.mTraceIndex = 6;
		CopyParameters.mTrace = Trace;
		const auto Copy = Graph.AttachOrFind("copy-after-join", "test.inductor.queue-copy", CopyParameters);
		ASSERT_TRUE(Copy);
		ASSERT_TRUE(Graph.AddDependency(Join.mValue, Copy.mValue));
		Nodes.push_back(Copy.mValue);
		eastl::vector<eastl::shared_ptr<eastl::vector<uint8_t>>> Readbacks;
		eastl::vector<FArdaGraphNodeHandle> ReadbackNodes;
		for (uint32_t Index = 0; Index < Buffers.size(); ++Index)
		{
			eastl::string Name = "read-after-join-";
			Name.push_back(char('0' + Index));
			Readbacks.push_back(eastl::make_shared<eastl::vector<uint8_t>>());
			const auto Read = Graph.AttachOrFind(Name,
			    "arda.readback",
			    FArdaGraphReadbackParameters{Buffers[Index], Readbacks.back()});
			ASSERT_TRUE(Read);
			ASSERT_TRUE(Graph.AddDependency(Join.mValue, Read.mValue));
			ReadbackNodes.push_back(Read.mValue);
		}
		const auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto CompiledQueue = [&](FArdaGraphNodeHandle Node)
		{
			const auto& Result = Graph.GetCompileResult();
			const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Node);
			if (Position == Result.mExecutionOrder.end())
			{
				ADD_FAILURE() << "Queue-test callback was unexpectedly culled";
				return EArdaRHIQueueType::Graphics;
			}
			return Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())];
		};
		const auto& Capabilities = mDevice->GetCapabilities();
		const auto ComputeQueue =
		    Capabilities.IsQueueSupported(EArdaRHIQueueType::Compute) && Capabilities.mQueues.mbGpuWaits
		    ? EArdaRHIQueueType::Compute
		    : EArdaRHIQueueType::Graphics;
		const auto CopyQueue = Capabilities.IsQueueSupported(EArdaRHIQueueType::Copy) && Capabilities.mQueues.mbGpuWaits
		    ? EArdaRHIQueueType::Copy
		    : EArdaRHIQueueType::Graphics;
		for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
		{
			EXPECT_EQ(CompiledQueue(Nodes[Index]),
			    Index < 4        ? ComputeQueue
			        : Index == 6 ? CopyQueue
			                     : EArdaRHIQueueType::Graphics);
			EXPECT_EQ(Trace->mCalls[Index].load(std::memory_order_relaxed), 0u);
		}
		for (const auto Read : ReadbackNodes)
		{
			EXPECT_EQ(CompiledQueue(Read), CopyQueue);
		}
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			SCOPED_TRACE(Frame);
			for (const auto& Bytes : Readbacks)
			{
				Bytes->assign(Desc.mByteSize, 0xff);
			}
			const auto Execution = Graph.Execute();
			ASSERT_TRUE(Execution.mStatus) << Execution.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_EQ(Execution.mStateConformanceFailureCount, 0u);
			EXPECT_EQ(mDiagnostics.mErrors.load(std::memory_order_relaxed), 0u);
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
			for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
			{
				EXPECT_EQ(Trace->mCalls[Index].load(std::memory_order_relaxed), Frame + 1);
				EXPECT_EQ(Trace->mQueues[Index].load(std::memory_order_relaxed), uint32_t(CompiledQueue(Nodes[Index])));
			}
			for (uint32_t Index = 0; Index < Readbacks.size(); ++Index)
			{
				ASSERT_EQ(Readbacks[Index]->size(), Desc.mByteSize);
				const uint32_t Expected = Index < 4 ? 0xC0100000u + Index : Index == 4 ? 0x6A4F1234u : 0xC0100003u;
				for (uint32_t Word = 0; Word < QueueTestWordCount; ++Word)
				{
					uint32_t Value = 0;
					std::memcpy(&Value, Readbacks[Index]->data() + Word * sizeof(Value), sizeof(Value));
					EXPECT_EQ(Value, Expected) << "buffer " << Index << ", word " << Word;
				}
			}
		}
	}

	INSTANTIATE_TEST_SUITE_P(Native, ArdaInductorQueueGpu, testing::Values("native-d3d12", "native-vulkan"));
}
