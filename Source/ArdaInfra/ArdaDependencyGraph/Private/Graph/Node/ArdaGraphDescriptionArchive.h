#pragma once
#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Node/ArdaDependencyNodeDesc.h"

namespace arda
{
	/** Archive the portable semantic projection; native preparation is reconstructed by the registered node. */
	void WriteArdaGraphDescription(FArdaGraphArchiveWriter& Archive, const FArdaDependencyNodeDesc& Description);
	/** Overlay portable semantics onto a description reconstructed from the registered implementation. */
	void ReadArdaGraphDescription(FArdaGraphArchiveReader& Archive,
	    FArdaDependencyNodeDesc& Description,
	    bool LegacyFeatures = false);
	void WriteArdaGraphRequirements(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyNodeRequirements& Requirements);
	void ReadArdaGraphRequirements(FArdaGraphArchiveReader& Archive,
	    FArdaDependencyNodeRequirements& Requirements,
	    bool LegacyFeatures = false);
}
