# Cornell denoising through the unified graph

This launcher reuses CornellBox's renderer. Its frame graph performs hardware ray
tracing, progressive linear HDR accumulation, graphics NCHW packing, an imported
pretrained CUDA subgraph, graphics unpacking, then the existing tone mapping and
presentation. CUDA and graphics resources use the ordinary graph dependency and
interop machinery. The denoised result never replaces the unbiased accumulation.

Two published pretrained models exercise the same package and graph API:
the OIDN `rt_hdr_small` spatial network and NPPD `small_2_spp` temporal neural
path-space denoiser. The application uses the CUDA driver and the existing
graphics backend; PyTorch, OIDN, and IREE are offline sources/compiler tools,
and no ML inference library is loaded by the application. Model paper timings
do not establish the performance of these exported artifacts.

Compilation produces an `.ardagpu` model descriptor and a separate `.ardastate`
checkpoint. `--denoise-package` selects the descriptor; the launcher loads its
sibling checkpoint by default. Use `--denoise-state-dict path/to/alternative.ardastate`
to use another checkpoint with the same model. Names, dtypes and shapes must match.
Legacy version-one packages still load their embedded weights.

Create a 256x256 artifact with the offline tools:

```powershell
.venv/model-export/Scripts/python.exe Tools/ModelExport/ArdaOidnExport.py --download --height 256 --width 256 --output .cache/model-export/oidn/oidn_hdr_small_256.onnx
.venv/model-export/Scripts/python.exe Tools/ModelExport/ArdaIreeExport.py .cache/model-export/oidn/oidn_hdr_small_256.onnx .cache/model-export/oidn/oidn_hdr_small_256.ardagpu
```

The package includes automatic HDR exposure, normalized PU encoding,
the trained U-Net and inverse HDR processing. No tone-mapped pixels enter it.

```powershell
python Scripts/Examples/RunCornellDenoising.py d3d12 --denoise-package .cache/model-export/oidn/oidn_hdr_small_256.ardagpu --frames 3 --samples-per-dispatch 1 --max-samples 2 --denoise-dump .cache/model-export/cornell/result
```

The script configures and builds `CornellDenoising` with CUDA enabled before
launching it. Add `--run-only` to use an existing build. Optional positional
arguments after the backend select the build directory and configuration:
`d3d12 build Release`. Pass `--denoise-state-dict` to select another checkpoint.
The script uses one sample per dispatch and eight total samples by default.
Building the CUDA provider requires CUDA SDK headers; use `--cuda-include-dir`
if they are not discovered automatically. A fresh AI example build does not
compile CUDA kernels with nvcc because the descriptor already contains them.

The window defaults to 256x256. Its dimensions must match the package's static
tensor ports; export a matching package
before using another resolution. Vulkan is selected with `--backend vulkan`.
`--hidden` supports automated smoke runs. After the sample limit is reached, the
renderer presents the retained result without repeating inference.

Optional `--denoise-dump` adds graph readbacks and writes little-endian linear HDR
PFM images with `.noisy.pfm` and `.denoised.pfm` suffixes. Omit it when profiling.
The normal CornellBox launcher also accepts these options; its ordinary behavior
is unchanged when no package is supplied. Configure
`ARDASHIR_CORNELL_DENOISING_PACKAGE` to register GPU tests using a 256x256 package.

For the temporal model, export the official pretrained checkpoint and compile it:

```powershell
.venv/model-export/Scripts/python.exe Tools/ModelExport/ArdaNppdExport.py --height 256 --width 256
.venv/model-export/Scripts/python.exe Tools/ModelExport/ArdaIreeExport.py .cache/model-export/nppd/nppd_small_2spp_256x256.onnx .cache/model-export/nppd/nppd_small_2spp_256x256.ardagpu
build/Source/ArdaTests/Examples/CornellDenoising/CornellDenoising.exe --backend d3d12 --denoise-package .cache/model-export/nppd/nppd_small_2spp_256x256.ardagpu --hidden --frames 2 --max-samples 4 --denoise-dump .cache/model-export/cornell/nppd
.venv/model-export/Scripts/python.exe Tools/ModelExport/ArdaValidateNppd.py .cache/model-export/cornell/nppd
```

NPPD consumes two independent path samples per pixel in `[1,C,H,W,2]` layout,
including their own primary-hit distance, normal, and material color. Depth is
`log(1 + 1 / distance)`, and normal channels are dot products with camera
forward/right/up. Packing occurs in ordinary graphics compute nodes. This path
forces two samples per dispatch and requires an even `--max-samples` of at least
two. Its noisy PFM contains the current two-sample batch mean; the spatial path's
noisy PFM contains the progressive accumulation.

The 38-channel temporal state is retained in alternating ordinary RHI buffers.
The model's image and state output views share their original output arena;
a graph copy saves the state view for the following frame. This static-scene
demonstration uses zero motion vectors while the camera is stationary. Camera
movement resets history before inference; continuous moving-camera reprojection
is not implemented. The full-frame crop offsets are zero. The final completed
frame's inputs and outputs are also written as `.NAME.bin` float32 files with a
`.tensors.json` manifest. Validation compares those exact rendered inputs with
the pinned authors' model, including the prior temporal state.

For OIDN numerical validation use
`python Tools/ModelExport/ArdaValidateDenoising.py PREFIX`. Its optional
`--high-spp reference.pfm` reports image-quality metrics separately from numerical
parity. A matching high-sample reference can be captured from the spatial path's
`.noisy.pfm`; keep the camera, dimensions, and maximum bounce count identical.

The checkpoint revision, SHA-256 and original source references are recorded next
to each exported ONNX file. The exporter validates the checkpoint hash before use.

Verified on Windows on 2026-10-02 at 256x256, two NPPD frames, two samples per
frame, and five path bounces: both D3D12 and Vulkan completed and matched the
original authors' CPU model. Maximum absolute HDR/state error was `3.25e-5` for
D3D12 and `4.01e-5` for Vulkan; every dumped tensor was finite. The second frame's
prior state contained 2,282,297 nonzero elements, confirming retained history was
used. These are numerical checks, not frame-time measurements.

A same-camera, five-bounce 256-sample reference was captured using:

```powershell
build/Source/ArdaTests/Examples/CornellDenoising/CornellDenoising.exe --backend d3d12 --denoise-package .cache/model-export/oidn/oidn_hdr_small_256.ardagpu --hidden --frames 4 --samples-per-dispatch 64 --max-samples 256 --max-bounces 5 --denoise-dump .cache/model-export/cornell/reference_256spp
```

Comparing its **noisy** PFM to the second NPPD frame gives full-frame display PSNR
`25.16 -> 37.32 dB`, linear HDR RMSE `0.18761 -> 0.08335`, and relative L1
`0.09407 -> 0.02281`, from current noisy batch to denoised output. Display PSNR
uses exposure one, Reinhard mapping, and gamma `1/2.2` for both images. The
reference has residual Monte Carlo noise, and the image includes black borders;
these figures describe this scene and do not establish general model quality.
The generated local artifacts are
`.cache/model-export/cornell/nppd_comparison_reference.png` and
`.cache/model-export/cornell/nppd_quality.json`.
