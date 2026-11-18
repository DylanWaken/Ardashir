#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	double NodeCost(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaDependencyNode& Node)
	{
		if (IsDeclaration(Graph, Node))
		{
			return 0;
		}
		const auto Cost = Graph.mCompile.mExecution->mCostOverrides.find(Node.mName);
		return Cost != Graph.mCompile.mExecution->mCostOverrides.end() &&
		        Cost->second.mDefinition.lock() == Node.mDefinition && Cost->second.mCanonicalKey == Node.mCanonicalKey
		    ? Cost->second.mCost
		    : double(Graph.GetNodeDescription(Node).mEstimatedCost);
	}
}
