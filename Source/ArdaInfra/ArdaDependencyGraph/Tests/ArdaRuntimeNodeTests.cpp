#include "ArdaDependencyGraph.h"
#include "ArdaBackend.h"
#include "ArdaGraphAdmissionTestDevice.h"
#include "Graph/Node/ArdaLegacyRuntimeKernel.h"
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
			FArdaRuntimeNodeDesc Desc;
			Desc.mName = Name;
			Desc.mKernel = {"test.runtime-library",
			    "test.runtime-provider",
			    "ArdaRuntimeAdd",
			    "runtime-test-signature"};
			Desc.mLayout.mParameterSize = 24;
			Desc.mLayout.mParameterAlignment = 8;
			Desc.mLayout.mArguments = {{0, 8, 8, EArdaNativeArgumentKind::Buffer},
			    {8, 8, 8, EArdaNativeArgumentKind::Buffer},
			    {16, 4, 4, EArdaNativeArgumentKind::Value}};
			Desc.mArguments = {{"Input", EArdaComputeAccess::Read},
			    {"Output", EArdaComputeAccess::Write},
			    {"Count", EArdaComputeAccess::Read}};
			return Desc;
		}

		FArdaCudaModuleDesc MakeLegacyModule()
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
			FArdaCudaModuleDesc Module;
			Module.mFormat = EArdaCudaModuleFormat::Ptx;
			Module.mBuild.mIdentity = "runtime-test-add-v1";
			Module.mBuild.mArchitectures.push_back({50, false});
			Module.mImage.assign(Ptx, Ptx + sizeof(Ptx) - 1);
			return Module;
		}

		void WriteLegacyArguments(FArdaGraphArchiveWriter& Archive, const FArdaRuntimeNodeDesc& Definition)
		{
			Archive.Write(uint64_t(Definition.mArguments.size()));
			for (size_t I = 0; I < Definition.mArguments.size(); ++I)
			{
				const auto& Argument = Definition.mArguments[I];
				const auto& Abi = Definition.mLayout.mArguments[I];
				Archive.Fields(Argument.mName,
				    Abi.mKind,
				    Argument.mAccess,
				    uint32_t(Abi.mOffset),
				    uint32_t(Abi.mSize),
				    uint32_t(Abi.mAlignment));
			}
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
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			return Desc;
		}

		eastl::string MakeRuntimeTestPackage(bool bReferenceOnly = false, const char* LegacyName = "test.package.add")
		{
			const auto Definition = MakeRuntimeTestDefinition(bReferenceOnly ? "test.package.reference" : LegacyName);
			FArdaGraphArchiveWriter Archive;
			Archive.WriteBytes("ARDAGPU\0", 8);
			Archive.Fields(uint32_t(bReferenceOnly ? 3 : 1), eastl::string("test-package"));
			if (!bReferenceOnly)
			{
				const auto Module = MakeLegacyModule();
				Archive.Fields(uint64_t{1}, uint8_t{0}, uint32_t{50}, Module.mBuild.mIdentity, Module.mImage);
			}
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
			if (bReferenceOnly)
			{
				Archive.Write(FArdaRuntimeNodeDefinition::Serialize(Definition).mValue);
			}
			else
			{
				Archive.Fields(Definition.mName,
				    uint32_t{0},
				    Definition.mKernel.mName,
				    Definition.mLayout.mParameterSize,
				    Definition.mLayout.mParameterAlignment);
				WriteLegacyArguments(Archive, Definition);
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
			if (bReferenceOnly)
			{
				Archive.Write(uint64_t{0});
			}
			return Archive.GetData();
		}

		FArdaRHIStatus RegisterTestFallback(const FArdaRuntimeNodeDesc& Definition)
		{
			FArdaNativeOperand Operand;
			Operand.mId = Definition.mName;
			Operand.mSignature = Definition.mKernel.mSignature;
			Operand.mParameterSize = Definition.mLayout.mParameterSize;
			for (size_t I = 0; I < Definition.mArguments.size(); ++I)
			{
				const auto& Arg = Definition.mArguments[I];
				if (Definition.mLayout.mArguments[I].mKind == EArdaNativeArgumentKind::Buffer)
				{
					Operand.mResources.push_back({Arg.mName, EArdaRHIResourceType::Buffer, Arg.mAccess});
				}
			}
			Operand.mFallback.mPrepare = [](const auto&, const auto&)
			{
				return TArdaRHIResult<FArdaNativeWorkRef>{MakeArdaNativeWork(
				                                              [](IArdaRHICommandList&) -> FArdaRHIStatus
				                                              {
					                                              return {};
				                                              }),
				    {}};
			};
			return GetArdaNativeComputeRegistry().RegisterOperand(eastl::move(Operand));
		}

		FArdaRHIDeviceRef MakeRuntimeTestDevice()
		{
			return CreateArdaRHIDevice(eastl::make_shared<requirements_tests::FArdaAdmissionProvider>());
		}

		constexpr char BatchProviderId[] = "test.runtime-context-switch";

		struct FArdaRuntimeConversionTrace
		{
			eastl::vector<eastl::string> mEvents;
			bool mbNativeSupported = true;
			bool mbFailBeforeNative = false;
		};

		FArdaRHIBufferRef AsRuntimeTestBuffer(const FArdaNativeResourceView& View)
		{
			return FArdaRHIBufferRef(dynamic_cast<IArdaRHIBuffer*>(View.mResource.Get()));
		}

		FArdaRHIStatus RecordConversionCopy(IArdaRHICommandList& Commands,
		    const FArdaRHIBufferRef& Source,
		    const FArdaRHIBufferRef& Destination,
		    EArdaRHIResourceState SourceAfter,
		    EArdaRHIResourceState DestinationAfter)
		{
			if (auto Status = Commands.SetBufferState(*Source, EArdaRHIResourceState::CopySource); !Status)
			{
				return Status;
			}
			if (auto Status = Commands.SetBufferState(*Destination, EArdaRHIResourceState::CopyDest); !Status)
			{
				return Status;
			}
			Commands.CommitBarriers();
			if (auto Status = Commands.CopyBuffer(*Destination, 0, *Source, 0, 64); !Status)
			{
				return Status;
			}
			if (auto Status = Commands.SetBufferState(*Source, SourceAfter); !Status)
			{
				return Status;
			}
			if (auto Status = Commands.SetBufferState(*Destination, DestinationAfter); !Status)
			{
				return Status;
			}
			Commands.CommitBarriers();
			return {};
		}

		struct FArdaRuntimeBatchSource final : IArdaNativeKernelSource
		{
			const char* GetProviderId() const noexcept override
			{
				return BatchProviderId;
			}

			const char* GetFormat() const noexcept override
			{
				return "test-kernel";
			}
		};

		struct FArdaRuntimeBatchProvider final : IArdaNativeComputeProvider
		{
			const FArdaNativeProviderDesc mDesc{BatchProviderId, {"*"}, {"test-kernel"}};
			eastl::shared_ptr<FArdaRuntimeConversionTrace> mTrace;

			const FArdaNativeProviderDesc& GetDesc() const noexcept override
			{
				return mDesc;
			}

			TArdaRHIResult<FArdaNativeProviderSupport> ProbeDevice(const IArdaRHIDevice&) const override
			{
				FArdaNativeProviderSupport Support;
				Support.mMode = EArdaNativeExecutionMode::ContextSwitching;
				return {Support, {}};
			}

			TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> LoadKernel(const FArdaNativeKernelBinary&,
			    const FArdaNativeKernelLayout&) const override
			{
				return {eastl::make_shared<FArdaRuntimeBatchSource>(), {}};
			}

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&,
			    const FArdaNativeOperand&,
			    const IArdaNativeKernelSource&) const override
			{
				return !mTrace || mTrace->mbNativeSupported
				    ? FArdaRHIStatus{}
				    : FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Native fixture disabled.");
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
			    const FArdaNativeResourceSlot& Slot,
			    const FArdaNativeResourceView&) const override
			{
				return {{EArdaNativeHandoff::Direct,
				            Slot.mAccess == EArdaComputeAccess::Read ? EArdaRHIResourceState::CopySource
				                                                     : EArdaRHIResourceState::CopyDest},
				    {}};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&,
			    const FArdaNativeOperand&,
			    const IArdaNativeKernelSource&,
			    const FArdaNativeInvocation& Invocation,
			    const FArdaNativeLaunch&) const override
			{
				if (Invocation.mResources.empty())
				{
					return {MakeArdaNativeWork(
					            [Trace = mTrace](IArdaRHICommandList&) -> FArdaRHIStatus
					            {
						            if (Trace)
						            {
							            Trace->mEvents.push_back("native work");
						            }
						            return {};
					            }),
					    {}};
				}
				return {MakeArdaNativeWork(
				            [Input = AsRuntimeTestBuffer(Invocation.mResources[0]),
				                Output = AsRuntimeTestBuffer(Invocation.mResources[1]),
				                Trace = mTrace](IArdaRHICommandList& Commands)
				            {
					            Trace->mEvents.push_back("native work");
					            return RecordConversionCopy(Commands,
					                Input,
					                Output,
					                EArdaRHIResourceState::CopySource,
					                EArdaRHIResourceState::CopyDest);
				            }),
				    {}};
			}

			FArdaRHIStatus RecordBatch(IArdaRHICommandList& Commands,
			    const eastl::vector<FArdaNativeWorkRef>& Work) const override
			{
				if (mTrace)
				{
					mTrace->mEvents.push_back("native batch");
				}
				for (const auto& Entry : Work)
				{
					if (auto Status = Entry->Record(Commands); !Status)
					{
						return Status;
					}
				}
				return {};
			}
		};

		TArdaRHIResult<eastl::shared_ptr<FArdaRuntimeBatchProvider>> GetRuntimeTestNativeProvider()
		{
			static const auto Provider = eastl::make_shared<FArdaRuntimeBatchProvider>();
			static const auto Status = GetArdaNativeComputeRegistry().RegisterProvider(Provider);
			return {Provider, Status};
		}

		TArdaRHIResult<FArdaRuntimeNodeDesc> MakeLegacyRuntimeDefinition(const char* Name)
		{
			auto Definition = MakeRuntimeTestDefinition(Name);
			auto Module = MakeLegacyModule();
			Module.mBuild.mName = "test-package";
			const auto Status = graph_detail::ImportLegacyRuntimeKernel(Definition, Module, Definition.mKernel.mName);
			return {eastl::move(Definition), Status};
		}

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaNativeAdmissionSubgraphParameters)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaNativeAdmissionSubgraph : TArdaNodeDefinition<FArdaNativeAdmissionSubgraph,
		                                          FArdaNativeAdmissionSubgraphParameters,
		                                          EArdaNodeType::Subgraph>
		{
			inline static uint32_t sBuilds = 0;

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.runtime.admission-subgraph", 1};
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				++sBuilds;
				auto& Edit = Context.GetEdit();
				(void)FArdaRuntimeNodeDefinition::Invoke(Edit, "test.runtime.nested-operand", {}, "native");
				return Edit.GetStatus();
			}
		};
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

	TEST(ArdaRuntimeNode, ImportsLegacyDefinitionIntoKernelLibrary)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.v1-import");
		const auto Module = MakeLegacyModule();
		FArdaGraphArchiveWriter Legacy;
		Legacy.Fields(uint32_t{1},
		    Definition.mName,
		    Definition.mVersion,
		    Module.mFormat,
		    Module.mImage,
		    Module.mBuild.mName,
		    Module.mBuild.mIdentity,
		    Module.mBuild.mbFastMath,
		    uint64_t{1},
		    uint32_t{50},
		    false,
		    Definition.mKernel.mName,
		    Definition.mLayout.mParameterSize,
		    Definition.mLayout.mParameterAlignment);
		WriteLegacyArguments(Legacy, Definition);
		const auto Imported = FArdaRuntimeNodeDefinition::Deserialize(Legacy.GetData());
		ASSERT_TRUE(Imported) << Imported.mStatus.mMessage.c_str();
		const auto Binary = FindArdaNativeKernelBinary(Imported.mValue.mKernel);
		ASSERT_TRUE(Binary) << Binary.mStatus.mMessage.c_str();
		ASSERT_EQ(Binary.mValue->mBytes.size(), Module.mImage.size() + 1);
		EXPECT_TRUE(std::equal(Module.mImage.begin(), Module.mImage.end(), Binary.mValue->mBytes.begin()));
		EXPECT_EQ(Binary.mValue->mBytes.back(), 0u);
		const auto Resaved = FArdaRuntimeNodeDefinition::Serialize(Imported.mValue);
		ASSERT_TRUE(Resaved);
		EXPECT_EQ(Resaved.mValue.find(".version 6.0"), eastl::string::npos);
		EXPECT_NE(Resaved.mValue.find(Imported.mValue.mKernel.mSignature), eastl::string::npos);
	}

	TEST(ArdaRuntimeNode, ArchiveRestoresReferenceWithoutPriorNodeOrKernelRegistration)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.archive");
		ASSERT_TRUE(RegisterTestFallback(Definition));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
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
		EXPECT_NE(Encoded.mValue.find(Definition.mKernel.mSignature), eastl::string::npos);
		EXPECT_EQ(Encoded.mValue.find(".visible .entry"), eastl::string::npos);
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

	TEST(ArdaRuntimeNode, MissingProviderRejectsCommitWithoutChangingGraph)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.commit-missing");
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(AllocateBuffer(Edit, RuntimeTestBufferDesc(), "retained"));
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Original = Graph.Serialize();
		ASSERT_TRUE(Original);
		{
			FArdaGraphEdit Edit(Graph);
			const auto Input = AllocateBuffer(Edit, RuntimeTestBufferDesc());
			const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
			ASSERT_TRUE(FArdaRuntimeNodeDefinition::Invoke(Edit,
			    Definition.mName,
			    MakeRuntimeTestParameters(Input, Output),
			    "missing-kernel"));
			Edit.Keep(Output);
			const auto Status = Edit.Commit();
			EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
			EXPECT_NE(Status.mMessage.find("missing-kernel"), eastl::string::npos);
			ASSERT_TRUE(Edit.Cancel());
		}
		const auto Unchanged = Graph.Serialize();
		ASSERT_TRUE(Unchanged);
		EXPECT_EQ(Original.mValue, Unchanged.mValue);
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, CulledOperandCanCommitWithoutProvider)
	{
		const auto Definition = MakeRuntimeTestDefinition("test.runtime.commit-culled");
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		FArdaGraphEdit Edit(Graph);
		const auto Input = AllocateInitializedBuffer(Edit, RuntimeTestBufferDesc(), eastl::vector<uint8_t>(64, 0));
		const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
		ASSERT_TRUE(
		    FArdaRuntimeNodeDefinition::Invoke(Edit, Definition.mName, MakeRuntimeTestParameters(Input, Output)));
		const auto Status = Edit.Commit();
		EXPECT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, IncompatibleShaderStateCannotSatisfyCommitAdmission)
	{
		auto Definition = MakeRuntimeTestDefinition("test.runtime.state-mismatch");
		Definition.mArguments[0].mState = EArdaRHIResourceState::CopySource;
		ASSERT_TRUE(RegisterTestFallback(Definition));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		const auto Encoded = FArdaRuntimeNodeDefinition::Serialize(Definition);
		ASSERT_TRUE(Encoded);
		const auto Decoded = FArdaRuntimeNodeDefinition::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded);
		EXPECT_EQ(Decoded.mValue.mArguments[0].mState, EArdaRHIResourceState::CopySource);
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		FArdaGraphEdit Edit(Graph);
		const auto Input = AllocateInitializedBuffer(Edit, RuntimeTestBufferDesc(), eastl::vector<uint8_t>(64, 0));
		const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
		ASSERT_TRUE(
		    FArdaRuntimeNodeDefinition::Invoke(Edit, Definition.mName, MakeRuntimeTestParameters(Input, Output)));
		Edit.Keep(Output);
		EXPECT_EQ(Edit.Commit().mCode, EArdaRHIResult::Unsupported);
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, BoundGraphValidatesNestedOperandsAndReusesAdmissionPreprocessing)
	{
		FArdaRuntimeNodeDesc Definition;
		Definition.mName = "test.runtime.nested-operand";
		Definition.mKernel = {"test.runtime-nested", "missing-test-provider", "fixture", "no-arguments"};
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		auto Options = Graph.GetOptions();
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaKeepAllNodesRule>());
		FArdaNativeAdmissionSubgraph::sBuilds = 0;
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(Options));
			ASSERT_TRUE(FArdaNativeAdmissionSubgraph::Invoke(Edit, FArdaNativeAdmissionSubgraphParameters{}, "nested"));
			EXPECT_EQ(Edit.Commit().mCode, EArdaRHIResult::Unsupported);
			ASSERT_TRUE(Edit.Cancel());
			EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		}
		ASSERT_TRUE(RegisterTestFallback(Definition));
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(Options));
			ASSERT_TRUE(FArdaNativeAdmissionSubgraph::Invoke(Edit, FArdaNativeAdmissionSubgraphParameters{}, "nested"));
			const auto Status = Edit.Commit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
		}
		EXPECT_EQ(FArdaNativeAdmissionSubgraph::sBuilds, 2u);
		const auto Prepared = Graph.Preprocess();
		ASSERT_TRUE(Prepared) << Prepared.mStatus.mMessage.c_str();
		EXPECT_EQ(FArdaNativeAdmissionSubgraph::sBuilds, 2u);
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, LateProviderRegistrationBatchesCompatiblePackagedOperands)
	{
		FArdaRuntimeNodeDesc First;
		First.mName = "test.runtime.batch-one";
		First.mKernel = {"test.runtime-batch", BatchProviderId, "fixture", "no-arguments"};
		auto Second = First;
		Second.mName = "test.runtime.batch-two";
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register({First, Second}));
		ASSERT_TRUE(GetRuntimeTestNativeProvider());

		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		const auto OptionsWithAllNodes = [&]
		{
			auto Options = Graph.GetOptions();
			Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaKeepAllNodesRule>());
			return Options;
		}();
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(OptionsWithAllNodes));
			ASSERT_TRUE(FArdaRuntimeNodeDefinition::Invoke(Edit, First.mName, {}, "first"));
			const auto Status = Edit.Commit();
			EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
			ASSERT_TRUE(Edit.Cancel());
		}
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(First.mKernel, {"test-kernel", "fixture", {}, {42}}));
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(OptionsWithAllNodes));
			const auto A = FArdaRuntimeNodeDefinition::Invoke(Edit, First.mName, {}, "first");
			const auto B = FArdaRuntimeNodeDefinition::Invoke(Edit, Second.mName, {}, "second");
			ASSERT_TRUE(A);
			ASSERT_TRUE(B);
			Edit.Order(A, B);
			const auto Status = Edit.Commit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
		}
		const auto Prepared = Graph.Preprocess();
		ASSERT_TRUE(Prepared) << Prepared.mStatus.mMessage.c_str();
		const auto Compiled = FArdaInductor::Compile(*Prepared.mValue);
		ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
		ASSERT_EQ(Compiled.mValue.mDispatches.size(), 1u);
		EXPECT_EQ(Compiled.mValue.mDispatches.front().mNodes.size(), 2u);
		EXPECT_TRUE(Compiled.mValue.mCudaBatches.empty());
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(First.mName));
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Second.mName));
	}

	TEST(ArdaRuntimeNode, RestoredCustomConversionCannotBecomeDirectWhenRegistrationIsMissing)
	{
		FArdaRuntimeNodeDesc Definition;
		Definition.mName = "test.runtime.requires-conversions";
		Definition.mKernel = {"test.runtime-requires-conversions", BatchProviderId, "fixture", "no-arguments"};
		Definition.mbRequiresRegisteredConversions = true;
		const auto Encoded = FArdaRuntimeNodeDefinition::Serialize(Definition);
		ASSERT_TRUE(Encoded);
		const auto Decoded = FArdaRuntimeNodeDefinition::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded);
		ASSERT_TRUE(GetRuntimeTestNativeProvider());
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(Decoded.mValue.mKernel, {"test-kernel", "fixture", {}, {47}}));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Decoded.mValue));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
		auto Options = Graph.GetOptions();
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaKeepAllNodesRule>());
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Invoke(Edit, Definition.mName, {}, "converted"));
		const auto Status = Edit.Commit();
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Status.mMessage.find("conversion"), eastl::string::npos);
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST(ArdaRuntimeNode, ArchiveKeepsGraphicsBindingKindSeparateFromNativeLayout)
	{
		auto Definition = MakeRuntimeTestDefinition("test.runtime.graphics-binding-override");
		Definition.mArguments[0].mResourceType = EArdaRHIResourceType::Texture;
		Definition.mArguments[0].mFormat = EArdaRHIFormat::RGBA8UInt;
		auto Encoded = FArdaRuntimeNodeDefinition::Serialize(Definition);
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		const auto Decoded = FArdaRuntimeNodeDefinition::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded);
		EXPECT_EQ(Decoded.mValue.mArguments[0].mResourceType, EArdaRHIResourceType::Texture);
		EXPECT_EQ(Decoded.mValue.mArguments[0].mFormat, EArdaRHIFormat::RGBA8UInt);
		EXPECT_EQ(Decoded.mValue.mLayout.mArguments[0].mKind, EArdaNativeArgumentKind::Buffer);
		EXPECT_TRUE(Decoded.mValue.mbRequiresRegisteredConversions);

		Encoded.mValue[Encoded.mValue.size() - 2] = 3;
		EXPECT_FALSE(FArdaRuntimeNodeDefinition::Deserialize(Encoded.mValue));
	}

	TEST(ArdaGraphPackage, ExpandsWithTypedTensorBindingsAndRejectsCorruptTransport)
	{
		constexpr char Name[] = "test.package.retains-reference";
		const auto Definition = MakeLegacyRuntimeDefinition(Name);
		ASSERT_TRUE(Definition);
		ASSERT_TRUE(RegisterTestFallback(Definition.mValue));
		const auto Bytes = MakeRuntimeTestPackage(false, Name);
		const auto Package = FArdaGraphPackage::Deserialize(Bytes);
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		for (const size_t Size : {size_t{0}, size_t{8}, Bytes.size() / 2, Bytes.size() - 1})
		{
			EXPECT_FALSE(FArdaGraphPackage::Deserialize(Bytes.substr(0, Size)));
		}
		EXPECT_FALSE(FArdaGraphPackage::Deserialize(Bytes + "x"));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
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
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded);
		EXPECT_EQ(Encoded.mValue.find(".visible .entry"), eastl::string::npos);
		const auto Directory =
		    fileops::TemporaryPath(std::filesystem::temp_directory_path() / "arda-legacy-graph-reference", "test");
		std::filesystem::create_directories(Directory);
		const auto Path = Directory / "model.ardagraph";
		const auto Sidecar = Directory / (std::string(Definition.mValue.mKernel.mLibrary.c_str()) + ".ardakernels");
		const fileops::FArdaTemporaryFiles Cleanup{{Path, Sidecar, Directory}};
		ASSERT_TRUE(Graph.Save(Path));
		EXPECT_TRUE(std::filesystem::is_regular_file(Sidecar));
		EXPECT_TRUE(FArdaDependencyGraph::Load(Path, Graph.GetDevice()));
	}

	TEST(ArdaGraphPackage, ReadsReferenceOnlyPackage)
	{
		const auto Package = FArdaGraphPackage::Deserialize(MakeRuntimeTestPackage(true));
		ASSERT_TRUE(Package) << Package.mStatus.mMessage.c_str();
		EXPECT_TRUE(Package.mValue->HasStateDict());
		const auto Definition = FArdaNodeRegistry::Get().Find("test.package.reference");
		ASSERT_TRUE(Definition);
		EXPECT_NE(Definition->mPortableDefinition.find("test.runtime-library"), eastl::string::npos);
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister("test.package.reference"));
	}

	TEST(ArdaGraphPackage, LoadedPackageRetainsItsImplementationAfterRegistryReplacement)
	{
		constexpr char Name[] = "test.package.registry-replacement";
		const auto Definition = MakeLegacyRuntimeDefinition(Name);
		ASSERT_TRUE(Definition);
		ASSERT_TRUE(RegisterTestFallback(Definition.mValue));
		const auto Package = FArdaGraphPackage::Deserialize(MakeRuntimeTestPackage(false, Name));
		ASSERT_TRUE(Package);
		const auto Original = FArdaNodeRegistry::Get().Find(Name);
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Name));
		auto Replacement = MakeRuntimeTestDefinition(Name);
		Replacement.mKernel.mSignature = "different-implementation";
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Replacement));
		FArdaDependencyGraph Graph(MakeRuntimeTestDevice());
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
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Name));
	}

	class FArdaRuntimeGraphBackend : public testing::TestWithParam<const char*>
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

	class FArdaRuntimeGraphGpu : public FArdaRuntimeGraphBackend
	{
	protected:
		void SetUp() override
		{
			FArdaRuntimeGraphBackend::SetUp();
			if (mDevice && !mDevice->GetCudaCapabilities())
			{
				GTEST_SKIP() << mDevice->GetCudaCapabilities().mUnavailableReason.c_str();
			}
		}
	};

	class FArdaRuntimeConversionGpu : public FArdaRuntimeGraphBackend
	{
	protected:
		void TearDown() override
		{
			GetRuntimeTestNativeProvider().mValue->mTrace.reset();
			FArdaRuntimeGraphBackend::TearDown();
		}
	};

	TEST_P(FArdaRuntimeConversionGpu, RestoredGraphRecordsConversionsAroundNativeWorkAndSkipsThemForFallback)
	{
		const auto Provider = GetRuntimeTestNativeProvider();
		ASSERT_TRUE(Provider);
		const auto Trace = eastl::make_shared<FArdaRuntimeConversionTrace>();
		Provider.mValue->mTrace = Trace;
		auto Definition = MakeRuntimeTestDefinition((eastl::string("test.runtime.conversion.") + GetParam()).c_str());
		Definition.mKernel = {"test.runtime-conversions", BatchProviderId, "fixture", "buffer-add-convert"};
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(Definition.mKernel, {"test-kernel", "fixture", {}, {43}}));

		FArdaNativeOperand Operand;
		Operand.mId = Definition.mName;
		Operand.mSignature = Definition.mKernel.mSignature;
		Operand.mParameterSize = Definition.mLayout.mParameterSize;
		Operand.mResources = {{"Input", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Read},
		    {"Output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
		FArdaNativeKernelVariant Variant{"fixture", eastl::make_shared<FArdaRuntimeBatchSource>()};
		Variant.mGraphicsStates = {EArdaRHIResourceState::UnorderedAccess, EArdaRHIResourceState::UnorderedAccess};
		Operand.mVariants.push_back(eastl::move(Variant));
		Operand.mGraphicsToNative =
		    [Trace](const FArdaNativeConversionContext& Context, FArdaNativeConversion& Conversion)
		{
			const auto Allocate = [&]
			{
				auto Desc = RuntimeTestBufferDesc();
				Desc.mInitialState = EArdaRHIResourceState::Common;
				return Context.mContext.mDevice->CreateBuffer(Desc);
			};
			auto In = Allocate();
			if (!In)
			{
				return In.mStatus;
			}
			auto Out = Allocate();
			if (!Out)
			{
				return Out.mStatus;
			}
			Conversion.mNative.mResources[0] = {In.mValue, {0, 64}};
			Conversion.mNative.mResources[1] = {Out.mValue, {0, 64}};
			Conversion.mBeforeNative.push_back(MakeArdaNativeWork(
			    [Trace, Input = AsRuntimeTestBuffer(Context.mGraphics.mResources[0]), Staging = In.mValue](
			        IArdaRHICommandList& Commands)
			    {
				    Trace->mEvents.push_back("graphics to native");
				    if (Trace->mbFailBeforeNative)
				    {
					    return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					        "Conversion fixture rejected recording.");
				    }
				    return RecordConversionCopy(Commands,
				        Input,
				        Staging,
				        EArdaRHIResourceState::UnorderedAccess,
				        EArdaRHIResourceState::CopySource);
			    }));
			return FArdaRHIStatus{};
		};
		Operand.mNativeToGraphics =
		    [Trace](const FArdaNativeConversionContext& Context, FArdaNativeConversion& Conversion)
		{
			Conversion.mAfterNative.push_back(MakeArdaNativeWork(
			    [Trace,
			        Staging = AsRuntimeTestBuffer(Conversion.mNative.mResources[1]),
			        Output = AsRuntimeTestBuffer(Context.mGraphics.mResources[1])](IArdaRHICommandList& Commands)
			    {
				    Trace->mEvents.push_back("native to graphics");
				    return RecordConversionCopy(Commands,
				        Staging,
				        Output,
				        EArdaRHIResourceState::CopySource,
				        EArdaRHIResourceState::UnorderedAccess);
			    }));
			return FArdaRHIStatus{};
		};
		Operand.mFallback.mPrepare = [Trace](const auto&, const FArdaNativeInvocation& Invocation)
		{
			return TArdaRHIResult<FArdaNativeWorkRef>{
			    MakeArdaNativeWork(
			        [Trace,
			            Input = AsRuntimeTestBuffer(Invocation.mResources[0]),
			            Output = AsRuntimeTestBuffer(Invocation.mResources[1])](IArdaRHICommandList& Commands)
			        {
				        Trace->mEvents.push_back("shader fallback");
				        return RecordConversionCopy(Commands,
				            Input,
				            Output,
				            EArdaRHIResourceState::UnorderedAccess,
				            EArdaRHIResourceState::UnorderedAccess);
			        }),
			    {}};
		};
		ASSERT_TRUE(GetArdaNativeComputeRegistry().RegisterOperand(eastl::move(Operand)));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register(Definition));
		const auto Directory =
		    fileops::TemporaryPath(std::filesystem::temp_directory_path() / "arda-native-conversions", "test");
		std::filesystem::create_directories(Directory);
		const auto Path = Directory / "converted.ardagraph";
		const auto Sidecar = Directory / (std::string(Definition.mKernel.mLibrary.c_str()) + ".ardakernels");
		const fileops::FArdaTemporaryFiles Cleanup{{Path, Sidecar, Directory}};
		{
			FArdaDependencyGraph Source(mDevice);
			FArdaGraphEdit Edit(Source);
			const auto Input =
			    Upload(Edit, AllocateBuffer(Edit, RuntimeTestBufferDesc()), eastl::vector<uint8_t>(64, 29));
			const auto Output = AllocateBuffer(Edit, RuntimeTestBufferDesc());
			ASSERT_TRUE(FArdaRuntimeNodeDefinition::Invoke(Edit,
			    Definition.mName,
			    MakeRuntimeTestParameters(Input, Output),
			    "native"));
			Edit.Keep(Output);
			ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
			ASSERT_TRUE(Source.Save(Path));
			EXPECT_TRUE(std::filesystem::is_regular_file(Sidecar));
		}
		const auto DefinitionBytes = FArdaNodeRegistry::Get().Find(Definition.mName)->mPortableDefinition;
		const auto Portable = FArdaRuntimeNodeDefinition::Deserialize(DefinitionBytes);
		ASSERT_TRUE(Portable);
		EXPECT_TRUE(Portable.mValue.mbRequiresRegisteredConversions);
		ASSERT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
		auto Restored = FArdaDependencyGraph::Load(Path, mDevice);
		ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
		eastl::shared_ptr<eastl::vector<uint8_t>> Bytes;
		{
			FArdaGraphEdit Edit(*Restored.mValue);
			const auto Output =
			    Edit.UseBuffer(Restored.mValue->FindOutput(Restored.mValue->FindNode("native"), "Output"));
			Bytes = Readback(Edit, Output);
			ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		}
		const auto Compiled = Restored.mValue->CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		auto Executed = Restored.mValue->ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, eastl::vector<uint8_t>(64, 29));
		EXPECT_EQ(Trace->mEvents,
		    (eastl::vector<eastl::string>{"graphics to native", "native batch", "native work", "native to graphics"}));

		Trace->mEvents.clear();
		Trace->mbNativeSupported = false;
		Executed = Restored.mValue->ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, eastl::vector<uint8_t>(64, 29));
		EXPECT_EQ(Trace->mEvents, (eastl::vector<eastl::string>{"shader fallback"}));

		Trace->mEvents.clear();
		Trace->mbNativeSupported = true;
		Trace->mbFailBeforeNative = true;
		Executed = Restored.mValue->ExecuteAndWait();
		EXPECT_EQ(Executed.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Trace->mEvents, (eastl::vector<eastl::string>{"graphics to native"}));
		EXPECT_TRUE(Bytes->empty());
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Definition.mName));
	}

	TEST_P(FArdaRuntimeConversionGpu, AdjacentDirectBindingsShareOneNativeSession)
	{
		const auto Provider = GetRuntimeTestNativeProvider();
		ASSERT_TRUE(Provider);
		const auto Trace = eastl::make_shared<FArdaRuntimeConversionTrace>();
		Provider.mValue->mTrace = Trace;
		FArdaRuntimeNodeDesc First;
		First.mName = eastl::string("test.runtime.session-first.") + GetParam();
		First.mKernel = {"test.runtime-session", BatchProviderId, "fixture", "no-arguments"};
		auto Second = First;
		Second.mName = eastl::string("test.runtime.session-second.") + GetParam();
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(First.mKernel, {"test-kernel", "fixture", {}, {45}}));
		ASSERT_TRUE(FArdaRuntimeNodeDefinition::Register({First, Second}));

		FArdaDependencyGraph Graph(mDevice);
		auto Options = Graph.GetOptions();
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaKeepAllNodesRule>());
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(Options));
			auto A = FArdaRuntimeNodeDefinition::Invoke(Edit, First.mName, {}, "first");
			auto B = FArdaRuntimeNodeDefinition::Invoke(Edit, Second.mName, {}, "second");
			ASSERT_TRUE(A);
			ASSERT_TRUE(B);
			Edit.Order(A, B);
			ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
		}
		const auto Compiled = Graph.CompileAndWait();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.ExecuteAndWait();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(Trace->mEvents, (eastl::vector<eastl::string>{"native batch", "native work", "native work"}));
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(First.mName));
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(Second.mName));
	}

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
			FArdaDependencyGraph Source(mDevice);
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
	INSTANTIATE_TEST_SUITE_P(Native, FArdaRuntimeConversionGpu, testing::Values("native-d3d12", "native-vulkan"));
}
