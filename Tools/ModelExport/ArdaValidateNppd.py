"""Compare real renderer NPPD tensor dumps with the pinned authors' CPU Model.step.

Input is PREFIX.tensors.json plus PREFIX.<tensor_name>.bin little-endian float32
files. The JSON contains a tensor list with name, shape and optional relative file.
This is an offline numerical validator, not an inference dependency of the renderer.
"""

import argparse
import json
from pathlib import Path

import numpy as np
import torch

from ArdaModelPackage import ExportError, check
from ArdaNppdExport import load_inference_model, original_step_reference


INPUTS = ("color", "depth", "normal", "diffuse", "motion", "crop_offset", "previous_crop_offset", "temporal")
OUTPUTS = ("denoised_hdr", "next_temporal")


def validate(prefix, checkpoint_directory, rtol, atol):
    manifest = Path(str(prefix) + ".tensors.json")
    metadata = json.loads(manifest.read_text())
    records = metadata["tensors"] if isinstance(metadata, dict) else metadata
    arrays = {}
    for record in records:
        name = record["name"]
        check(name not in arrays, "Duplicate tensor in renderer dump: " + name)
        path = manifest.parent / record["file"] if "file" in record else Path(str(prefix) + "." + name + ".bin")
        values = np.fromfile(path, dtype="<f4")
        shape = record["shape"]
        check(values.size == int(np.prod(shape)), "Raw tensor shape does not match file: " + name)
        arrays[name] = values.reshape(shape)
        check(bool(np.isfinite(values).all()), "Non-finite renderer tensor: " + name)
    check(set(INPUTS + OUTPUTS) <= set(arrays), "Renderer dump is missing required NPPD tensors")
    shape = arrays["color"].shape
    check(len(shape) == 5 and shape[0] == 1 and shape[1] == 3 and shape[4] == 2, "Expected color layout B1,C3,H,W,S2")
    model, source, _ = load_inference_model(checkpoint_directory, shape[2], shape[3])
    inputs = {name: torch.from_numpy(arrays[name]) for name in INPUTS}
    with torch.no_grad():
        expected = original_step_reference(model, inputs, source)[:2]
        adapted = model(*inputs.values())
        for author, adapter in zip(expected, adapted):
            torch.testing.assert_close(author, adapter, rtol=0, atol=0)
    metrics = {}
    for name, reference in zip(OUTPUTS, expected):
        reference = reference.numpy()
        actual = arrays[name]
        check(actual.shape == reference.shape, "Renderer output shape mismatch: " + name)
        check(bool(np.isfinite(reference).all()), "CPU reference produced non-finite values: " + name)
        difference = actual - reference
        metrics[name] = {"max_abs_error": float(np.max(np.abs(difference))),
            "mean_abs_error": float(np.mean(np.abs(difference))),
            "rmse": float(np.sqrt(np.mean(difference ** 2))),
            "finite": True, "passed": bool(np.allclose(actual, reference, rtol=rtol, atol=atol))}
    report = {"reference": "Pinned original NPPD Model.step on the exact renderer inputs",
              "rtol": rtol, "atol": atol,
              "temporal_input": {"nonzero_elements": int(np.count_nonzero(arrays["temporal"])),
                  "elements": int(arrays["temporal"].size),
                  "max_abs": float(np.max(np.abs(arrays["temporal"])))},
              "outputs": metrics}
    Path(str(prefix) + ".validation.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--checkpoint-dir", type=Path, default=Path(".cache/model-export/nppd"))
    parser.add_argument("--rtol", type=float, default=0.0001)
    parser.add_argument("--atol", type=float, default=0.0001)
    args = parser.parse_args()
    try:
        report = validate(args.prefix, args.checkpoint_dir, args.rtol, args.atol)
        print(json.dumps(report, indent=2))
        check(all(item["passed"] for item in report["outputs"].values()), "Renderer output exceeded numerical tolerance")
    except ExportError as error:
        parser.exit(2, f"Validation rejected: {error}\n")


if __name__ == "__main__":
    main()
