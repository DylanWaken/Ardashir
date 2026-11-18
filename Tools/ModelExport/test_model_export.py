"""Contract regression tests; run with the pinned offline exporter environment."""

from pathlib import Path
import copy
import struct
import tempfile
import unittest
from unittest import mock

from ArdaModelPackage import (ExportError, load_state_dict, read_package, read_state_dict,
    validate_package, validate_state_dict, write_package, write_state_dict)


def package_fixture():
    return {"version": 1, "name": "fixture", "modules": [{"format": 0, "target_sm": 80,
        "build_identity": "test", "image": b".version 7.6\n.target sm_80\n.address_size 64\n"
            b".visible .entry fixture(.param .u64 pointer, .param .u32 count) { ret; }"}],
        "buffers": [{"name": "data", "byte_size": 16, "initializer": b""}],
        "tensors": [{"name": "data", "buffer": 0, "role": 3, "dtype": 12, "shape": [4],
                      "strides": [1], "offset": 0, "byte_size": 16}],
        "kernels": [{"name": "fixture", "module": 0, "entry": "fixture", "parameter_size": 12,
            "parameter_alignment": 8, "arguments": [
                {"name": "pointer", "kind": 1, "access": 2, "offset": 0, "size": 8, "alignment": 8},
                {"name": "count", "kind": 0, "access": 0, "offset": 8, "size": 4, "alignment": 4}]}],
        "commands": [{"kind": "fill", "buffer": 0, "offset": 0, "size": 16, "pattern": b"\0"},
            {"kind": "copy", "source": 0, "source_offset": 0, "destination": 0, "destination_offset": 8, "size": 8},
            {"kind": "dispatch", "kernel": 0, "grid": [1, 1, 1], "block": [32, 1, 1], "shared_memory": 0,
             "arguments": [{"buffer": 0, "offset": 0, "range": 16, "value": b""},
                           {"buffer": 0xffffffff, "offset": 0, "range": 0, "value": b"\4\0\0\0"}]}]}


class PackageContractTests(unittest.TestCase):
    def test_package_architecture_must_match_embedded_ptx_target(self):
        package = package_fixture()
        package["modules"][0]["target_sm"] = 120
        with self.assertRaisesRegex(ExportError, "PTX target"):
            validate_package(package)

    def test_tensor_storage_size_and_alignment_must_match_dtype(self):
        package = package_fixture()
        package["tensors"][0]["dtype"] = 10
        with self.assertRaisesRegex(ExportError, "byte size"):
            validate_package(package)
        package["tensors"][0].update(byte_size=8, offset=1)
        with self.assertRaisesRegex(ExportError, "aligned"):
            validate_package(package)

    def test_binary_round_trip_preserves_weights_layout_and_all_commands(self):
        package = package_fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.ardagpu"
            write_package(package, path)
            self.assertEqual(package, read_package(path))
            valid = path.read_bytes()
            for corrupted in (valid[:-1], valid + b"extra", valid[:12] + bytes([255]) * 8 + valid[20:]):
                path.write_bytes(corrupted)
                with self.assertRaises(ExportError):
                    read_package(path)

    def test_external_gpu_library_symbols_are_rejected(self):
        for symbol in (b".extern .func library_gemm();", b".extern .align 16 .global .b8 external_weights[64];"):
            package = package_fixture()
            package["modules"][0]["image"] += symbol
            with self.assertRaisesRegex(ExportError, "unresolved external"):
                validate_package(package)

    def test_registry_identity_separates_models_and_changed_device_code(self):
        from ArdaIreeExport import kernel_identity
        first = kernel_identity("model64", b"ptx1", "main")
        self.assertEqual(first, kernel_identity("model64", b"ptx1", "main"))
        self.assertNotEqual(first, kernel_identity("model256", b"ptx1", "main"))
        self.assertNotEqual(first, kernel_identity("model64", b"ptx2", "main"))

    def test_mutable_storage_is_initialized_but_external_inputs_are_not_overwritten(self):
        from ArdaIreeExport import StaticHalExporter
        from iree.compiler import ir
        with ir.Context(), ir.Location.unknown():
            exporter = StaticHalExporter(ir.Module.create(), "init", 80)
            exporter.allocate(64, name="input", initialize_zero=False)
            exporter.allocate(64, initializer=bytes(64), name="checkpoint")
            scratch = exporter.allocate(64)
            self.assertEqual(exporter.package["commands"], [{"kind": "fill", "buffer": scratch.index,
                "offset": 0, "size": 64, "pattern": b"\0"}])

    def test_dynamic_shared_memory_declaration_is_allowed(self):
        package = package_fixture()
        package["modules"][0]["image"] += b".extern .shared .align 16 .b8 shared[];"
        validate_package(package)

    def test_ptx_parameter_abi_mismatch_is_rejected(self):
        package = package_fixture()
        package["modules"][0]["image"] = package["modules"][0]["image"].replace(b".param .u32", b".param .u64")
        with self.assertRaisesRegex(ExportError, "parameter types"):
            validate_package(package)

    def test_out_of_bounds_binding_and_overlapping_copy_are_rejected(self):
        package = package_fixture()
        package["commands"][2]["arguments"][0]["range"] = 17
        with self.assertRaisesRegex(ExportError, "out of bounds"):
            validate_package(package)
        package = package_fixture()
        package["commands"][1]["destination_offset"] = 4
        with self.assertRaisesRegex(ExportError, "Overlapping"):
            validate_package(package)

    def test_export_rejects_bindings_the_runtime_cannot_load(self):
        package = package_fixture()
        for bad_range in ({"range": 0}, {"offset": 16}):
            invalid = copy.deepcopy(package)
            invalid["commands"][2]["arguments"][0].update(bad_range)
            with self.assertRaises(ExportError):
                validate_package(invalid)
        for field in ("offset", "range"):
            invalid = copy.deepcopy(package)
            invalid["commands"][2]["arguments"][1][field] = 1
            with self.assertRaisesRegex(ExportError, "Invalid scalar"):
                validate_package(invalid)
        invalid = copy.deepcopy(package)
        invalid["buffers"][0]["initializer"] = bytes(16)
        with self.assertRaisesRegex(ExportError, "cannot write state slots or initialized buffers"):
            validate_package(invalid)

    def test_malformed_text_reports_the_package_error_contract(self):
        package = package_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "model.ardagpu"
            write_package(package, path)
            data = bytearray(path.read_bytes())
            data[20] = 0xff  # First byte of the length-prefixed model name.
            path.write_bytes(data)
            with self.assertRaisesRegex(ExportError, "Invalid UTF-8"):
                read_package(path)
        package["modules"][0]["image"] = b"\xff"
        with self.assertRaisesRegex(ExportError, "Invalid UTF-8"):
            validate_package(package)

    def test_unknown_host_operation_cannot_escape_into_package(self):
        from ArdaIreeExport import StaticHalExporter
        from iree.compiler import ir
        with ir.Context():
            module = ir.Module.parse('module { util.func public @main() { %0 = arith.constant 1.0 : f32 util.return } }')
            with self.assertRaisesRegex(ExportError, "Non-integer host arithmetic"):
                StaticHalExporter(module, "unknown", 80).export("main")

    def test_data_dependent_shapes_are_rejected(self):
        from ArdaIreeExport import StaticHalExporter
        from iree.compiler import ir
        with ir.Context():
            module = ir.Module.parse('module { util.func public @main(%n: index) { util.return } }')
            with self.assertRaisesRegex(ExportError, "tensor buffer views"):
                StaticHalExporter(module, "dynamic", 80).export("main")

    def test_host_integer_overflow_and_narrowing_are_rejected(self):
        from ArdaIreeExport import StaticHalExporter
        from iree.compiler import ir
        programs = [
            '%a = arith.constant 2147483647 : i32 %b = arith.constant 1 : i32 %v = arith.addi %a, %b : i32',
            '%a = arith.constant 300 : index %v = arith.index_cast %a : index to i8']
        with ir.Context():
            for program in programs:
                module = ir.Module.parse('module { util.func public @main() { ' + program + ' util.return } }')
                with self.assertRaisesRegex(ExportError, "overflow or narrowing"):
                    StaticHalExporter(module, "overflow", 80).export("main")

    def test_unknown_encoding_and_queue_flags_are_rejected(self):
        from ArdaIreeExport import StaticHalExporter
        from iree.compiler import ir
        with ir.Context(), ir.Location.unknown():
            exporter = StaticHalExporter(ir.Module.create(), "flags", 80)
            operation = ir.Operation.create("hal.encoding_type", attributes={"encoding": ir.StringAttr.get("opaque")})
            with self.assertRaisesRegex(ExportError, "encoding"):
                exporter.evaluate(operation, [], 0)
            operation = ir.Operation.create("hal.device.queue.execute",
                attributes={"flags": ir.IntegerAttr.get(ir.IntegerType.get_signless(64), 1)})
            with self.assertRaisesRegex(ExportError, "flags"):
                exporter.evaluate(operation, [], 0)

    def test_negative_and_parent_exceeding_subspans_are_rejected(self):
        from ArdaIreeExport import Buffer, StaticHalExporter
        from iree.compiler import ir
        with ir.Context(), ir.Location.unknown():
            exporter = StaticHalExporter(ir.Module.create(), "bounds", 80)
            exporter.allocate(64)
            operation = ir.Operation.create("hal.buffer.subspan")
            for offset, length in ((-1, 8), (0, -1), (8, 9)):
                with self.assertRaisesRegex(ExportError, "out of range"):
                    exporter.evaluate(operation, [Buffer(0, 16, 16), offset, length], 0)
            operation = ir.Operation.create("hal.allocator.import")
            with self.assertRaisesRegex(ExportError, "Invalid weight"):
                exporter.evaluate(operation, [None] * 4 + [bytes(64), -1, 16], 0)


def descriptor_fixture():
    package = package_fixture()
    package["version"] = 2
    package["buffers"].append({"name": "weight", "byte_size": 8, "initializer": b""})
    package["state_slots"] = [{"name": "layer.weight", "buffer": 1, "dtype": 12,
        "shape": [2], "strides": [1], "offset": 0, "byte_size": 8}]
    state = {"version": 1, "entries": [{"name": "layer.weight", "dtype": 12,
        "shape": [2], "data": struct.pack("<ff", 2.0, 3.0)}]}
    return package, state


class StateDictContractTests(unittest.TestCase):
    def test_descriptor_checkpoint_round_trip_and_independent_weight_replacement(self):
        descriptor, state = descriptor_fixture()
        with tempfile.TemporaryDirectory() as directory:
            model_path, state_path = Path(directory) / "model.ardagpu", Path(directory) / "first.ardastate"
            write_package(descriptor, model_path)
            write_state_dict(state, state_path, descriptor)
            self.assertEqual(read_package(model_path), descriptor)
            self.assertEqual(read_state_dict(state_path), state)
            first = load_state_dict(descriptor, state_path)
            state["entries"][0]["data"] = struct.pack("<ff", 7.0, 11.0)
            second = load_state_dict(descriptor, state)
            self.assertEqual(descriptor["buffers"][1]["initializer"], b"")
            self.assertNotEqual(first["buffers"][1]["initializer"], second["buffers"][1]["initializer"])
            self.assertEqual(first["modules"], second["modules"])
            self.assertEqual(first["commands"], second["commands"])
            self.assertEqual(read_package(model_path), descriptor)
            valid = state_path.read_bytes()
            for corrupted in (valid[:-1], valid + b"extra", valid[:12] + b"\xff" * 8 + valid[20:]):
                state_path.write_bytes(corrupted)
                with self.assertRaises(ExportError):
                    read_state_dict(state_path)

    def test_checkpoint_rejects_missing_unexpected_duplicate_dtype_and_shape_before_mutation(self):
        descriptor, state = descriptor_fixture()
        invalid = []
        invalid.append({"version": 1, "entries": []})
        extra = copy.deepcopy(state)
        extra["entries"][0]["name"] = "wrong.weight"
        invalid.append(extra)
        duplicate = copy.deepcopy(state)
        duplicate["entries"] *= 2
        invalid.append(duplicate)
        dtype = copy.deepcopy(state)
        dtype["entries"][0]["dtype"] = 6
        invalid.append(dtype)
        shape = copy.deepcopy(state)
        shape["entries"][0]["shape"] = [1, 2]
        invalid.append(shape)
        data = copy.deepcopy(state)
        data["entries"][0]["data"] = b"short"
        invalid.append(data)
        original = copy.deepcopy(descriptor)
        for bad in invalid:
            with self.assertRaises(ExportError):
                load_state_dict(descriptor, bad)
            self.assertEqual(descriptor, original)

    def test_descriptor_rejects_embedded_parameter_bytes(self):
        descriptor, state = descriptor_fixture()
        descriptor["buffers"][1]["initializer"] = state["entries"][0]["data"]
        with self.assertRaisesRegex(ExportError, "cannot embed"):
            validate_package(descriptor)

    def test_state_slots_reject_fill_copy_and_mutable_dispatch_writes(self):
        for kind in ("fill", "copy", "dispatch"):
            descriptor, _ = descriptor_fixture()
            if kind == "fill":
                descriptor["commands"][0].update(buffer=1, size=8)
            elif kind == "copy":
                descriptor["commands"][1].update(destination=1, destination_offset=0)
            else:
                descriptor["commands"][2]["arguments"][0].update(buffer=1, range=8)
            with self.subTest(kind=kind), self.assertRaisesRegex(ExportError, "cannot write state"):
                validate_package(descriptor)
            if kind == "dispatch":
                descriptor["kernels"][0]["arguments"][0]["access"] = 0
                validate_package(descriptor)

    def test_model_publication_preserves_previous_pair_when_checkpoint_or_publish_fails(self):
        from ArdaIreeExport import _publish_model
        descriptor, state = descriptor_fixture()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "existing.ardagpu"
            checkpoint = output.with_suffix(".ardastate")
            previous = {output: b"previous model", checkpoint: b"previous checkpoint",
                        output.with_suffix(".json"): b"previous metadata"}
            for path, contents in previous.items():
                path.write_bytes(contents)
            with mock.patch("ArdaIreeExport.write_state_dict", side_effect=OSError("checkpoint failure")):
                with self.assertRaisesRegex(OSError, "checkpoint failure"):
                    _publish_model(descriptor, state, output, checkpoint)
            self.assertEqual({path: path.read_bytes() for path in previous}, previous)
            replace = Path.replace
            def fail_descriptor_publish(path, target):
                if path.name == "model.ardagpu" and target == output:
                    raise OSError("publish failure")
                return replace(path, target)
            with mock.patch.object(Path, "replace", fail_descriptor_publish):
                with self.assertRaisesRegex(OSError, "publish failure"):
                    _publish_model(descriptor, state, output, checkpoint)
            self.assertEqual({path: path.read_bytes() for path in previous}, previous)
            self.assertEqual(set(Path(directory).iterdir()), set(previous))

    def test_compiler_rejects_source_metadata_and_intermediate_output_collisions(self):
        from ArdaIreeExport import compile_model
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "source.onnx", Path(directory) / "model.ardagpu"
            source.write_bytes(b"source must be preserved")
            for state_output in (source, output, output.with_suffix(".json"),
                                 Path(directory) / "model-compiler" / "hal.mlir"):
                with self.subTest(state_output=state_output), self.assertRaises(ExportError):
                    compile_model(source, output, 80, state_output=state_output)
            self.assertEqual(source.read_bytes(), b"source must be preserved")
            self.assertEqual(list(Path(directory).iterdir()), [source])

    def test_scalar_state_and_empty_checkpoint_are_supported(self):
        descriptor, state = descriptor_fixture()
        descriptor["state_slots"][0].update(shape=[], strides=[], byte_size=4)
        descriptor["buffers"][1]["byte_size"] = 4
        state["entries"][0].update(shape=[], data=struct.pack("<f", 2.0))
        validate_state_dict(state, descriptor)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.ardastate"
            empty = {"version": 1, "entries": []}
            write_state_dict(empty, path)
            self.assertEqual(read_state_dict(path), empty)

    def test_repeated_scalar_assertion_preserves_the_same_state_buffer(self):
        from ArdaIreeExport import Buffer, StaticHalExporter, Tensor
        from iree.compiler import ir
        with ir.Context(), ir.Location.unknown():
            exporter = StaticHalExporter(ir.Module.create(), "scalar", 80)
            operation = ir.Operation.create("hal.buffer_view.assert")
            scalar = Tensor(Buffer(-1), [], "", "scale")
            exporter.evaluate(operation, [scalar, "f32", "dense"], 0)
            original = scalar.buffer
            exporter.evaluate(operation, [scalar, "f32", "dense"], 0)
            self.assertEqual(scalar.buffer, original)
            self.assertEqual(len(exporter.package["buffers"]), 1)
            with self.assertRaisesRegex(ExportError, "assertion mismatch"):
                exporter.evaluate(operation, [scalar, "f32", "dense", 1], 0)

    def test_onnx_explicit_state_preserves_raw_low_precision_and_retains_structural_constants(self):
        import onnx
        from ArdaIreeExport import externalize_onnx_state
        weight = onnx.TensorProto(name="weight", data_type=onnx.TensorProto.BFLOAT16,
                                  dims=[2], raw_data=b"\x01\x80\xc1\x7f")
        shape = onnx.helper.make_tensor("shape", onnx.TensorProto.INT64, [1], [2])
        model = onnx.helper.make_model(onnx.helper.make_graph([], "main", [], [], [weight, shape]))
        onnx.helper.set_model_props(model, {"arda.state_dict": '["weight"]'})
        state = externalize_onnx_state(model)
        self.assertEqual(state["entries"][0]["data"], weight.raw_data)
        self.assertEqual(state["entries"][0]["dtype"], 11)
        self.assertEqual([item.name for item in model.graph.input], ["weight"])
        self.assertEqual([item.name for item in model.graph.initializer], ["shape"])

    def test_compiling_changed_weights_produces_identical_descriptor_and_different_checkpoint(self):
        import onnx
        from ArdaIreeExport import compile_model
        with tempfile.TemporaryDirectory() as directory:
            source, descriptor = Path(directory) / "model.onnx", Path(directory) / "model.ardagpu"
            def save(values):
                graph = onnx.helper.make_graph([onnx.helper.make_node("MatMul", ["x", "weight"], ["y"])],
                    "main", [onnx.helper.make_tensor_value_info("x", onnx.TensorProto.FLOAT, [1, 2])],
                    [onnx.helper.make_tensor_value_info("y", onnx.TensorProto.FLOAT, [1, 2])],
                    [onnx.helper.make_tensor("weight", onnx.TensorProto.FLOAT, [2, 2], values)])
                onnx.save(onnx.helper.make_model(graph, opset_imports=[onnx.helper.make_opsetid("", 17)]), source)
            save([1, 2, 3, 4])
            first = compile_model(source, descriptor, 80)
            first_bytes = descriptor.read_bytes()
            first_state = read_state_dict(descriptor.with_suffix(".ardastate"))
            save([7, 11, 13, 17])
            second = compile_model(source, descriptor, 80)
            self.assertEqual(first_bytes, descriptor.read_bytes())
            self.assertNotEqual(first_state, read_state_dict(descriptor.with_suffix(".ardastate")))
            self.assertEqual([slot["name"] for slot in second["state_slots"]], ["weight"])
            self.assertEqual([port["name"] for port in second["tensors"]], ["x", "y"])
            self.assertEqual(first["modules"], second["modules"])


if __name__ == "__main__":
    unittest.main()
