"""Regression coverage for the canonical source inventory used by formatting/check runs."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

with patch.object(sys, "path", [str(Path(__file__).resolve().parents[1]), *sys.path]):
    import FormatCode as format_code


class FormatInventoryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arda format inventory ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.git("init", "--quiet")
        self.root_patch = patch.object(format_code, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def git(self, *arguments):
        return subprocess.run(["git", *arguments], cwd=self.root, check=True, capture_output=True)

    def write(self, relative, text="int Example;\n"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def inventory(self, *paths):
        return [path.relative_to(self.root).as_posix()
                for path in format_code.source_files([Path(path) for path in paths])]

    def test_tracked_and_untracked_canonical_sources_are_included_once(self):
        self.write("Source/Tracked.cpp")
        self.git("add", "Source/Tracked.cpp")
        self.write("Source/New Node.cuh")
        self.write("Shaders/Surface.hlsl")
        self.write("cmake/Registration.cpp.in")
        self.write("Docs/ArdaBackend/Examples/Direct.cu")
        self.write("Docs/ArdaRDG/Examples/Nodes/NewNode.cpp")
        expected = [
            "Docs/ArdaBackend/Examples/Direct.cu",
            "Docs/ArdaRDG/Examples/Nodes/NewNode.cpp",
            "Shaders/Surface.hlsl",
            "Source/New Node.cuh",
            "Source/Tracked.cpp",
            "cmake/Registration.cpp.in",
        ]
        self.assertEqual(self.inventory(), sorted(expected))
        self.git("add", "Docs/ArdaRDG/Examples/Nodes/NewNode.cpp")
        self.assertEqual(self.inventory(), sorted(expected))

    def test_dependency_build_and_generated_reference_sources_are_excluded_even_when_tracked(self):
        canonical = "Source/Module/Node.cpp"
        self.write(canonical)
        for excluded in [
                "Source/ThirdParty/Vendor.h", "Source/deps/Vendor.h", "Source/_deps/SDK.hpp",
                "Source/build/Generated.cpp", "Source/build-debug/Generated.cpp", "Source/out/Generated.h",
                "ThirdParty/Vendor.cpp", "build/Generated.cpp", "Source/node_modules/Native.cc",
                "Docs/ArdaBackend/Examples/Reference/Copied.cu",
                "Docs/ArdaRDG/Examples/Reference/Copied.cpp"]:
            self.write(excluded)
        self.git("add", "--force", ".")
        self.assertEqual(self.inventory(), [canonical])
        with self.assertRaisesRegex(SystemExit, "No authored project"):
            self.inventory("Docs/ArdaRDG/Examples/Reference")

    def test_ignored_untracked_and_removed_tracked_files_are_excluded(self):
        removed = self.write("Source/Removed.cpp")
        self.write("Source/Kept.h")
        self.git("add", "Source")
        removed.unlink()
        self.write(".gitignore", "Source/Ignored.cpp\n")
        self.write("Source/Ignored.cpp")
        self.write("Source/New.cpp")
        self.assertEqual(self.inventory(), ["Source/Kept.h", "Source/New.cpp"])

    def test_path_selection_accepts_new_files_and_rejects_outside_paths(self):
        self.write("Source/A/File.cpp")
        self.write("Source/B/File.cpp")
        self.assertEqual(self.inventory("Source/A"), ["Source/A/File.cpp"])
        self.assertEqual(self.inventory("Source/B/File.cpp"), ["Source/B/File.cpp"])
        with self.assertRaisesRegex(SystemExit, "must stay in the repository"):
            self.inventory("..")

    def test_shader_templates_receive_the_hlsl_layout_pass(self):
        self.assertEqual(format_code.source_extension(Path("Nodes.hlsl.in")), ".hlsl")
        self.assertEqual(format_code.source_extension(Path("Nodes.cuh.in")), ".cuh")
        path = self.root / "Shaders/Nodes.hlsl.in"
        with patch.object(format_code.subprocess, "run",
                          return_value=subprocess.CompletedProcess([], 0, stdout=b"formatted")) as run:
            self.assertEqual(format_code.formatted_source("clang-format", path, b"source"), b"formatted")
        self.assertEqual(run.call_count, 2)
        self.assertTrue(run.call_args_list[1].args[0][-1].endswith(".cs"))

    def test_canonical_cmake_directory_case_is_included(self):
        self.write("Cmake/ArdaAllocator.h")
        self.assertEqual(self.inventory(), ["Cmake/ArdaAllocator.h"])


if __name__ == "__main__":
    unittest.main()
