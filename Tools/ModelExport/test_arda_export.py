"""Public torch export contract tests using the pinned offline compiler environment."""

from pathlib import Path
import errno
import json
import os
import shutil
import tempfile
import unittest
from unittest import mock

import numpy as np
import onnx
from onnx import numpy_helper
import torch

import arda
from ArdaModelPackage import ExportError, load_state_dict, read_package, read_state_dict


class CheckpointModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.scale = torch.nn.Parameter(torch.ones(4))
        self.register_buffer("bias", torch.zeros(4))
        self.register_buffer("scratch", torch.tensor([17.0]), persistent=False)
        self.identity = torch.nn.Identity().eval()

    def forward(self, left, right, *, strength=0.25):
        prediction = self.identity(left * self.scale + self.bias + right * strength)
        return prediction, prediction - left


class DataDependentModule(torch.nn.Module):
    def forward(self, value):
        if value.sum().item() > 0:
            return value + 1
        return value - 1


class StatefulModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.register_buffer("counter", torch.zeros(1))

    def forward(self, value):
        self.counter.add_(1)
        return value + self.counter


class InputMutationModule(torch.nn.Module):
    def forward(self, value):
        return value.add_(1)


class RandomModule(torch.nn.Module):
    def forward(self, value):
        return value + torch.rand_like(value)


class DifferentShapeModule(torch.nn.Module):
    def forward(self, matrix, bias):
        return matrix + bias


class KeywordOnlyModule(torch.nn.Module):
    def forward(self, matrix, *, bias):
        return matrix + bias


class FrozenArgumentModule(torch.nn.Module):
    def forward(self, matrix, strength, *, bias, shift=0.5):
        return matrix * strength + bias + shift


class OptionalBeforeRequiredModule(torch.nn.Module):
    def forward(self, matrix, optional=None, *, bias):
        result = matrix + bias
        return result if optional is None else result + optional


class NestedInputModule(torch.nn.Module):
    def forward(self, batch, scale, *, state):
        return {"prediction": batch["image"] + batch["features"][0] * scale + state}


class UnusedAndConstantStateModule(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.weight = torch.nn.Parameter(torch.tensor(2.0))
        self.register_buffer("unused", torch.ones(3))
        self.register_buffer("constant", torch.tensor([3.0]), persistent=False)

    def forward(self, value):
        return value * self.weight + self.constant


def make_checkpoint_model():
    model = CheckpointModule()
    model.load_state_dict({"scale": torch.tensor([1.25, -2.5, 3.75, 0.125]),
                           "bias": torch.tensor([0.5, 2.0, -0.75, 4.0])})
    model.scale.grad = torch.tensor([11.0, 12.0, 13.0, 14.0])
    return model


def snapshot_model(model):
    return {
        "training": [(name, child.training) for name, child in model.named_modules()],
        "tensors": {name: (id(value), value.device, value.dtype, value.detach().clone())
                    for name, value in list(model.named_parameters()) + list(model.named_buffers())},
        "gradients": {name: None if value.grad is None else value.grad.detach().clone()
                      for name, value in model.named_parameters()},
    }


def snapshot_tree(root):
    return {str(path.relative_to(root)): None if path.is_dir() else path.read_bytes()
            for path in root.rglob("*")}


class ArdaExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.model = make_checkpoint_model()
        cls.examples = {"left": torch.arange(8, dtype=torch.float32).reshape(2, 4),
                        "right": torch.full((2, 4), 2.0)}
        cls.before = snapshot_model(cls.model)
        cls.rng_before = torch.random.get_rng_state().clone()
        cls.result = arda.export(cls.model, tuple(cls.examples.values()), target_archs=[80, "sm_86"], input_names=list(cls.examples),
                                 output_names=["prediction", "residual"], kwargs={"strength": 0.25},
                                 f=cls.directory, name="checkpoint")
        cls.rng_after = torch.random.get_rng_state().clone()

    def assert_model_preserved(self, model, before):
        after = snapshot_model(model)
        self.assertEqual(after["training"], before["training"])
        self.assertEqual(after["tensors"].keys(), before["tensors"].keys())
        for name, original in before["tensors"].items():
            current = after["tensors"][name]
            self.assertEqual(current[:3], original[:3], name)
            self.assertTrue(torch.equal(current[3], original[3]), name)
        for name, original in before["gradients"].items():
            current = after["gradients"][name]
            if original is None:
                self.assertIsNone(current)
            else:
                self.assertTrue(torch.equal(current, original), name)


    def test_export_preserves_model_weights_buffers_gradients_training_flags_and_rng(self):
        self.assert_model_preserved(self.model, self.before)
        self.assertTrue(torch.equal(self.rng_before, self.rng_after))
        torch.testing.assert_close(self.examples["left"], torch.arange(8, dtype=torch.float32).reshape(2, 4))
        torch.testing.assert_close(self.examples["right"], torch.full((2, 4), 2.0))

    def test_multi_input_named_outputs_and_all_architectures_are_published(self):
        self.assertEqual(self.result.name, "checkpoint")
        self.assertEqual(set(self.result.packages), {"sm_80", "sm_86"})
        self.assertEqual(self.result.descriptors, self.result.packages)
        self.assertEqual(set(self.result.checkpoints), set(self.result.descriptors))
        self.assertEqual({artifact.target_sm for artifact in self.result.artifacts}, {80, 86})
        for path in (self.result.manifest_path, self.result.onnx_path, self.result.reference_path):
            self.assertTrue(Path(path).is_file(), path)
        manifest = json.loads(self.result.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["version"], 2)
        self.assertEqual(set(manifest["runtime_files"]),
                         {path.name for path in [*self.result.descriptors.values(), *self.result.checkpoints.values()]})
        self.assertIn("not_run", json.dumps(manifest))
        for artifact in self.result.artifacts:
            with self.subTest(architecture=artifact.target_sm):
                self.assertTrue(artifact.package_path.is_file())
                self.assertTrue(artifact.metadata_path.is_file())
                self.assertTrue(artifact.checkpoint_path.is_file())
                self.assertEqual(artifact.descriptor_path, artifact.package_path)
                self.assertEqual(self.result.packages[f"sm_{artifact.target_sm}"], artifact.package_path)
                package = read_package(artifact.package_path)
                self.assertEqual(package["version"], 2)
                self.assertEqual({slot["name"] for slot in package["state_slots"]}, {"scale", "bias"})
                self.assertTrue(all(not package["buffers"][slot["buffer"]]["initializer"]
                                    for slot in package["state_slots"]))
                self.assertEqual({module["target_sm"] for module in package["modules"]}, {artifact.target_sm})
                tensors = {port["name"]: port for port in package["tensors"] if port["role"]}
                self.assertEqual(set(tensors), {"left", "right", "prediction", "residual"})
                for name, port in tensors.items():
                    self.assertEqual(port["shape"], [2, 4], name)
                    self.assertEqual(port["dtype"], 12, name)
                    self.assertEqual(port["role"], 1 if name in self.examples else 2, name)

    def test_loaded_checkpoint_and_reference_outputs_are_preserved(self):
        initializers = [numpy_helper.to_array(value) for value in onnx.load(self.result.onnx_path).graph.initializer]
        for name in ("scale", "bias"):
            expected = self.before["tensors"][name][3].numpy()
            self.assertTrue(any(np.array_equal(value, expected) for value in initializers), name)
        with np.load(self.result.reference_path) as reference:
            self.assertEqual(set(reference.files), {"left", "right", "prediction", "residual"})
            expected = (self.examples["left"] * self.model.scale + self.model.bias + self.examples["right"] * 0.25)
            np.testing.assert_array_equal(reference["left"], self.examples["left"].numpy())
            np.testing.assert_array_equal(reference["right"], self.examples["right"].numpy())
            np.testing.assert_allclose(reference["prediction"], expected.detach().numpy(), rtol=0, atol=0)
            np.testing.assert_allclose(reference["residual"], (expected - self.examples["left"]).detach().numpy(),
                                       rtol=0, atol=0)

    def test_replacement_checkpoint_uses_existing_descriptors_without_compiling(self):
        alternate = {name: value.detach().clone() + 5 for name, value in self.model.state_dict().items()}
        descriptors_before = {target: path.read_bytes() for target, path in self.result.descriptors.items()}
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler, \
                mock.patch("torch.export.export") as capture:
            path = arda.export_state_dict(alternate, self.result, Path(directory) / "alternate.ardastate")
            compiler.assert_not_called()
            capture.assert_not_called()
            state = read_state_dict(path)
            self.assertEqual({entry["name"] for entry in state["entries"]}, set(alternate))
            for artifact in self.result.artifacts:
                descriptor = read_package(artifact.descriptor_path)
                loaded = load_state_dict(descriptor, state)
                initial = load_state_dict(descriptor, artifact.checkpoint_path)
                for slot in descriptor["state_slots"]:
                    self.assertEqual(loaded["buffers"][slot["buffer"]]["initializer"],
                                     alternate[slot["name"]].numpy().tobytes())
                    self.assertNotEqual(loaded["buffers"][slot["buffer"]]["initializer"],
                                        initial["buffers"][slot["buffer"]]["initializer"])
                    self.assertEqual(descriptor["buffers"][slot["buffer"]]["initializer"], b"")
            self.assertEqual(descriptors_before,
                             {target: descriptor.read_bytes() for target, descriptor in self.result.descriptors.items()})

    def test_invalid_replacement_preserves_the_existing_checkpoint(self):
        original = {name: value.detach().clone() for name, value in self.model.state_dict().items()}
        invalid = [({"scale": original["scale"]}, "keys"),
                   ({**original, "unexpected": torch.ones(4)}, "keys"),
                   ({**original, "scale": original["scale"].half()}, "dtype"),
                   ({**original, "bias": torch.ones(1)}, "shape")]
        with tempfile.TemporaryDirectory() as directory:
            path = arda.export_state_dict(self.model, self.result.artifacts[0], Path(directory) / "state.ardastate")
            before = snapshot_tree(Path(directory))
            for state, message in invalid:
                with self.subTest(message=message), self.assertRaisesRegex(ExportError, message):
                    arda.export_state_dict(state, self.result, path)
                self.assertEqual(snapshot_tree(Path(directory)), before)

    @unittest.skipUnless(os.environ.get("ARDA_TEST_CUDA_PRECISION") == "1", "requires a CUDA GPU sm_80 or newer")
    def test_driver_reuses_one_descriptor_with_independent_checkpoints(self):
        from ArdaCudaReplay import CudaReplay
        artifact = self.result.artifacts[0]
        before = artifact.descriptor_path.read_bytes()
        descriptor = read_package(artifact.descriptor_path)
        state = {"scale": self.model.scale.detach() * 2, "bias": self.model.bias.detach() + 3}
        inputs = {name: value.numpy().tobytes() for name, value in self.examples.items()}
        expected_initial = (self.examples["left"] * self.model.scale + self.model.bias +
                            self.examples["right"] * 0.25).detach().numpy()
        expected_alternate = (self.examples["left"] * state["scale"] + state["bias"] +
                              self.examples["right"] * 0.25).numpy()
        with tempfile.TemporaryDirectory() as directory, mock.patch("arda._export.compile_model") as compiler:
            alternate = arda.export_state_dict(state, self.result, Path(directory) / "alternate.ardastate")
            with CudaReplay(descriptor, state_dict=artifact.checkpoint_path) as original_runner, \
                    CudaReplay(descriptor, state_dict=alternate) as alternate_runner:
                first = np.frombuffer(original_runner.run(inputs)["prediction"], dtype=np.float32).reshape(2, 4)
                second = np.frombuffer(alternate_runner.run(inputs)["prediction"], dtype=np.float32).reshape(2, 4)
                first_again = np.frombuffer(original_runner.run(inputs)["prediction"], dtype=np.float32).reshape(2, 4)
                np.testing.assert_allclose(first, expected_initial, rtol=1e-6, atol=1e-6)
                np.testing.assert_allclose(second, expected_alternate, rtol=1e-6, atol=1e-6)
                np.testing.assert_array_equal(first_again, first)
                self.assertFalse(np.array_equal(first, second))
            compiler.assert_not_called()
        self.assertEqual(artifact.descriptor_path.read_bytes(), before)
        self.assertTrue(all(not descriptor["buffers"][slot["buffer"]]["initializer"]
                            for slot in descriptor["state_slots"]))

    def test_scalar_state_and_known_unused_keys_are_supported_without_exporting_constants(self):
        model = UnusedAndConstantStateModule()
        with tempfile.TemporaryDirectory() as directory:
            result = arda.export(model, torch.ones(2, 4), directory)
            descriptor = read_package(result.descriptors["sm_80"])
            self.assertEqual([(slot["name"], slot["shape"]) for slot in descriptor["state_slots"]], [("weight", [])])
            state = {"weight": torch.tensor(4.0), "unused": torch.zeros(3)}
            path = arda.export_state_dict(state, result, Path(directory) / "updated.ardastate")
            self.assertEqual(read_state_dict(path)["entries"],
                             [{"name": "weight", "dtype": 12, "shape": [], "data": state["weight"].numpy().tobytes()}])
            with self.assertRaisesRegex(ExportError, "unexpected=.*unused"):
                arda.export_state_dict(state, result.descriptors["sm_80"], path)

    def test_tied_state_is_rejected_before_capture(self):
        model = make_checkpoint_model()
        model.register_parameter("tied_scale", model.scale)
        with mock.patch("torch.export.export") as capture:
            with self.assertRaisesRegex(ExportError, "tied parameters"):
                arda.export(model, tuple(self.examples.values()))
            capture.assert_not_called()

    def test_model_argument_binding_is_checked_before_capture(self):
        matrix, bias = torch.ones(2, 4), torch.ones(4)
        for args, kwargs in (((matrix,), {}), ((matrix, bias, bias), {}),
                             ((matrix, bias), {"unknown": 1}), ((matrix, bias), {"matrix": matrix})):
            with self.subTest(args=len(args), kwargs=list(kwargs)), tempfile.TemporaryDirectory() as directory:
                with mock.patch("torch.export.export") as capture, mock.patch("arda._export.compile_model") as compiler:
                    with self.assertRaises(ExportError):
                        arda.export(DifferentShapeModule(), args, directory, kwargs=kwargs)
                    capture.assert_not_called()
                    compiler.assert_not_called()
                self.assertEqual(snapshot_tree(Path(directory)), {})

    def test_keyword_only_tensors_and_frozen_scalars_infer_tensor_ports(self):
        matrix = torch.arange(8, dtype=torch.float32).reshape(2, 4)
        bias = torch.arange(4, dtype=torch.float32)
        with tempfile.TemporaryDirectory() as directory:
            result = arda.export(FrozenArgumentModule(), (matrix, 2.0), directory, kwargs={"bias": bias},
                                 input_names=["image", "offset"], output_names=["prediction"])
            ports = [port for port in read_package(result.packages["sm_80"])["tensors"] if port["role"] == 1]
            self.assertEqual([(port["name"], port["shape"]) for port in ports], [("image", [2, 4]), ("offset", [4])])
            with np.load(result.reference_path) as reference:
                np.testing.assert_array_equal(reference["prediction"], (matrix * 2 + bias + 0.5).numpy())

    def test_nested_inputs_preserve_order_values_and_output_paths(self):
        matrix = torch.arange(8, dtype=torch.float32).reshape(2, 4)
        features, state = torch.ones(4), torch.full((2, 4), 0.5)
        batch = {"image": matrix, "features": [features]}
        with tempfile.TemporaryDirectory() as directory:
            result = arda.export(NestedInputModule(), (batch, 3.0), directory, kwargs={"state": state},
                                 input_names=["image", "features", "history"], output_names=["prediction"])
            with np.load(result.reference_path) as reference:
                self.assertEqual(set(reference.files), {"image", "features", "history", "prediction"})
                np.testing.assert_array_equal(reference["image"], matrix.numpy())
                np.testing.assert_array_equal(reference["prediction"], (matrix + features * 3 + state).numpy())
            torch.testing.assert_close(batch["image"], matrix)

    def test_deferred_save_owns_temporary_files_and_preserves_saved_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            with arda.export(torch.nn.ReLU(), (torch.arange(-4, 4, dtype=torch.float32),)) as pending:
                temporary_manifest = pending.manifest_path
                self.assertTrue(temporary_manifest.is_file())
                saved = pending.save(Path(directory) / "saved")
                self.assertTrue(temporary_manifest.is_file())
                self.assertEqual(saved.manifest_path.read_bytes(), temporary_manifest.read_bytes())
                for architecture in saved.packages:
                    self.assertEqual(saved.packages[architecture].read_bytes(), pending.packages[architecture].read_bytes())
            self.assertFalse(temporary_manifest.exists())
            saved.close()
            self.assertTrue(saved.manifest_path.exists())

    def test_invalid_tensor_examples_do_not_enter_capture(self):
        for example in (torch.empty(0, 4), torch.ones(2, 4, dtype=torch.complex64),
                        torch.ones(2, 4).t(), torch.full((2, 4), float("nan"))):
            with self.subTest(dtype=example.dtype, shape=example.shape), tempfile.TemporaryDirectory() as directory:
                with mock.patch("torch.export.export") as capture:
                    with self.assertRaises(ExportError):
                        arda.export(torch.nn.ReLU(), (example,), directory)
                    capture.assert_not_called()
                self.assertEqual(snapshot_tree(Path(directory)), {})

    def test_architecture_errors_do_not_start_compilation_or_change_destination(self):
        for architectures in ([], [80, "sm_80"], [999], ["vulkan"], [True]):
            with self.subTest(architectures=architectures), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "keep.txt").write_text("existing output", encoding="utf-8")
                original = snapshot_tree(root)
                with mock.patch("arda._export.compile_model") as compiler:
                    with self.assertRaises((ValueError, TypeError)):
                        arda.export(torch.nn.ReLU(), (torch.ones(2, 4),), target_archs=architectures, f=root)
                    compiler.assert_not_called()
                self.assertEqual(snapshot_tree(root), original)


    def test_publication_supports_a_destination_on_a_different_filesystem(self):
        import arda._export as implementation
        compile_model = implementation.compile_model
        replace = os.replace
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            output = root / "published"

            def separate_filesystems(source, destination, *args, **kwargs):
                source = Path(source).resolve()
                destination = Path(destination).resolve()
                if destination.is_relative_to(root) and not source.is_relative_to(root):
                    raise OSError(errno.EXDEV, "Cross-device rename", str(source))
                return replace(source, destination, *args, **kwargs)

            def compile_before_publication(*args, **kwargs):
                self.assertFalse(output.exists())
                return compile_model(*args, **kwargs)

            with mock.patch("os.replace", side_effect=separate_filesystems), \
                    mock.patch("arda._export.compile_model", side_effect=compile_before_publication):
                result = arda.export(torch.nn.ReLU(), (torch.ones(2, 4),), target_archs=[80],
                                     f=output, name="cross-device")
            self.assertTrue(result.manifest_path.is_file())
            self.assertTrue(result.reference_path.is_file())
            self.assertTrue(read_package(result.packages["sm_80"])["commands"])


    def test_names_and_frozen_arguments_cannot_conflict(self):
        cases = ({"input_names": ["same", "same"]},
                 {"input_names": ["left"]},
                 {"output_names": ["prediction"]},
                 {"output_names": ["prediction", "prediction"]},
                 {"output_names": ["input0", "residual"]},
                 {"kwargs": {"left": 2.0}},
                 {"kwargs": {"unknown": 0.25}})
        for options in cases:
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                before = snapshot_model(self.model)
                with mock.patch("arda._export.compile_model") as compiler:
                    with self.assertRaises((ValueError, TypeError)):
                        arda.export(self.model, tuple(self.examples.values()), target_archs=[80], f=root, **options)
                    compiler.assert_not_called()
                self.assertEqual(snapshot_tree(root), {})
                self.assert_model_preserved(self.model, before)

    def test_second_architecture_failure_preserves_existing_bundle_and_model(self):
        import arda._export as implementation
        compile_model = implementation.compile_model
        completed_targets = []

        def fail_second_target(source, output, target, *args, **kwargs):
            if target == 86:
                raise ExportError("injected second-target compiler failure")
            package = compile_model(source, output, target, *args, **kwargs)
            completed_targets.append(target)
            return package

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(self.directory, root, dirs_exist_ok=True)
            original = snapshot_tree(root)
            before = snapshot_model(self.model)
            rng_before = torch.random.get_rng_state().clone()
            with mock.patch("arda._export.compile_model", side_effect=fail_second_target):
                with self.assertRaisesRegex(ExportError, "second-target"):
                    arda.export(self.model, tuple(self.examples.values()), target_archs=[80, 86], input_names=list(self.examples),
                                output_names=["prediction", "residual"], f=root, name="checkpoint")
            self.assertEqual(completed_targets, [80])
            self.assertEqual(snapshot_tree(root), original)
            self.assert_model_preserved(self.model, before)
            self.assertTrue(torch.equal(rng_before, torch.random.get_rng_state()))

    def test_manifest_publication_failure_restores_the_complete_previous_bundle(self):
        replace = os.replace
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            shutil.copytree(self.directory, root, dirs_exist_ok=True)
            original = snapshot_tree(root)
            failed = False

            def fail_new_manifest(source, destination, *args, **kwargs):
                nonlocal failed
                if not failed and Path(destination).resolve() == root / "checkpoint.manifest.json":
                    failed = True
                    raise OSError(errno.EACCES, "Injected manifest publication failure")
                return replace(source, destination, *args, **kwargs)

            with mock.patch("os.replace", side_effect=fail_new_manifest):
                with self.assertRaisesRegex(OSError, "manifest publication"):
                    arda.export(self.model, tuple(self.examples.values()), target_archs=[80, 86], input_names=list(self.examples),
                                output_names=["prediction", "residual"], f=root, name="checkpoint")
            self.assertTrue(failed)
            self.assertEqual(snapshot_tree(root), original)

    def test_data_dependent_python_control_flow_is_rejected_without_trace_specialization(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch("arda._export.compile_model") as compiler:
                with self.assertRaises(ExportError):
                    arda.export(DataDependentModule(), (torch.ones(2, 4),), target_archs=[80], f=root)
                compiler.assert_not_called()
            self.assertEqual(snapshot_tree(root), {})

    def test_implicit_state_mutation_and_randomness_are_rejected_before_compilation(self):
        for model in (StatefulModule(), InputMutationModule(), RandomModule()):
            with self.subTest(model=type(model).__name__), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                example = torch.ones(2, 4)
                before = snapshot_model(model)
                rng_before = torch.random.get_rng_state().clone()
                with mock.patch("arda._export.compile_model") as compiler:
                    with self.assertRaises(ExportError):
                        arda.export(model, (example,), target_archs=[80], f=root)
                    compiler.assert_not_called()
                self.assertEqual(snapshot_tree(root), {})
                self.assert_model_preserved(model, before)
                torch.testing.assert_close(example, torch.ones(2, 4))
                self.assertTrue(torch.equal(rng_before, torch.random.get_rng_state()))


if __name__ == "__main__":
    unittest.main()
