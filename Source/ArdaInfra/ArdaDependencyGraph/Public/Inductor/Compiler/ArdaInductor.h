/** Compilation entry point for dependency graph schedules and physical allocations. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "Inductor/Compiler/ArdaInductorCompileResult.h"

namespace arda
{
	class FArdaDependencyGraph;
	struct FArdaInductorExecutionState;

	/** Compiler for semantic dependency resolution, CUDA grouping, queue selection and physical memory planning. */
	class FArdaInductor final
	{
	public:
		/** Read a preprocessed graph and return a self-contained execution plan without changing the graph.
		 * Use Graph.Preprocess() before this standalone call. Graph.Compile() combines preprocessing,
		 * revision caching and compilation on the caller.
		 */
		[[nodiscard]] static TArdaRHIResult<FArdaInductorCompileResult> Compile(const FArdaDependencyGraph& Graph);
		/** Compile an offline graph for a destination device without changing its authoring context. */
		[[nodiscard]] static TArdaRHIResult<FArdaInductorCompileResult> Compile(const FArdaDependencyGraph& Graph,
		    FArdaRHIDeviceRef Device);

	private:
		friend class FArdaDependencyGraph;
		static TArdaRHIResult<FArdaInductorCompileResult> CompileWithContext(const FArdaDependencyGraph& Graph,
		    const FArdaInductorExecutionState* Context,
		    FArdaRHIDeviceRef Device = {});
		static FArdaRHIStatus CompilePending(FArdaDependencyGraph& Graph,
		    const FArdaInductorExecutionState* Context = nullptr);
	};
}
