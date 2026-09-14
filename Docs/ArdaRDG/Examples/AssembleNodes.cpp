#include "Nodes/MeshTriangleNode.h"
#include "Nodes/WorkWriteNode.h"
#include "Nodes/ComputeWriteNode.h"
#include "Nodes/RasterTriangleNode.h"
#include "ArdaDependencyGraphNodes.h"
#include <cstring>

namespace arda
{
	// Optional: discovery before any graph or device exists. Typed attachment also registers automatically.
	FArdaRHIStatus RegisterRecipeLibrary()
	{
		if (auto S = FArdaMeshTriangleNode::Register(); !S)
		{
			return S;
		}
		if (auto S = FArdaWorkWriteNode::Register(); !S)
		{
			return S;
		}
		if (auto S = FArdaComputeWriteNode::Register(); !S)
		{
			return S;
		}
		return FArdaRasterTriangleNode::Register();
	}

	// BEGIN mesh-assembly
	FArdaRHIStatus RunMeshRecipe(FArdaRHIDeviceRef Device)
	{

		// Begin a transaction before attaching self-contained node classes.
		FArdaDependencyGraph Graph(Device);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			return S;
		}
		auto First = Graph.AttachOrFind<FArdaMeshTriangleNode>("mesh A", {});
		if (!First)
		{
			return First.mStatus;
		}
		auto Second = Graph.AttachOrFind<FArdaMeshTriangleNode>("mesh B", {});
		if (!Second)
		{
			return Second.mStatus;
		}
		if (auto S = Graph.MarkOutput(Graph.FindOutput(First.mValue, "Color")); !S)
		{
			return S;
		}
		if (auto S = Graph.MarkOutput(Graph.FindOutput(Second.mValue, "Color")); !S)
		{
			return S;
		}

		// Compile dependencies, resource lifetimes, and automatic pipeline assignments together.
		if (auto S = Graph.EndGraphEdit(); !S)
		{
			return S;
		}
		// Two instances, matching shader/layout references and attachment formats: one mesh PSO.
		const auto Compiled = Graph.GetPipelineCacheStats();
		if (Compiled.mMeshletEntries != 1 || Compiled.mHits < 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Expected mesh PSO reuse.");
		}

		// Replay the same compiled graph across frames.
		for (unsigned Frame = 0; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}
		}
		return {}; // These offscreen textures are released with Graph. Retain Graph in a renderer.
	}

	// END mesh-assembly

	// BEGIN work-assembly
	FArdaRHIStatus RunWorkRecipe(FArdaRHIDeviceRef Device)
	{

		// Begin a transaction before attaching self-contained node classes.
		FArdaDependencyGraph Graph(Device);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			return S;
		}
		auto Work = Graph.AttachOrFind<FArdaWorkWriteNode>("write", {{}, 123});
		if (!Work)
		{
			return Work.mStatus;
		}
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto Read =
		    Graph.AttachOrFind<FArdaGraphReadbackNode>("read", {Graph.FindOutput(Work.mValue, "Output"), Bytes});
		if (!Read)
		{
			return Read.mStatus;
		}

		// Compile dependencies, resource lifetimes, and automatic pipeline assignments together.
		if (auto S = Graph.EndGraphEdit(); !S)
		{
			return S;
		}

		// Replay the same compiled graph across frames.
		for (unsigned Frame = 0; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}

			// The synchronous convenience call completed the readback; validate the observable result.
			uint32_t Value = 0;
			if (Bytes->size() != sizeof(Value))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Unexpected readback size.");
			}
			std::memcpy(&Value, Bytes->data(), sizeof(Value));
			if (Value != 123)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Unexpected work-graph output.");
			}
		}
		return {};
	}

	// END work-assembly
	// BEGIN raster-assembly
	FArdaRHIStatus RunRasterRecipe(FArdaRHIDeviceRef Device)
	{

		// Begin a transaction before attaching self-contained node classes.
		FArdaDependencyGraph Graph(Device);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			return S;
		}
		auto First = Graph.AttachOrFind<FArdaRasterTriangleNode>("raster A", {});
		if (!First)
		{
			return First.mStatus;
		}
		auto Second = Graph.AttachOrFind<FArdaRasterTriangleNode>("raster B", {});
		if (!Second)
		{
			return Second.mStatus;
		}
		if (auto S = Graph.MarkOutput(Graph.FindOutput(First.mValue, "Color")); !S)
		{
			return S;
		}
		if (auto S = Graph.MarkOutput(Graph.FindOutput(Second.mValue, "Color")); !S)
		{
			return S;
		}

		// Compile dependencies, resource lifetimes, and automatic pipeline assignments together.
		if (auto S = Graph.EndGraphEdit(); !S)
		{
			return S;
		}
		// Two instances, matching shader/layout references and attachment formats: one raster PSO.
		const auto Compiled = Graph.GetPipelineCacheStats();
		if (Compiled.mGraphicsEntries != 1 || Compiled.mHits < 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Expected raster PSO reuse.");
		}

		// Replay the same compiled graph across frames.
		for (unsigned Frame = 0; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}
		}
		return {}; // These offscreen textures are released with Graph. Retain Graph in a renderer.
	}

	// END raster-assembly

	// BEGIN compute-assembly
	FArdaRHIStatus RunComputeRecipe(FArdaRHIDeviceRef Device)
	{

		// Begin a transaction before attaching self-contained node classes.
		FArdaDependencyGraph Graph(Device);
		if (auto S = Graph.BeginGraphEdit(); !S)
		{
			return S;
		}
		auto Compute = Graph.AttachOrFind<FArdaComputeWriteNode>("write", {{}, 123});
		if (!Compute)
		{
			return Compute.mStatus;
		}
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto Read =
		    Graph.AttachOrFind<FArdaGraphReadbackNode>("read", {Graph.FindOutput(Compute.mValue, "Output"), Bytes});
		if (!Read)
		{
			return Read.mStatus;
		}

		// Compile dependencies, resource lifetimes, and automatic pipeline assignments together.
		if (auto S = Graph.EndGraphEdit(); !S)
		{
			return S;
		}

		// Replay the same compiled graph across frames.
		for (unsigned Frame = 0; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}

			// The synchronous convenience call completed the readback; validate the observable result.
			uint32_t Value = 0;
			if (Bytes->size() != sizeof(Value))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Unexpected readback size.");
			}
			std::memcpy(&Value, Bytes->data(), sizeof(Value));
			if (Value != 123)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Unexpected compute-graph output.");
			}
		}
		return {};
	}

	// END compute-assembly
}
