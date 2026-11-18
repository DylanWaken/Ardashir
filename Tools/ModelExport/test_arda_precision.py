"""Precision profiles, architecture gates and bit-preserving export references."""

from pathlib import Path
import json
import tempfile
import unittest
from unittest import mock

import numpy as np
import onnx
from onnx.reference import ReferenceEvaluator
import torch

import arda
from ArdaDTypes import decode_array, dtype_info
from ArdaModelPackage import ExportError, read_package, read_state_dict
from arda._export import _normalize_torch_float8_casts, _torch_float8_saturation


class PrecisionAffine(torch.nn.Module):
    def __init__(self, dtype):
        super().__init__()
        self.register_buffer("bias", torch.tensor([0.5, -1.0, 2.0, 0.25]).to(dtype))

    def forward(self, value):
        return value + self.bias


class Int8Accumulation(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.register_buffer("offset", torch.tensor([1, -2, 3, -4], dtype=torch.int8))

    def forward(self, value):
        return value.to(torch.int32) * 3 + self.offset.to(torch.int32)


class MixedPrecision(torch.nn.Module):
    def forward(self, half, brain, quantized):
        return {"sum": half.float() + brain.float(),
                "half": half * 2,
                "counts": quantized.to(torch.int32) * 2}


class HiddenBfloat16(torch.nn.Module):
    def forward(self, value):
        return value.to(torch.bfloat16).float() + 1


class Float8Roundtrip(torch.nn.Module):
    def __init__(self, dtype):
        super().__init__()
        self.dtype = dtype
        self.register_buffer("offset", torch.tensor([0.5, -1.0, 2.0, 0.25]).to(dtype))

    def forward(self, value):
        # Quantization/casts are model operations; the API must not invent them.
        return (value.float() + self.offset.float()).to(self.dtype)


class AliasSensitive(torch.nn.Module):
    def forward(self, left, right):
        return left + right + (0 if left is right else 100)


class ArdaPrecisionTests(unittest.TestCase):
    def test_repeated_and_shared_storage_inputs_reject_before_capture(self):
        example = torch.ones(2, 4)
        original = example.clone()
        for other in (example, example.view_as(example)):
            with self.subTest(shared_identity=other is example), mock.patch("arda._export._capture") as capture:
                with self.assertRaisesRegex(ExportError, "shares storage"):
                    arda.export(AliasSensitive(), (example, other))
                capture.assert_not_called()
                torch.testing.assert_close(example, original)

    def test_dtype_aliases_and_stable_backend_ids(self):
        for alias, canonical, identifier in (("fp16", "float16", 10), (torch.float16, "float16", 10),
                ("bf16", "bfloat16", 11), (torch.bfloat16, "bfloat16", 11),
                ("fp8", "float8_e4m3fn", 14), (torch.float8_e5m2, "float8_e5m2", 15),
                ("int8", "int8", 2), (torch.int32, "int32", 6)):
            with self.subTest(alias=alias):
                self.assertEqual(arda.canonical_dtype(alias), canonical)
                self.assertEqual(arda.dtype_info(alias).dtype_id, identifier)

    def test_unsupported_input_dtype_is_rejected_without_casting(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler:
            with self.assertRaisesRegex(ExportError, "Unsupported exported tensor dtype"):
                arda.export(torch.nn.ReLU(), torch.ones(2, 4, dtype=torch.float64), directory)
            compiler.assert_not_called()
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_every_target_is_checked_before_compilation(self):
        cases = (("bf16", [80, 75]), ("fp8", [80]), ("int8", [60]))
        for dtype, targets in cases:
            with self.subTest(dtype=dtype), tempfile.TemporaryDirectory() as directory:
                with mock.patch("arda._export.compile_model") as compiler, self.assertRaisesRegex(ExportError, "requires sm_"):
                    example = torch.zeros(2, 4).to(getattr(torch, arda.canonical_dtype(dtype)))
                    arda.export(torch.nn.Identity(), example, directory, target_archs=targets)
                compiler.assert_not_called()
                self.assertEqual(list(Path(directory).iterdir()), [])

    def test_low_target_cannot_hide_bfloat16_model_buffers_behind_float32_ports(self):
        model = PrecisionAffine(torch.bfloat16)
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler:
            with self.assertRaisesRegex(ExportError, "bfloat16 export requires sm_80"):
                arda.export(model, torch.ones(2, 4), directory, target_archs=[75])
            compiler.assert_not_called()
            self.assertEqual(model.bias.dtype, torch.bfloat16)

    def test_low_target_cannot_hide_bfloat16_intermediates_behind_float32_ports(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler:
            with self.assertRaisesRegex(ExportError, "bfloat16 export requires sm_80"):
                arda.export(HiddenBfloat16(), torch.ones(2, 4), directory, target_archs=[75])
            compiler.assert_not_called()

    def _check_export(self, model, dtype, expected_id, storage, target=80, opset=19):
        example = torch.arange(8, dtype=torch.float32).reshape(2, 4).to(dtype)
        expected = model(example).detach()
        with tempfile.TemporaryDirectory() as directory:
            result = arda.export(model, (example,), directory, target_archs=[target],
                input_names=["source"], output_names=["result"], name="precision", opset_version=opset)
            package = read_package(result.packages[f"sm_{target}"])
            checkpoint = read_state_dict(result.checkpoints[f"sm_{target}"])
            for entry in checkpoint["entries"]:
                value = model.state_dict()[entry["name"]].contiguous()
                expected_bytes = value.reshape(-1).view(torch.uint8).numpy().tobytes()
                self.assertEqual(entry["data"], expected_bytes)
                self.assertEqual(entry["dtype"], dtype_info(value.dtype).dtype_id)
            with mock.patch("arda._export.compile_model") as compiler:
                copied = arda.export_state_dict(model, result, Path(directory) / "copy.ardastate")
                self.assertEqual(read_state_dict(copied), checkpoint)
                compiler.assert_not_called()
            ports = {port["name"]: port for port in package["tensors"]}
            self.assertEqual(ports["source"]["dtype"], expected_id)
            self.assertEqual(ports["result"]["dtype"], dtype_info(expected.dtype).dtype_id)
            self.assertEqual(ports["source"]["byte_size"], example.numel() * example.element_size())
            with np.load(result.reference_path) as reference:
                self.assertEqual(reference["source"].dtype, np.dtype(storage))
                np.testing.assert_array_equal(decode_array(reference["source"], dtype), example.float().numpy())
                np.testing.assert_array_equal(decode_array(reference["result"], expected.dtype), expected.float().numpy())
            manifest = json.loads(result.manifest_path.read_text())
            self.assertEqual(manifest["capture"]["inputs"][0]["dtype"], arda.canonical_dtype(dtype))
            self.assertEqual(manifest["capture"]["inputs"][0]["reference_storage"], dtype_info(dtype).storage_dtype)
            self.assertEqual(manifest["artifacts"][0]["verification"]["status"], "not_run")
            if arda.canonical_dtype(dtype).startswith("float8_"):
                casts = [node for node in onnx.load(result.onnx_path).graph.node if node.op_type == "Cast" and
                         any(attribute.name == "to" and attribute.i in (17, 19) for attribute in node.attribute)]
                self.assertTrue(casts)
                saturation = _torch_float8_saturation(17 if expected_id == 14 else 19)
                self.assertTrue(all(any(attribute.name == "saturate" and attribute.i == saturation
                    for attribute in node.attribute) for node in casts))

    def test_float16_export_and_numeric_reference(self):
        self._check_export(PrecisionAffine(torch.float16), torch.float16, 10, "float16")

    def test_bfloat16_export_and_raw_reference_bits(self):
        self._check_export(PrecisionAffine(torch.bfloat16), torch.bfloat16, 11, "uint16")

    def test_default_opset_supports_a_bfloat16_convolution(self):
        model = torch.nn.Conv2d(1, 2, 3, padding=1).bfloat16()
        example = torch.arange(64, dtype=torch.float32).reshape(1, 1, 8, 8).div(64).bfloat16()
        with arda.export(model, (example,)) as result:
            self.assertEqual(json.loads(result.manifest_path.read_text())["capture"]["opset_version"], 22)
            self.assertTrue(all(port["dtype"] == 11 for port in read_package(result.packages["sm_80"])["tensors"]))

    def test_int8_input_and_int32_accumulation_output(self):
        self._check_export(Int8Accumulation(), torch.int8, 2, "int8", target=61)

    def test_tensor_dtype_is_inferred_without_converting_model_weights(self):
        with tempfile.TemporaryDirectory() as directory:
            model = PrecisionAffine(torch.float16)
            result = arda.export(model, torch.zeros(2, 4, dtype=torch.float16), directory)
            package = read_package(result.packages["sm_80"])
            self.assertTrue(all(port["dtype"] == 10 for port in package["tensors"]))
            self.assertEqual(model.bias.dtype, torch.float16)

    def test_mixed_tensor_ports_keep_individual_dtypes(self):
        examples = (torch.ones(2, 4, dtype=torch.float16), torch.ones(2, 4, dtype=torch.bfloat16),
                    torch.ones(2, 4, dtype=torch.int8))
        with tempfile.TemporaryDirectory() as directory:
            result = arda.export(MixedPrecision(), examples, directory,
                input_names=["half_input", "brain_input", "quantized"])
            ports = {port["name"]: port["dtype"] for port in read_package(result.packages["sm_80"])["tensors"]}
            self.assertEqual(ports, {"half_input": 10, "brain_input": 11, "quantized": 2, "sum": 12, "half": 10, "counts": 6})

    def test_float8_e4m3fn_roundtrip_and_raw_reference_bits(self):
        self._check_export(Float8Roundtrip(torch.float8_e4m3fn), torch.float8_e4m3fn, 14, "uint8", target=89, opset=19)

    def test_float8_e5m2_roundtrip_and_raw_reference_bits(self):
        self._check_export(Float8Roundtrip(torch.float8_e5m2), torch.float8_e5m2, 15, "uint8", target=89, opset=19)

    def test_float8_requires_explicit_onnx_opset_19_or_newer(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler:
            with self.assertRaisesRegex(ExportError, "opset_version >= 19"):
                arda.export(Float8Roundtrip(torch.float8_e4m3fn),
                    torch.zeros(2, 4).to(torch.float8_e4m3fn), directory, target_archs=[89], opset_version=18)
            compiler.assert_not_called()

    def test_temporary_bundle_cannot_be_saved_under_its_owned_directory(self):
        with arda.export(torch.nn.ReLU(), torch.ones(2, 4)) as result:
            for directory in (result.manifest_path.parent, result.manifest_path.parent / "saved"):
                with self.subTest(directory=directory), self.assertRaisesRegex(ExportError, "outside its owned"):
                    result.save(directory)
            self.assertTrue(result.manifest_path.is_file())

    def test_float8_cast_normalization_matches_torch_dtype_specific_overflow(self):
        for dtype, enum, maximum in ((torch.float8_e4m3fn, 17, 448.0), (torch.float8_e5m2, 19, 57344.0)):
            with self.subTest(dtype=dtype):
                values = np.asarray([-maximum * 2, -maximum, -0.1, -0.0, 0.0, 0.1, maximum, maximum * 2], dtype=np.float32)
                cast = onnx.helper.make_node("Cast", ["source"], ["encoded"], to=enum)
                cast.metadata_props.add(key="pkg.torch.onnx.fx_node",
                    value="%cast = call_function[target=torch.ops.aten._to_copy.default](args=(%source,), kwargs={dtype: torch.float8})")
                graph = onnx.helper.make_graph([cast, onnx.helper.make_node("Cast", ["encoded"], ["result"], to=1)],
                    "fp8_boundary", [onnx.helper.make_tensor_value_info("source", 1, [len(values)])],
                    [onnx.helper.make_tensor_value_info("result", 1, [len(values)])])
                model = onnx.helper.make_model(graph, opset_imports=[onnx.helper.make_opsetid("", 19)])
                _normalize_torch_float8_casts(model)
                actual = ReferenceEvaluator(model).run(None, {"source": values})[0]
                expected = torch.from_numpy(values).to(dtype).float().numpy()
                np.testing.assert_array_equal(actual, expected)

    def test_unidentified_float8_cast_provenance_is_rejected(self):
        graph = onnx.helper.make_graph([onnx.helper.make_node("Cast", ["source"], ["result"], to=17)],
            "unknown_cast", [onnx.helper.make_tensor_value_info("source", 1, [2])],
            [onnx.helper.make_tensor_value_info("result", 17, [2])])
        with self.assertRaisesRegex(ExportError, "Cannot establish PyTorch conversion semantics"):
            _normalize_torch_float8_casts(onnx.helper.make_model(graph))


if __name__ == "__main__":
    unittest.main()
