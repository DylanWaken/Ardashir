#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#if __has_include("NVAPI/ArdaNativeNvapiProvider.h")
#include "CUDA/ArdaNativeCudaProvider.h"
#include "NVAPI/ArdaNativeNvapiProvider.h"
#endif

#include <cstdio>

namespace arda
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSharedLibraryParameters)
		ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbRequireCuda, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaSharedLibraryNode final : TArdaNodeDefinition<FArdaSharedLibraryNode, FArdaSharedLibraryParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.shared-library-callback", 1};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Description;
			Description.mbSideEffect = true;
			return Description;
		}

		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters)
		{
			FArdaDependencyNodeRequirements Requirements;
			Requirements.mbRequireCuda = Parameters.mbRequireCuda;
			return Requirements;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	// This exercises the same C++20 node callable used by Unreal game nodes.
	ARDA_DEFINE_GRAPH_NODE(FArdaSharedLibraryNode, ArdaSharedLibraryOperation);
}

int main()
{
	using namespace arda;
#if __has_include("NVAPI/ArdaNativeNvapiProvider.h")
	// Exercise both provider factories exported by the SDK-free DLL, including
	// retaining their descriptors in a registry constructed by the host.
	FArdaNativeComputeRegistry Registry;
	if (!Registry.RegisterProvider(CreateArdaNativeCudaInGraphicsProvider()) ||
	    !Registry.RegisterProvider(CreateArdaNativeNvapiProvider()))
	{
		std::fprintf(stderr, "The shared library could not register its NVIDIA compute providers.\n");
		return 7;
	}
	const auto CiG = Registry.FindProvider(ArdaNativeCudaInGraphicsProviderId);
	const auto Nvapi = Registry.FindProvider(ArdaNativeNvapiProviderId);
	if (!CiG || !Nvapi || CiG->GetDesc().mGraphicsApis.size() != 2 ||
	    Nvapi->GetDesc().mGraphicsApis != eastl::vector<eastl::string>{"native-d3d12"})
	{
		std::fprintf(stderr, "The host received incorrect NVIDIA provider descriptors from the shared library.\n");
		return 8;
	}
#endif
	const auto Supported = FArdaSharedLibraryNode::CheckRequiredFeatures(nullptr, {});
	const auto Unsupported = FArdaSharedLibraryNode::CheckRequiredFeatures(nullptr, {{}, true});
	if (!Supported || Unsupported.mCode != EArdaRHIResult::Unsupported)
	{
		std::fprintf(stderr, "Cross-library required-feature preflight returned an incorrect status.\n");
		return 6;
	}
	const uint64_t FirstIdentity = AllocateArdaGraphIdentity();
	FArdaDependencyGraph Graph;
	const uint64_t GraphIdentity = Graph.GetTopology().GetGraphIdentity();
	const uint64_t LastIdentity = AllocateArdaGraphIdentity();
	if (!(FirstIdentity < GraphIdentity && GraphIdentity < LastIdentity))
	{
		std::fprintf(stderr, "Graph identities are duplicated between the executable and shared library.\n");
		return 1;
	}

	const auto Build = [&Graph]()
	{
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Description;
		Description.mByteSize = 16;
		Description.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		const FArdaGraphBuffer Buffer = AllocateBuffer(Edit, Description);
		if (!ArdaSharedLibraryOperation(Edit, Buffer))
		{
			return Edit.GetStatus();
		}
		Edit.Keep(Buffer);
		return Edit.Commit();
	};

	for (uint32_t Construction = 0; Construction < 2; ++Construction)
	{
		const FArdaRHIStatus Built = Build();
		if (!Built)
		{
			std::fprintf(stderr, "Cross-library node attachment failed: %s\n", Built.mMessage.c_str());
			return 2;
		}
		const FArdaRHIStatus Compiled = Graph.Compile();
		if (!Compiled)
		{
			std::fprintf(stderr, "Cross-library graph compilation failed: %s\n", Compiled.mMessage.c_str());
			return 3;
		}
		// No graphics device is needed. A failed submission releases the edit lock.
		if (Graph.Execute().mStatus)
		{
			std::fprintf(stderr, "A CPU-only ABI test unexpectedly found a graphics device.\n");
			return 4;
		}
	}
	const FArdaGraphRevisionStats Stats = Graph.GetRevisionStats();
	if (Stats.mCompileCount != 1 || Stats.mCacheHitCount != 1)
	{
		std::fprintf(stderr, "Executable node callbacks did not see the graph DLL's construction-key scope.\n");
		return 5;
	}
	std::puts("Ardashir shared-library ABI: feature preflight, unique identities and callback revision reuse passed.");
	return 0;
}
