"""Real CMake/nvcc regression checks; run in a developer shell, no GPU needed.

nvcc is taken from PATH, or ARDA_TEST_NVCC can name an unpacked test toolkit.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
NVCC = os.environ.get("ARDA_TEST_NVCC") or shutil.which("nvcc")
READY = NVCC and shutil.which("cmake") and shutil.which("ninja")
if os.name == "nt":
    READY = READY and shutil.which("cl")


@unittest.skipUnless(READY, "Requires nvcc, CMake, Ninja and a supported host compiler in the terminal.")
class CudaCompilerDiscoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arda cuda compiler ")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name).resolve()
        self.nvcc = Path(NVCC).resolve()
        self.env = {key: value for key, value in os.environ.items()
                    if not key.upper().startswith("CUDA")}
        self.env["PATH"] = str(self.nvcc.parent) + os.pathsep + os.environ["PATH"]

    def configure(self, *args, env=None, succeeds=True):
        result = subprocess.run([
            "cmake", "-S", str(ROOT / "Cmake/Tests/CudaCompilerDiscovery"),
            "-B", str(self.build), "-G", "Ninja", *args,
        ], env=env or self.env, capture_output=True, text=True, timeout=120)
        output = result.stdout + result.stderr
        if succeeds:
            self.assertEqual(result.returncode, 0, output)
            compiler, include = (self.build / "selected-toolchain.txt").read_text().splitlines()
            self.assertEqual(Path(compiler).resolve(), self.nvcc)
            self.assertTrue((Path(include) / "cuda.h").is_file(), include)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def test_cached_notfound_recovers_from_path_and_builds(self):
        self.configure("-DCMAKE_CUDA_COMPILER=NOTFOUND")
        result = subprocess.run(["cmake", "--build", str(self.build)], env=self.env,
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_successful_compiler_selection_survives_environment_change(self):
        self.configure()
        self.configure(env={**self.env, "CUDACXX": str(self.build / "missing-nvcc")})

    def test_invalid_explicit_compiler_is_not_replaced_with_path_nvcc(self):
        output = self.configure(f"-DCMAKE_CUDA_COMPILER={self.build / 'missing-nvcc'}", succeeds=False)
        self.assertIn("missing-nvcc", output)

    def test_host_compiler_failure_reports_the_actual_cause(self):
        output = self.configure(f"-DCMAKE_CUDA_HOST_COMPILER={self.build / 'missing-host'}", succeeds=False)
        self.assertIn("missing-host", output)
        self.assertNotIn("CUDA kernel builds require nvcc at build time.", output)


if __name__ == "__main__":
    unittest.main()
