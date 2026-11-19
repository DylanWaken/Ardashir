# Offline model export

This toolchain compiles supported static models into an `.ardagpu` model descriptor:
PTX, kernel parameter layouts, launch dimensions, buffer allocation sizes, tensor
ports, an ordered dispatch/fill/copy program, and typed, named state slots. A separate
`.ardastate` checkpoint stores parameter and persistent-buffer values. Graph constants
that are not model state remain in the descriptor.
Arda imports those commands into its ordinary dependency graph. The package contains
no ONNX session, PyTorch code, IREE VM bytecode, TensorRT engine, or vendor math-library
calls. GPU execution requires the CUDA driver.

The implementation is a bounded compiler adapter, not universal model coverage.
IREE 3.11.0 is pinned because its CUDA executable metadata and HAL operations are part
of this adapter's input contract. Unsupported operations fail export. Extending
coverage means extending or upgrading the offline compiler and validating its output;
it does not require adding a C++ graph node class for each ML operator.

## Implemented and remaining

| Area | Available now | Remaining |
| --- | --- | --- |
| Runtime graph | Data-defined kernels, exact argument layouts, declared buffer effects and ordinary graph scheduling | Additional execution providers; universal model/operator coverage |
| Weights | Separate named checkpoints, strict state-dict loading and initialized persistent buffers | Larger/streamed checkpoints and dynamic-shape refitting |
| Tensor interfaces | Backend tensor metadata and buffer views; static dense FP32/FP16/BF16/FP8/INT8/INT32 ports | Dynamic dimensions, arbitrary strided ports and broader operator/precision coverage |
| Architectures | One package for each supported compiler target, with an export manifest | Automatic runtime variant selection and validation on more deployment GPUs |
| Renderer integration | OIDN and NPPD Cornell examples on D3D12 and Vulkan, including temporal state | Continuous moving-camera reprojection in the Cornell temporal example |

This is offline model compilation. It does not extract every kernel and hidden
library dependency from an existing `torch.compile` model, CUDA Graph capture or
TensorRT engine. Unsupported operators and data-dependent host behavior fail export.
The runtime has no special model graph: imported commands and existing graphics
operations share the same registry, graph edits, resource identities and executor.

## Environment

Use Python 3.12 on Windows or another platform supported by the compiler wheel:

```powershell
python -m venv .venv/model-export
.venv/model-export/Scripts/python -m pip install -r Tools/ModelExport/requirements.txt
```

The Python model frontend and pretrained model adapters additionally use PyTorch
offline. CPU PyTorch is sufficient:

```powershell
.venv/model-export/Scripts/python -m pip install torch==2.14.1 --index-url https://download.pytorch.org/whl/cpu
.venv/model-export/Scripts/python -m pip install -e Tools/ModelExport
```

No Python environment is deployed with `.ardagpu` and `.ardastate`. `ArdaCudaReplay.py` is an independent
validation tool: its execution class uses Python's standard library and `nvcuda.dll`
(or `libcuda.so.1`). The CLI uses NumPy for reference arrays and error measurements.

Compilation does not require the export machine to have a CUDA GPU. Numerical
driver replay requires a compatible NVIDIA GPU and installed CUDA driver. Use the
same virtual environment for export and validation; the C++ application only loads
the resulting `.ardagpu` descriptor and its `.ardastate` checkpoint.

## Export a PyTorch module

The API follows the call convention of `torch.onnx.export`: pass the model, actual
example arguments, and an optional output destination. Tensor shapes and dtypes
come from those arguments. The current parameters and persistent buffers become the
initial checkpoint; they remain replaceable runtime inputs to the compiled graph.
For example:

```python
import torch
import arda

model = torch.nn.Sequential(
    torch.nn.Conv2d(3, 8, kernel_size=3, padding=1),
    torch.nn.ReLU(),
    torch.nn.Conv2d(8, 3, kernel_size=1),
).half()
image = torch.ones(1, 3, 32, 32, dtype=torch.float16)
result = arda.export(
    model,
    (image,),
    ".cache/model-export/torch-api",
    target_archs=["sm_80", "sm_86"],
    input_names=["image"],
    output_names=["filtered_image"],
    name="image_filter",
    verify=True,
    rtol=0.005,
    atol=0.002,
)
print(result.manifest_path)
print(result.descriptors["sm_80"])
print(result.checkpoints["sm_80"])
```

This untrained example demonstrates packaging, not denoising quality. The
[runnable version](Examples/ArdaExportTorchModule.py) uses this model:

```powershell
.venv/model-export/Scripts/python Tools/ModelExport/Examples/ArdaExportTorchModule.py --verify
```

The public signature is:

```python
arda.export(model, args=(), f=None, *, kwargs=None,
            target_archs=("sm_80",), input_names=None, output_names=None,
            name=None, opset_version=22, keep_ir=False, verify=False,
            rtol=1e-4, atol=1e-5)
```

| Argument | Meaning |
| --- | --- |
| `model` | An eager `torch.nn.Module` with its desired weights loaded. Capture runs on a CPU copy in evaluation mode. |
| `args`, `kwargs` | The actual `forward` call's positional and keyword arguments. Nested tuples, lists and string-keyed dictionaries are supported. Tensor leaves define ports; non-tensor scalar leaves are frozen configuration. |
| `f` | A bundle **directory**, not a single ONNX file. Omit it to retain the bundle in temporary storage owned by the result. |
| `target_archs` | Nonempty CUDA compiler target list, such as `[80, "sm_86"]`. Each target is compiled separately. |
| `input_names`, `output_names` | Unique names for the flattened tensor ports. They do not reorder or change the model call. |
| `keep_ir` | Retain intermediate compiler IR for inspection. |
| `verify`, `rtol`, `atol` | Run every variant against the CPU reference through an isolated CUDA-driver process and specify comparison tolerances. Integer outputs compare exactly. |

A tensor can be passed directly as `args` for a single input. Multiple arguments
use a tuple. For `forward(image, history, *, strength=0.1)`, write:

```python
class TemporalExample(torch.nn.Module):
    def forward(self, image, history, *, strength=0.1):
        return image + strength * history[:, :3]

with arda.export(
    TemporalExample(),
    (torch.ones(1, 3, 64, 64), torch.zeros(1, 38, 64, 64)),
    kwargs={"strength": 0.1},
    target_archs=["sm_80"],
    input_names=["current_frame", "previous_state"],
    output_names=["filtered_image"],
) as temporary:
    result = temporary.save(".cache/model-export/temporal-example")
```

`f=None` leaves files available while the result is alive. `result.save(directory)`
publishes a complete copy and returns persistent paths. `close()` or a context
manager releases only the result's owned temporary files; saved bundles remain.
`ExportResult` exposes `name`, `manifest_path`, `onnx_path`, `reference_path`,
`artifacts`, `descriptors`, and `checkpoints` (keyed by architecture). `packages` remains
an alias for `descriptors`. Each artifact exposes `target_sm`, `descriptor_path`,
`checkpoint_path`, `package_path` and `metadata_path`.

Input tensors must have rank at least one, fixed positive dimensions and contiguous
dense storage. Repeated tensor objects or tensor arguments sharing storage are
rejected by this frontend; supply independent clones when those values should be
separate ports.
Inputs and outputs can have different supported dtypes. Tensor leaves in nested
inputs/outputs flatten into named ports; the manifest records their original paths.
Changing temporal state belongs in explicit tensor inputs/outputs, as in NPPD.
Stateful forward mutation and implicit random operations are rejected. The exporter
preserves the caller's model, gradients, training flags and RNG state.

Strict functional capture, comparison against a pristine eager CPU reference,
and compiler closure checks always run. `verify=True` adds local GPU numerical
validation; `verify=False` does not establish numerical correctness on a deployment
GPU. Supply representative finite inputs and choose tolerances appropriate to the
model's precision. One successful example does not prove every possible input.
Unsupported operators, shapes, dtypes or architecture combinations fail export.

For `name="image_filter"`, the bundle contains:

```text
image_filter.manifest.json
image_filter.onnx
image_filter.reference.npz
image_filter.sm_80.ardagpu
image_filter.sm_80.ardastate
image_filter.sm_80.json
image_filter.sm_86.ardagpu
image_filter.sm_86.ardastate
image_filter.sm_86.json
```

Verification adds per-target `.actual.npz` and `.actual.json` results. Only the
selected `.ardagpu` and a compatible `.ardastate` are needed at runtime. All requested variants are staged before
publication; a failed compile does not publish a partial bundle.

## Load and replace model state

Capture and compile the structure once, then export another checkpoint without
running the compiler:

```python
model.load_state_dict(other_weights)
checkpoint = arda.export_state_dict(model, result, "alternative.ardastate")
# A mapping such as model.state_dict() is also accepted.
```

With an `ExportResult`, the helper checks the original model's full state schema and
selects the persistent tensors used by the descriptor. With a descriptor path, pass
exactly its named state slots. Missing/extra keys, different shapes or dtypes fail;
values are never implicitly cast. Changing topology, static input shapes, precision,
or frozen forward configuration requires a new descriptor.

```cpp
const auto Descriptor = FArdaGraphPackage::Load("image_filter.sm_80.ardagpu");
if (!Descriptor) return Descriptor.mStatus;
const auto Model = Descriptor.mValue->LoadStateDict("alternative.ardastate");
if (!Model) return Model.mStatus;
const auto Invocation = Model.mValue->Invoke(Edit, {{"image", Image}}, "Filter");
if (!Invocation) return Invocation.mStatus;
```

`GetStateSlots()` reflects the state schema; `HasStateDict()` reports readiness.
`LoadStateDict` and `DeserializeStateDict` return a new immutable loaded instance.
Loading a different checkpoint does not mutate previously recorded invocations.
Record the new instance in the next graph edit to use its weights. A descriptor
with required state cannot be invoked before loading it. The descriptor is a
reusable subgraph recipe: `Invoke` joins its operations and resources into the
parent edit, including when called from a typed subgraph's `Build` method.

The ONNX compiler honors `arda.state_dict` metadata (a JSON list of initializer
names). The PyTorch, OIDN and NPPD exporters emit it and preserve named state before
compilation. Generic ONNX without this metadata treats every initializer as state;
mark structural initializers explicitly by excluding them from the list. A state
value required for compile-time shape/control-flow specialization fails compilation.
MLIR inputs have no source state mapping and export only structural constants.

`ArdaIreeExport.py` writes a sibling `.ardastate` by default; `--state-output` chooses
another path. `ArdaCudaReplay.py --state-dict alternative.ardastate` selects a
checkpoint, defaulting to the sibling file. Legacy version-one packages retain
their embedded snapshot behavior and cannot be refitted.

## Precision and architecture

The frontend preserves the model and tensors' existing precision; it does not cast
weights, calibrate quantization scales, invent accumulators or quantize a model.
Use `model.half()` and FP16 inputs explicitly for a half-precision model. BF16 models
use `model.bfloat16()` and BF16 arguments. The default ONNX opset is 22, which
includes BF16 convolution; an explicitly requested older opset is not upgraded.
Integer models must express conversion,
scaling and accumulation explicitly, for example `x.to(torch.int32) * 3` for signed
INT8 inputs with INT32 results.

| Tensor dtype | Minimum requested CUDA target under export policy |
| --- | --- |
| FP32, INT32 | Any audited target |
| FP16 | SM53; audited compiler targets currently start at SM60 |
| BF16 | SM80 |
| FP8 E4M3FN and E5M2 | SM89 |
| INT8 | SM61 |

The policy checks parameters, inputs, outputs and observed internal dtypes. It is
separate from Backend storage: a device can hold typed bytes without providing a
particular arithmetic instruction. A supported dtype does not guarantee Tensor
Core use, accelerated matrix multiplication, or support for every operator.

FP8 currently uses the pinned compiler's **bit-arithmetic conversion lowering**
with two audited corrections for negative NaNs and subnormal rounding carry. It
retains one-byte FP8 storage; it is not a promise of native FP8 arithmetic speed.
ONNX Cast saturation is preserved. The PyTorch frontend checks known cast provenance
and the installed PyTorch conversion behavior before encoding the matching
saturation attribute. FP8 requires ONNX opset 19 or newer.

For explicit FP8 weights and scales, a small model can express dequantization as:

```python
class ScaledFp8Linear(torch.nn.Module):
    def __init__(self, weights, weight_scale):
        super().__init__()
        self.register_buffer("weights", weights.to(torch.float8_e4m3fn))
        self.register_buffer("weight_scale", weight_scale.float())

    def forward(self, values, input_scale):
        return (values.float() * input_scale) @ (
            self.weights.float() * self.weight_scale
        ).T

model = ScaledFp8Linear(torch.ones(8, 16), torch.tensor(0.25))
values = torch.ones(2, 16).to(torch.float8_e4m3fn)
result = arda.export(model, (values, torch.tensor([0.5])),
                     ".cache/model-export/fp8-linear", target_archs=["sm_89"])
```

Here the FP32 matmul and scales are explicit model operations. Selecting a storage
dtype alone does not create a quantization algorithm or an FP8 Tensor Core kernel.
This snippet was compiled for SM89 and passed CUDA-driver reference verification.

`arda.supported_architectures()` returns `sm_60`, `sm_61`, `sm_62`, `sm_70`, `sm_72`,
`sm_75`, `sm_80`, `sm_86`, `sm_87`, `sm_89`, `sm_90`, and `sm_120`. The first nine use
IREE 3.11's built-in target profiles. SM89/90/120 use explicit conservative SIMT
profiles with PTX 8.4/8.4/8.7 respectively; emitted PTX is checked against the exact
requested SM. Unsupported targets fail instead of substituting an older architecture.
Profiles do not establish a tested GPU/driver matrix. The manifest lists compiled
variants; the application still selects the compatible `.ardagpu` itself.

The audited target bridge follows IREE's [CUDA target implementation](https://github.com/iree-org/iree/blob/v3.11.0/compiler/plugins/target/CUDA/CUDATarget.cpp),
[GPU target attributes](https://github.com/iree-org/iree/blob/v3.11.0/compiler/src/iree/compiler/Codegen/Dialect/GPU/IR/IREEGPUAttrs.td),
and NVIDIA's [PTX instruction and target definitions](https://docs.nvidia.com/cuda/parallel-thread-execution/).

Earlier FP32 public-API validation loaded the pinned OIDN checkpoint and exported
nonzero lognormal `[1,3,64,64]` inputs. Its 39-kernel, 61-command SM80 package matched
CPU PyTorch with maximum absolute error `5.72205e-6` and passed the native D3D12 and
Vulkan [`ExportedModelMatchesOfflineReference` tests](../../Source/ArdaInfra/ArdaDependencyGraph/Tests/ArdaRuntimeNodeTests.cpp).
Precision-specific compiler tests now check typed FP16/BF16/INT8/FP8 packages and
exact target emission. Driver tests cover both FP8 formats' 256 encodings, signed
zero, subnormals, rounding midpoints and neighboring FP32 values, overflow, NaN,
infinity and both ONNX saturation modes.

The [precision export example](Examples/ArdaExportPrecisionModels.py) produces
small typed fixtures. Ten packages passed public-API driver verification and native
D3D12/Vulkan execution: FP16/BF16 on SM80, each FP8 format on SM89/90/120, and
INT8 inputs with INT32 accumulation on SM61/80. These are correctness checks;
they do not measure model throughput or prove Tensor Core instruction selection.
FP16 and BF16 Conv/ReLU/Conv models also matched their CPU references for nonzero
inputs; BF16 used opset 22. An isolated installed wheel exported and driver-verified
mixed FP16/BF16/FP8/INT8 tensor ports for SM89 and SM120.

## Load a package in the existing C++ graph

Given the path printed above, an initialized CUDA-capable graphics `Device`, and
the packed input bytes matching the reflected dtype (FP16 for the example above) in `ImageBytes` (`eastl::vector<uint8_t>`), the
application can write:

```cpp
#include "ArdaDependencyGraph.h"
#include "Graph/Node/ArdaGraphPackage.h"

const auto Descriptor = FArdaGraphPackage::Load(PackagePath);
if (!Descriptor) return Descriptor.mStatus;
const auto Package = Descriptor.mValue->LoadStateDict(CheckpointPath);
if (!Package) return Package.mStatus;
const FArdaGraphPackageTensor* Port = nullptr;
for (const auto& Tensor : Package.mValue->GetTensors())
    if (Tensor.mName == "image" && Tensor.mRole == 1) Port = &Tensor;
if (!Port)
    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Missing image input.");

FArdaDependencyGraph Graph(Device);
FArdaGraphEdit Edit(Graph);
FArdaRHIBufferDesc Storage;
Storage.mByteSize = Port->mStorageByteSize;
Storage.mbCudaInterop = true;
const auto Buffer = AllocateInitializedBuffer(Edit, Storage, ImageBytes, "image");
const auto Model = Package.mValue->Invoke(Edit, {{"image", {Buffer, Port->mDesc}}});
if (!Model) return Model.mStatus;
const auto* Output = Model.mValue.Find("filtered_image");
if (!Output)
    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Missing model output.");
Edit.Keep(Output->mBuffer); // Or attach a graphics consumer to this tensor's buffer.
if (auto Status = Edit.Commit(); !Status) return Status;
if (auto Status = Graph.Compile(); !Status) return Status;
return Graph.Execute().mStatus;
```

The snippet is inside an application function using namespace `arda`; `ImageBytes`
must initialize the complete declared input view. For a live renderer, bind an
existing interop buffer or produce the input with an ordinary graph packing node
instead of initializing constant input bytes. `Invoke` allocates omitted outputs
and adds the compiled operations to this same edit.

Tensor ports can share storage and have nonzero byte offsets. When supplying output
buffers yourself, use `mStorageByteSize` for capacity and preserve each `mDesc` and
shared `mBuffer` identity. The NPPD image and next-state outputs demonstrate this
case. See the [graph runtime guide](../../Source/ArdaInfra/ArdaDependencyGraph/README.md#runtime-kernels-tensor-ports-and-model-packages)
and [Cornell example](../../Source/ArdaTests/Examples/CornellDenoising/README.md).

## OIDN spatial HDR example

```powershell
.venv/model-export/Scripts/python Tools/ModelExport/ArdaOidnExport.py --download --height 64 --width 64 --output .cache/model-export/oidn/oidn_hdr_small_64.onnx --reference .cache/model-export/oidn/oidn_hdr_small_64_reference.npz
.venv/model-export/Scripts/python Tools/ModelExport/ArdaIreeExport.py .cache/model-export/oidn/oidn_hdr_small_64.onnx .cache/model-export/oidn/oidn_hdr_small_64.ardagpu --keep-ir
.venv/model-export/Scripts/python Tools/ModelExport/ArdaCudaReplay.py .cache/model-export/oidn/oidn_hdr_small_64.ardagpu .cache/model-export/oidn/oidn_hdr_small_64_reference.npz --rtol 0.0001 --atol 0.00001
```

The OIDN adapter downloads the official 634 KB `rt_hdr_small.tza` checkpoint and checks
its pinned SHA256. It includes HDR exposure and transfer functions. Input `noisy_hdr`
and output `denoised_hdr` are linear HDR float32 NCHW tensors. This checkpoint is spatial;
it does not consume motion vectors or temporal state. Dimensions must be multiples of
16. See the adjacent provenance JSON and `ArdaOidn.LICENSE.txt`.

Validated on Windows, RTX PRO 6000 Blackwell, CUDA driver 610.62: 64x64 and 256x256
exports each contain 39 kernels and 61 commands. Driver-only replay against the CPU
PyTorch adapter produced maximum absolute errors of 3.58e-6 and 4.89e-6 respectively.
Those are numerical reference checks on the fixed-seed test inputs, not image-quality
scores or a claim of identical output from the complete OIDN runtime.

## NPPD temporal model

```powershell
.venv/model-export/Scripts/python Tools/ModelExport/ArdaNppdExport.py --height 64 --width 64
.venv/model-export/Scripts/python Tools/ModelExport/ArdaIreeExport.py .cache/model-export/nppd/nppd_small_2spp_64x64.onnx .cache/model-export/nppd/nppd_small_2spp_64x64.ardagpu --keep-ir
.venv/model-export/Scripts/python Tools/ModelExport/ArdaCudaReplay.py .cache/model-export/nppd/nppd_small_2spp_64x64.ardagpu .cache/model-export/nppd/nppd_small_2spp_64x64_reference.npz
```

The adapter downloads only the official `small_2_spp` checkpoint member through HTTP
Range requests (171 MB compressed), checks ZIP CRC and a pinned SHA256, and loads
tensors with `torch.load(weights_only=True)`. It downloads two source files at a pinned
revision and verifies their hashes. No training or reference dataset is downloaded.
The inference adapter removes the training framework and exposes state explicitly;
the original 15,593,341 pretrained inference parameters are loaded strictly.

Inputs are color, depth, normal, diffuse, motion, current and previous crop offsets,
and temporal state. Per-sample features have shape `B,C,H,W,S` with fixed `B=1,S=2`.
Motion uses Noisebase's screen-space pixel displacement convention with IJ channel
order. Normals contain dot products with camera forward, right, up in that order;
depth is `log(1 + 1 / distance_to_camera)`. Diffuse is the per-sample material diffuse
color. These transforms follow the pinned Noisebase dataset loader. State is NCHW
with 38 channels: previous color3, previous output3, features32.
Outputs are `denoised_hdr` and `next_temporal`. Feed the next state into the next frame;
reset it on scene cuts. The reference includes nonzero history and subpixel motion.
The caller must supply the actual per-sample renderer features; noisy RGB alone is
insufficient. The static adapter requires dimensions of at least 64 and multiples of 32.
See `ArdaNppd.LICENSE.txt` and the generated provenance JSON.
The camera reprojection helpers follow Noisebase; its license is preserved in
`ArdaNoisebase.LICENSE.txt`.

The adapter's outputs are checked bitwise against the original pinned `Model.step`
on the exported reference case. Both 64x64 and 256x256 compiled packages were replayed
through the CUDA driver, each with 169 kernels and 348 commands. Maximum absolute
errors versus CPU PyTorch were 1.55e-6/1.31e-6 for HDR output and 4.77e-6/5.72e-6 for
temporal state. The `_raw` directory contains named float32 buffers and `tensors.json`;
it also contains second-frame references for graph-level state feedback testing.

For a real Cornell renderer dump, compare the exact GPU inputs and outputs with the
original authors' CPU inference method:

```powershell
.venv/model-export/Scripts/python Tools/ModelExport/ArdaValidateNppd.py .cache/cornell/nppd_dump
```

This reads `nppd_dump.tensors.json` and `nppd_dump.<name>.bin` files, checks finite
inputs and outputs, verifies the adapter against the pinned original `Model.step`,
then writes `nppd_dump.validation.json` with maximum/mean absolute error and RMSE for
both HDR and temporal state. A mismatch exits with failure. The JSON manifest must
give each tensor's name and shape; an optional `file` supplies a path relative to the
manifest directory. Validation is offline and does not add a renderer dependency.

The actual 256x256 Cornell renderer completed two frames on both backends using its
two independent path samples and primary-hit guides. Comparing the final-frame raw
dumps against the original CPU `Model.step` passed with `rtol=atol=1e-4`:

| Backend | HDR maximum error | HDR mean error | State maximum error |
| --- | ---: | ---: | ---: |
| Native D3D12 | 3.24249e-5 | 2.71090e-8 | 3.24249e-5 |
| Native Vulkan | 4.00543e-5 | 2.63446e-8 | 4.00543e-5 |

Both temporal inputs contained 2,282,297 nonzero values out of 2,490,368, confirming
that the second frame received history. All dumped inputs and outputs were finite.
The reports are `.cache/model-export/cornell/nppd_d3d12.validation.json` and
`nppd_vulkan.validation.json`. These checks establish numerical integration parity;
they are not denoising quality scores against a converged rendering.

`ArdaTemporalModelGpuTests.cpp` binds all eight inputs and executes two model invocations
in one ordinary graph, feeding the first output's tensor view into the second input.
Run it with `ARDA_TEST_TEMPORAL_MODEL_PACKAGE` and
`ARDA_TEST_TEMPORAL_MODEL_RAW_DIR` pointing at the generated package and `_raw` folder.
The 64x64 two-frame graph test passed on both native D3D12 and native Vulkan. Frame2
maximum errors were 9.54e-7 for HDR and 3.81e-6 for state, with identical error results
on both backends. This also checks rebinding the state view at its nonzero offset
inside the first invocation's shared output allocation.

## Compiler and closure checks

`ArdaIreeExport.py` imports ONNX with IREE's importer, compiles through HAL with direct
command bindings, reads structured MLIR and CUDA `CDA1` executable metadata, then
evaluates the supported static host program. It derives scheduling, buffer subranges,
kernel argument order, workgroups, dynamic shared memory, and embedded constants from
compiler output. It does not attempt to recover opaque cuDNN/cuBLAS implementations.
This exporter emits PTX. The Backend module API also accepts compatible cubin
artifacts, but the current IREE adapter does not produce cubin variants.

The adapter rejects unresolved shapes, unsupported host operations, external host
calls, unsupported encodings/queue modes/flags, integer arithmetic that would require
unimplemented wrapping or narrowing, non-CUDA executables, inconsistent launch ABI,
unresolved PTX externals, out-of-bounds resources, and overlapping copies. Dynamic
shared-memory declarations are allowed. Dispatch argument types are cross-checked
against PTX entry signatures. Unknown CUDA metadata versions require a new adapter
audit. CUDA module loading and numerical replay complete validation of a concrete
artifact; text inspection alone cannot prove arbitrary PTX is safe or correct.

Current exports use supported typed dense tensor ports, static dimensions and a single CUDA
device. The generic transport records dtype and stride metadata, but this adapter
does not yet export arbitrary strided ports or data-dependent host control flow.
Commands execute in compiler order; this preserves barriers conservatively and does
not optimize independent command concurrency. Scratch allocations are preserved;
there is no new cross-model memory planner yet. Mutable internal/output allocations
receive explicit zero-fill commands before use because IREE's mutable bindings expose
conservative read/write ranges rather than precise write-only footprints. Input ports
remain caller-initialized. Node identities include package name, the PTX content hash,
and entry point, allowing differently compiled models to coexist in the immutable registry.

The default target is `sm_80`. Use `--target-sm` for an exact audited target and
validate the resulting package on the deployment GPU; FP8 requires SM89 or newer.
Explicit modern profiles and FP8 conversion lowering are recorded in producer metadata.

Named model state is externalized before compiler optimization, so checkpoint values
cannot be folded into PTX. Structural constants can still be folded. Compilation emits an inspectable JSON companion
with hashes and a producer record. `--keep-ir` preserves imported and lowered IR for
diagnostics. These build artifacts are not needed by the runtime.

The current archive limit is 512 MiB. The `.ardagpu` transport stores each PTX module
once, and native CUDA modules are shared across kernel entries. Expanded runtime
definitions still retain a portable PTX payload per entry, so host memory and graph
archive size can exceed the compact package size. Shared code assets in the graph
archive and streaming weight storage remain scaling work for larger models.

## Binary transports

All numbers are little endian. Strings and blobs are `u64 length` followed by bytes;
vectors begin with `u64 count`. The descriptor header is `ARDAGPU\0`, `u32 version=2`, and name.
Following vectors contain modules, buffers, tensors, kernels and commands in that order.
`ArdaModelPackage.py` is the executable format specification, paired with the C++
`FArdaGraphPackage` reader. Tensor dtype IDs match `EArdaRHIDataType`.

Commands are dispatch (opcode0), fill (opcode1), or copy (opcode2). Fill carries a
1/2/4-byte repeating pattern. Copy field order is source index, source offset,
destination index, destination offset, byte size. Scalar dispatch arguments use buffer
index `UINT32_MAX` and exact little-endian scalar bytes. Buffers include optional full
initializers. Kernel argument layouts separately record scalar versus pointer values,
read/write access, packed offsets, sizes and alignment. Version two appends a state
slot vector after commands: name, `u32 buffer`, `u16 dtype`, dimensions (`u64` vector),
strides (`i64` vector), and `u64 byte offset`. Slots occupy dedicated dense buffers
at offset zero with no embedded initializer. Scalar state uses an empty shape.

Checkpoints have the eight-byte magic `ARDASTAT`, `u32 version=1`, then a vector of
entries: name, `u16 dtype`, dimensions (`u64` vector), and a raw byte blob. Entries
match descriptor slots by name, independent of order. Duplicate, missing or extra
keys and incompatible metadata fail before state is applied. Readers retain support
for version-one `.ardagpu` files. Descriptor and checkpoint readers each enforce
the current 512 MiB limit.

Run contract regression checks with:

```powershell
.venv/model-export/Scripts/python -m unittest discover -s Tools/ModelExport -p test_model_export.py -v
.venv/model-export/Scripts/python -m unittest discover -s Tools/ModelExport -p test_arda_export.py -v
```

Run precision conversion tests, including GPU boundary cases, with:

```powershell
$env:ARDA_TEST_CUDA_PRECISION = "1"
.venv/model-export/Scripts/python -m unittest discover -s Tools/ModelExport -p test_precision_compiler.py -v
.venv/model-export/Scripts/python -m unittest discover -s Tools/ModelExport -p test_arda_precision.py -v
```

The GPU cases require SM89 or newer. Without the environment flag, CPU/compiler
checks run and the two GPU conversion tests are skipped. Deployment-specific
numerical checks remain necessary for each compiled model and precision.

The complete Python suite passed 76 tests with GPU cases enabled on Windows and
the installed SM120 GPU: 20 public API, 18 frontend precision/edge, 24 transport,
10 compiler precision and 4 storage checks. This includes switching checkpoints
on an unchanged descriptor and verifying independent outputs on the GPU. The native
suite also passed 32 state/binding/subgraph tests and four OIDN/NPPD parity cases
across D3D12 and Vulkan using separate descriptors and checkpoints.
