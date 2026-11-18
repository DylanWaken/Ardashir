#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"

namespace arda
{
	eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> CloneArdaGraphConstruction(
	    const FArdaDependencyGraph::FArdaImpl& Source,
	    bool bPreservePreprocessing)
	{
		const auto& Authored = !bPreservePreprocessing && Source.mCompilerSource ? *Source.mCompilerSource : Source;
		auto Snapshot = eastl::make_shared<FArdaDependencyGraph::FArdaImpl>();
		Snapshot->mIdentity = Authored.mIdentity;
		Snapshot->mNextResourceGeneration = Authored.mNextResourceGeneration;
		Snapshot->mNextAttachmentOrder = Authored.mNextAttachmentOrder;
		Snapshot->mNextOperationName = Authored.mNextOperationName;
		Snapshot->mOptions = Source.mOptions;
		Snapshot->mTopology = Authored.mTopology.CloneSnapshot();
		Snapshot->mNodePreparations = Authored.mNodePreparations;
		Snapshot->mExternalValues = Authored.mExternalValues;
		Snapshot->mArchiveNodes = Authored.mArchiveNodes;
		Snapshot->mResources = Authored.mResources;
		Snapshot->mCommittedEdits = Authored.mCommittedEdits;
		Snapshot->mResourceGenerations = Authored.mResourceGenerations;
		Snapshot->mResourceGenerationSource = Authored.mResourceGenerationSource;
		Snapshot->mResourceRemaps = Authored.mResourceRemaps;
		Snapshot->mCompiledResourceRemaps = Authored.mCompiledResourceRemaps;
		Snapshot->mNodeRemaps = Authored.mNodeRemaps;
		Snapshot->mGeneratedNames = Authored.mGeneratedNames;
		Snapshot->mImportedBuffers = Authored.mImportedBuffers;
		Snapshot->mImportedTextures = Authored.mImportedTextures;
		Snapshot->mImportedAccelerationStructures = Authored.mImportedAccelerationStructures;
		Snapshot->mResourceAliases = Authored.mResourceAliases;
		Snapshot->mCompiledResourceAliases = Authored.mCompiledResourceAliases;
		Snapshot->mNames = Authored.mNames;
		Snapshot->mResourceNames = Authored.mResourceNames;
		Snapshot->mManualEdges = Authored.mManualEdges;
		Snapshot->mSubgraphAdmissions = Authored.mSubgraphAdmissions;
		Snapshot->mbPreprocessed = Authored.mbPreprocessed;
		if (bPreservePreprocessing)
		{
			Snapshot->mCompilerSource = Source.mCompilerSource;
		}
		// Device configuration is authoring context; no compiled nodes, schedule or runtime are copied.
		Snapshot->mCompile.mExecution->mDevice = Source.mCompile.mExecution->mDevice;
		Snapshot->mCompile.mExecution->mNextFrameSequence = Source.mCompile.mExecution->mNextFrameSequence;
		return Snapshot;
	}
}
