#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	double NodeCost(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaDependencyNode& Node)
	{
		if (IsDeclaration(Node))
		{
			return 0;
		}
		const auto Cost = Graph.mCostOverrides.find(Node.mName);
		const auto Key = Graph.mCostOverrideKeys.find(Node.mName);
		const auto Definition = Graph.mCostOverrideDefinitions.find(Node.mName);
		return Cost != Graph.mCostOverrides.end() && Key != Graph.mCostOverrideKeys.end() &&
		        Definition != Graph.mCostOverrideDefinitions.end() && Definition->second.lock() == Node.mDefinition &&
		        Key->second == Node.mCanonicalKey
		    ? Cost->second
		    : double(Node.mDesc.mEstimatedCost);
	}
}
