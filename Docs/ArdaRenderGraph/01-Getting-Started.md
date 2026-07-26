# 1. Getting started

[← Documentation home](README.md) · [Next: Core concepts →](02-Core-Concepts.md)

## Prerequisites

You need:

- a C++17 target linked to `Ardashir::ArdaRenderGraph`;
- an initialized `nvrhi::IDevice`;
- accurate graphics, compute, and copy queue capabilities for that device; and
- any NVRHI shaders, binding sets, and pipelines your pass callbacks use.

See the
[NVRHI programming guide](https://github.com/NVIDIA-RTX/NVRHI/blob/main/doc/ProgrammingGuide.md)
for NVRHI device and pipeline setup. `ArdaBackend` can supply the device and
queue capabilities, but ArdaRenderGraph only depends on the resulting context.

## Create the context

`FARDGRenderGraphContext` is copied into the builder and remains immutable:

```cpp
FARDGRenderGraphContext MakeGraphContext(
    nvrhi::DeviceHandle Device,
    const FARDGQueueCapabilities& Capabilities)
{
    FARDGRenderGraphContext Context;
    Context.mDevice = Device;
    Context.mQueueCapabilities = Capabilities;
    return Context;
}
```

`mbGraphics` must be true. `mbCompute` and `mbCopy` describe distinct queues
that may be selected. They do not mean that compute or copies are impossible
when false: eligible passes deterministically fall back to graphics.

A device is not needed for declaration or `Compile()`, which is useful in unit
tests and tooling. It is required by `Execute()`.

## Minimal compute/buffer graph

This complete-style example creates a logical buffer, declares a UAV write,
clears it, extracts the physical handle, and asks for `CopySource` at graph
exit:

```cpp
#include "ArdaRenderGraph.h"

using namespace arda::render_graph;

ARDG_BEGIN_PARAMETER_STRUCT(FClearBufferParameters)
    ARDG_BUFFER_ACCESS(mOutput)
ARDG_END_PARAMETER_STRUCT()

nvrhi::BufferHandle BuildAndSubmitClear(nvrhi::DeviceHandle Device)
{
    FARDGRenderGraphContext Context;
    Context.mDevice = Device;
    Context.mQueueCapabilities.mbGraphics = true;

    FARDGBuilder Graph(Context);

    nvrhi::BufferDesc Desc;
    Desc.setDebugName("Example output")
        .setByteSize(1024)
        .setCanHaveUAVs(true);
    FARDGBufferRef Output = Graph.CreateBuffer(Desc);

    FClearBufferParameters Parameters;
    Parameters.mOutput = {
        Output,
        nvrhi::ResourceStates::UnorderedAccess,
        nvrhi::EntireBuffer
    };

    (void)Graph.AddPass(
        "Clear output",
        &Parameters,
        EARDGPassFlags::Compute,
        [](FARDGPassExecutionContext& PassContext,
           const FClearBufferParameters& Frozen)
        {
            PassContext.mCommandList.clearBufferUInt(
                PassContext.GetBuffer(Frozen.mOutput.mBuffer),
                0x12345678u);
        });

    nvrhi::BufferHandle Extracted;
    Graph.QueueBufferExtraction(
        Output,
        Extracted,
        nvrhi::ResourceStates::CopySource);

    const FARDGExecutionResult& Result = Graph.Execute();
    // Submission is complete, not necessarily GPU execution.
    (void)Result;
    return Extracted;
}
```

Why each part matters:

- `ARDG_BUFFER_ACCESS` tells the graph which buffer is used, in which state, and
  over which range.
- `setCanHaveUAVs(true)` is required by validation for unordered access.
- `GetBuffer` validates that this pass declared the requested logical buffer.
- Extraction makes the output observable, so the producing pass is not culled.
- The epilogue transitions the extracted buffer to `CopySource`.
- `Execute()` submits asynchronously. Wait with your normal NVRHI/device
  synchronization when CPU or subsequent external work requires completion.

Stack parameters are safe. `AddPass` copies ("freezes") them into graph-owned
arena storage before returning. `AllocateParameters<T>()` is also available
when direct graph-arena construction is more convenient.

## Lifecycle

![Graph lifecycle](assets/graph-lifecycle.svg)

A builder has a one-way lifecycle:

### 1. Building

You may create/import resources and views, allocate parameters, mutate the
blackboard, add passes and dependencies, and queue extraction.

### 2. Compiled

`Compile()` validates and freezes topology, appends the epilogue sentinel,
culls dead work, and produces queue, lifetime, barrier, and raster-group data.
Calling `Compile()` again returns the same immutable result. Building mutations
now throw.

### 3. Executed

`Execute()` compiles if needed, materializes physical resources, records and
submits command lists, fills extraction outputs, and runs device garbage
collection. A builder executes at most once.

If compilation or execution throws, the graph enters a failed state. It cannot
be compiled or executed again; discard it and build a new graph.

## Inspect without executing

```cpp
const FARDGCompileResult& Compiled = Graph.Compile();
std::string Description = Graph.DumpGraph();
```

`DumpGraph()` is deterministic and includes execution order, resources, pass
pipelines and culling, dependencies, transitions, lifetimes, raster groups, and
cross-queue edges. It requires a compiled graph.

## Build and test

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Set `ARDASHIR_BUILD_TESTS=OFF` to omit tests. The CPU-only render-graph tests
exercise compilation without a device; backend-dependent execution tests skip
when a supported GPU backend or queue is unavailable.

---

[← Documentation home](README.md) · [Next: Core concepts →](02-Core-Concepts.md)
