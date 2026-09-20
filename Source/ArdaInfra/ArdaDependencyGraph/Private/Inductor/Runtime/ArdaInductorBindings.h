#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Inductor/Runtime/ArdaInductorFrame.h"

namespace arda
{
	FArdaRHIResourceRef Physical(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    FArdaDependencyResourceHandle R,
	    const FArdaRHISamplerRef& Sampler);

	bool MatchesSchema(const FArdaDependencyShaderBindings& B, const FArdaRHIBindingLayoutRef& Layout);

	FArdaRHIBindingSetDesc MakeSet(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    const FArdaDependencyShaderBindings& B,
	    const FArdaRHIBindingLayoutRef& Layout);

	FArdaRHIStatus PrepareRayTable(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    uint32_t NodeId,
	    const FArdaDependencyNode& Node,
	    const eastl::string& Slot,
	    const FArdaRHIRayTracingPipelineRef& Pipeline);

	FArdaRHIStatus PrepareArdaShaderBindings(const FArdaDependencyGraph::FArdaImpl& Graph, FArdaInductorFrame& Frame);
}
