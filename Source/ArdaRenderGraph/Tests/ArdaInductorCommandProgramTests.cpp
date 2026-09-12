#include "ArdaInductorCommandProgram.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	class FPlaneTestTexture final : public IArdaRHITexture
	{
	public:
		FArdaRHITextureDesc mDesc;
		uint32_t mReferences = 0;

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			--mReferences;
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return EArdaRHIResourceType::Texture;
		}

		const char* GetDebugName() const noexcept override
		{
			return "depth stencil planes";
		}

		const FArdaRHITextureDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return this;
		}
	};

	TEST(ArdaInductorCommandProgram, DepthAndStencilRetainIndependentStatesAtTheSameMipAndSlice)
	{
		FPlaneTestTexture Native;
		Native.mDesc.mWidth = Native.mDesc.mHeight = 32;
		Native.mDesc.mMipLevels = 3;
		Native.mDesc.mArraySize = 2;
		Native.mDesc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Native.mDesc.mFormat = EArdaRHIFormat::D24S8;
		Native.mDesc.mUsage = EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::ShaderResource;
		{
			FArdaInductorCommandProgram Program({});
			auto* Texture = Program.BindTexture(FArdaRHITextureRef(&Native), EArdaRHIResourceState::Common, "planes");
			const FArdaRHITextureSubresourceRange Depth{1, 1, 1, 1, 0, 1};
			const FArdaRHITextureSubresourceRange Stencil{1, 1, 1, 1, 1, 1};
			FArdaInductorCommandAccesses FirstAccesses;
			FirstAccesses.mTextures = {{Texture->GetHandle(), Depth, EArdaRHIResourceState::CopyDest, true},
			    {Texture->GetHandle(), Stencil, EArdaRHIResourceState::CopySource, false}};
			const auto First = Program.AppendCommand("depth write and stencil read",
			    EArdaRHIQueueType::Graphics,
			    eastl::move(FirstAccesses),
			    {});
			FArdaInductorCommandAccesses SecondAccesses;
			SecondAccesses.mTextures = {{Texture->GetHandle(), Depth, EArdaRHIResourceState::CopySource, false},
			    {Texture->GetHandle(), Stencil, EArdaRHIResourceState::CopyDest, true}};
			const auto Second = Program.AppendCommand("depth read and stencil write",
			    EArdaRHIQueueType::Copy,
			    eastl::move(SecondAccesses),
			    {});
			Program.AddDependency(First, Second);
			const auto& Plan = Program.Finalize();
			const auto& Initial = Program.GetCommand(First).GetState().mTextureTransitions;
			const auto& Next = Program.GetCommand(Second).GetState().mTextureTransitions;
			ASSERT_EQ(Initial.size(), 2u);
			ASSERT_EQ(Next.size(), 2u);
			EXPECT_EQ(Initial[0].mSubresources, Depth);
			EXPECT_EQ(Initial[1].mSubresources, Stencil);
			EXPECT_EQ(Initial[0].mStateBefore, EArdaRHIResourceState::Common);
			EXPECT_EQ(Initial[1].mStateBefore, EArdaRHIResourceState::Common);
			EXPECT_EQ(Next[0].mSubresources, Depth);
			EXPECT_EQ(Next[1].mSubresources, Stencil);
			EXPECT_EQ(Next[0].mStateBefore, EArdaRHIResourceState::CopyDest);
			EXPECT_EQ(Next[1].mStateBefore, EArdaRHIResourceState::CopySource);
			EXPECT_EQ(Next[0].mStateAfter, EArdaRHIResourceState::CopySource);
			EXPECT_EQ(Next[1].mStateAfter, EArdaRHIResourceState::CopyDest);
			const auto& Exit = Program.GetCommand(Plan.mEpilogue).GetState().mTextureTransitions;
			ASSERT_EQ(Exit.size(), 2u);
			EXPECT_EQ(Exit[0].mSubresources, Depth);
			EXPECT_EQ(Exit[1].mSubresources, Stencil);
			EXPECT_EQ(Exit[0].mStateAfter, EArdaRHIResourceState::Common);
			EXPECT_EQ(Exit[1].mStateAfter, EArdaRHIResourceState::Common);
			EXPECT_EQ(&Program.Finalize(), &Plan);
		}
		EXPECT_EQ(Native.mReferences, 0u);
	}
}
