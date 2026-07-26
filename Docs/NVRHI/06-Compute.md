# Compute

[Previous](05-Graphics.md) · [Home](README.md) · Next: [Ray tracing](07-Ray-Tracing.md)

## Minimal compute pass

HLSL:

```hlsl
cbuffer Constants : register(b0) { uint ElementCount; };
StructuredBuffer<float> Input : register(t0);
RWStructuredBuffer<float> Output : register(u0);

[numthreads(64, 1, 1)]
void MainCS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x < ElementCount)
        Output[tid.x] = Input[tid.x] * 2.0;
}
```

C++ setup:

```cpp
auto layout = device->createBindingLayout(
    nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
        .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0))
        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t))));

auto set = device->createBindingSet(
    nvrhi::BindingSetDesc()
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, input))
        .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, output))
        .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t))),
    layout);

auto pipeline = device->createComputePipeline(
    nvrhi::ComputePipelineDesc()
        .setComputeShader(shader)
        .addBindingLayout(layout));
```

Recording:

```cpp
auto state = nvrhi::ComputeState()
    .setPipeline(pipeline)
    .addBindingSet(set);

commandList->setComputeState(state);
commandList->setPushConstants(&elementCount, sizeof(elementCount));
commandList->dispatch((elementCount + 63) / 64, 1, 1);
```

Always bounds-check over-dispatched threads in the shader.

## Buffer and texture compute

For writable buffers set `canHaveUAVs = true`. Choose typed, structured, or raw views deliberately:

- typed views impose a format and typed shader operations;
- structured views use `structStride`;
- raw views expose byte-address access and require `canHaveRawViews`.

For writable textures set `isUAV = true`. A texture UAV binding defaults to one mip and all array slices; explicitly select a mip for mip-generation chains.

Use format capability queries before relying on UAV load, store, or atomic operations.

## UAV ordering

Resource state and memory ordering are separate concerns. Remaining in `UnorderedAccess` avoids a state transition but does not imply one dispatch's writes are visible in the required order to the next.

NVRHI normally adds UAV barriers between successive state-setting calls that use the same UAV. A common sequence:

```cpp
commandList->setComputeState(passA);
commandList->dispatch(...);
commandList->setComputeState(passB); // automatic UAV barrier if required
commandList->dispatch(...);
```

If the same state stays bound while constants change, an automatic barrier may not be inserted. Use:

```cpp
nvrhi::utils::BufferUavBarrier(commandList, buffer);
// or textureUavBarrier
```

Disable per-resource UAV barriers only when accesses are independent:

```cpp
commandList->setEnableUavBarriersForBuffer(buffer, false);
// independent work
commandList->setEnableUavBarriersForBuffer(buffer, true);
```

This is a proof-based optimization. A missing UAV barrier can create intermittent corruption.

## Indirect dispatch

Create an argument buffer with `isDrawIndirectArgs`, fill a `DispatchIndirectArguments` record, and bind it:

```cpp
auto state = nvrhi::ComputeState()
    .setPipeline(pipeline)
    .addBindingSet(set)
    .setIndirectParams(indirectArgs);
commandList->setComputeState(state);
commandList->dispatchIndirect(byteOffset);
```

If a preceding compute pass wrote the arguments, order UAV writes and transition the argument buffer to `IndirectArgument` before dispatch.

## Graphics/compute interaction

On one graphics queue, automatic state tracking can transition a resource:

```text
graphics SRV → compute UAV → graphics SRV
```

State setting inserts transitions and UAV ordering where it has complete binding information. Bindless resources need explicit treatment.

For producer/consumer work on different queues:

1. Record a command list with `queueType = Compute`.
2. Submit to `CommandQueue::Compute`; keep the returned instance ID.
3. Call `queueWaitForCommandList(Graphics, Compute, instance)`.
4. Submit the consuming graphics work.

Queue waits establish execution dependency. Resource state ownership/transition details still need a coherent policy; keep cross-queue resources in compatible common/permanent states or use explicit transitions supported by the backends.

Check `Feature::ComputeQueue`; otherwise run compute on the graphics queue.

## Async-compute suitability

Good candidates:

- no immediate graphics dependency;
- enough work to amortize synchronization;
- limited contention for the same bandwidth-heavy resources;
- independent resources or clean queue boundaries.

Poor candidates:

- tiny dispatches;
- work immediately consumed by graphics;
- heavy contention with the graphics pass;
- frequent cross-queue transitions;
- algorithms requiring CPU readback.

Measure GPU overlap with timer queries and an external profiler. A separate queue does not guarantee parallel hardware execution.

## Mip generation recipe

For each destination mip:

1. Bind mip `i - 1` as texture SRV.
2. Bind mip `i` as texture UAV.
3. Dispatch over mip `i` dimensions.
4. Establish UAV ordering before using that mip as the next source.

Use separate immutable sets per mip pair or cache them. Automatic tracking works at texture subresource granularity, but be explicit about subresource sets to avoid transitioning unrelated mips.

## Prefix sums, compaction, and GPU-driven output

Multi-pass compute algorithms often reuse buffers as UAVs. Keep their state as `UnorderedAccess` between phases and issue UAV barriers. At the end:

- transition compacted data to `ShaderResource` for graphics reads;
- transition generated argument buffers to `IndirectArgument`;
- transition generated vertex/index buffers to their respective states if consumed as fixed-function inputs.

Keep counters in dedicated ranges or buffers with clear ownership. Reset counters with `clearBufferUInt` or a small dispatch before reuse.

## Cooperative vectors

Cooperative vectors expose hardware-accelerated matrix/vector operations intended for neural rendering and inference/training shader paths.

Capability queries:

```cpp
bool inference = device->queryFeatureSupport(
    nvrhi::Feature::CooperativeVectorInferencing);
bool training = device->queryFeatureSupport(
    nvrhi::Feature::CooperativeVectorTraining);

auto support = device->queryCoopVecMatMulFormatSupport(combo);
auto trainingSupport =
    device->queryCoopVecTrainingFormatSupport(nvrhi::coopvec::DataType::Float16);
```

Data types include integer, packed integer, FP8, FP16, BF16, FP32, and FP64 categories. Actual supported combinations are device-specific; query the exact `MatMulFormatCombo`.

Matrix layouts:

- row-major;
- column-major;
- inferencing-optimal;
- training-optimal.

Use `getCoopVecMatrixSize` for device layout allocation. Convert matrices on the GPU:

```cpp
nvrhi::coopvec::ConvertMatrixLayoutDesc conversion;
conversion.src = {/* buffer, offset, type, RowMajor, size, stride */};
conversion.dst = {/* different buffer, offset, type, InferencingOptimal, size, 0 */};
conversion.numRows = rows;
conversion.numColumns = columns;
commandList->convertCoopVecMatrices(&conversion, 1);
```

Source and destination buffers must differ. Required states are `ConvertCoopVecMatrixInput` and `ConvertCoopVecMatrixOutput`; automatic barriers can derive them from the conversion command.

The NVRHI C++ API provides capability, sizing, and layout conversion. Shader-side cooperative-vector syntax comes from the enabled D3D12/Vulkan shader extensions and compiler toolchain.

## Compute performance practices

- Choose thread-group dimensions for the target algorithm, not by habit.
- Keep shader bounds checks for ceil-divided grids.
- Batch related dispatches under stable pipeline and sets.
- Use push constants for tiny high-frequency data; volatile CBs for larger changing blocks.
- Avoid rebuilding binding sets every dispatch.
- Use subresource-specific UAV bindings for mip/array workflows.
- Remove UAV barriers only after proving independence.
- Keep readback delayed and frame-buffered.
- Prefer GPU-produced indirect work counts over CPU synchronization.
- Profile occupancy, bandwidth, cache behavior, and queue overlap on every target API.

## Compute correctness checklist

- UAV creation flags and format support are present.
- Layout type exactly matches the shader declaration.
- Dispatch dimensions and shader group size agree.
- Every inter-dispatch dependency has UAV or transition ordering.
- Indirect arguments are in the correct binary layout and state.
- Async queue support is queried and waits point in the right direction.
- Bindless resources have manual state and lifetime management.
- Cooperative-vector format/layout support is queried for the exact operation.

