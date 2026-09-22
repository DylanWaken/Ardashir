# ArdaMath

ArdaMath is the project's standard CPU linear algebra module. It uses the pinned
Eigen 5.0.1 dependency in `ThirdParty/Eigen` and exposes graphics types and
construction helpers through [ArdaMath.h](Public/ArdaMath.h).

```cmake
target_link_libraries(MyRenderer PRIVATE Ardashir::ArdaMath)
```

```cpp
#include "ArdaMath.h"
```

The dependency on `Eigen3::Eigen` is public, so consumers can use Eigen operations
on these types directly. Linking ArdaMath also propagates
`EIGEN_DEFAULT_TO_ROW_MAJOR`, so directly declared Eigen matrix types follow the
same storage default. Additional Eigen algorithms can be included from the
appropriate public Eigen header.

## Types and conventions

| Type | Meaning |
| --- | --- |
| `FArdaVector2f`, `FArdaVector3f`, `FArdaVector4f` | Float column vectors |
| `FArdaMatrix2f`, `FArdaMatrix3f`, `FArdaMatrix4f` | Float matrices with row-major storage |
| `FArdaModelMatrix` | Model-to-world `FArdaMatrix4f` |
| `FArdaViewMatrix` | World-to-view `FArdaMatrix4f` |
| `FArdaProjectionMatrix` | View-to-clip `FArdaMatrix4f` |
| `FArdaViewProjectionMatrix` | World-to-clip `FArdaMatrix4f` |
| `FArdaNormalMatrix` | Inverse-transpose linear transform, `FArdaMatrix3f` |
| `FArdaCameraMatrices` | World-to-view and view-to-clip matrices, initialized to identity |
| `FArdaAffineTransform3f`, `FArdaQuaternionf` | Eigen affine transforms and quaternions |
| `FArdaMatrix3x4f` | Row-major affine coefficients for native ray-tracing instances |

Double-precision vector, matrix, quaternion, and affine-transform types have a
`d` suffix. `TArdaMatrix<Scalar, Rows, Columns>` covers other dimensions. Semantic
matrix names are aliases, so Eigen expressions work without conversion wrappers.
They do not provide compile-time checking of coordinate spaces.

Matrices are stored **row-major**, while the algebra uses **column vectors**:
`Clip = Projection * View * Model * Position`. Storage order determines the
coefficient layout in memory; it does not reverse multiplication order. Column
vectors use Eigen's supported contiguous column-vector storage. Initialize raw
Eigen values explicitly with `Identity()`, `Zero()`, or coefficient values.

Camera space looks along **positive Z**, with positive X to the right and
positive Y up. `MakeArdaViewMatrix(Position, Forward, Up)` accepts a direction
for `Forward`; use `Target - Position` when starting with a target point. It
normalizes the orientation and derives an orthogonal basis from the supplied
world-up direction. The project does not impose a world-up axis.

Projection helpers use a `[0, 1]` device-depth interval and default to reversed
depth: **near maps to 1, far maps to 0**, matching Unreal's device-Z convention.
Use `ArdaDepthClearValue` (`0`) and a `Greater` or `GreaterOrEqual` depth comparison with
these defaults. No backend-specific Y flip is included in the matrix.

| Helper | Parameters and depth behavior |
| --- | --- |
| `MakeArdaPerspectiveProjection` | Vertical FOV in radians, width/height aspect, `0 < Near < Far`; finite far plane |
| `MakeArdaInfinitePerspectiveProjection` | Vertical FOV, aspect, positive near plane; depth approaches 0 at infinite distance |
| `MakeArdaOrthographicProjection` | Left/right/bottom/top bounds and `0 <= Near < Far`; finite orthographic volume |

All three accept a final `ReverseDepth` argument. Passing `false` selects forward
depth, mapping near to 0 and far to 1; pair that with a clear of 1 and a `Less`
or `LessOrEqual` comparison. View, projection, and normal-matrix helpers reject
invalid/nonfinite inputs with `std::invalid_argument`, including a degenerate
view basis, an invalid projection volume, or a singular/non-affine model matrix.

## Camera and mesh transforms

```cpp
using namespace arda;

FArdaCameraMatrices Camera;
Camera.mWorldToView = MakeArdaViewMatrix(
    FArdaVector3f(0.f, 1.f, -5.f), // Camera position.
    FArdaVector3f(0.f, 0.f, 1.f),  // Forward direction.
    FArdaVector3f(0.f, 1.f, 0.f)); // World up.
Camera.mViewToClip = MakeArdaPerspectiveProjection(
    1.04719755f, 16.f / 9.f, 0.1f, 1000.f);

FArdaModelMatrix Model = FArdaModelMatrix::Identity();
Model.block<3, 1>(0, 3) = FArdaVector3f(2.f, 0.f, 0.f);
Model(0, 0) = 2.f; // Nonuniform scale requires the inverse-transpose normal matrix.

const FArdaNormalMatrix Normal = MakeArdaNormalMatrix(Model);
const FArdaMatrix4f ModelToClip = Camera.GetWorldToClip() * Model;
const FArdaVector4f ClipPosition =
    ModelToClip * FArdaVector4f(0.f, 0.f, 0.f, 1.f);
const FArdaVector3f WorldNormal =
    (Normal * FArdaVector3f(0.f, 1.f, 0.f)).normalized();
```

`MakeArdaNormalMatrix` transforms model-space normals to world space. Normalize
the transformed normal before lighting; a view-space normal requires the linear
view transform as well.

## Shader upload boundary

CPU Eigen objects and lazy Eigen expressions are not shader ABI structures.
Store evaluated matrices in node parameters or CPU scene data, then pack their
coefficients into the explicit, trivially copyable upload types:

```cpp
struct alignas(16) FArdaMeshConstants
{
    FArdaShaderMatrix4f mModelToClip;
    FArdaShaderMatrix3f mNormalToWorld;
};

const FArdaMeshConstants Constants{
    PackArdaShaderMatrix(ModelToClip),
    PackArdaShaderMatrix(Normal)};

const FArdaShaderCameraMatrices CameraConstants =
    PackArdaShaderCameraMatrices(Camera);
// Upload Constants or CameraConstants through the usual constant-buffer operation.
```

The corresponding HLSL constant-buffer layout explicitly uses `row_major`:

```hlsl
cbuffer MeshConstants : register(b0)
{
    row_major float4x4 ModelToClip;
    row_major float3x3 NormalToWorld;
};

float4 ClipPosition = mul(ModelToClip, float4(Position, 1));
float3 WorldNormal = normalize(mul(NormalToWorld, Normal));
```

`FArdaShaderMatrix4f` occupies 64 bytes. `FArdaShaderMatrix3f` occupies 48 bytes:
three rows padded to four floats each, matching HLSL constant-buffer matrix
packing. A plain CPU `FArdaMatrix3f` contains nine coefficients and cannot be
uploaded as that constant-buffer field directly. `FArdaShaderCameraMatrices`
contains two consecutive packed 4x4 matrices: world-to-view followed by
view-to-clip, for 128 bytes total. These are constant-buffer layouts; structured
buffer schemas should describe their own explicit element layout.

The ray-tracing instance boundary uses the native **row-major 3x4** format:

```cpp
FArdaRHIRayTracingInstanceDesc Instance;
Instance.mTransform = Model.topRows<3>();
```

Include `ArdaBackend.h` for that RHI descriptor. The omitted fourth row of an
affine transform is `(0, 0, 0, 1)`. Native providers copy the twelve transform
coefficients into their API descriptors; shader constant-buffer padding is not
used at this boundary.

## Dependency and tests

Initialize dependencies with `git submodule update --init --recursive`.
Eigen is pinned to commit `bc3b39870ecb690a623a3f49149a358b95c5781d`
([release 5.0.1](https://gitlab.com/libeigen/eigen/-/tags/5.0.1)). Its upstream
`COPYING.README` and `COPYING.*` files retain the applicable license notices.
The integration builds only the header-only library; Eigen's own tests,
documentation, demos, BLAS/LAPACK libraries, and benchmarks are disabled.

Build `ArdaMathTests` for numerical and upload-layout checks and
`ArdaMathPublicHeaders` for the standalone public-header check.
