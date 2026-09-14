"""Offline failure/repair checks for the root SDK setup tool."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("setup_graphics", REPO / "SetupGraphicsSDK.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class FakeRegistry:
    HKEY_LOCAL_MACHINE = "machine"
    KEY_READ, KEY_WRITE, KEY_WOW64_64KEY, REG_DWORD = 1, 2, 4, 4

    def __init__(self, entries):
        self.entries = dict(entries)

    def CreateKeyEx(self, hive, path, reserved, access):
        assert hive == self.HKEY_LOCAL_MACHINE and path == setup.VALIDATION_REGISTRY_KEY
        assert access & self.KEY_WOW64_64KEY
        return self

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def QueryInfoKey(self, key):
        return 0, len(self.entries), 0

    def EnumValue(self, key, index):
        name, (value, kind) = list(self.entries.items())[index]
        return name, value, kind

    def SetValueEx(self, key, name, reserved, kind, value):
        self.entries[name] = (value, kind)

    def DeleteValue(self, key, name):
        if name not in self.entries:
            raise FileNotFoundError(name)
        del self.entries[name]


class SetupTests(unittest.TestCase):
    def setUp(self):
        # Windows platform.machine() reads environment variables that several
        # discovery tests deliberately clear; keep the host architecture fixed.
        machine = patch.object(setup.platform, "machine", return_value="AMD64")
        machine.start()
        self.addCleanup(machine.stop)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_corrupt_download_is_repaired_and_valid_cache_is_reused(self):
        artifact = self.root / "sdk.zip"
        artifact.write_bytes(b"corrupt")
        content = b"verified package"
        digest = hashlib.sha256(content).hexdigest()
        with patch.object(setup.urllib.request, "urlopen", return_value=io.BytesIO(content)) as fetch:
            setup.download("https://example.test/sdk.zip", artifact, digest)
            fetch.assert_called_once()
        with patch.object(setup.urllib.request, "urlopen", side_effect=AssertionError("must reuse cache")):
            setup.download("https://example.test/sdk.zip", artifact, digest)
        self.assertEqual(artifact.read_bytes(), content)

    def test_hash_failure_keeps_prior_file_and_removes_partial_download(self):
        artifact = self.root / "sdk.zip"
        artifact.write_bytes(b"prior")
        with patch.object(setup.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                setup.download("https://example.test/sdk.zip", artifact, "0" * 64)
        self.assertEqual(artifact.read_bytes(), b"prior")
        self.assertFalse(artifact.with_suffix(".zip.partial").exists())

    def test_zip_rejects_escaping_paths_before_extracting_anything(self):
        for name in ("../escape", "..\\escape", "C:/escape"):
            archive = self.root / "sdk.zip"
            with zipfile.ZipFile(archive, "w") as stream:
                stream.writestr("valid", "first")
                stream.writestr(name, "bad")
            destination = self.root / "install"
            with self.assertRaisesRegex(RuntimeError, "escapes installation"):
                setup.extract_zip(archive, destination)
            self.assertFalse((destination / "valid").exists())
            self.assertFalse((self.root / "escape").exists())

    def test_manifest_without_loadable_library_is_not_an_installation(self):
        (self.root / "VkLayer_khronos_validation.json").write_text(json.dumps({"layer": {
            "name": "VK_LAYER_KHRONOS_validation", "library_path": "missing.dll"}}), encoding="utf-8")
        self.assertIsNone(setup.find_layer([self.root]))
        (self.root / "missing.dll").write_bytes(b"not a native library")
        self.assertIsNone(setup.find_layer([self.root]))

    def test_headers_check_major_minor_and_patch(self):
        headers = self.root / "vulkan"
        headers.mkdir()
        (headers / "vulkan.hpp").write_text("// present", encoding="utf-8")
        for major, minor, patch_version, expected in ((1, 4, 356, False), (1, 3, 999, False), (1, 4, 357, True), (1, 4, 358, True)):
            (headers / "vulkan_core.h").write_text(
                f"#define VK_HEADER_VERSION {patch_version}\n#define VK_HEADER_VERSION_COMPLETE VK_MAKE_API_VERSION(0, {major}, {minor}, VK_HEADER_VERSION)\n", encoding="utf-8")
            self.assertEqual(setup.compatible_headers(self.root, "v1.4.357"), expected)

    def test_explicit_broken_layer_path_fails_without_provisioning(self):
        with patch.object(setup, "compatible_headers", return_value=True), \
             patch.object(setup, "provision_layer", side_effect=AssertionError("override must be preserved")), \
             patch.dict(setup.os.environ, {"VK_LAYER_PATH": str(self.root)}):
            with self.assertRaisesRegex(RuntimeError, "Explicit VK_LAYER_PATH"):
                setup.main(["--backend", "vulkan", "--check"])

    def test_missing_check_is_read_only(self):
        destination = self.root / "not-created"
        with patch.object(setup, "compatible_headers", return_value=False), \
             patch.object(setup, "find_layer", return_value=None), \
             patch.dict(setup.os.environ, {}, clear=True):
            self.assertEqual(setup.main(["--backend", "vulkan", "--check", "--root", str(destination)]), 1)
        self.assertFalse(destination.exists())

    def test_provision_warning_is_a_setup_failure_not_success(self):
        with patch.object(setup.shutil, "which", return_value="tool"), \
             patch.object(setup, "generator", return_value=("Ninja", "")), \
             patch.object(setup, "run"), patch.object(setup, "find_layer", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "did not produce a loadable layer"):
                setup.provision_layer(self.root, None)

    def test_pins_are_current_build_values(self):
        url, digest, tag = setup.pins()
        self.assertTrue(url.startswith("https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/"))
        self.assertRegex(digest, r"^[0-9a-f]{64}$")
        self.assertRegex(tag, r"^v\d+\.\d+\.\d+$")

    def run_validation_request(self, values, prelude="", succeeds=True):
        request = self.root / "request.cmake"
        values = {"VALIDATION_ROOT": self.root / "validation", "VULKAN_ENABLED": "ON", **values}
        request.write_text(prelude + "\n" + "\n".join(
            f"set({key} {setup.cmake_value(value)})" for key, value in values.items()), encoding="utf-8")
        result = subprocess.run([
            "cmake", f"-DREQUEST={request.as_posix()}", "-P", str(REPO / "Cmake/ProvisionValidation.cmake"),
        ], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        return (self.root / "validation/layer-path.txt").read_text(encoding="utf-8").strip()

    @unittest.skipUnless(shutil.which("cmake"), "CMake validation discovery")
    def test_cmake_reuses_enabled_registered_layer_without_provisioning(self):
        # Fake registry queries, not the user's registry. Any attempted source
        # build fails immediately, so this regression cannot use the network.
        enabled = self.root / "registered layer"
        disabled = self.root / "disabled layer"
        for directory in (enabled, disabled):
            directory.mkdir()
            (directory / setup.VALIDATION_MANIFEST).write_text("{}", encoding="utf-8")
        prelude = f'''
set(WIN32 TRUE)
function(cmake_host_system_information)
    cmake_parse_arguments(QUERY "VALUE_NAMES" "RESULT;VALUE;VIEW" "" ${{ARGN}})
    if(NOT QUERY_VIEW STREQUAL "64")
        message(FATAL_ERROR "Expected the x64 registry view")
    endif()
    if(QUERY_VALUE_NAMES)
        set(${{QUERY_RESULT}} {setup.cmake_value(disabled / setup.VALIDATION_MANIFEST)}
            {setup.cmake_value(self.root / "missing" / setup.VALIDATION_MANIFEST)}
            {setup.cmake_value(enabled / setup.VALIDATION_MANIFEST)} PARENT_SCOPE)
    elseif(QUERY_VALUE STREQUAL {setup.cmake_value(disabled / setup.VALIDATION_MANIFEST)})
        set(${{QUERY_RESULT}} 1 PARENT_SCOPE)
    else()
        set(${{QUERY_RESULT}} 0 PARENT_SCOPE)
    endif()
endfunction()
function(execute_process)
    message(FATAL_ERROR "Installed validation must not invoke source provisioning")
endfunction()
'''
        selected = self.run_validation_request({"PROVISION": "ON"}, prelude)
        self.assertEqual(Path(selected), enabled)
        self.assertFalse((self.root / "validation/source").exists())

    @unittest.skipUnless(shutil.which("cmake"), "CMake validation discovery")
    def test_cmake_explicit_missing_layer_never_falls_back_or_downloads(self):
        prelude = '''
function(cmake_host_system_information)
    message(FATAL_ERROR "Explicit paths must suppress registry fallback")
endfunction()
function(execute_process)
    message(FATAL_ERROR "Explicit paths must suppress source provisioning")
endfunction()
'''
        self.assertEqual(self.run_validation_request({
            "PROVISION": "ON", "LAYER_DIR": self.root / "missing layer",
        }, prelude), "")

    @unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake/Ninja validation integration")
    def test_validation_setup_is_incremental_and_recovers_missing_outputs(self):
        # Use the real CMake module in a compiler-free project. Keep the layer
        # explicit so the result is independent of installed SDKs and drivers.
        layer = self.root / "layer"
        layer.mkdir()
        (layer / setup.VALIDATION_MANIFEST).write_text("{}", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.24)
project(ValidationSetupTest NONE)
set(ARDASHIR_ENABLE_GPU_VALIDATION ON)
set(ARDASHIR_BACKEND_VULKAN ON)
set(ARDASHIR_VULKAN_VALIDATION_DIR {setup.cmake_value(layer)} CACHE PATH "")
include({setup.cmake_value(REPO / "Cmake/TestValidation.cmake")})
''', encoding="utf-8")
        build = self.root / "build"

        def run(*arguments, succeeds=True):
            result = subprocess.run(["cmake", *arguments], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)

        configure = ("-S", str(self.root), "-B", str(build), "-G", "Ninja")
        compile_setup = ("--build", str(build), "--target", "ArdaValidationLayers")
        run(*configure)
        self.assertIn("ARDASHIR_PROVISION_VALIDATION:BOOL=OFF", (build / "CMakeCache.txt").read_text())
        run(*compile_setup)
        request = build / "validation-layers/request.cmake"
        stamp = build / "validation-layers/attempt.stamp"
        output = build / "validation-layers/layer-path.txt"
        timestamps = [path.stat().st_mtime_ns for path in (request, stamp, output)]

        run(*configure)
        run(*compile_setup)
        self.assertEqual([path.stat().st_mtime_ns for path in (request, stamp, output)], timestamps)
        output.unlink()
        run(*compile_setup)
        self.assertEqual(Path(output.read_text().strip()), layer)

        # A failed process cannot leave a successful completion stamp behind.
        stamp.unlink()
        request.write_text(request.read_text() + f'''
set(PROVISION ON)
set(FORCE_PROVISION ON)
set(LAYER_SOURCE {setup.cmake_value(self.root / "unavailable source")})
function(execute_process)
    message(FATAL_ERROR "simulated setup failure")
endfunction()
''')
        run(*compile_setup, succeeds=False)
        self.assertFalse(stamp.exists())
        run(*configure)
        run(*compile_setup)
        self.assertTrue(stamp.exists())

    @unittest.skipUnless(shutil.which("cmake"), "CMake cache integration")
    def test_sdk_defaults_fill_empty_cache_and_preserve_custom_paths(self):
        layer = self.root / "verified layer"
        setup.write_configuration(self.root, None, None, layer)
        defaults = self.root / "GraphicsSdkDefaults.cmake"
        key = "ARDASHIR_VULKAN_VALIDATION_DIR"
        result_file = self.root / "value.txt"
        check = self.root / "check.cmake"
        check.write_text(f'file(WRITE {setup.cmake_value(result_file)} "${{{key}}}")\n', encoding="utf-8")

        def configure(*arguments):
            result = subprocess.run(["cmake", *arguments, "-P", str(check)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result_file.read_text(encoding="utf-8")

        self.assertEqual(configure("-C", str(defaults)), layer.as_posix())
        self.assertEqual(configure(f"-D{key}:PATH=", "-C", str(defaults)), layer.as_posix())
        custom = (self.root / "custom layer").as_posix()
        self.assertEqual(configure(f"-D{key}:PATH={custom}", "-C", str(defaults)), custom)
        self.assertEqual(configure("-C", str(defaults), f"-D{key}:PATH=explicit"), "explicit")
        self.assertEqual(configure(f"-D{key}:PATH={custom}", "-C", str(self.root / "GraphicsSdk.cmake")), layer.as_posix())

    def test_system_registration_preserves_other_sdks_and_is_repeatable(self):
        old = str(self.root / "old" / setup.VALIDATION_MANIFEST)
        other = str(self.root.parent / "other SDK" / setup.VALIDATION_MANIFEST)
        target = self.root / "new" / setup.VALIDATION_MANIFEST
        registry = FakeRegistry({old: (0, 4), other: (1, 4)})
        with patch.dict(sys.modules, {"winreg": registry}), \
                patch.object(setup, "verify_system_validation") as verify:
            setup.register_system_validation(target, self.root)
            setup.register_system_validation(target, self.root)
        self.assertEqual(registry.entries, {other: (1, 4), str(target): (0, 4)})
        self.assertEqual(verify.call_count, 2)

    def test_registered_validation_needs_no_activation_path_override(self):
        layer = self.root / "registered layer"
        with patch.object(setup, "registered_layers", return_value=[layer]):
            setup.write_configuration(self.root, None, None, layer)
        for filename in ("ActivateGraphicsSDK.ps1", "activate-graphics-sdk.sh"):
            self.assertNotIn("VK_ADD_LAYER_PATH", (self.root / filename).read_text(encoding="utf-8"))
        self.assertIn(layer.as_posix(), (self.root / "GraphicsSdk.cmake").read_text(encoding="utf-8"))

    def test_activation_preserves_explicit_layer_selection_and_is_repeatable(self):
        shell = (shutil.which("pwsh") or shutil.which("powershell")) if setup.os.name == "nt" else shutil.which("sh")
        if not shell:
            self.skipTest("Shell activation integration")
        layer = self.root / "layer ' & $() literal"
        with patch.object(setup, "registered_layers", return_value=[]):
            setup.write_configuration(self.root, None, None, layer)
        if setup.os.name == "nt":
            activation = str(self.root / "ActivateGraphicsSDK.ps1").replace("'", "''")
            runner = self.root / "activate-twice.ps1"
            runner.write_text(f". '{activation}'\n. '{activation}'\n[Console]::Write($env:VK_ADD_LAYER_PATH)\n", encoding="utf-8")
            command = [shell, "-NoProfile", "-NonInteractive", "-File", str(runner)]
        else:
            activation = str(self.root / "activate-graphics-sdk.sh").replace("'", "'\"'\"'")
            runner = self.root / "activate-twice.sh"
            runner.write_text(f". '{activation}'\n. '{activation}'\nprintf %s \"$VK_ADD_LAYER_PATH\"\n", encoding="utf-8")
            command = [shell, str(runner)]
        for explicit in (False, True):
            with self.subTest(explicit_layer_path=explicit):
                env = dict(setup.os.environ)
                env.pop("VK_LAYER_PATH", None)
                env["VK_ADD_LAYER_PATH"] = "existing layer directory"
                if explicit:
                    env["VK_LAYER_PATH"] = "authoritative layer directory"
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = env["VK_ADD_LAYER_PATH"] if explicit else str(layer) + setup.os.pathsep + env["VK_ADD_LAYER_PATH"]
                self.assertEqual(result.stdout, expected)

    def test_failed_loader_verification_restores_prior_registration(self):
        old = str(self.root / "old" / setup.VALIDATION_MANIFEST)
        target = self.root / "new" / setup.VALIDATION_MANIFEST
        other = str(self.root.parent / "other SDK" / setup.VALIDATION_MANIFEST)
        for prior_target in ({}, {str(target): (1, 4)}):
            initial = {old: (0, 4), other: (1, 4), **prior_target}
            registry = FakeRegistry(initial)
            with patch.dict(sys.modules, {"winreg": registry}), \
                    patch.object(setup, "verify_system_validation", side_effect=RuntimeError("loader rejected layer")):
                with self.assertRaisesRegex(RuntimeError, "loader rejected"):
                    setup.register_system_validation(target, self.root)
            self.assertEqual(registry.entries, initial)

    def test_system_install_copies_runtime_and_reuses_identical_package(self):
        source = self.root / "source"
        source.mkdir()
        library = source / "VkLayer_khronos_validation.dll"
        library.write_bytes(b"test layer")
        (source / "dependency.dll").write_bytes(b"runtime dependency")
        (source / "unrelated.exe").write_bytes(b"not installed")
        (source / setup.VALIDATION_MANIFEST).write_text(json.dumps({"layer": {
            "name": "VK_LAYER_KHRONOS_validation", "library_path": str(library)}}), encoding="utf-8")
        destination_root = self.root / "Program Files" / "Ardashir" / "VulkanValidation"
        with patch.object(setup, "windows_administrator", return_value=True), \
                patch.object(setup, "system_validation_root", return_value=destination_root), \
                patch.object(setup, "protect_install_directory", side_effect=lambda p: p.mkdir(parents=True, exist_ok=True)), \
                patch.object(setup, "layer_library", side_effect=lambda p: p / library.name), \
                patch.object(setup, "register_system_validation") as register:
            first = setup.install_system_validation(source)
            modification = (first / library.name).stat().st_mtime_ns
            self.assertEqual(setup.install_system_validation(first), first)
            self.assertEqual((first / library.name).stat().st_mtime_ns, modification)
            library.write_bytes(b"new version")
            second = setup.install_system_validation(source)
        self.assertNotEqual(first, second)
        self.assertEqual((first / library.name).read_bytes(), b"test layer")
        self.assertEqual((second / library.name).read_bytes(), b"new version")
        self.assertEqual((second / "dependency.dll").read_bytes(), b"runtime dependency")
        self.assertFalse((second / "unrelated.exe").exists())
        self.assertEqual(json.loads((second / setup.VALIDATION_MANIFEST).read_text())["layer"]["library_path"],
                         ".\\" + library.name)
        register.assert_called_with(second / setup.VALIDATION_MANIFEST, destination_root)

    def test_system_install_requires_admin_before_touching_files(self):
        with patch.object(setup, "windows_administrator", return_value=False), \
                patch.object(setup, "layer_library") as load, \
                patch.object(setup, "protect_install_directory") as create:
            with self.assertRaisesRegex(RuntimeError, "Administrator privileges"):
                setup.install_system_validation(self.root)
        load.assert_not_called()
        create.assert_not_called()

    def test_system_probe_removes_local_paths_only_in_child_environment(self):
        with patch.dict(setup.os.environ, {"VK_LAYER_PATH": "local", "VK_ADD_LAYER_PATH": "extra", "ARDA_PROBE": "kept"}), \
                patch.object(setup.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            setup.verify_system_validation()
            self.assertEqual(setup.os.environ["VK_LAYER_PATH"], "local")
        child = run.call_args.kwargs["env"]
        self.assertNotIn("VK_LAYER_PATH", child)
        self.assertNotIn("VK_ADD_LAYER_PATH", child)
        self.assertEqual(child["ARDA_PROBE"], "kept")

    @unittest.skipUnless(setup.os.name == "nt", "Windows system registration policy")
    def test_check_is_read_only_and_reports_missing_system_registration(self):
        with patch.object(setup, "compatible_headers", return_value=True), \
                patch.object(setup, "find_layer", side_effect=[self.root, None]), \
                patch.object(setup, "registered_layers", return_value=[]), \
                patch.object(setup, "install_system_validation") as install, \
                patch.object(setup, "write_configuration") as write, \
                patch.dict(setup.os.environ, {}, clear=True):
            self.assertEqual(setup.main(["--backend", "vulkan", "--check"]), 1)
        install.assert_not_called()
        write.assert_not_called()

    def test_local_only_setup_never_registers_system_layer(self):
        with patch.object(setup, "compatible_headers", return_value=True), \
                patch.object(setup, "find_layer", return_value=self.root), \
                patch.object(setup, "install_system_validation") as install, \
                patch.object(setup, "write_configuration") as write, \
                patch.dict(setup.os.environ, {}, clear=True):
            self.assertEqual(setup.main(["--backend", "vulkan", "--local-only", "--root", str(self.root)]), 0)
        install.assert_not_called()
        write.assert_called_once()


if __name__ == "__main__":
    unittest.main()
