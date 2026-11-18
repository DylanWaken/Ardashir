/** Final native-state and draw-range proof for compiler-approved instancing candidates. */
#pragma once

#include "Graph/Resources/ArdaGraphicsState.h"

namespace arda
{
	/** Merge only identical native geometry/state and adjacent nonempty instance ranges.
	 * Caller must establish the compiler's stage-purity, pipeline and logical-binding proof first.
	 * Failure leaves Accumulated unchanged; success changes only its instance count.
	 */
	bool TryMergeArdaGraphicsInstances(FArdaGraphicsWork& Accumulated, const FArdaGraphicsWork& Next) noexcept;
}
