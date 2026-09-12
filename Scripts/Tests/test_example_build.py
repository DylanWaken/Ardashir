"""Regression coverage for building examples outside a developer shell."""

import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

with patch.object(sys, "path", [str(Path(__file__).resolve().parents[1] / "Examples"), *sys.path]):
    import ExampleBuild as build


class ExampleBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arda build ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def test_cached_msvc_installation_and_exact_toolset_take_precedence(self):
        instance = self.directory / "VS 2022"
        compiler = instance / "VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe"
        with patch.object(build.subprocess, "run") as run:
            found, version = build.find_visual_studio(
                {"VSINSTALLDIR": "another installation"}, {"CMAKE_CXX_COMPILER": str(compiler)})
        self.assertEqual(found, instance)
        self.assertEqual(version, "14.44.35207")
        run.assert_not_called()

    def test_vswhere_finds_cpp_workload(self):
        with patch.object(build.shutil, "which", return_value="vswhere.exe"):
            with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], 0, stdout=b"D:\\Visual Studio\r\n")) as run:
                instance, version = build.find_visual_studio({}, {})
        self.assertEqual(instance, Path(r"D:\Visual Studio"))
        self.assertIsNone(version)
        self.assertIn("Microsoft.VisualStudio.Component.VC.Tools.x86.x64", run.call_args.args[0])

    def test_missing_cpp_tools_has_actionable_error(self):
        with patch.object(build.platform, "system", return_value="Windows"), \
                patch.dict(os.environ, {}, clear=True), \
                patch.object(build, "find_visual_studio", return_value=(None, None)), \
                patch.object(build.shutil, "which", return_value=None):
            with self.assertRaisesRegex(SystemExit, "Desktop development with C\\+\\+"):
                build.prepare_build_environment({}, "Ninja")

    def test_existing_generator_and_last_cmake_generator_override_are_respected(self):
        (self.directory / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022\n", encoding="utf-8")
        with patch.object(build, "prepare_build_environment", return_value=("cmake", {})) as prepare, \
                patch.object(build.subprocess, "run") as run:
            build.build_example("ARDGExample", self.directory, "Debug", [])
            self.assertEqual(prepare.call_args.args[1], "Visual Studio 17 2022")
            self.assertNotIn("-G", run.call_args_list[0].args[0])
            build.build_example("PixelSort", self.directory, "Release",
                                ["-G", "Ninja Multi-Config"], generator="Ninja")
            self.assertEqual(prepare.call_args.args[1], "Ninja Multi-Config")

    def test_non_windows_does_not_probe_visual_studio(self):
        with patch.object(build.platform, "system", return_value="Linux"), \
                patch.object(build, "find_visual_studio") as find:
            _, env = build.prepare_build_environment({}, "Ninja")
        self.assertEqual(env, dict(os.environ))
        find.assert_not_called()

    def test_local_sdk_defaults_precede_explicit_launcher_options(self):
        defaults = self.directory / "build/graphics-sdk/GraphicsSdkDefaults.cmake"
        defaults.parent.mkdir(parents=True)
        defaults.touch()
        override = "-DARDASHIR_VULKAN_VALIDATION_DIR=custom layer directory"
        with patch.object(build, "SOURCE_DIRECTORY", self.directory), \
                patch.object(build, "prepare_build_environment", return_value=("cmake", {})), \
                patch.object(build.subprocess, "run") as run:
            build.build_example("CornellBox", self.directory / "example", "Debug", [override])
        configure = run.call_args_list[0].args[0]
        self.assertEqual(configure[configure.index("-C") + 1], str(defaults))
        self.assertLess(configure.index("-C"), configure.index(override))

    def test_no_setup_does_not_add_a_missing_initial_cache(self):
        with patch.object(build, "SOURCE_DIRECTORY", self.directory), \
                patch.object(build, "prepare_build_environment", return_value=("cmake", {})), \
                patch.object(build.subprocess, "run") as run:
            build.build_example("CornellBox", self.directory / "example", "Debug", [])
        self.assertNotIn("-C", run.call_args_list[0].args[0])

    @unittest.skipUnless(platform.system() == "Windows", "Windows executable lookup")
    def test_bundled_cmake_and_ninja_are_found_after_developer_setup(self):
        tools = self.directory / "tools"
        tools.mkdir()
        for name in ("cl.exe", "link.exe", "rc.exe", "mt.exe"):
            (tools / name).touch()
        bundle = self.directory / "Common7/IDE/CommonExtensions/Microsoft/CMake"
        for name in ("CMake/bin/cmake.exe", "Ninja/ninja.exe"):
            path = bundle / name
            path.parent.mkdir(parents=True)
            path.touch()
        prepared = {"PATH": str(tools), "INCLUDE": "headers", "LIB": "libraries",
                    "VSCMD_ARG_TGT_ARCH": "x64"}
        original = dict(os.environ)
        with patch.object(build, "find_visual_studio", return_value=(self.directory, None)), \
                patch.object(build, "developer_environment", return_value=prepared) as setup:
            cmake, env = build.prepare_build_environment({}, "Ninja")
        self.assertEqual(Path(cmake), bundle / "CMake/bin/cmake.exe")
        self.assertIn(str(bundle / "Ninja"), env["PATH"])
        setup.assert_called_once()
        self.assertEqual(dict(os.environ), original)
        with patch.dict(os.environ, env, clear=True), \
                patch.object(build, "find_visual_studio", return_value=(self.directory, None)), \
                patch.object(build, "developer_environment") as setup:
            build.prepare_build_environment({}, "Ninja")
            setup.assert_not_called()
        (bundle / "Ninja/ninja.exe").unlink()
        with patch.object(build, "find_visual_studio", return_value=(self.directory, None)), \
                patch.object(build, "developer_environment", return_value=dict(prepared, PATH=str(tools))):
            with self.assertRaisesRegex(SystemExit, "C\\+\\+ CMake tools for Windows"):
                build.prepare_build_environment({}, "Ninja")

    @unittest.skipUnless(platform.system() == "Windows", "cmd.exe batch quoting")
    def test_batch_path_special_characters_and_environment_values_survive(self):
        instance = self.directory / "VS & %ARDA_TEST_TOKEN% !"
        script = instance / "Common7/Tools/VsDevCmd.bat"
        script.parent.mkdir(parents=True)
        script.write_text('@echo off\nset "ARDA_PROBE=child=value & !"\n', encoding="ascii")
        env = {key.upper(): value for key, value in os.environ.items()}
        env["ARDA_TEST_TOKEN"] = "must not expand"
        configured = build.developer_environment(env, instance, None)
        self.assertEqual(configured["ARDA_PROBE"], "child=value & !")
        self.assertEqual(configured["ARDA_TEST_TOKEN"], "must not expand")
        self.assertNotIn("ARDA_VSDEVCMD", configured)
        self.assertNotIn("ARDA_PROBE", env)
        script.write_text('@echo off\necho secret environment value\nexit /b 5\n', encoding="ascii")
        with self.assertRaisesRegex(SystemExit, "setup failed \\(5\\)") as error:
            build.developer_environment(env, instance, None)
        self.assertNotIn("secret", str(error.exception))


if __name__ == "__main__":
    unittest.main()
