#include "ArdaBackend.h"
#include "ArdaDependencyGraph.h"
#include "ArdaTestBackend.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		using FArdaArchiveCallback = eastl::function<void(uint32_t)>;
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaExternalCallbackParameters)
			ARDA_NODE_PARAMETER(TArdaGraphExternal<FArdaArchiveCallback>, mCallback)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 41)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaExternalCallbackNode final : public TArdaNodeDefinition<FArdaExternalCallbackNode,
		                                            FArdaExternalCallbackParameters,
		                                            EArdaNodeType::Copy>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.archive.external-callback", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				FArdaDependencyNodeDesc Description;
				Description.mbSideEffect = true;
				return Description;
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
			    const FArdaParameters& Parameters,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				auto Callback = Context.ResolveExternal(Parameters.mCallback);
				if (!Callback)
				{
					return Callback.mStatus;
				}
				Callback.mValue(Parameters.mValue);
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaExternalCallbackNode, ArchivedCallback);
		ARDA_DEFINE_GRAPH_NODE(FArdaExternalCallbackNode, ArchivedCallback);

		uint32_t ArchiveImmutablePreparations = 0;
		uint32_t ArchiveMutablePreparations = 0;
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArchivePreparationParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mValue, 1)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaArchivePreparationNode final : public TArdaNodeDefinition<FArdaArchivePreparationNode,
		                                              FArdaArchivePreparationParameters,
		                                              EArdaNodeType::Copy>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.archive.preparation-boundary", 1};
			}

			static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device)
			{
				++ArchiveImmutablePreparations;
				if (!Device)
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Native preparation requires a device.")};
				}
				return {eastl::make_shared<FArdaState>(), {}};
			}

			static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
			    const FArdaParameters&,
			    const FArdaState&)
			{
				++ArchiveMutablePreparations;
				return {eastl::make_shared<FArdaInstanceState>(), {}};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				FArdaDependencyNodeDesc Description;
				Description.mbSideEffect = true;
				return Description;
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaArchivePreparationNode, ArchivedPreparation);
		ARDA_DEFINE_GRAPH_NODE(FArdaArchivePreparationNode, ArchivedPreparation);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaExternalPointerParameters)
			ARDA_NODE_PARAMETER(uint32_t*, mPointer)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaExternalPointerNode final
		    : public TArdaNodeDefinition<FArdaExternalPointerNode, FArdaExternalPointerParameters, EArdaNodeType::Copy>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.archive.external-pointer", 1};
			}

			static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
			{
				return FArdaDependencyKeyBuilder().Value(reinterpret_cast<uintptr_t>(Parameters.mPointer)).Build();
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaExternalPointerNode, ArchivedPointer);
		ARDA_DEFINE_GRAPH_NODE(FArdaExternalPointerNode, ArchivedPointer);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaExternalNestedParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mDepth, 2)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaExternalNestedNode final : public TArdaNodeDefinition<FArdaExternalNestedNode,
		                                          FArdaExternalNestedParameters,
		                                          EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.archive.external-nested", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 64;
				return Context.Buffer(Parameters.mOutput, "Output", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters& Parameters)
			{
				if (Parameters.mDepth)
				{
					const auto Child = Invoke(Context.GetEdit(), FArdaParameters{Parameters.mDepth - 1}, "Nested");
					return Context.DeclareOutput("Output", Child.GetBuffer("Output"));
				}
				FArdaMemoryUploadBufferParameters Upload;
				Upload.mBytes.resize(64, 93);
				return Context.DeclareOutput("Output", UploadBuffer(Context.GetEdit(), Upload, "Upload").GetBuffer());
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaExternalNestedNode, ArchivedNested);
		ARDA_DEFINE_GRAPH_NODE(FArdaExternalNestedNode, ArchivedNested);

		FArdaRHIStatus RecordExternalReadback(FArdaDependencyGraph& Graph,
		    const eastl::shared_ptr<eastl::vector<uint8_t>>& Destination,
		    bool bNested = false,
		    eastl::string* OutputName = nullptr)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaMemoryUploadBufferParameters Upload;
			Upload.mBytes.resize(64, 93);
			const auto Uploaded = bNested
			    ? ArchivedNested(Edit, FArdaExternalNestedParameters{}, "Nested root").GetBuffer("Output")
			    : UploadBuffer(Edit, Upload, "Upload").GetBuffer();
			if (OutputName && Uploaded)
			{
				*OutputName = Edit.GetGraph().FindResource(Uploaded)->mName;
			}
			FArdaMemoryReadbackBufferParameters Readback;
			Readback.mSource = Uploaded;
			Readback.mDestination = Destination;
			ReadbackBuffer(Edit, Readback, "Readback");
			return Edit.Commit();
		}

		TEST(ArdaGraphSerialization, RequiresNamedRawPointersAndChecksExternalValueTypes)
		{
			uint32_t Value = 7;
			FArdaDependencyGraph Graph;
			{
				FArdaGraphEdit Edit(Graph);
				ASSERT_TRUE(ArchivedPointer(Edit, FArdaExternalPointerParameters{&Value}));
				ASSERT_TRUE(Edit.Commit());
			}
			EXPECT_FALSE(Graph.Serialize());
			ASSERT_TRUE(Graph.BindExternalValue("Counter", &Value, "application.counter"));
			const auto Encoded = Graph.Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			const auto Slots = Loaded.mValue->GetExternalValueSlots(true);
			ASSERT_EQ(Slots.size(), 1u);
			EXPECT_EQ(Slots[0].mName, "Counter");
			EXPECT_EQ(Slots[0].mTypeName, "application.counter");
			double Wrong = 0;
			EXPECT_FALSE(Loaded.mValue->BindExternalValue("Counter", &Wrong));
			EXPECT_FALSE(Loaded.mValue->BindExternalValue("Counter", &Value, "other.counter"));
			ASSERT_TRUE(Loaded.mValue->BindExternalValue("Counter", &Value));
			EXPECT_TRUE(Loaded.mValue->GetExternalValueSlots(true).empty());
		}

		TEST(ArdaGraphSerialization, UnboundPreprocessedHostSlotsRetainNamesWhenSavedAgain)
		{
			const auto Original = eastl::make_shared<eastl::vector<uint8_t>>(1, 17);
			FArdaDependencyGraph Graph;
			const auto Recorded = RecordExternalReadback(Graph, Original);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			ASSERT_TRUE(Graph.BindExternalValue("Readback bytes", Original, "host.bytes"));
			const auto Encoded = Graph.Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			auto Preprocessed = Loaded.mValue->Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			const auto Unbound = Preprocessed.mValue->Serialize();
			ASSERT_TRUE(Unbound) << Unbound.mStatus.mMessage.c_str();
			auto Reloaded = FArdaDependencyGraph::Deserialize(Unbound.mValue);
			ASSERT_TRUE(Reloaded) << Reloaded.mStatus.mMessage.c_str();
			const auto Slots = Reloaded.mValue->GetExternalValueSlots(true);
			ASSERT_EQ(Slots.size(), 1u);
			EXPECT_EQ(Slots[0].mName, "Readback bytes");
			const auto Missing = Reloaded.mValue->CompileAndWait();
			EXPECT_FALSE(Missing);
			EXPECT_NE(Missing.mMessage.find("Readback bytes"), eastl::string::npos);
			const auto Replacement = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Reloaded.mValue->BindExternalValue("Readback bytes", Replacement));
			const auto Readback = Reloaded.mValue->FindNode("Readback");
			ASSERT_TRUE(Readback);
			const auto& Parameters = *static_cast<const FArdaMemoryReadbackBufferParameters*>(
			    Reloaded.mValue->GetTopology().TryGetNode(Readback)->mPayload.mSourceParameters.get());
			EXPECT_EQ(Parameters.mDestination, Replacement);
			EXPECT_EQ(*Original, (eastl::vector<uint8_t>{17}));
		}

		TEST(ArdaGraphSerialization, PreprocessedNestedArchivesPreserveRemovedInterfaceAdmissions)
		{
			const auto Original = eastl::make_shared<eastl::vector<uint8_t>>(1, 17);
			FArdaDependencyGraph Source;
			eastl::string OuterOutputName;
			const auto Recorded = RecordExternalReadback(Source, Original, true, &OuterOutputName);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			ASSERT_TRUE(Source.BindExternalValue("Readback bytes", Original, "host.bytes"));
			auto Preprocessed = Source.Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			EXPECT_FALSE(Preprocessed.mValue->FindNode("Nested root"));
			const auto Encoded = Preprocessed.mValue->Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Restored = FArdaDependencyGraph::Deserialize(Encoded.mValue);
			ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
			EXPECT_TRUE(Restored.mValue->IsPreprocessed());
			const auto Readback = Restored.mValue->FindNode("Readback");
			ASSERT_TRUE(Readback);
			const auto& Parameters = *static_cast<const FArdaMemoryReadbackBufferParameters*>(
			    Restored.mValue->GetTopology().TryGetNode(Readback)->mPayload.mSourceParameters.get());
			const auto OuterOutput = Restored.mValue->FindResource(OuterOutputName);
			ASSERT_TRUE(OuterOutput);
			EXPECT_EQ(Restored.mValue->FindResource(OuterOutput), Restored.mValue->FindResource(Parameters.mSource));
			const auto Resaved = Restored.mValue->Serialize();
			ASSERT_TRUE(Resaved) << Resaved.mStatus.mMessage.c_str();
			EXPECT_EQ(Resaved.mValue, Encoded.mValue);
			ASSERT_TRUE(
			    Restored.mValue->BindExternalValue("Readback bytes", eastl::make_shared<eastl::vector<uint8_t>>()));
			auto Compiled = FArdaInductor::Compile(*Restored.mValue);
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			EXPECT_EQ(Compiled.mValue.mSubgraphAdmissions.size(), 3u);
		}

		class FArdaExternalValueSerializationGpu : public testing::TestWithParam<const char*>
		{
		protected:
			FArdaTestDiagnosticCallback mDiagnostics;

			void SetUp() override
			{
				ShutdownBackend();
				auto Configuration = MakeArdaTestBackendConfiguration();
				Configuration.mBackendName = GetParam();
				Configuration.mMessageCallback = &mDiagnostics;
				ASSERT_TRUE(ConfigureBackend(Configuration));
				ARDA_REQUIRE_BACKEND();
			}

			void TearDown() override
			{
				if (const auto Device = GetDevice())
				{
					EXPECT_TRUE(Device->WaitForIdle());
				}
				ShutdownBackend();
				EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		};

		TEST_P(FArdaExternalValueSerializationGpu, ReadbackBindsNewHostDestinationAndRebindsAfterExecution)
		{
			const auto Original = eastl::make_shared<eastl::vector<uint8_t>>(1, 17);
			FArdaDependencyGraph Source;
			const auto Recorded = RecordExternalReadback(Source, Original);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			ASSERT_TRUE(Source.BindExternalValue("Readback bytes", Original, "host.bytes"));
			const auto Encoded = Source.Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue, GetDevice());
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			auto Prepared = Loaded.mValue->Preprocess();
			ASSERT_TRUE(Prepared) << Prepared.mStatus.mMessage.c_str();
			const auto Destination = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Prepared.mValue->BindExternalValue("Readback bytes", Destination));
			auto Compiled = Prepared.mValue->CompileAndWait();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			auto Executed = Prepared.mValue->ExecuteAndWait();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Destination, eastl::vector<uint8_t>(64, 93));
			EXPECT_EQ(*Original, (eastl::vector<uint8_t>{17}));
			const auto Second = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Prepared.mValue->BindExternalValue("Readback bytes", Second));
			Compiled = Prepared.mValue->CompileAndWait();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			Executed = Prepared.mValue->ExecuteAndWait();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Second, eastl::vector<uint8_t>(64, 93));
			EXPECT_EQ(*Original, (eastl::vector<uint8_t>{17}));
		}

		TEST_P(FArdaExternalValueSerializationGpu, NamedCallbackResolvesDestinationContext)
		{
			uint32_t SourceValue = 0;
			uint32_t DestinationValue = 0;
			FArdaDependencyGraph Source;
			ASSERT_TRUE(Source.BindExternalValue("On recorded",
			    FArdaArchiveCallback(
			        [&](uint32_t Value)
			        {
				        SourceValue += Value;
			        }),
			    "callback.counter"));
			{
				FArdaGraphEdit Edit(Source);
				ASSERT_TRUE(ArchivedCallback(Edit, FArdaExternalCallbackParameters{{"On recorded"}, 41}));
				ASSERT_TRUE(Edit.Commit());
			}
			const auto Encoded = Source.Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue, GetDevice());
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			ASSERT_TRUE(Loaded.mValue->BindExternalValue("On recorded",
			    FArdaArchiveCallback(
			        [&](uint32_t Value)
			        {
				        DestinationValue += Value;
			        })));
			auto Prepared = Loaded.mValue->Preprocess();
			ASSERT_TRUE(Prepared) << Prepared.mStatus.mMessage.c_str();
			auto Compiled = FArdaInductor::Compile(*Prepared.mValue);
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			FArdaGraphExecutor Executor(eastl::move(Compiled.mValue));
			Loaded.mValue.reset();
			Prepared.mValue.reset();
			const auto Executed = Executor.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(SourceValue, 0u);
			EXPECT_EQ(DestinationValue, 41u);
		}

		TEST_P(FArdaExternalValueSerializationGpu, PreprocessedNestedArchiveExecutesWithNewHostContext)
		{
			const auto Original = eastl::make_shared<eastl::vector<uint8_t>>(1, 17);
			FArdaDependencyGraph Source;
			const auto Recorded = RecordExternalReadback(Source, Original, true);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			ASSERT_TRUE(Source.BindExternalValue("Readback bytes", Original, "host.bytes"));
			auto Preprocessed = Source.Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			const auto Encoded = Preprocessed.mValue->Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Restored = FArdaDependencyGraph::Deserialize(Encoded.mValue, GetDevice());
			ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
			const auto Destination = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Restored.mValue->BindExternalValue("Readback bytes", Destination));
			auto Compiled = FArdaInductor::Compile(*Restored.mValue);
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			EXPECT_EQ(Compiled.mValue.mSubgraphAdmissions.size(), 3u);
			FArdaGraphExecutor Executor(eastl::move(Compiled.mValue));
			Restored.mValue.reset();
			const auto Executed = Executor.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Destination, eastl::vector<uint8_t>(64, 93));
			EXPECT_EQ(*Original, (eastl::vector<uint8_t>{17}));
		}

		TEST_P(FArdaExternalValueSerializationGpu, OfflineNativeSlotReceivesDestinationDeviceStorage)
		{
			FArdaRHIBufferDesc Expected;
			Expected.mByteSize = 64;
			Expected.mUsage = EArdaRHIBufferUsage::None;
			const auto Original = eastl::make_shared<eastl::vector<uint8_t>>(1, 17);
			FArdaDependencyGraph Source;
			{
				FArdaGraphEdit Edit(Source);
				const auto Buffer = DeclareExternalBuffer(Edit, Expected, "Device buffer");
				Upload(Edit, Buffer, eastl::vector<uint8_t>(64, 57));
				FArdaMemoryReadbackBufferParameters Readback;
				Readback.mSource = Buffer;
				Readback.mDestination = Original;
				ASSERT_TRUE(ReadbackBuffer(Edit, Readback));
				ASSERT_TRUE(Edit.Commit());
			}
			ASSERT_TRUE(Source.BindExternalValue("Readback bytes", Original, "host.bytes"));
			auto Preprocessed = Source.Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			const auto Encoded = Preprocessed.mValue->Serialize();
			ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
			auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			const auto Native = GetDevice()->CreateBuffer(Expected);
			ASSERT_TRUE(Native) << Native.mStatus.mMessage.c_str();
			const auto Destination = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Loaded.mValue->BindExternalResource("Device buffer", Native.mValue));
			ASSERT_TRUE(Loaded.mValue->BindExternalValue("Readback bytes", Destination));
			auto Compiled = FArdaInductor::Compile(*Loaded.mValue, GetDevice());
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			FArdaGraphExecutor Executor(eastl::move(Compiled.mValue));
			Loaded.mValue.reset();
			const auto Executed = Executor.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Destination, eastl::vector<uint8_t>(64, 57));
			EXPECT_EQ(*Original, (eastl::vector<uint8_t>{17}));
		}

		TEST_P(FArdaExternalValueSerializationGpu, LoadingNeverInvokesNativeOrMutablePreparation)
		{
			eastl::string Data;
			{
				FArdaDependencyGraph Source(GetDevice());
				FArdaGraphEdit Edit(Source);
				ASSERT_TRUE(ArchivedPreparation(Edit, FArdaArchivePreparationParameters{}, "Prepared work"));
				ASSERT_TRUE(Edit.Commit());
				const auto Encoded = Source.Serialize();
				ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
				Data = Encoded.mValue;
			}
			ArchiveImmutablePreparations = 0;
			ArchiveMutablePreparations = 0;
			auto Offline = FArdaDependencyGraph::Deserialize(Data);
			ASSERT_TRUE(Offline) << Offline.mStatus.mMessage.c_str();
			auto WithContext = FArdaDependencyGraph::Deserialize(Data, GetDevice());
			ASSERT_TRUE(WithContext) << WithContext.mStatus.mMessage.c_str();
			EXPECT_EQ(ArchiveImmutablePreparations, 0u);
			EXPECT_EQ(ArchiveMutablePreparations, 0u);
			auto Preprocessed = Offline.mValue->Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			EXPECT_EQ(ArchiveImmutablePreparations, 0u);
			EXPECT_EQ(ArchiveMutablePreparations, 0u);
			auto Compiled = FArdaInductor::Compile(*Preprocessed.mValue, GetDevice());
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			EXPECT_EQ(ArchiveImmutablePreparations, 1u);
			EXPECT_EQ(ArchiveMutablePreparations, 1u);
			FArdaGraphExecutor Executor(eastl::move(Compiled.mValue));
			const auto Executed = Executor.Execute();
			EXPECT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		}

#if defined(ARDA_TEST_NATIVE_D3D12) || defined(ARDA_TEST_NATIVE_VULKAN)
		const char* const ArdaExternalArchiveBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
		    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
		    "native-vulkan",
#endif
		};
		INSTANTIATE_TEST_SUITE_P(Native,
		    FArdaExternalValueSerializationGpu,
		    testing::ValuesIn(ArdaExternalArchiveBackends));
#else
		GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FArdaExternalValueSerializationGpu);
#endif
	}
}
