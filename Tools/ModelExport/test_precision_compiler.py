"""Compiler precision contracts; opt in to driver tests with ARDA_TEST_CUDA_PRECISION=1."""

import os
from pathlib import Path
import re
import tempfile
import unittest

import ml_dtypes
import numpy as np
import onnx
from onnx import helper as h, TensorProto as T
from iree.compiler import ir

from ArdaIreeExport import (compile_model, constant_bytes, lower_onnx_fp8_casts, target_options,
                            validate_mlir_precision, validate_onnx_precision)
from ArdaModelPackage import ExportError


def cast_model(source_type, destination_type, count, saturation=None):
    attrs = {} if saturation is None else {"saturate": saturation}
    graph = h.make_graph([h.make_node("Cast", ["x"], ["y"], to=destination_type, **attrs)],
                         "main_graph", [h.make_tensor_value_info("x", source_type, [count])],
                         [h.make_tensor_value_info("y", destination_type, [count])])
    return h.make_model(graph, opset_imports=[h.make_opsetid("", 19)], ir_version=10)


def compile_in(directory, name, model, target):
    source, output = Path(directory) / (name + ".onnx"), Path(directory) / (name + ".ardagpu")
    onnx.save(model, source)
    return compile_model(source, output, target)


class PrecisionCompilerTests(unittest.TestCase):
    def test_float8_dense_constants_preserve_every_bit_pattern(self):
        raw = bytes(range(256))
        with ir.Context():
            for dtype in ("f8E4M3FN", "f8E5M2"):
                attr = ir.Attribute.parse(f'dense<"0x{raw.hex()}"> : tensor<256x{dtype}>')
                self.assertEqual(constant_bytes(attr), raw)

    def test_bfloat16_nan_payload_signed_zero_and_splat_bits(self):
        with ir.Context():
            self.assertEqual(constant_bytes(ir.Attribute.parse(
                "dense<[0x7FC1, 0xFFC7, 0x8000, 0x0001, 0x3F80]> : tensor<5xbf16>")),
                bytes.fromhex("c17fc7ff00800100803f"))
            self.assertEqual(constant_bytes(ir.Attribute.parse("dense<0x7FC1> : tensor<17xbf16>")),
                             bytes.fromhex("c17f") * 17)

    def test_internal_onnx_casts_cannot_hide_low_precision(self):
        for dtype, target in ((T.BFLOAT16, 75), (T.FLOAT8E4M3FN, 80), (T.FLOAT8E5M2, 86), (T.INT8, 60)):
            model = cast_model(T.FLOAT, dtype, 8)
            # Public ports deliberately lie about type, to exercise the Cast
            # attribute check independently of output metadata.
            model.graph.output[0].type.tensor_type.elem_type = T.FLOAT
            with self.subTest(dtype=dtype), self.assertRaisesRegex(ExportError, "requires sm_"):
                validate_onnx_precision(model, target)

    def test_shaped_and_internal_mlir_types_are_checked(self):
        with ir.Context() as context:
            context.allow_unregistered_dialects = True
            for dtype, target in (("bf16", 75), ("f8E4M3FN", 80), ("i8", 60)):
                module = ir.Module.parse('module { func.func @main(%x: tensor<4x8xf32>) -> tensor<4x8xf32> {'
                    f' %0 = "test.hidden"(%x) : (tensor<4x8xf32>) -> tensor<4x8x{dtype}>'
                    ' return %x : tensor<4x8xf32> } }')
                with self.subTest(dtype=dtype), self.assertRaisesRegex(ExportError, "requires sm_"):
                    validate_mlir_precision(module, target)

    def test_unaudited_targets_reject_before_compilation(self):
        for target in (53, 88, 100, 121, "89", True):
            with self.subTest(target=target), self.assertRaisesRegex(ExportError, "Unsupported audited"):
                target_options(target)

    def test_unknown_float8_cast_rounding_attributes_fail_closed(self):
        with ir.Context() as context:
            context.allow_unregistered_dialects = True
            module = ir.Module.parse('module { func.func @main(%x: !torch.vtensor<[8],f32>) -> !torch.vtensor<[8],f8E4M3FN> {'
                ' %0 = torch.operator "onnx.Cast"(%x) {torch.onnx.to = 17 : si64, torch.onnx.round_mode = "up"}'
                ' : (!torch.vtensor<[8],f32>) -> !torch.vtensor<[8],f8E4M3FN>'
                ' return %0 : !torch.vtensor<[8],f8E4M3FN> } }')
            with self.assertRaisesRegex(ExportError, "Unsupported FP8 Cast semantic attribute"):
                lower_onnx_fp8_casts(module)

    def test_modern_architectures_emit_the_exact_requested_ptx_target(self):
        with tempfile.TemporaryDirectory() as directory:
            for target, version in ((89, b"8.4"), (90, b"8.4"), (120, b"8.7")):
                graph = h.make_graph([h.make_node("Add", ["x", "x"], ["y"])], "main_graph",
                    [h.make_tensor_value_info("x", T.FLOAT, [8])], [h.make_tensor_value_info("y", T.FLOAT, [8])])
                model = h.make_model(graph, opset_imports=[h.make_opsetid("", 19)], ir_version=10)
                package = compile_in(directory, "target" + str(target), model, target)
                self.assertTrue(package["modules"])
                for module in package["modules"]:
                    self.assertEqual(int(re.search(rb"\.target\s+sm_(\d+)", module["image"])[1]), target)
                    self.assertIn(b".version " + version, module["image"])

    def test_real_low_precision_constant_arithmetic_compiles(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = ((T.FLOAT16, np.float16, 10, 80), (T.BFLOAT16, ml_dtypes.bfloat16, 11, 80),
                     (T.FLOAT8E4M3FN, ml_dtypes.float8_e4m3fn, 14, 89),
                     (T.FLOAT8E5M2, ml_dtypes.float8_e5m2, 15, 120), (T.INT8, np.int8, 2, 61))
            for dtype, storage, identifier, target in cases:
                bias = T(name="bias", data_type=dtype, dims=[8],
                         raw_data=np.arange(8).astype(storage).tobytes())
                accumulation = T.INT32 if dtype == T.INT8 else T.FLOAT
                nodes = [h.make_node("Cast", ["x"], ["xf"], to=accumulation),
                         h.make_node("Cast", ["bias"], ["bf"], to=accumulation),
                         h.make_node("Add", ["xf", "bf"], ["sum"]),
                         h.make_node("Cast", ["sum"], ["y"], to=dtype)]
                graph = h.make_graph(nodes, "main_graph", [h.make_tensor_value_info("x", dtype, [8])],
                                     [h.make_tensor_value_info("y", dtype, [8])], [bias])
                model = h.make_model(graph, opset_imports=[h.make_opsetid("", 19)], ir_version=10)
                package = compile_in(directory, "dtype" + str(identifier), model, target)
                self.assertEqual([port["dtype"] for port in package["tensors"]], [identifier, identifier])
                self.assertTrue(package["kernels"])

    @unittest.skipUnless(os.environ.get("ARDA_TEST_CUDA_PRECISION") == "1", "requires a CUDA GPU sm_89 or newer")
    def test_driver_float8_decodes_all_256_encodings(self):
        from ArdaCudaReplay import CudaReplay
        with tempfile.TemporaryDirectory() as directory:
            raw = np.arange(256, dtype=np.uint8)
            for dtype, storage in ((T.FLOAT8E4M3FN, ml_dtypes.float8_e4m3fn), (T.FLOAT8E5M2, ml_dtypes.float8_e5m2)):
                package = compile_in(directory, "decode" + str(dtype), cast_model(dtype, T.FLOAT, 256), 89)
                with CudaReplay(package) as runner:
                    actual = np.frombuffer(runner.run({"x": raw.tobytes()})["y"], dtype=np.float32)
                expected = raw.view(storage).astype(np.float32)
                np.testing.assert_array_equal(np.isnan(actual), np.isnan(expected))
                finite_or_inf = ~np.isnan(expected)
                np.testing.assert_array_equal(actual[finite_or_inf].view(np.uint32), expected[finite_or_inf].view(np.uint32))

    @unittest.skipUnless(os.environ.get("ARDA_TEST_CUDA_PRECISION") == "1", "requires a CUDA GPU sm_89 or newer")
    def test_driver_float8_rounding_subnormals_overflow_and_saturation(self):
        from ArdaCudaReplay import CudaReplay
        with tempfile.TemporaryDirectory() as directory:
            for dtype, storage, limit in ((T.FLOAT8E4M3FN, ml_dtypes.float8_e4m3fn, 448.),
                                          (T.FLOAT8E5M2, ml_dtypes.float8_e5m2, 57344.)):
                encodings = np.arange(256, dtype=np.uint8).view(storage).astype(np.float32)
                finite = np.unique(encodings[np.isfinite(encodings)])
                midpoints = (finite[:-1] + finite[1:]) / np.float32(2)
                source = np.concatenate((finite, midpoints, np.nextafter(midpoints, np.float32(-np.inf)),
                    np.nextafter(midpoints, np.float32(np.inf)),
                    np.array([0., -0., 465., -465., 61439., 61440., 61441., 1e30, -1e30, np.inf, -np.inf, np.nan], dtype=np.float32)))
                for saturation in (0, 1):
                    model = cast_model(T.FLOAT, dtype, source.size, saturation)
                    package = compile_in(directory, f"encode{dtype}_{saturation}", model, 89)
                    with CudaReplay(package) as runner:
                        actual = np.frombuffer(runner.run({"x": source.tobytes()})["y"], dtype=np.uint8)
                    values = np.clip(source, -limit, limit) if saturation else source
                    expected = values.astype(storage).view(np.uint8)
                    # Conversion may canonicalize NaN payload/sign. Every
                    # finite value, infinity and signed zero must match bits.
                    expected_nan = np.isnan(expected.view(storage).astype(np.float32))
                    np.testing.assert_array_equal(np.isnan(actual.view(storage).astype(np.float32)), expected_nan)
                    np.testing.assert_array_equal(actual[~expected_nan], expected[~expected_nan])


if __name__ == "__main__":
    unittest.main()
