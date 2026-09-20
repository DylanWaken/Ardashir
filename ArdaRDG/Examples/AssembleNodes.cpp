#include "Nodes/MeshTriangleNode.h"
#include "Nodes/WorkWriteNode.h"
#include "Nodes/ComputeWriteNode.h"
#include "Nodes/RasterTriangleNode.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include <cstring>

namespace arda
{
	// Optional: discovery before any graph or device exists. Named operations register on first use.
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

		// Each invocation records an operation and returns its typed resources.
		FArdaDependencyGraph Graph(Device);
		FArdaGraphEdit Edit(Graph);
		const auto First = MeshTriangle(Edit, {}).GetTexture("Color");
		const auto Second = MeshTriangle(Edit, {}).GetTexture("Color");
		Edit.Keep(First);
		Edit.Keep(Second);

		// Queue this construction, then select or compile its persistent revision.
		if (auto S = Edit.Commit(); !S)
		{
			return S;
		}
		if (auto S = Graph.Compile(); !S)
		{
			return S;
		}
		// Compilation retains pipeline patterns without creating native PSOs.
		const auto Compiled = Graph.GetPipelineCacheStats();
		if (Compiled.mMeshletEntries != 0 || Compiled.mHits != 0 || Compiled.mMisses != 0)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Compilation created a native mesh PSO.");
		}
		if (const auto FirstRun = Graph.Execute(); !FirstRun.mStatus)
		{
			return FirstRun.mStatus;
		}
		// First submission prepares both instances and reuses their matching mesh PSO.
		const auto Prepared = Graph.GetPipelineCacheStats();
		if (Prepared.mMeshletEntries != 1 || Prepared.mHits < 1 || Prepared.mMisses != 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Expected mesh PSO reuse.");
		}

		// Later frames reuse the prepared objects without further cache requests.
		for (unsigned Frame = 1; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}
			const auto Replayed = Graph.GetPipelineCacheStats();
			if (Replayed.mMeshletEntries != Prepared.mMeshletEntries || Replayed.mHits != Prepared.mHits ||
			    Replayed.mMisses != Prepared.mMisses)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Mesh replay changed pipeline cache activity.");
			}
		}
		return {}; // These offscreen textures are released with Graph. Retain Graph in a renderer.
	}

	// END mesh-assembly

	// BEGIN work-assembly
	FArdaRHIStatus RunWorkRecipe(FArdaRHIDeviceRef Device)
	{

		// Readback retains the producer chain and publishes its result after execution.
		FArdaDependencyGraph Graph(Device);
		FArdaGraphEdit Edit(Graph);
		const auto Work = WorkWrite(Edit, {{}, 123}).GetBuffer("Output");
		const auto Bytes = Readback(Edit, Work);

		// Queue this construction, then select or compile its persistent revision.
		if (auto S = Edit.Commit(); !S)
		{
			return S;
		}
		if (auto S = Graph.Compile(); !S)
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

		// Each invocation records an operation and returns its typed resources.
		FArdaDependencyGraph Graph(Device);
		FArdaGraphEdit Edit(Graph);
		const auto First = RasterTriangle(Edit, {}).GetTexture("Color");
		const auto Second = RasterTriangle(Edit, {}).GetTexture("Color");
		Edit.Keep(First);
		Edit.Keep(Second);

		// Queue this construction, then select or compile its persistent revision.
		if (auto S = Edit.Commit(); !S)
		{
			return S;
		}
		if (auto S = Graph.Compile(); !S)
		{
			return S;
		}
		// Compilation retains pipeline patterns without creating native PSOs.
		const auto Compiled = Graph.GetPipelineCacheStats();
		if (Compiled.mGraphicsEntries != 0 || Compiled.mHits != 0 || Compiled.mMisses != 0)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Compilation created a native raster PSO.");
		}
		if (const auto FirstRun = Graph.Execute(); !FirstRun.mStatus)
		{
			return FirstRun.mStatus;
		}
		// First submission prepares both instances and reuses their matching graphics PSO.
		const auto Prepared = Graph.GetPipelineCacheStats();
		if (Prepared.mGraphicsEntries != 1 || Prepared.mHits < 1 || Prepared.mMisses != 1)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Expected raster PSO reuse.");
		}

		// Later frames reuse the prepared objects without further cache requests.
		for (unsigned Frame = 1; Frame < 3; ++Frame)
		{
			const auto Run = Graph.Execute();
			if (!Run.mStatus)
			{
				return Run.mStatus;
			}
			const auto Replayed = Graph.GetPipelineCacheStats();
			if (Replayed.mGraphicsEntries != Prepared.mGraphicsEntries || Replayed.mHits != Prepared.mHits ||
			    Replayed.mMisses != Prepared.mMisses)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Raster replay changed pipeline cache activity.");
			}
		}
		return {}; // These offscreen textures are released with Graph. Retain Graph in a renderer.
	}

	// END raster-assembly

	// BEGIN compute-assembly
	FArdaRHIStatus RunComputeRecipe(FArdaRHIDeviceRef Device)
	{

		// Readback retains the producer chain and publishes its result after execution.
		FArdaDependencyGraph Graph(Device);
		FArdaGraphEdit Edit(Graph);
		const auto Compute = ComputeWrite(Edit, {{}, 123}).GetBuffer("Output");
		const auto Bytes = Readback(Edit, Compute);

		// Queue this construction, then select or compile its persistent revision.
		if (auto S = Edit.Commit(); !S)
		{
			return S;
		}
		if (auto S = Graph.Compile(); !S)
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
