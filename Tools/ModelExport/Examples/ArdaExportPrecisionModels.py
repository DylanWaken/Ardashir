"""Export typed model examples and raw fixtures for Arda's native graph tests.

These small models demonstrate explicit low-precision math and conversion. They
are not automatic quantizers and do not promise Tensor Core instruction selection.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import torch

import arda
from ArdaDTypes import decode_array
from ArdaModelPackage import read_package


class TypedAffine(torch.nn.Module):
    def __init__(self, dtype):
        super().__init__()
        self.register_buffer("weight", torch.tensor([0.25, -0.5, 1.5, 2.0]).to(dtype))

    def forward(self, value):
        return value * self.weight + value


class Float8Affine(TypedAffine):
    def forward(self, value):
        # The model specifies widened computation and an explicit FP8 output
        # rounding boundary. Scales/calibration belong to the model author.
        return (value.float() * self.weight.float() + 0.25).to(value.dtype)


class Int8Affine(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.register_buffer("weight", torch.tensor([2, -3, 4, 5], dtype=torch.int8))

    def forward(self, value):
        return value.to(torch.int32) * self.weight.to(torch.int32) + 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(".cache/model-export/precision"))
    parser.add_argument("--verify", action="store_true", help="Run every target on the local CUDA device")
    options = parser.parse_args()
    cases = [("float16", TypedAffine(torch.float16), torch.float16, [80], 0.002),
             ("bfloat16", TypedAffine(torch.bfloat16), torch.bfloat16, [80], 0.02),
             ("float8_e4m3fn", Float8Affine(torch.float8_e4m3fn), torch.float8_e4m3fn, [89, 90, 120], 0.0),
             ("float8_e5m2", Float8Affine(torch.float8_e5m2), torch.float8_e5m2, [89, 90, 120], 0.0),
             ("int8", Int8Affine(), torch.int8, [61, 80], 0.0)]
    fixtures = []
    for name, model, dtype, targets, tolerance in cases:
        source = torch.arange(-16, 16, dtype=torch.float32).reshape(8, 4)
        source = (source if dtype == torch.int8 else source / 4).to(dtype)
        result = arda.export(model, (source,), options.output_dir / name,
                             target_archs=targets, input_names=["source"], output_names=["result"],
                             name=name, verify=options.verify, rtol=tolerance, atol=tolerance)
        with np.load(result.reference_path) as reference:
            input_path = result.reference_path.parent / "input.bin"
            reference_path = result.reference_path.parent / "reference.bin"
            reference["source"].tofile(input_path)
            package = read_package(result.artifacts[0].package_path)
            port = next(port for port in package["tensors"] if port["name"] == "result")
            decode_array(reference["result"], port["dtype"]).astype(np.float32).tofile(reference_path)
        for artifact in result.artifacts:
            fixtures.append({"dtype": name, "target": artifact.architecture,
                             "descriptor": str(artifact.descriptor_path), "package": str(artifact.package_path),
                             "state_dict": str(artifact.checkpoint_path), "input": str(input_path),
                             "reference": str(reference_path), "rtol": tolerance, "atol": tolerance})
    manifest = options.output_dir / "native-fixtures.json"
    manifest.write_text(json.dumps(fixtures, indent=2) + "\n", encoding="utf-8")
    print(manifest.resolve())


if __name__ == "__main__":
    main()
