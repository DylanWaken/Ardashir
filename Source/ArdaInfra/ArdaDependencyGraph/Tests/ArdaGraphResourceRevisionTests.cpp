#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Inductor/Compiler/ArdaGraphRuleArchive.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	using namespace arda;

	class FArdaRevisionNativeBuffer final : public IArdaRHIBuffer
	{
	public:
		explicit FArdaRevisionNativeBuffer(const void* Identity)
		    : mIdentity(Identity)
		{
			mDescription.mByteSize = 16;
			mDescription.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
		}

		void AddRef() noexcept override
		{
			mReferences.fetch_add(1, std::memory_order_relaxed);
		}

		void Release() noexcept override
		{
			if (mReferences.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return EArdaRHIResourceType::Buffer;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Revision import";
		}

		const FArdaRHIBufferDesc& GetDesc() const noexcept override
		{
			return mDescription;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity;
		}

	private:
		std::atomic<uint32_t> mReferences{0};
		const void* mIdentity;
		FArdaRHIBufferDesc mDescription;
	};

	FArdaGraphOperation WriteRevision(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Buffer,
	    uint8_t Value,
	    const eastl::string& Name)
	{
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mDestination = Buffer;
		Parameters.mBytes = eastl::vector<uint8_t>(16, Value);
		return Edit.Invoke<FArdaMemoryUploadBufferNode>(eastl::move(Parameters), Name);
	}

	FArdaGraphBuffer CreateRevisionVariable(FArdaDependencyGraph& Graph, const eastl::string& Name)
	{
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Description;
		Description.mByteSize = 16;
		const auto Buffer = AllocateBuffer(Edit, Description, Name);
		Edit.Keep(Buffer);
		EXPECT_TRUE(Edit.Commit());
		return Buffer;
	}

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRevisionSubgraphParameters)
		ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_NODE_PARAMETER(uint8_t, mValue)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaRevisionSubgraphNode final : public TArdaNodeDefinition<FArdaRevisionSubgraphNode,
	                                            FArdaRevisionSubgraphParameters,
	                                            EArdaNodeType::Subgraph>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.revision.subgraph", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
		{
			FArdaRHIBufferDesc Description;
			Description.mByteSize = 16;
			return Context.Buffer(Parameters.mDestination, "Destination", Description);
		}

		static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters& Parameters)
		{
			const auto Destination = Context.GetBufferOutput();
			WriteRevision(Context.GetEdit(), Destination, Parameters.mValue, "Leaf");
			return Context.DeclareOutput("Destination", Destination);
		}
	};

	void ExpectRevisionBefore(const FArdaDependencyGraph& Graph,
	    const eastl::string& Before,
	    const eastl::string& After)
	{
		const auto Left = Graph.FindNode(Before);
		const auto Right = Graph.FindNode(After);
		ASSERT_TRUE(Left);
		ASSERT_TRUE(Right);
		EXPECT_LT(Graph.GetTopology().TryGetNode(Left)->mPayload.mAttachmentOrder,
		    Graph.GetTopology().TryGetNode(Right)->mPayload.mAttachmentOrder);
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Left, Right));
	}

	TEST(ArdaGraphResourceRevision, SharedLogicalVersionsOverrideCommitOrder)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Shared");
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 9));
			WriteRevision(Edit, Buffer, 9, "High");
			ASSERT_TRUE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 2));
			WriteRevision(Edit, Buffer, 2, "Low");
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		ExpectRevisionBefore(Graph, "Low", "High");
	}

	TEST(ArdaGraphResourceRevision, EqualVersionsPreserveCommitOrderAndWithinEditCallOrder)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Shared");
		for (uint32_t Index = 0; Index < 3; ++Index)
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 7));
			WriteRevision(Edit, Buffer, uint8_t(Index), "First " + eastl::to_string(Index));
			WriteRevision(Edit, Buffer, uint8_t(Index), "Second " + eastl::to_string(Index));
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		for (uint32_t Index = 0; Index < 3; ++Index)
		{
			ExpectRevisionBefore(Graph, "First " + eastl::to_string(Index), "Second " + eastl::to_string(Index));
			if (Index)
			{
				ExpectRevisionBefore(Graph,
				    "Second " + eastl::to_string(Index - 1),
				    "First " + eastl::to_string(Index));
			}
		}
	}

	TEST(ArdaGraphResourceRevision, ConcurrentNativeAliasesCoalesceAndSortAllCommittedEdits)
	{
		constexpr uint32_t Count = 24;
		uint32_t PhysicalStorage = 0;
		FArdaDependencyGraph Graph;
		std::mutex Mutex;
		std::condition_variable Ready;
		uint32_t Arrived = 0;
		std::atomic<uint32_t> Turn{Count};
		std::vector<std::thread> Threads;
		std::vector<FArdaRHIStatus> Statuses(Count);
		std::vector<FArdaGraphBuffer> Aliases(Count);
		for (uint32_t Revision = 0; Revision < Count; ++Revision)
		{
			Threads.emplace_back(
			    [&, Revision]
			    {
				    FArdaGraphEdit Edit(Graph);
				    FArdaRHIBufferRef Native(new FArdaRevisionNativeBuffer(&PhysicalStorage));
				    Aliases[Revision] = BindBuffer(Edit, Native, "Import " + eastl::to_string(Revision), Revision);
				    WriteRevision(Edit, Aliases[Revision], uint8_t(Revision), "Write " + eastl::to_string(Revision));
				    Edit.Keep(Aliases[Revision]);
				    {
					    std::unique_lock<std::mutex> Lock(Mutex);
					    ++Arrived;
					    Ready.notify_all();
					    EXPECT_TRUE(Ready.wait_for(Lock,
					        std::chrono::seconds(20),
					        [&]
					        {
						        return Arrived == Count;
					        }));
				    }
				    while (Turn.load(std::memory_order_acquire) != Revision + 1)
				    {
					    std::this_thread::yield();
				    }
				    Statuses[Revision] = Edit.Commit();
				    Turn.fetch_sub(1, std::memory_order_release);
			    });
		}
		for (auto& Thread : Threads)
		{
			Thread.join();
		}
		for (uint32_t Revision = 0; Revision < Count; ++Revision)
		{
			ASSERT_TRUE(Statuses[Revision]) << Statuses[Revision].mMessage.c_str();
			ASSERT_NE(Graph.FindResource(Aliases[Revision]), nullptr);
			EXPECT_EQ(Graph.FindResource(Aliases[Revision]), Graph.FindResource(Aliases[0]));
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		for (uint32_t Revision = 1; Revision < Count; ++Revision)
		{
			ExpectRevisionBefore(Graph,
			    "Write " + eastl::to_string(Revision - 1),
			    "Write " + eastl::to_string(Revision));
		}
	}

	TEST(ArdaGraphResourceRevision, EarlierImportReadPrecedesItsOnlyLaterWriter)
	{
		uint32_t PhysicalStorage = 0;
		FArdaRHIBufferRef Native(new FArdaRevisionNativeBuffer(&PhysicalStorage));
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = BindBuffer(Edit, Native, "Imported", 20);
			WriteRevision(Edit, Buffer, 20, "Overwrite");
			ASSERT_TRUE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = BindBuffer(Edit, Native, "Read alias", 10);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			const auto Output = AllocateBuffer(Edit, Desc);
			FArdaMemoryCopyBufferParameters Parameters;
			Parameters.mSource = Buffer;
			Parameters.mDestination = Output;
			Edit.Invoke<FArdaMemoryCopyBufferNode>(Parameters, "Read initial contents");
			Edit.Keep(Output);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		ExpectRevisionBefore(Graph, "Read initial contents", "Overwrite");
	}

	TEST(ArdaGraphResourceRevision, ContradictoryVersionsAcrossTwoResourcesAreFatal)
	{
		FArdaDependencyGraph Graph;
		const auto A = CreateRevisionVariable(Graph, "A");
		const auto B = CreateRevisionVariable(Graph, "B");
		for (uint32_t Index = 0; Index < 2; ++Index)
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(A, Index));
			ASSERT_TRUE(Edit.SetResourceRevision(B, 1 - Index));
			WriteRevision(Edit, A, uint8_t(Index), "A " + eastl::to_string(Index));
			WriteRevision(Edit, B, uint8_t(Index), "B " + eastl::to_string(Index));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_DEATH((void)Graph.CompileAndWait(), "Resource revision ordering is impossible");
	}

	TEST(ArdaGraphResourceRevision, StaleIdentityAndTwoVersionsInOneEditAreRejectedAtomically)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Live");
		FArdaGraphBuffer Stale;
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Description;
			Description.mByteSize = 16;
			Stale = AllocateBuffer(Edit, Description);
			ASSERT_TRUE(Edit.Cancel());
		}
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Edit.SetResourceRevision(Stale, 1));
			EXPECT_FALSE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 1));
			EXPECT_TRUE(Edit.SetResourceRevision(Buffer, 1));
			EXPECT_FALSE(Edit.SetResourceRevision(Buffer, 2));
			EXPECT_FALSE(Edit.Commit());
		}
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		EXPECT_FALSE(Graph.FindResource(Buffer)->mbRevisionOrdered);
	}

	TEST(ArdaGraphResourceRevision, RevisionChangesParticipateInPersistentCacheIdentity)
	{
		FArdaDependencyGraph Graph;
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Buffer = CreateRevisionVariable(Graph, "Shared");
			{
				FArdaGraphEdit Edit(Graph);
				ASSERT_TRUE(Edit.SetResourceRevision(Buffer, Frame == 1 ? 1 : 9));
				WriteRevision(Edit, Buffer, 10, "First");
				ASSERT_TRUE(Edit.Commit());
			}
			{
				FArdaGraphEdit Edit(Graph);
				ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 2));
				WriteRevision(Edit, Buffer, 20, "Second");
				ASSERT_TRUE(Edit.Commit());
			}
			ASSERT_TRUE(Graph.CompileAndWait());
			ExpectRevisionBefore(Graph, Frame == 1 ? "First" : "Second", Frame == 1 ? "Second" : "First");
			EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, Frame == 0 ? 1u : 2u);
			EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, Frame == 2 ? 1u : 0u);
			EXPECT_FALSE(Graph.ExecuteAndWait().mStatus); // No native device; the attempted frame unlocks authoring.
		}
	}

	TEST(ArdaGraphResourceRevision, ExplicitDependencyContradictingVersionsIsFatal)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Shared");
		FArdaGraphOperation High;
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 9));
			High = WriteRevision(Edit, Buffer, 9, "High");
			ASSERT_TRUE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, 2));
			const auto Low = WriteRevision(Edit, Buffer, 2, "Low");
			ASSERT_TRUE(Edit.Order(High, Low));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_DEATH((void)Graph.CompileAndWait(), "Resource revision ordering is impossible");
	}

	TEST(ArdaGraphResourceRevision, SerializationRetainsEditVersionsAndReorderedHazards)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Shared");
		for (uint32_t Revision : {9u, 2u})
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, Revision));
			WriteRevision(Edit, Buffer, uint8_t(Revision), "Version " + eastl::to_string(Revision));
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Archive = Graph.Serialize();
		ASSERT_TRUE(Archive) << Archive.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Archive.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		ASSERT_TRUE(Loaded.mValue->CompileAndWait());
		ExpectRevisionBefore(*Loaded.mValue, "Version 2", "Version 9");
	}

	TEST(ArdaGraphResourceRevision, ExpandedSubgraphsRetainMembershipAcrossPreprocessedArchiveAndNewEdits)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = CreateRevisionVariable(Graph, "Shared");
		for (uint8_t Revision : {uint8_t(9), uint8_t(2)})
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetResourceRevision(Buffer, Revision));
			Edit.Invoke<FArdaRevisionSubgraphNode>(FArdaRevisionSubgraphParameters{Buffer, Revision},
			    "Version " + eastl::to_string(Revision));
			ASSERT_TRUE(Edit.Commit());
		}
		auto Preprocessed = Graph.Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Preprocessed.mValue->IsPreprocessed());
		ExpectRevisionBefore(*Preprocessed.mValue, "Version 2/Leaf", "Version 9/Leaf");
		const auto Archive = Preprocessed.mValue->Serialize();
		ASSERT_TRUE(Archive) << Archive.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Archive.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		ASSERT_TRUE(Loaded.mValue->IsPreprocessed());
		{
			FArdaGraphEdit Edit(*Loaded.mValue);
			const auto Shared = Edit.UseBuffer(Edit.GetGraph().FindResource("Shared"));
			ASSERT_TRUE(Shared);
			ASSERT_TRUE(Edit.SetResourceRevision(Shared, 5));
			WriteRevision(Edit, Shared, 5, "Version 5");
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Loaded.mValue->CompileAndWait());
		ExpectRevisionBefore(*Loaded.mValue, "Version 2/Leaf", "Version 5");
		ExpectRevisionBefore(*Loaded.mValue, "Version 5", "Version 9/Leaf");
	}

	TEST(ArdaGraphResourceRevision, ReadsVersionTwoAndThreeArchivesWithoutRevisionMetadata)
	{
		for (const uint32_t Version : {2u, 3u})
		{
			SCOPED_TRACE(Version);
			FArdaGraphArchiveWriter Archive;
			Archive.Fields(uint64_t{0x4850415247414452ull}, Version, false);
			ASSERT_TRUE(WriteArdaGraphOptions(Archive, FArdaInductorOptions{}));
			if (Version >= 3)
			{
				Archive.WriteInteger(0, 8); // Portable definitions.
			}
			Archive.WriteInteger(0, 8); // External values.
			Archive.WriteInteger(1, 8); // Resource declarations.
			Archive.Fields(eastl::string("Legacy"), false, false, true, EArdaGraphExternalResourceKind::None);
			FArdaRHIBufferDesc Description;
			Description.mByteSize = 16;
			Archive.Write(Description);
			if (Version >= 3)
			{
				Archive.WriteInteger(0, 8); // Buffer initialization bytes.
			}
			Archive.WriteInteger(1, 8); // Identity alias table.
			Archive.Write(uint32_t{0});
			Archive.WriteInteger(0, 8); // Nodes.
			Archive.WriteInteger(0, 8); // Manual edges.
			Archive.WriteInteger(0, 8); // Topology edges.
			Archive.WriteInteger(0, 8); // Subgraph admissions.
			ASSERT_TRUE(Archive.GetStatus());
			auto Loaded = FArdaDependencyGraph::Deserialize(Archive.GetData());
			ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
			const auto Handle = Loaded.mValue->FindResource("Legacy");
			ASSERT_TRUE(Handle);
			ASSERT_NE(Loaded.mValue->FindResource(Handle), nullptr);
			EXPECT_FALSE(Loaded.mValue->FindResource(Handle)->mbRevisionOrdered);
		}
	}

	TEST(ArdaGraphResourceRevision, ConcurrentEditorsInspectTheirOwnUncommittedNodes)
	{
		FArdaDependencyGraph Graph;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		FArdaGraphEdit Outer(Graph);
		const auto Parent = AllocateBuffer(Outer, Desc);
		ASSERT_TRUE(WriteRevision(Outer, Parent, 1, "parent"));
		Outer.Keep(Parent);
		std::thread Peer(
		    [&]
		    {
			    FArdaGraphEdit Inner(Graph);
			    ASSERT_TRUE(Inner.IsOpen());
			    EXPECT_FALSE(Graph.FindNode("parent"));
			    const auto Child = AllocateBuffer(Inner, Desc);
			    ASSERT_TRUE(WriteRevision(Inner, Child, 2, "child"));
			    Inner.Keep(Child);
			    EXPECT_TRUE(Inner.Commit());
		    });
		Peer.join();
		EXPECT_TRUE(Graph.FindNode("parent"));
		EXPECT_FALSE(Outer.GetGraph().FindNode("child"));
		ASSERT_TRUE(Outer.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}
}
