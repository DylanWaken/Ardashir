#pragma once
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphArchive.h"

namespace arda
{
	FArdaRHIStatus ValidateArdaGraphExternalBindings(const FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus WriteArdaGraphExternalValues(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus ReadArdaGraphExternalValues(FArdaGraphArchiveReader& Archive,
	    FArdaDependencyGraph::FArdaImpl& Graph);
	void ConfigureArdaGraphExternalWriter(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyGraph::FArdaImpl& Graph);
	void ConfigureArdaGraphExternalReader(FArdaGraphArchiveReader& Archive, FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus RebindArdaGraphExternalParameters(FArdaDependencyGraph::FArdaImpl& Graph);
	FArdaRHIStatus CaptureArdaGraphExternalParameters(FArdaDependencyGraph::FArdaImpl& Graph);
}
