# Getting Started

This guide creates a CPU trace capture and opens it in the local web viewer.

![ArdaTrace capture workflow](assets/capture-workflow.svg)

## 1. Build the runtime

ArdaTrace is registered by the root CMake project and tracing is enabled by
default:

```powershell
cmake -S . -B build -DARDASHIR_ENABLE_TRACE=ON
cmake --build build --config Debug
```

If another CMake target uses ArdaTrace, link the module:

```cmake
target_link_libraries(MyApplication PRIVATE Ardashir::ArdaTrace)
```

Set `ARDASHIR_ENABLE_TRACE=OFF` when instrumentation must compile to no-ops.
The disabled scope, counter, and marker macros do not record events.

## 2. Include the public API

```cpp
#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"
```

The public API is in the `arda::trace` namespace. Instrumentation macros are
available globally.

## 3. Record a capture

Start the process-wide capture before launching instrumented worker threads,
then stop it after those threads are quiescent:

```cpp
#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"

#include <cstdio>

int main()
{
    using namespace arda::trace;

    if (!StartTraceCapture("frame.ardatrace"))
    {
        std::fprintf(stderr, "Trace start failed: %s\n", GetTraceError().c_str());
        return 1;
    }

    SetCurrentTraceThreadName("Main Thread");
    {
        ARDA_NAMED_SCOPE_TIMER("Frame");
        ARDA_TRACE_MARKER("Frame Begin");

        {
            ARDA_NAMED_SCOPE_TIMER("Rendering");
            ARDA_TRACE_COUNTER("Visible Objects", 412);
            // RenderFrame();
        }
    }

    if (!StopTraceCapture())
    {
        std::fprintf(stderr, "Trace stop failed: %s\n", GetTraceError().c_str());
        return 1;
    }
}
```

Successful shutdown writes a completion record. A crash, forced termination, or
missing `StopTraceCapture()` leaves an incomplete capture that the strict reader
will reject.

## 4. Install the viewer

The viewer requires Python and Flask. From the repository root:

```powershell
python -m venv Tools\ArdaTraceViewer\.venv
Tools\ArdaTraceViewer\.venv\Scripts\Activate.ps1
python -m pip install -r Tools\ArdaTraceViewer\requirements.txt
```

Linux:

```sh
python3 -m venv Tools/ArdaTraceViewer/.venv
. Tools/ArdaTraceViewer/.venv/bin/activate
python3 -m pip install -r Tools/ArdaTraceViewer/requirements.txt
```

## 5. Open the capture

Windows:

```powershell
python Scripts\RunTraceViewer.py frame.ardatrace
```

Linux:

```sh
python3 Scripts/RunTraceViewer.py frame.ardatrace
```

Open `http://127.0.0.1:5000` in a browser. You can also launch the viewer
without a path and upload a capture through **Open capture**.

## 6. Verify the installation

Run the native and viewer tests:

```powershell
python Scripts\RunAllTests.py
cd Tools\ArdaTraceViewer
python -m unittest discover -s tests
```

## Next step

Read [Instrumentation](02-Instrumentation.md) for nested scope behavior,
thread naming, reusable registered names, and overhead guidance.
