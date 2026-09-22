#include "Inductor/Runtime/ArdaInductorCudaContract.h"

#include <gtest/gtest.h>

namespace arda::cuda_contract_tests
{
	/** CPU-only resource identities let contract failures run without a CUDA adapter. */
	template <class Interface, class Desc, EArdaRHIResourceType Kind>
	class TArdaContractResource final : public Interface
	{
	public:
		explicit TArdaContractResource(Desc Description)
		    : mDesc(eastl::move(Description))
		{
		}

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (!--mReferences)
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
			return "CUDA contract fixture";
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return this;
		}

		const Desc& GetDesc() const noexcept override
		{
			return mDesc;
		}

	private:
		Desc mDesc;
		uint32_t mReferences = 0;
	};

	FArdaRHIBufferRef MakeBuffer()
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		return FArdaRHIBufferRef(
		    new TArdaContractResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>(Desc));
	}

	FArdaRHITextureRef MakeTexture()
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = Desc.mHeight = 16;
		Desc.mMipLevels = 3;
		Desc.mArraySize = 4;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mFormat = EArdaRHIFormat::RGBA32UInt;
		Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		return FArdaRHITextureRef(
		    new TArdaContractResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>(Desc));
	}

	FArdaInductorCudaResourceAccess Declare(FArdaRHIResourceRef Resource, EArdaDependencyAccess Mode)
	{
		FArdaInductorCudaResourceAccess Result;
		Result.mResource = eastl::move(Resource);
		Result.mAccess.mAccess = Mode;
		Result.mAccess.mState = EArdaRHIResourceState::UnorderedAccess;
		return Result;
	}

	TEST(ArdaCudaContract, ReadDeclarationCannotAuthorizeWritingOperand)
	{
		const auto Buffer = MakeBuffer();
		const auto Declared = Declare(Buffer, EArdaDependencyAccess::Read);
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read}}, {Declared}));
		for (const auto Mode : {EArdaComputeAccess::Write, EArdaComputeAccess::ReadWrite})
		{
			const auto Status = ValidateArdaInductorCudaBindings({{Buffer, Mode}}, {Declared});
			EXPECT_FALSE(Status);
			EXPECT_NE(Status.mMessage.find("writes outside"), eastl::string::npos);
		}
	}

	TEST(ArdaCudaContract, WriteDeclarationCannotAuthorizeReadingOperand)
	{
		const auto Buffer = MakeBuffer();
		const auto Declared = Declare(Buffer, EArdaDependencyAccess::Write);
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Write}}, {Declared}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read}}, {Declared}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite}}, {Declared}));
	}

	TEST(ArdaCudaContract, RejectsUndeclaredNativeResourceEvenWithMatchingDescriptor)
	{
		const auto First = MakeBuffer(), Second = MakeBuffer();
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Second, EArdaComputeAccess::Write}},
		    {Declare(First, EArdaDependencyAccess::ReadWrite)}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{}}, {}));
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({}, {}));
	}

	TEST(ArdaCudaContract, BufferRequirementsMustFitDeclaredRange)
	{
		const auto Buffer = MakeBuffer();
		auto Access = Declare(Buffer, EArdaDependencyAccess::ReadWrite);
		Access.mAccess.mBufferRange = {64, 64};
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite, {80, 16}}}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite, {32, 64}}}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite, {64, 65}}}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite}}, {Access}));
		Access.mAccess.mBufferRange = {};
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read, {128, 129}}}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read, {256, 1}}}, {Access}));
	}

	TEST(ArdaCudaContract, AdjacentDeclarationsCoverUnionWithoutFillingGaps)
	{
		const auto Buffer = MakeBuffer();
		auto First = Declare(Buffer, EArdaDependencyAccess::Read);
		auto Second = First;
		First.mAccess.mBufferRange = {0, 128};
		Second.mAccess.mBufferRange = {128, 128};
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read}}, {Second, First}));
		Second.mAccess.mBufferRange = {129, 127};
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read}}, {First, Second}));
	}

	TEST(ArdaCudaContract, ReadAndWriteCoverageAreCheckedIndependently)
	{
		const auto Buffer = MakeBuffer();
		auto Read = Declare(Buffer, EArdaDependencyAccess::Read);
		auto Write = Declare(Buffer, EArdaDependencyAccess::Write);
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite}}, {Read, Write}));
		Write.mAccess.mBufferRange = {0, 128};
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite}}, {Read, Write}));
		EXPECT_TRUE(
		    ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::ReadWrite, {0, 128}}}, {Read, Write}));
	}

	TEST(ArdaCudaContract, RequiresCudaCompatibleDeclaredResourceState)
	{
		const auto Buffer = MakeBuffer();
		auto Access = Declare(Buffer, EArdaDependencyAccess::Read);
		Access.mAccess.mState = EArdaRHIResourceState::ShaderResource;
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Buffer, EArdaComputeAccess::Read}}, {Access}));
	}

	TEST(ArdaCudaContract, TextureRequiresItsMipAndEveryArrayLayer)
	{
		const auto Texture = MakeTexture();
		auto Access = Declare(Texture, EArdaDependencyAccess::ReadWrite);
		Access.mAccess.mTextureRange = {1, 1, 0, 4};
		const FArdaCudaBinding Binding{Texture, EArdaComputeAccess::Write, {}, 1};
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({Binding}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Texture, EArdaComputeAccess::Write, {}, 0}}, {Access}));
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({{Texture, EArdaComputeAccess::Write, {}, 3}}, {Access}));
		Access.mAccess.mTextureRange.mArraySliceCount = 2;
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({Binding}, {Access}));
		auto Remaining = Access;
		Remaining.mAccess.mTextureRange.mBaseArraySlice = 2;
		EXPECT_TRUE(ValidateArdaInductorCudaBindings({Binding}, {Remaining, Access}));
		Remaining.mAccess.mTextureRange.mBasePlane = 1;
		EXPECT_FALSE(ValidateArdaInductorCudaBindings({Binding}, {Remaining, Access}));
	}
}
