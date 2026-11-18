"""Validate and replay .ardagpu using only the CUDA driver for GPU computation.

NumPy is used by the command-line reference comparison, never for GPU execution.
This independent binary reader/runner is an exporter validation tool, not Arda's runtime.
"""

from __future__ import annotations

import argparse
import ctypes as ct
import json
import sys
from pathlib import Path

from ArdaModelPackage import ExportError, check, load_state_dict, read_package, validate_package
from ArdaDTypes import decode_array, decode_bytes, dtype_info, numpy_storage_dtype


class CudaReplay:
    def __init__(self, package, device_index=0, *, state_dict=None):
        if state_dict is not None:
            package = load_state_dict(package, state_dict)
        validate_package(package)
        check(not package.get("state_slots"), "Model descriptor requires a state dict before CUDA replay")
        self.package, self.allocations, self.modules, self.functions = package, [], [], []
        self.context, self.stream = ct.c_void_p(), ct.c_void_p()
        self.device = ct.c_int()
        self.driver = ct.WinDLL("nvcuda.dll") if sys.platform == "win32" else ct.CDLL("libcuda.so.1")
        self.call("cuInit", ct.c_uint(0))
        self.call("cuDeviceGet", ct.byref(self.device), ct.c_int(device_index))
        major, minor = ct.c_int(), ct.c_int()
        self.call("cuDeviceGetAttribute", ct.byref(major), ct.c_int(75), self.device)
        self.call("cuDeviceGetAttribute", ct.byref(minor), ct.c_int(76), self.device)
        self.compute_capability = major.value * 10 + minor.value
        check(all(module["target_sm"] <= self.compute_capability for module in package["modules"]),
              f"Package requires a newer CUDA architecture than local sm_{self.compute_capability}")
        self.call("cuDevicePrimaryCtxRetain", ct.byref(self.context), self.device)
        try:
            self.call("cuCtxSetCurrent", self.context)
            self.call("cuStreamCreate", ct.byref(self.stream), ct.c_uint(1))
            for buffer in package["buffers"]:
                pointer = ct.c_uint64()
                self.call("cuMemAlloc_v2", ct.byref(pointer), ct.c_size_t(buffer["byte_size"]))
                self.allocations.append(pointer.value)
                if buffer["initializer"]:
                    self.upload(pointer.value, buffer["initializer"])
            for module in package["modules"]:
                handle = ct.c_void_p()
                image = ct.create_string_buffer(module["image"])
                self.call("cuModuleLoadDataEx", ct.byref(handle), ct.cast(image, ct.c_void_p),
                          ct.c_uint(0), None, None)
                self.modules.append(handle)
            for kernel in package["kernels"]:
                function = ct.c_void_p()
                self.call("cuModuleGetFunction", ct.byref(function), self.modules[kernel["module"]],
                          ct.c_char_p(kernel["entry"].encode("utf-8")))
                self.functions.append(function)
            shared_memory = {}
            for command in package["commands"]:
                if command["kind"] == "dispatch":
                    index = command["kernel"]
                    shared_memory[index] = max(shared_memory.get(index, 0), command["shared_memory"])
            for index, size in shared_memory.items():
                if size > 48 * 1024:
                    self.call("cuFuncSetAttribute", self.functions[index], ct.c_int(8), ct.c_int(size))
        except BaseException:
            self.close()
            raise

    def call(self, name, *args):
        result = getattr(self.driver, name)(*args)
        if result:
            message = ct.c_char_p()
            self.driver.cuGetErrorString(ct.c_int(result), ct.byref(message))
            raise ExportError(f"{name} failed ({result}): {message.value.decode() if message.value else 'unknown'}")

    def upload(self, pointer, value):
        memory = ct.create_string_buffer(value)
        self.call("cuMemcpyHtoD_v2", ct.c_uint64(pointer), ct.cast(memory, ct.c_void_p), ct.c_size_t(len(value)))

    def run(self, inputs):
        self.call("cuCtxSetCurrent", self.context)
        input_ports = [port for port in self.package["tensors"] if port["role"] in (1, 3)]
        check(set(inputs) == {port["name"] for port in input_ports}, "Input tensor names do not match package")
        for port in input_ports:
            value = inputs[port["name"]]
            check(len(value) == port["byte_size"], "Input tensor byte size mismatch: " + port["name"])
            self.upload(self.allocations[port["buffer"]] + port["offset"], value)
        for index, command in enumerate(self.package["commands"]):
            try:
                kind = command["kind"]
                if kind == "fill":
                    width = len(command["pattern"])
                    self.call(f"cuMemsetD{width * 8}Async", ct.c_uint64(self.allocations[command["buffer"]] + command["offset"]),
                              {1: ct.c_ubyte, 2: ct.c_ushort, 4: ct.c_uint}[width](int.from_bytes(command["pattern"], "little")),
                              ct.c_size_t(command["size"] // width), self.stream)
                elif kind == "copy":
                    self.call("cuMemcpyDtoDAsync_v2", ct.c_uint64(self.allocations[command["destination"]] + command["destination_offset"]),
                              ct.c_uint64(self.allocations[command["source"]] + command["source_offset"]),
                              ct.c_size_t(command["size"]), self.stream)
                else:
                    values = [ct.c_uint64(self.allocations[arg["buffer"]] + arg["offset"])
                              if arg["buffer"] != 0xFFFFFFFF else ct.create_string_buffer(arg["value"], len(arg["value"]))
                              for arg in command["arguments"]]
                    parameters = (ct.c_void_p * len(values))(*[ct.addressof(value) for value in values])
                    dims = [ct.c_uint(value) for value in command["grid"] + command["block"]]
                    self.call("cuLaunchKernel", self.functions[command["kernel"]], *dims,
                              ct.c_uint(command["shared_memory"]), self.stream, parameters, None)
            except ExportError as error:
                raise ExportError(f"Command {index} ({command['kind']}): {error}") from error
        self.call("cuStreamSynchronize", self.stream)
        outputs = {}
        for port in self.package["tensors"]:
            if port["role"] in (2, 3):
                value = ct.create_string_buffer(port["byte_size"])
                self.call("cuMemcpyDtoH_v2", value, ct.c_uint64(self.allocations[port["buffer"]] + port["offset"]),
                          ct.c_size_t(port["byte_size"]))
                outputs[port["name"]] = value.raw
        return outputs

    def close(self):
        if self.context.value:
            self.driver.cuCtxSetCurrent(self.context)
            if self.stream.value:
                self.driver.cuStreamSynchronize(self.stream)
            for module in reversed(self.modules):
                self.driver.cuModuleUnload(module)
            for pointer in reversed(self.allocations):
                self.driver.cuMemFree_v2(ct.c_uint64(pointer))
            if self.stream.value:
                self.driver.cuStreamDestroy_v2(self.stream)
            self.driver.cuDevicePrimaryCtxRelease_v2(self.device)
            self.context.value = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("reference", type=Path, help="NPZ with named input tensors and expected output tensors")
    parser.add_argument("--state-dict", type=Path, help="Checkpoint; defaults to the descriptor's sibling .ardastate")
    parser.add_argument("--output", type=Path, help="Save actual tensors and comparison metrics")
    parser.add_argument("--rtol", type=float, default=0.005)
    parser.add_argument("--atol", type=float, default=0.002)
    args = parser.parse_args()
    try:
        import numpy as np
        package = read_package(args.package)
        if args.state_dict is not None or package.get("state_slots"):
            package = load_state_dict(package, args.state_dict or args.package.with_suffix(".ardastate"))
        reference = np.load(args.reference)
        ports = package["tensors"]
        inputs = {}
        for port in ports:
            if port["role"] in (1, 3):
                array = reference[port["name"]]
                check(list(array.shape) == port["shape"], "Reference input shape mismatch")
                check(bool(np.isfinite(decode_array(array, port["dtype"])).all()), "Non-finite reference input")
                inputs[port["name"]] = array.tobytes()
        with CudaReplay(package) as runner:
            raw = runner.run(inputs)
        outputs, metrics = {}, {}
        for port in ports:
            if port["role"] in (2, 3):
                storage = np.frombuffer(raw[port["name"]], dtype=numpy_storage_dtype(port["dtype"])).reshape(port["shape"])
                actual = decode_array(storage, port["dtype"])
                expected = decode_array(reference[port["name"]], port["dtype"])
                check(actual.shape == expected.shape, "Reference output shape mismatch")
                check(bool(np.isfinite(actual).all() and np.isfinite(expected).all()), "Non-finite verification output")
                # Promote before subtraction to avoid signed INT8/INT32 overflow.
                difference = actual.astype(np.float64) - expected.astype(np.float64)
                exact = actual.dtype.kind in "biu"
                passed = bool(np.array_equal(actual, expected) if exact else
                              np.allclose(actual, expected, rtol=args.rtol, atol=args.atol))
                metrics[port["name"]] = {"max_abs_error": float(np.max(np.abs(difference))),
                    "mean_abs_error": float(np.mean(np.abs(difference))),
                    "rmse": float(np.sqrt(np.mean(difference ** 2))), "passed": passed,
                    "dtype": dtype_info(port["dtype"]).name, "comparison": "exact" if exact else "tolerance"}
                outputs[port["name"]] = storage
        check(not any(name.split(".")[0] in ("torch", "iree", "onnxruntime", "tensorrt", "cupy") for name in sys.modules),
              "An inference runtime was imported into driver replay")
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            np.savez(args.output, **outputs)
            args.output.with_suffix(".json").write_text(json.dumps(metrics, indent=2) + "\n")
        print(json.dumps(metrics, indent=2))
        check(all(item["passed"] for item in metrics.values()), "CUDA results exceeded the declared reference tolerance")
    except ExportError as error:
        parser.exit(2, f"Replay rejected: {error}\n")


if __name__ == "__main__":
    main()
