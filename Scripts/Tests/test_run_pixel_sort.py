"""Check launch-only isolation, build failures, and executable/argument selection."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "run_pixel_sort", Path(__file__).resolve().parents[1] / "Examples" / "RunPixelSort.py")
launcher = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(Path(SPEC.origin).parent), *sys.path]):
    SPEC.loader.exec_module(launcher)
import ExampleBuild


class PixelSortLauncherTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="pixel sort ")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name).resolve()
        self.output = self.build / "Source" / "ArdaTests" / "Examples" / "PixelSort"
        self.output.mkdir(parents=True)
        self.executable = self.output / "PixelSort.exe"
        self.executable.touch()
        self.windows = patch.object(launcher.platform, "system", return_value="Windows")
        self.windows.start()
        self.addCleanup(self.windows.stop)
        self.environment = patch.object(ExampleBuild, "prepare_build_environment",
                                        return_value=("cmake", {"PATH": "build tools"}))
        self.prepare = self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_run_only_never_invokes_build_tools_and_preserves_skip_code(self):
        capture = self.build / "frame with spaces.png"
        with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 77)) as run:
            result = launcher.main([
                "vulkan", str(self.build), "Release", "--run-only", "--cuda-mode", "graphics",
                "--frames", "6", "--hidden", "--verify", "--resize-test", "--time", "4",
                "--capture", str(capture), "--channel", "2", "--threshold", "0",
            ])
        self.assertEqual(result, 77)
        self.prepare.assert_not_called()
        run.assert_called_once_with([
            str(self.executable), "--backend", "vulkan", "--cuda-mode", "graphics",
            "--frames", "6", "--channel", "2", "--threshold", "0", "--time", "4.0",
            "--capture", str(capture), "--hidden", "--verify", "--resize-test",
        ], check=False)

    def test_multiconfig_selects_requested_configuration(self):
        executable = self.output / "Debug" / "PixelSort.exe"
        executable.parent.mkdir()
        executable.touch()
        with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            launcher.main(["d3d12", str(self.build), "Debug", "--run-only"])
        self.assertEqual(run.call_args.args[0][0], str(executable))

    def test_missing_binary_does_not_fall_back_to_old_location_or_build(self):
        self.executable.unlink()
        stale = self.build / "Examples" / "PixelSort" / "PixelSort.exe"
        stale.parent.mkdir(parents=True)
        stale.touch()
        with patch.object(launcher.subprocess, "run") as run:
            with self.assertRaisesRegex(SystemExit, "omit --run-only"):
                launcher.main(["d3d12", str(self.build), "--run-only"])
        run.assert_not_called()

    def test_build_receives_toolkit_and_architecture_options_as_single_arguments(self):
        nvcc = self.build / "CUDA toolkit" / "bin" / "nvcc.exe"
        with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertEqual(launcher.main([
                "d3d12", str(self.build), "Release", "--nvcc", str(nvcc), "--generator", "Ninja",
                "--architectures", "80;120", "--cmake-arg=-DARDASHIR_BUILD_TESTS=ON",
            ]), 0)
        configure, build, launch = [call.args[0] for call in run.call_args_list]
        self.assertIn(f"-DCMAKE_CUDA_COMPILER={nvcc}", configure)
        self.assertIn(f"-DARDASHIR_CUDA_INCLUDE_DIR={nvcc.parent.parent / 'include'}", configure)
        self.assertIn("-DARDASHIR_CUDA_ARCHITECTURES=80;120", configure)
        self.assertIn("-DARDASHIR_BUILD_ARDG_EXAMPLE=OFF", configure)
        self.assertEqual(configure[-1], "-DARDASHIR_BUILD_TESTS=ON")
        self.assertEqual(build[build.index("--target") + 1], "PixelSort")
        self.assertEqual(launch[0], str(self.executable))
        self.assertEqual(run.call_args_list[0].kwargs["env"], {"PATH": "build tools"})
        self.assertEqual(run.call_args_list[1].kwargs["env"], {"PATH": "build tools"})
        self.assertNotIn("env", run.call_args_list[2].kwargs)

    def test_existing_build_preserves_test_configuration(self):
        (self.build / "CMakeCache.txt").touch()
        with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            launcher.main(["vulkan", str(self.build)])
        configure = run.call_args_list[0].args[0]
        self.assertFalse(any(arg.startswith("-DARDASHIR_BUILD_TESTS=") for arg in configure))
        self.assertNotIn("-G", configure)

    def test_new_build_uses_ninja_without_an_explicit_generator(self):
        with patch.dict(os.environ, {"CMAKE_GENERATOR": ""}):
            with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
                launcher.main(["vulkan", str(self.build)])
        configure = run.call_args_list[0].args[0]
        self.assertEqual(configure[configure.index("-G") + 1], "Ninja")

    def test_generator_environment_and_command_line_are_respected(self):
        with patch.dict(os.environ, {"CMAKE_GENERATOR": "Visual Studio 17 2022"}):
            with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
                launcher.main(["vulkan", str(self.build)])
                self.assertNotIn("-G", run.call_args_list[0].args[0])
            with patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
                launcher.main(["vulkan", str(self.build), "--generator", "Ninja Multi-Config"])
                configure = run.call_args_list[0].args[0]
                self.assertEqual(configure[configure.index("-G") + 1], "Ninja Multi-Config")

    def test_failed_configure_stops_before_build_or_launch(self):
        with patch.object(launcher.subprocess, "run", side_effect=subprocess.CalledProcessError(9, "cmake")) as run:
            self.assertEqual(launcher.main(["vulkan", str(self.build)]), 9)
        run.assert_called_once()


if __name__ == "__main__":
    unittest.main()
