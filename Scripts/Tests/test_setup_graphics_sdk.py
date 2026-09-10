"""Offline failure/repair checks for the root SDK setup tool."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("setup_graphics", REPO / "SetupGraphicsSDK.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class SetupTests(unittest.TestCase):
    def setUp(self):
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


if __name__ == "__main__":
    unittest.main()
