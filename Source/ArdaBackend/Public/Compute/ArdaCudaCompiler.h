/** @file ArdaCudaCompiler.h
 * Optional NVRTC adapter for reusable CUDA C++ source modules.
 */
#pragma once

#include "ArdaCudaModule.h"

namespace arda
{
    /** Loads an explicit NVRTC shared-library path and returns a retaining compiler callback.
     * Link Ardashir::ArdaCudaCompiler and configure ARDASHIR_NVRTC_INCLUDE_DIR to enable this adapter.
     * Missing build support, library or exports returns Unsupported without affecting PTX operands.
     * The caller must deploy NVRTC and its matching builtins library on the runtime library search
     * path (PATH on Windows). No CUDA driver/device is needed
     * to create the compiler. The callback targets compute_<SM>, forwards includes/options, preserves
     * compiler errors and returns PTX without its terminating NUL. Export kernels with extern "C";
     * template/name-expression lowering and device linking are the caller's compiler-adapter policy.
     * LibraryPath must be an absolute, nonempty path; the library remains loaded while any callback
     * copy is retained. Source options must not override the target architecture selected by the adapter.
     */
    [[nodiscard]] TArdaRHIResult<FArdaCudaModuleCompiler> CreateArdaNvrtcCompiler(const char* LibraryPath);
}
