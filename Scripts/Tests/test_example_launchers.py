"""Check that example diagnostic options remain separate explicit requests."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


LAUNCHERS = Path(__file__).resolve().parents[1] / "Examples"


def load_launcher(name):
    spec = importlib.util.spec_from_file_location(name, LAUNCHERS / f"Run{name}.py")
    module = importlib.util.module_from_spec(spec)
    with patch.object(sys, "path", [str(LAUNCHERS), *sys.path]):
        spec.loader.exec_module(module)
    return module


class ExampleLauncherTests(unittest.TestCase):
    def run_launcher(self, name, options):
        launcher = load_launcher(name)
        with tempfile.TemporaryDirectory(prefix="arda example ") as directory:
            build = Path(directory).resolve()
            executable = build / "Source" / "ArdaTests" / "Examples" / name / f"{name}.exe"
            executable.parent.mkdir(parents=True)
            executable.touch()
            arguments = [f"Run{name}.py", "vulkan", str(build), "Release", "--hidden", "--frames", "2", *options]
            with patch.object(sys, "argv", arguments), \
                    patch.object(launcher.platform, "system", return_value="Windows"), \
                    patch.object(launcher, "build_example") as build_example, \
                    patch.object(launcher.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
                self.assertEqual(launcher.main(), 0)
            build_example.assert_called_once()
            run.assert_called_once()
            return run.call_args.args[0]

    def test_native_validation_is_forwarded_only_when_requested(self):
        for name in ("ARDGExample", "CornellBox"):
            for options in ([], ["--validation"]):
                with self.subTest(name=name, options=options):
                    command = self.run_launcher(name, options)
                    self.assertEqual("--validation" in command, "--validation" in options)

    def test_terrain_verification_and_native_validation_are_independent(self):
        for options in (["--verify"], ["--verify", "--validation"]):
            with self.subTest(options=options):
                command = self.run_launcher("ARDGExample", options)
                self.assertIn("--verify", command)
                self.assertEqual("--validation" in command, "--validation" in options)


if __name__ == "__main__":
    unittest.main()
