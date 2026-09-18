#include "ArdaTestComputeOperand.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include <gtest/gtest.h>
#include <atomic>
#include <future>

namespace
{
	using namespace arda;
#define SCALAR_FIELDS(V, B, S) V(uint32_t, mValue)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaScalarParameters, SCALAR_FIELDS)
#define INVALID_FIELDS(V, B, S) V(eastl::string, mUnsupported)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaInvalidParameters, INVALID_FIELDS)

	void ScalarKernel(FArdaScalarParameters::FArdaCuda)
	{
	}

	void OtherKernel(uint32_t)
	{
	}

	struct FArdaFakeEntry final : IArdaCudaKernelEntry
	{
		FArdaCudaKernelSignature mSignature{&typeid(FArdaScalarParameters::FArdaCuda),
		    sizeof(FArdaScalarParameters::FArdaCuda),
		    alignof(FArdaScalarParameters::FArdaCuda),
		    true};
		FArdaCudaBuildInfo mBuild{"test", "test-build", {{80, false}, {90, true}, {120, false}}, false};

		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return mSignature;
		}

		const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
		{
			return mBuild;
		}

		TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
		{
			return {{256, 0, 32768}, {}};
		}

		FArdaRHIStatus Launch(void*, const FArdaCudaLaunchConfig&, const void*, size_t) const override
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Host test entry must never execute.");
		}
	};

	using FArdaRegistry = TArdaCudaKernelRegistry<FArdaScalarParameters, uint32_t>;

	auto Variant(const char* Name = "scalar")
	{
		return TArdaCudaKernelVariant<&ScalarKernel, uint32_t>{eastl::make_shared<FArdaFakeEntry>(), 32u, Name, {}};
	}

	FArdaCudaCapabilities Capabilities(uint32_t Sm = 120)
	{
		FArdaCudaCapabilities C;
		C.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		C.mComputeCapability = Sm;
		C.mMaxThreadsPerBlock = 1024;
		C.mMaxSharedMemoryBytes = 49152;
		for (int I = 0; I < 3; ++I)
		{
			C.mMaxBlockSize[I] = 1024;
			C.mMaxGridSize[I] = 65535;
		}
		return C;
	}

	TEST(ArdaCudaParameters, GeneratesSeparateHostAndCudaLayoutsAndRejectsTypesAtRuntime)
	{
		EXPECT_TRUE(IsArdaCudaSurfaceElementSupported<uint32_t>(EArdaRHIFormat::R32UInt));
		EXPECT_FALSE(IsArdaCudaSurfaceElementSupported<float>(EArdaRHIFormat::R32UInt));
		EXPECT_FALSE(IsArdaCudaSurfaceElementSupported<uint32_t>(EArdaRHIFormat::R32Float));
		EXPECT_TRUE(FArdaScalarParameters::GetCudaMetadata().GetStatus());
		EXPECT_TRUE(FArdaAddParameters::GetCudaMetadata().GetStatus());
		EXPECT_FALSE(FArdaInvalidParameters::GetCudaMetadata().GetStatus());
		EXPECT_NE(sizeof(FArdaAddParameters), sizeof(FArdaAddParameters::FArdaCuda));
		const auto& M = FArdaAddParameters::GetCudaMetadata().GetMembers();
		ASSERT_EQ(M.size(), 4u);
		EXPECT_EQ(M[0].mAccess, EArdaComputeAccess::Read);
		EXPECT_EQ(M[1].mAccess, EArdaComputeAccess::Write);
		EXPECT_EQ(M[2].mCudaOffset, offsetof(FArdaAddParameters::FArdaCuda, mCount));
	}

	TEST(ArdaCudaVariants, EnforcesExactSignatureAndBuildManifestAtRuntime)
	{
		FArdaRegistry Good;
		ASSERT_TRUE(Good.Add(Variant()));
		ASSERT_TRUE(Good.Freeze());
		EXPECT_FALSE(Good.Add(Variant("later")));
		EXPECT_EQ(Good.GetVariants().size(), 1u);
		auto Wrong = eastl::make_shared<FArdaFakeEntry>();
		Wrong->mSignature.mType = &typeid(uint32_t); // Same byte size, different parameter contract.
		FArdaRegistry Bad;
		TArdaCudaKernelVariant<&OtherKernel, uint32_t> Other{Wrong, 1u, "wrong", {}};
		EXPECT_FALSE(Bad.Add(Other));
		EXPECT_FALSE(Bad.Freeze());
		FArdaRegistry Missing;
		auto V = Variant();
		auto Entry = eastl::make_shared<FArdaFakeEntry>();
		Entry->mBuild.mArchitectures.clear();
		V.mEntry = Entry;
		EXPECT_FALSE(Missing.Add(V));
		FArdaRegistry Duplicate;
		ASSERT_TRUE(Duplicate.Add(Variant()));
		EXPECT_FALSE(Duplicate.Add(Variant()));
	}

	TEST(ArdaCudaParameters, FreezesValuesWithoutDeviceAndPreservesOutputOnFailure)
	{
		FArdaScalarParameters Parameters;
		Parameters.mValue = 42;
		FArdaCudaDispatch Prepared;
		const auto& Metadata = FArdaScalarParameters::GetCudaMetadata();
		ASSERT_TRUE(Metadata.Prepare(&Parameters, Prepared));
		ASSERT_EQ(Prepared.mKernels.size(), 1u);
		Parameters.mValue = 99;
		FArdaScalarParameters::FArdaCuda Frozen;
		std::memcpy(&Frozen, Prepared.mKernels.front().mParameters.data(), sizeof(Frozen));
		EXPECT_EQ(Frozen.mValue, 42u);
		const auto Before = Prepared.mKernels.front().mParameters;
		EXPECT_FALSE(Metadata.Prepare(nullptr, Prepared));
		EXPECT_EQ(Prepared.mKernels.front().mParameters, Before);
	}

	TEST(ArdaCudaVariants, FiltersBinaryCoverageAndExecutionRestrictions)
	{
		FArdaRegistry R;
		auto V = Variant();
		V.mRequirements.mMinimumComputeCapability = 86;
		ASSERT_TRUE(R.Add(V));
		ASSERT_TRUE(R.Freeze());
		EXPECT_TRUE(R.GetCompatibleVariants(Capabilities(80)).empty());
		EXPECT_EQ(R.GetCompatibleVariants(Capabilities(86)).size(), 1u);
		EXPECT_TRUE(R.GetCompatibleVariants(Capabilities(100)).empty());
		EXPECT_TRUE(R.GetCompatibleVariants(Capabilities(121)).empty());
		EXPECT_FALSE((FArdaCudaArchitecture{90, true}.Supports(100)));
		EXPECT_TRUE((FArdaCudaArchitecture{80, false}.Supports(89)));
		EXPECT_FALSE((FArdaCudaArchitecture{86, false}.Supports(80)));
		EXPECT_FALSE((FArdaCudaArchitecture{80, false}.Supports(90)));
	}

	TEST(ArdaCudaVariants, EnumeratesAndPrunesCompileTimePermutations)
	{
		eastl::vector<int> Selected;
		ForEachArdaCudaPermutation(eastl::integer_sequence<int, 16, 32, 64, 128>{},
		    [&](auto Tile)
		    {
			    if constexpr (decltype(Tile)::value >= 32)
			    {
				    Selected.push_back(decltype(Tile)::value);
			    }
		    });
		EXPECT_EQ(Selected, (eastl::vector<int>{32, 64, 128}));
	}

	class FArdaOperand final : public TArdaComputeOperand<FArdaScalarParameters, uint32_t>
	{
	public:
		FArdaOperand()
		    : TArdaComputeOperand({})
		{
		}

		mutable std::atomic<uint32_t> mBindings{0};

		const char* GetName() const noexcept override
		{
			return "test.scalar";
		}

		void BindKernelVariants(FArdaRegistry& R) const override
		{
			++mBindings;
			R.Add(Variant());
		}

		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FArdaParameters&,
		    const FArdaCudaSelectionContext&,
		    const FArdaVariants&) const override
		{
			return {{}, {}};
		}
	};

	TEST(ArdaComputeOperand, BindsOnceAcrossConcurrentInspectionWithoutCreatingCuda)
	{
		FArdaOperand Operand;
		eastl::vector<std::future<bool>> Futures;
		for (int I = 0; I < 16; ++I)
		{
			Futures.push_back(std::async(std::launch::async,
			    [&]
			    {
				    return bool(Operand.GetKernelVariants());
			    }));
		}
		for (auto& F : Futures)
		{
			EXPECT_TRUE(F.get());
		}
		EXPECT_EQ(Operand.mBindings.load(), 1u);
		EXPECT_FALSE(Operand.Dispatch({})); // Missing device is a runtime failure.
		EXPECT_EQ(Operand.mBindings.load(), 1u);
	}

	TEST(ArdaCudaLaunch, RejectsMultipleKernelsInvalidGeometryAndParameterPatches)
	{
		FArdaCudaKernel K;
		K.mEntry = eastl::make_shared<FArdaFakeEntry>();
		K.mParameters.resize(4);
		EXPECT_TRUE(ValidateArdaCudaKernels({K}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({K, K}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({K}, 0, Capabilities(100)));
		auto Bad = K;
		Bad.mBlockSize[0] = 2048;
		EXPECT_FALSE(ValidateArdaCudaKernels({Bad}, 0, Capabilities()));
		Bad = K;
		Bad.mParameters.resize(8);
		EXPECT_FALSE(ValidateArdaCudaKernels({Bad}, 0, Capabilities()));
		Bad = K;
		Bad.mPatches.push_back({0, 0, 4});
		EXPECT_FALSE(ValidateArdaCudaKernels({Bad}, 1, Capabilities()));
		Bad = K;
		Bad.mEntry.reset();
		EXPECT_FALSE(ValidateArdaCudaKernels({Bad}, 0, Capabilities()));
	}

	TEST(ArdaCudaSequence, BatchValidatesEveryKernelWithoutRelaxingSingleDispatch)
	{
		FArdaCudaKernel Kernel;
		Kernel.mEntry = eastl::make_shared<FArdaFakeEntry>();
		Kernel.mParameters.resize(4);
		EXPECT_TRUE(ValidateArdaCudaKernelBatch({Kernel, Kernel}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({Kernel, Kernel}, 0, Capabilities()));
		auto Invalid = Kernel;
		Invalid.mGridSize[0] = 0;
		EXPECT_FALSE(ValidateArdaCudaKernelBatch({Kernel, Invalid}, 0, Capabilities()));
		Invalid = Kernel;
		Invalid.mEntry.reset();
		EXPECT_FALSE(ValidateArdaCudaKernelBatch({Kernel, Invalid}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernelBatch({}, 0, Capabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernelBatch({Kernel}, 0, {}));
		FArdaCudaSequence MissingDevice({});
		EXPECT_FALSE(MissingDevice.GetStatus());
		EXPECT_FALSE(MissingDevice.Dispatch());
		EXPECT_EQ(MissingDevice.GetKernelCount(), 0u);
	}
}
