#pragma once

#include "ArdaRenderGraphDefinitions.h"
#include "ArdaRenderGraphResources.h"
#include "ArdaRenderGraphParameters.h"
#include "ArdaRenderGraphPass.h"
#include "ArdaRenderGraphBlackboard.h"
#include "ArdaRenderGraphBuilder.h"

namespace arda::render_graph
{
    /** Returns the stable name of the render-graph module. */
    [[nodiscard]] const char* GetModuleName() noexcept;
}
