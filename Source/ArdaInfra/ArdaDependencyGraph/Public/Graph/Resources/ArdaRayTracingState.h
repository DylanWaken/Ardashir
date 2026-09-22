/** Typed logical ray-shader values used to assemble independent ray-generation dispatches. */
#pragma once
#include "Graph/Resources/ArdaGraphicsState.h"

namespace arda
{
	/** A miss, hit or callable shader declaration. This value does not allocate GPU storage. */
	using FArdaGraphRayShader = TArdaGraphStageValue<EArdaGraphStageValueKind::RayShader>;
}
