#include "ArdaBackend.h"
#include "CUDA/ArdaNativeCudaProvider.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaRuntimeNodeDefinition.h"
#include "NativeCompute/ArdaNativeCompute.h"
#include "RHI/Scheduling/ArdaRHICommandList.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
	using namespace arda;

	constexpr char OperandId[] = "Example.CiG.Fill";
	constexpr uint32_t WordCount = 8;
	constexpr uint32_t ByteCount = WordCount * sizeof(uint32_t);

	// The provider patches the device address. The caller supplies only Base.
	struct alignas(8) FArdaFillParameters
	{
		uint64_t mOutput;
		uint32_t mBase;
		uint32_t mPadding;
	};

	void Check(const FArdaRHIStatus& Status)
	{
		if (!Status)
		{
			throw std::runtime_error(Status.mMessage.c_str());
		}
	}

	template <typename T>
	T Require(TArdaRHIResult<T> Result)
	{
		Check(Result.mStatus);
		return eastl::move(Result.mValue);
	}

	FArdaRHIBufferDesc OutputDescription()
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = ByteCount;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		return Desc;
	}

	FArdaRHIStatus ValidateFill(const FArdaNativeInvocation& Call)
	{
		const auto& View = Call.mResources[0];
		const auto& Range = View.mBufferRange;
		const auto& Buffer = dynamic_cast<const IArdaRHIBuffer&>(*View.mResource);
		const auto Bytes =
		    Range.mByteSize == ArdaRHIWholeBuffer ? Buffer.GetDesc().mByteSize - Range.mByteOffset : Range.mByteSize;
		const auto& Launch = Call.mLaunch;
		if (Range.mByteOffset % alignof(uint32_t) || Bytes != uint64_t(Launch.mBlockSize[0]) * sizeof(uint32_t) ||
		    Launch.mBlockSize[1] != 1 || Launch.mBlockSize[2] != 1 || Launch.mGridSize[0] != 1 ||
		    Launch.mGridSize[1] != 1 || Launch.mGridSize[2] != 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Fill needs one 1D block with one thread per word in the aligned output range.");
		}
		return {};
	}

	void RegisterFill()
	{
		const FArdaNativeKernelKey Key{"example-cig-fill", ArdaNativeCudaInGraphicsProviderId, "fill", "fill-u32-v1"};
		const FArdaNativeKernelLayout Layout{sizeof(FArdaFillParameters),
		    alignof(FArdaFillParameters),
		    {{offsetof(FArdaFillParameters, mOutput),
		         sizeof(uint64_t),
		         alignof(uint64_t),
		         EArdaNativeArgumentKind::Buffer},
		        {offsetof(FArdaFillParameters, mBase),
		            sizeof(uint32_t),
		            alignof(uint32_t),
		            EArdaNativeArgumentKind::Value}}};

		FArdaNativeOperand Operand;
		Operand.mId = OperandId;
		Operand.mParameterSize = Layout.mParameterSize;
		FArdaNativeResourceSlotsCreator ResourceSlots;
		ResourceSlots.AddBuffer("Output", EArdaComputeAccess::Write);
		Operand.mResources = ResourceSlots.Finalize();
		Operand.mValidate = ValidateFill;
		UseArdaNativeCudaResources(Operand); // Install graphics-to-native and native-to-graphics conversions.
		Operand = Require(MakeArdaPackagedNativeOperand(Key, Layout, eastl::move(Operand)));
		auto& Variant = Operand.mVariants.back();
		Variant.mNativeResources = Operand.mResources;
		Variant.mNativeResources[0].mCategory = ArdaNativeCudaBufferCategory;
		Variant.mGraphicsStates = {EArdaRHIResourceState::UnorderedAccess};
		Check(GetArdaNativeComputeRegistry().RegisterOperand(eastl::move(Operand)));

		// Publish the graph binding schema under the same operand ID and kernel signature.
		FArdaRuntimeNodeDesc Node;
		Node.mName = OperandId;
		Node.mKernel = Key;
		Node.mLayout = Layout;
		Node.mArguments = {{"Output", EArdaComputeAccess::Write}, {"Base", EArdaComputeAccess::Read}};
		Node.mbRequiresRegisteredConversions = true;
		Check(FArdaRuntimeNodeDefinition::Register(Node));
	}

	eastl::vector<uint8_t> FillDirect(const FArdaRHIDeviceRef& Device, uint32_t Base)
	{
		auto Output = Require(Device->CreateBuffer(OutputDescription()));
		FArdaNativeInvocation Call;
		Call.mResources = {{Output, {0, ByteCount}}};
		Call.mLaunch.mBlockSize[0] = WordCount;
		const FArdaFillParameters Parameters{0, Base, 0};
		Call.mParameters.resize(sizeof(Parameters));
		std::memcpy(Call.mParameters.data(), &Parameters, sizeof(Parameters));

		const FArdaNativeComputeContext Context{Device,
		    EArdaRHIQueueType::Graphics,
		    EArdaNativeComputePreference::NativeOnly};
		auto Plan = Require(GetArdaNativeComputeRegistry().Prepare(OperandId, Context, Call));
		auto Commands = Require(Device->CreateCommandList(Context.mQueue));
		eastl::vector<uint8_t> Bytes;
		Check(Commands->Open());
		Check(Commands->SetBufferState(*Output, EArdaRHIResourceState::UnorderedAccess));
		Commands->CommitBarriers();
		Check(Plan.Record(*Commands));
		Check(Commands->CopyBufferDeviceToHost(*Output, Bytes));
		Check(Commands->Close());
		Check(Device->ExecuteCommandList(Commands).mStatus);
		Check(Device->WaitForIdle());
		return Bytes;
	}

	eastl::vector<uint8_t> FillGraph(const FArdaRHIDeviceRef& Device, uint32_t Base)
	{
		FArdaDependencyGraph Graph(Device);
		eastl::shared_ptr<eastl::vector<uint8_t>> Bytes;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Output = AllocateBuffer(Edit, OutputDescription(), "Output");
			FArdaRuntimeNodeParameters Args;
			Args.mBlock[0] = WordCount;
			Args.mArguments.resize(2);
			Args.mArguments[0].mBuffer = Output;
			Args.mArguments[0].mRange = {0, ByteCount};
			Args.mArguments[1].mValue.resize(sizeof(Base));
			std::memcpy(Args.mArguments[1].mValue.data(), &Base, sizeof(Base));
			(void)FArdaRuntimeNodeDefinition::Invoke(Edit, OperandId, eastl::move(Args), "Fill");
			Bytes = Readback(Edit, Output); // Its dependency keeps Fill live.
			Check(Edit.Commit());
		}
		Check(Graph.CompileAndWait());
		Check(Graph.ExecuteAndWait().mStatus);
		return eastl::move(*Bytes);
	}

	void Verify(const char* Path, const eastl::vector<uint8_t>& Bytes, uint32_t Base)
	{
		if (Bytes.size() != ByteCount)
		{
			throw std::runtime_error("Fill returned an unexpected readback size.");
		}
		std::printf("%s", Path);
		for (uint32_t Index = 0; Index < WordCount; ++Index)
		{
			uint32_t Word;
			std::memcpy(&Word, Bytes.data() + Index * sizeof(Word), sizeof(Word));
			if (Word != Base + Index)
			{
				throw std::runtime_error("Fill returned an unexpected word.");
			}
			std::printf(" %u", Word);
		}
		std::puts("");
	}
}

int main(int argc, char** argv)
{
	using namespace arda;
	FArdaBackendConfiguration Config;
	if (argc > 1)
	{
		Config.mBackendName = argv[1];
	}
	Config.mCudaExecutionMode = EArdaCudaExecutionMode::GraphicsQueue;
	Config.mbEnableValidation = false;
	if (!ConfigureBackend(Config) || !InitializeBackend())
	{
		std::fprintf(stderr, "%s\n", GetBackendError().c_str());
		return 1;
	}
	try
	{
		RegisterFill();
		const auto Device = GetDevice();
		Verify("Direct", FillDirect(Device, 40), 40);
		Verify("Graph ", FillGraph(Device, 100), 100);
	}
	catch (const std::exception& Error)
	{
		std::fprintf(stderr, "%s\n", Error.what());
		ShutdownBackend();
		return 1;
	}
	ShutdownBackend();
	return 0;
}
