#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		template <class Interface, class Description, EArdaRHIResourceType Kind>
		class TArdaArchiveNativeResource : public Interface
		{
		public:
			explicit TArdaArchiveNativeResource(Description Desc)
			    : mDesc(eastl::move(Desc))
			{
			}

			void AddRef() noexcept override
			{
				++mReferences;
			}

			void Release() noexcept override
			{
				if (--mReferences == 0)
				{
					delete this;
				}
			}

			EArdaRHIResourceType GetResourceType() const noexcept override
			{
				return Kind;
			}

			const char* GetDebugName() const noexcept override
			{
				return mDesc.mDebugName.c_str();
			}

			const Description& GetDesc() const noexcept override
			{
				return mDesc;
			}

			const void* GetPhysicalIdentity() const noexcept override
			{
				return this;
			}

		private:
			Description mDesc;
			uint32_t mReferences = 0;
		};

		using FArdaArchiveNativeBuffer =
		    TArdaArchiveNativeResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		using FArdaArchiveNativeTexture =
		    TArdaArchiveNativeResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;

		class FArdaArchiveNativeStructure final : public TArdaArchiveNativeResource<IArdaRHIAccelStruct,
		                                              FArdaRHIAccelStructDesc,
		                                              EArdaRHIResourceType::AccelStruct>
		{
		public:
			using TArdaArchiveNativeResource::TArdaArchiveNativeResource;

			bool IsCompacted() const noexcept override
			{
				return false;
			}

			uint64_t GetDeviceAddress() const noexcept override
			{
				return 256;
			}
		};

		FArdaRHIBufferRef MakeArchiveBuffer(uint64_t Bytes = 64)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = Bytes;
			Desc.mUsage = EArdaRHIBufferUsage::None;
			return FArdaRHIBufferRef(new FArdaArchiveNativeBuffer(Desc));
		}

		FArdaRHITextureRef MakeArchiveTexture()
		{
			FArdaRHITextureDesc Desc;
			Desc.mWidth = Desc.mHeight = 8;
			Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
			Desc.mUsage = EArdaRHITextureUsage::None;
			return FArdaRHITextureRef(new FArdaArchiveNativeTexture(Desc));
		}
	}

	TEST(ArdaGraphSerialization, ExternalImportsRequireExplicitNonemptyNames)
	{
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(BindBuffer(Edit, MakeArchiveBuffer(), {}));
			EXPECT_FALSE(Edit.Commit());
		}
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(BindTexture(Edit, MakeArchiveTexture(), {}));
			EXPECT_FALSE(Edit.Commit());
		}
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const FArdaRHIAccelStructRef Structure(new FArdaArchiveNativeStructure(FArdaRHIAccelStructDesc{}));
			EXPECT_FALSE(BindAccelerationStructure(Edit, Structure, {}));
			EXPECT_FALSE(Edit.Commit());
		}
	}

	TEST(ArdaGraphSerialization, ExternalSlotsRoundTripWithoutNativeObjectsAndValidateBindings)
	{
		FArdaDependencyGraph Graph;
		const auto Buffer = MakeArchiveBuffer();
		const auto Texture = MakeArchiveTexture();
		FArdaRHIAccelStructDesc StructureDesc;
		StructureDesc.mbTopLevel = true;
		StructureDesc.mTopLevelMaxInstances = 32;
		const FArdaRHIAccelStructRef Structure(new FArdaArchiveNativeStructure(StructureDesc));
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(BindBuffer(Edit, Buffer, "Shared buffer"));
			ASSERT_TRUE(BindTexture(Edit, Texture, "Shared texture"));
			ASSERT_TRUE(BindAccelerationStructure(Edit, Structure, "Shared scene"));
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		const auto Slots = Loaded.mValue->GetExternalResourceSlots(true);
		ASSERT_EQ(Slots.size(), 3u);
		EXPECT_EQ(Slots[0].mExternalKind, EArdaGraphExternalResourceKind::Buffer);
		EXPECT_EQ(Slots[0].mBuffer.mByteSize, 64u);
		EXPECT_EQ(Slots[1].mExternalKind, EArdaGraphExternalResourceKind::Texture);
		EXPECT_EQ(Slots[1].mTexture.mWidth, 8u);
		EXPECT_EQ(Slots[2].mExternalKind, EArdaGraphExternalResourceKind::AccelerationStructure);
		EXPECT_EQ(Slots[2].mAccelerationStructure.mTopLevelMaxInstances, 32u);
		for (const auto& Slot : Slots)
		{
			EXPECT_FALSE(Slot.HasExternalBinding());
		}
		EXPECT_FALSE(Loaded.mValue->BindExternalResource("Missing", Buffer));
		EXPECT_FALSE(Loaded.mValue->BindExternalResource("Shared buffer", MakeArchiveBuffer(128)));
		EXPECT_FALSE(Loaded.mValue->BindExternalResource("Shared buffer", Texture));
		EXPECT_FALSE(Loaded.mValue->BindExternalResource("Shared buffer", FArdaRHIBufferRef{}));
		ASSERT_TRUE(Loaded.mValue->BindExternalResource("Shared buffer", MakeArchiveBuffer()));
		ASSERT_TRUE(Loaded.mValue->BindExternalResource("Shared texture", Texture));
		ASSERT_TRUE(Loaded.mValue->BindExternalResource("Shared scene", Structure));
		EXPECT_TRUE(Loaded.mValue->GetExternalResourceSlots(true).empty());
		const auto Reencoded = Loaded.mValue->Serialize();
		ASSERT_TRUE(Reencoded) << Reencoded.mStatus.mMessage.c_str();
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
	}

	TEST(ArdaGraphSerialization, AuthorsUnboundExternalShapesWithoutCreatingNativeObjects)
	{
		FArdaDependencyGraph Graph;
		FArdaRHIBufferDesc BufferDescription;
		BufferDescription.mByteSize = 64;
		BufferDescription.mUsage = EArdaRHIBufferUsage::None;
		FArdaRHITextureDesc TextureDescription;
		TextureDescription.mWidth = TextureDescription.mHeight = 8;
		TextureDescription.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDescription.mUsage = EArdaRHITextureUsage::None;
		FArdaRHIAccelStructDesc StructureDescription;
		StructureDescription.mbTopLevel = true;
		StructureDescription.mTopLevelMaxInstances = 8;
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(DeclareExternalBuffer(Edit, BufferDescription, "Offline buffer"));
			ASSERT_TRUE(DeclareExternalTexture(Edit, TextureDescription, "Offline texture"));
			ASSERT_TRUE(DeclareExternalAccelerationStructure(Edit, StructureDescription, "Offline scene"))
			    << Edit.GetStatus().mMessage.c_str();
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_EQ(Graph.GetExternalResourceSlots(true).size(), 3u);
		auto Preprocessed = Graph.Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		const auto Encoded = Preprocessed.mValue->Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Restored = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
		ASSERT_EQ(Restored.mValue->GetExternalResourceSlots(true).size(), 3u);
		ASSERT_TRUE(Restored.mValue->BindExternalResource("Offline buffer", MakeArchiveBuffer()));
		ASSERT_TRUE(Restored.mValue->BindExternalResource("Offline texture", MakeArchiveTexture()));
		const FArdaRHIAccelStructRef Structure(new FArdaArchiveNativeStructure(StructureDescription));
		ASSERT_TRUE(Restored.mValue->BindExternalResource("Offline scene", Structure));
		EXPECT_TRUE(Restored.mValue->GetExternalResourceSlots(true).empty());
	}

	TEST(ArdaGraphSerialization, AccelerationStructureOperationsAcceptUnboundTypedSlots)
	{
		FArdaDependencyGraph Graph;
		FArdaRHIAccelStructDesc Description;
		Description.mbTopLevel = true;
		Description.mTopLevelMaxInstances = 8;
		Description.mBuildFlags = EArdaRHIAccelStructBuildFlags::AllowCompaction;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Source = DeclareExternalAccelerationStructure(Edit, Description, "Source scene");
			const auto Copy = DeclareExternalAccelerationStructure(Edit, Description, "Copied scene");
			Description.mResultSizeOverride = 1024;
			const auto Compact = DeclareExternalAccelerationStructure(Edit, Description, "Compact scene");
			ASSERT_TRUE(CopyTo(Edit, Source, Copy)) << Edit.GetStatus().mMessage.c_str();
			FArdaMemoryCompactAccelerationStructureParameters Parameters;
			Parameters.mSource = Source;
			Parameters.mDestination = Compact;
			ASSERT_TRUE(CompactAccelerationStructure(Edit, Parameters)) << Edit.GetStatus().mMessage.c_str();
			const auto Committed = Edit.Commit();
			ASSERT_TRUE(Committed) << Committed.mMessage.c_str();
		}
		auto Preprocessed = Graph.Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		const auto Encoded = Preprocessed.mValue->Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Restored = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
		EXPECT_EQ(Restored.mValue->GetExternalResourceSlots(true).size(), 3u);
		{
			FArdaGraphEdit Edit(*Restored.mValue);
			const auto Slot = Edit.GetGraph().FindResource("Source scene");
			ASSERT_TRUE(Slot);
			EXPECT_FALSE(Edit.UseBuffer(Slot));
			EXPECT_NE(Edit.GetStatus().mMessage.find("wrong resource kind"), eastl::string::npos);
		}
	}

	TEST(ArdaGraphSerialization, RebindingPreservesSlotIdentityAndRejectsHiddenNativeAliases)
	{
		FArdaDependencyGraph Graph;
		const auto First = MakeArchiveBuffer();
		const auto Second = MakeArchiveBuffer();
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(BindBuffer(Edit, First, "First"));
			ASSERT_TRUE(BindBuffer(Edit, Second, "Second"));
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Before = Graph.Serialize();
		ASSERT_TRUE(Before);
		EXPECT_FALSE(Graph.BindExternalResource("Second", First));
		const auto Replacement = MakeArchiveBuffer();
		ASSERT_TRUE(Graph.BindExternalResource("First", Replacement));
		ASSERT_TRUE(Graph.BindExternalResource("Second", First));
		const auto Slots = Graph.GetExternalResourceSlots();
		ASSERT_EQ(Slots.size(), 2u);
		EXPECT_EQ(Slots[0].mExternalBuffer, Replacement);
		EXPECT_EQ(Slots[1].mExternalBuffer, First);
		const auto After = Graph.Serialize();
		ASSERT_TRUE(After);
		EXPECT_EQ(Before.mValue, After.mValue);
	}

	TEST(ArdaGraphSerialization, UnboundSlotsAllowPreprocessingAndBlockCompilationWithTheirName)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Input = BindBuffer(Edit, MakeArchiveBuffer(), "Frame source");
			const auto Output = Clone(Edit, Input);
			Edit.Keep(Output);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		auto Preprocessed = Loaded.mValue->Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		EXPECT_TRUE(Preprocessed.mValue->IsPreprocessed());
		const auto Missing = Preprocessed.mValue->CompileAndWait();
		ASSERT_FALSE(Missing);
		EXPECT_NE(Missing.mMessage.find("Frame source"), eastl::string::npos);
		const auto PreprocessedArchive = Preprocessed.mValue->Serialize();
		ASSERT_TRUE(PreprocessedArchive) << PreprocessedArchive.mStatus.mMessage.c_str();
		auto Restored = FArdaDependencyGraph::Deserialize(PreprocessedArchive.mValue);
		ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
		EXPECT_TRUE(Restored.mValue->IsPreprocessed());
		EXPECT_EQ(Restored.mValue->GetTopology().GetNodeCount(), Preprocessed.mValue->GetTopology().GetNodeCount());
		EXPECT_EQ(Restored.mValue->GetTopology().GetEdges().size(),
		    Preprocessed.mValue->GetTopology().GetEdges().size());
		ASSERT_TRUE(Restored.mValue->BindExternalResource("Frame source", MakeArchiveBuffer()));
		const auto Compiled = Restored.mValue->CompileAndWait();
		EXPECT_TRUE(Compiled) << Compiled.mMessage.c_str();
	}
}
