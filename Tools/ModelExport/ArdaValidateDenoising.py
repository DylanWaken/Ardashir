"""Compare Cornell's captured CUDA HDR result to its offline pretrained reference."""

import argparse
import json
from pathlib import Path

import numpy as np
import torch

from ArdaOidnExport import ArdaOidnHdr, ArdaOidnSmall, read_weights


def read_pfm(path):
    with Path(path).open("rb") as stream:
        if stream.readline().strip() != b"PF":
            raise ValueError("Expected a three-channel PFM image")
        width, height = map(int, stream.readline().split())
        scale = float(stream.readline())
        if min(width, height) <= 0 or scale == 0:
            raise ValueError("Invalid PFM image header")
        values = np.frombuffer(stream.read(), dtype="<f4" if scale < 0 else ">f4")
    if values.size != width * height * 3:
        raise ValueError("PFM payload size differs from its dimensions")
    image = values.reshape(height, width, 3)[::-1].copy() * abs(scale)
    if not np.isfinite(image).all():
        raise ValueError("HDR image contains non-finite values")
    return image


def quality(image, reference):
    # Fixed exposure and Reinhard+sRGB-like display transform, shared by both comparisons.
    display = np.power(np.maximum(image, 0) / (1 + np.maximum(image, 0)), 1 / 2.2)
    target = np.power(np.maximum(reference, 0) / (1 + np.maximum(reference, 0)), 1 / 2.2)
    error = image - reference
    display_mse = float(np.mean((display - target) ** 2))
    return {"linear_rmse": float(np.sqrt(np.mean(error ** 2))),
            "relative_l1": float(np.mean(np.abs(error) / (np.abs(reference) + 0.01))),
            "display_psnr": float(-10 * np.log10(max(display_mse, 1e-30)))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path, help="Prefix passed to Cornell --denoise-dump")
    parser.add_argument("--weights", type=Path, default=Path(".cache/model-export/oidn/rt_hdr_small.tza"))
    parser.add_argument("--high-spp", type=Path, help="Optional fixed-camera high-sample Cornell noisy.pfm reference")
    args = parser.parse_args()
    noisy = read_pfm(str(args.prefix) + ".noisy.pfm")
    actual = read_pfm(str(args.prefix) + ".denoised.pfm")
    if actual.shape != noisy.shape or min(noisy.shape[:2]) < 16 or any(x % 16 for x in noisy.shape[:2]):
        raise ValueError("Unexpected or mismatched Cornell model dimensions")
    network = ArdaOidnSmall().eval()
    network.load_state_dict(read_weights(args.weights), strict=True)
    with torch.inference_mode():
        expected = ArdaOidnHdr(network)(torch.from_numpy(noisy.transpose(2, 0, 1)[None].copy()))
    expected = expected.numpy()[0].transpose(1, 2, 0)
    np.testing.assert_allclose(actual, expected, rtol=2e-4, atol=2e-4)
    report = {"shape_hwc": list(actual.shape), "cpu_reference_max_abs": float(np.max(np.abs(actual - expected))),
              "cpu_reference_mean_abs": float(np.mean(np.abs(actual - expected))),
              "nonnegative": bool((actual >= 0).all()), "finite": bool(np.isfinite(actual).all())}
    if args.high_spp:
        target = read_pfm(args.high_spp)
        if target.shape != noisy.shape:
            raise ValueError("High-sample reference shape differs from the model inputs")
        report["noisy_vs_high_spp"] = quality(noisy, target)
        report["denoised_vs_high_spp"] = quality(actual, target)
    Path(str(args.prefix) + ".validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
