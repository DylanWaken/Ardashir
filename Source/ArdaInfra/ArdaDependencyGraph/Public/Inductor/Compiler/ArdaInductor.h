/** Compilation entry point for dependency graph schedules and physical allocations. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
	class FArdaDependencyGraph;

	/** Compiler for semantic dependency resolution, CUDA grouping, queue selection and physical memory planning. */
	class FArdaInductor final
	{
	public:
		/** Stage closed graph edits through the graph's persistent construction cache. */
		static FArdaRHIStatus Compile(FArdaDependencyGraph& Graph);

	private:
		friend class FArdaDependencyGraph;
		static FArdaRHIStatus CompilePending(FArdaDependencyGraph& Graph);
	};
}
