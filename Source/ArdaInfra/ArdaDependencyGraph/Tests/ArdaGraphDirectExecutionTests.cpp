#include "ArdaDependencyGraph.h"
#include "ArdaTestBackend.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Inductor/Rules/ArdaGraphRule.h"

#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace arda
{
	namespace
	{
		class FArdaDirectCompileRule final : public FArdaGraphCompileRule
		{
		public:
			explicit FArdaDirectCompileRule(std::function<FArdaRHIStatus()> Callback)
			    : mCallback(std::move(Callback))
			{
			}

			const char* GetName() const noexcept override
			{
				return "DirectExecutionTest";
			}

			FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult&) const override
			{
				return mCallback();
			}

		private:
			std::function<FArdaRHIStatus()> mCallback;
		};

		FArdaRHIStatus QueueDirectUpload(FArdaDependencyGraph& Graph, const char* Name = nullptr)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			const auto Buffer = AllocateBuffer(Edit, Desc, Name ? Name : "");
			Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 7));
			Edit.Keep(Buffer);
			return Edit.Commit();
		}

		void ExpectDirectAuthoringUnlocked(FArdaDependencyGraph& Graph)
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
			if (Edit.IsOpen())
			{
				EXPECT_TRUE(Edit.Cancel());
			}
		}
	}

	TEST(ArdaGraphDirectExecution, IndependentThreadsPublishEditsBeforeCompile)
	{
		FArdaDependencyGraph Graph;
		constexpr uint32_t Lanes = 8;
		std::vector<FArdaRHIStatus> Status(Lanes);
		std::vector<std::thread> Authors;
		for (uint32_t Lane = 0; Lane < Lanes; ++Lane)
		{
			Authors.emplace_back(
			    [&, Lane]
			    {
				    FArdaGraphEdit Edit(Graph);
				    FArdaRHIBufferDesc BufferDesc;
				    BufferDesc.mByteSize = 16;
				    for (uint32_t Index = 0; Index < 12; ++Index)
				    {
					    auto Buffer = AllocateBuffer(Edit, BufferDesc);
					    Upload(Edit, Buffer, eastl::vector<uint8_t>(16, static_cast<uint8_t>(Lane)));
					    Edit.Keep(Buffer);
				    }
				    Status[Lane] = Edit.Commit();
			    });
		}
		for (auto& Author : Authors)
		{
			Author.join();
		}
		for (const auto& Result : Status)
		{
			ASSERT_TRUE(Result) << Result.mMessage.c_str();
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, Lanes);
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), Lanes * 12u);
	}

	TEST(ArdaGraphDirectExecution, CompileRunsOnCallingThread)
	{
		FArdaDependencyGraph Graph;
		std::thread::id CompileThread;
		uint32_t CompileCalls = 0;
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaDirectCompileRule>(
		    [&]
		    {
			    CompileThread = std::this_thread::get_id();
			    ++CompileCalls;
			    return FArdaRHIStatus{};
		    }));
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		const auto Buffer = AllocateBuffer(Edit, Desc);
		Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 3));
		Edit.Keep(Buffer);
		ASSERT_TRUE(Edit.Commit());

		const auto Status = Graph.Compile();
		EXPECT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(CompileCalls, 1u);
		EXPECT_EQ(CompileThread, std::this_thread::get_id());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
	}

	TEST(ArdaGraphDirectExecution, CompileFailureRetainsConstructionAndRepairsInPlace)
	{
		FArdaDependencyGraph Graph;
		bool bFail = true;
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaDirectCompileRule>(
		    [&]
		    {
			    return bFail ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Direct compile failure.")
			                 : FArdaRHIStatus{};
		    }));
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		const auto Buffer = AllocateBuffer(Edit, Desc);
		Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 11));
		Edit.Keep(Buffer);
		ASSERT_TRUE(Edit.Commit());

		const auto Failed = Graph.Compile();
		EXPECT_EQ(Failed.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_NE(Failed.mMessage.find("Direct compile failure."), eastl::string::npos);
		auto Stats = Graph.GetRevisionStats();
		EXPECT_EQ(Stats.mPendingEditCount, 1u);
		EXPECT_FALSE(Stats.mbEditQueueLocked);

		bFail = false;
		const auto Repaired = Graph.Compile(true);
		ASSERT_TRUE(Repaired) << Repaired.mMessage.c_str();
		Stats = Graph.GetRevisionStats();
		EXPECT_EQ(Stats.mPendingEditCount, 0u);
		EXPECT_TRUE(Stats.mbEditQueueLocked);
		EXPECT_EQ(Stats.mCompileCount, 1u);

		// This graph has no device; the attempt still consumes the staged lock and reports the cause.
		const auto Unsupported = Graph.Execute();
		EXPECT_FALSE(Unsupported.mStatus);
		EXPECT_FALSE(Unsupported.mStatus.mMessage.empty());
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
		ExpectDirectAuthoringUnlocked(Graph);
	}

	TEST(ArdaGraphDirectExecution, SubmitAndExecuteRequireCompilationAndReportDiagnostics)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(QueueDirectUpload(Graph));
		const auto Uncompiled = Graph.Submit();
		EXPECT_FALSE(Uncompiled.mStatus);
		EXPECT_EQ(Uncompiled.mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_NE(Uncompiled.mStatus.mMessage.find("Compile"), eastl::string::npos);
		const auto UncompiledExecution = Graph.Execute();
		EXPECT_FALSE(UncompiledExecution.mStatus);
		EXPECT_EQ(UncompiledExecution.mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);

		ASSERT_TRUE(Graph.Compile());
		const auto MissingDevice = Graph.Submit();
		EXPECT_FALSE(MissingDevice.mStatus);
		EXPECT_FALSE(MissingDevice.mStatus.mMessage.empty());
		EXPECT_FALSE(Graph.GetRevisionStats().mbEditQueueLocked);
		const auto MissingDeviceExecution = Graph.Execute();
		EXPECT_FALSE(MissingDeviceExecution.mStatus);
		EXPECT_FALSE(MissingDeviceExecution.mStatus.mMessage.empty());
		ExpectDirectAuthoringUnlocked(Graph);
	}

	class FArdaGraphDirectExecutionGpuTests : public FArdaTestBackendFixture
	{
	};

	TEST_P(FArdaGraphDirectExecutionGpuTests, ConcurrentGraphsCompileAndExecuteOnCallingThreads)
	{
		constexpr size_t Count = 4;
		std::vector<std::thread> Callers;
		std::vector<FArdaRHIStatus> Status(Count);
		std::vector<eastl::shared_ptr<eastl::vector<uint8_t>>> Outputs(Count);
		for (size_t Index = 0; Index < Count; ++Index)
		{
			Callers.emplace_back(
			    [&, Index]
			    {
				    FArdaDependencyGraph Graph(mDevice);
				    FArdaGraphEdit Edit(Graph);
				    FArdaRHIBufferDesc BufferDesc;
				    BufferDesc.mByteSize = 64;
				    auto Buffer = AllocateBuffer(Edit, BufferDesc);
				    Upload(Edit, Buffer, eastl::vector<uint8_t>(64, static_cast<uint8_t>(Index + 1)));
				    Outputs[Index] = Readback(Edit, Buffer);
				    Status[Index] = Edit.Commit();
				    if (Status[Index])
				    {
					    Status[Index] = Graph.Compile();
				    }
				    if (Status[Index])
				    {
					    Status[Index] = Graph.Execute().mStatus;
				    }
			    });
		}
		for (auto& Caller : Callers)
		{
			Caller.join();
		}
		for (size_t Index = 0; Index < Count; ++Index)
		{
			ASSERT_TRUE(Status[Index]) << Status[Index].mMessage.c_str();
			ASSERT_TRUE(Outputs[Index]);
			EXPECT_EQ(*Outputs[Index], eastl::vector<uint8_t>(64, static_cast<uint8_t>(Index + 1)));
		}
	}

	TEST_P(FArdaGraphDirectExecutionGpuTests, SequencedGraphsShareBufferWithoutSchedulerDependencies)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 64;
		Desc.mUsage = EArdaRHIBufferUsage::None;
		const auto Shared = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Shared.mStatus) << Shared.mStatus.mMessage.c_str();
		ASSERT_TRUE(Shared.mValue);
		FArdaDependencyGraph Producer(mDevice);
		FArdaDependencyGraph Consumer(mDevice);
		{
			FArdaGraphEdit Edit(Producer);
			const auto Buffer = BindBuffer(Edit, Shared.mValue, "Shared output", 0);
			Upload(Edit, Buffer, eastl::vector<uint8_t>(64, 117));
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
		}
		eastl::shared_ptr<eastl::vector<uint8_t>> Output;
		{
			FArdaGraphEdit Edit(Consumer);
			const auto Input = BindBuffer(Edit, Shared.mValue, "Shared input", 1);
			Output = Readback(Edit, Input);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Producer.Compile());
		ASSERT_TRUE(Consumer.Compile());
		const auto Production = Producer.Execute();
		ASSERT_TRUE(Production.mStatus) << Production.mStatus.mMessage.c_str();
		const auto Consumption = Consumer.Execute();
		ASSERT_TRUE(Consumption.mStatus) << Consumption.mStatus.mMessage.c_str();
		ASSERT_TRUE(Output);
		EXPECT_EQ(*Output, eastl::vector<uint8_t>(64, 117));
	}

	INSTANTIATE_TEST_SUITE_P(Native,
	    FArdaGraphDirectExecutionGpuTests,
	    testing::Values("native-d3d12", "native-vulkan"));
}
