"""Export OIDN's pretrained spatial HDR baseline for the offline CUDA compiler.

Network topology and PU transfer are adapted from Intel Open Image Denoise:
https://github.com/RenderKit/oidn/tree/6602ee2ca38a1a2a02135beed8f6e68eed630180/training
Copyright 2018 Intel Corporation. SPDX-License-Identifier: Apache-2.0

This script uses PyTorch only offline. The output contains the network, automatic
exposure, PU encoding and inverse transform. No OIDN runtime is used or exported.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import urllib.request
from pathlib import Path

import numpy as np
import onnx
import torch
from torch import nn
from torch.nn import functional as functional


WEIGHT_REVISION = "28883d1769d5930e13cf7f1676dd852bd81ed9e7"
WEIGHT_SHA256 = "c9171947f2bceb4367725b7a0d5b4e0d663ac44108386af42f1b49e4361952e7"
WEIGHT_URL = f"https://media.githubusercontent.com/media/RenderKit/oidn-weights/{WEIGHT_REVISION}/rt_hdr_small.tza"
SOURCE_REVISION = "6602ee2ca38a1a2a02135beed8f6e68eed630180"
PU_A, PU_B, PU_C, PU_D = 1.41283765e3, 1.64593172, 4.31384981e-1, -2.94139609e-3
PU_E, PU_F, PU_G = 1.92653254e-1, 6.26026094e-3, 9.98620152e-1
PU_Y0, PU_Y1 = 1.57945760e-6, 3.22087631e-2
PU_X0, PU_X1 = 2.23151711e-3, 3.70974749e-1


def read_weights(path):
    """Read the pinned little-endian TZA v2 archive, rejecting malformed ranges."""
    data = Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest() != WEIGHT_SHA256:
        raise ValueError("OIDN checkpoint does not match the pinned pretrained weight hash")
    magic, major, minor, table = struct.unpack_from("<HBBQ", data)
    if magic != 0x41D7 or major != 2 or table >= len(data):
        raise ValueError("Invalid OIDN tensor archive")
    cursor = table

    def take(fmt):
        nonlocal cursor
        result = struct.unpack_from("<" + fmt, data, cursor)
        cursor += struct.calcsize("<" + fmt)
        return result[0] if len(result) == 1 else result

    result = {}
    for _ in range(take("I")):
        length = take("H")
        name = data[cursor:cursor + length].decode("utf-8")
        cursor += length
        rank = take("B")
        shape = tuple(take("I") for _ in range(rank))
        layout = data[cursor:cursor + rank].decode("ascii")
        cursor += rank
        dtype = {"f": "<f4", "h": "<f2"}[chr(take("B"))]
        offset = take("Q")
        size = math.prod(shape) * np.dtype(dtype).itemsize
        if offset + size > table or name in result:
            raise ValueError("Invalid or duplicate OIDN tensor")
        array = np.frombuffer(data, dtype=dtype, count=math.prod(shape), offset=offset).reshape(shape)
        expected_layout = "oihw" if rank == 4 else "x"
        if layout != expected_layout:
            raise ValueError(f"Unexpected tensor layout for {name}: {layout}")
        result[name] = torch.from_numpy(array.astype(np.float32))
    return result


def pu_forward(image):
    return torch.where(image <= PU_Y0, PU_A * image,
                       torch.where(image <= PU_Y1, PU_B * torch.pow(image, PU_C) + PU_D,
                                   PU_E * torch.log(image + PU_F) + PU_G))


PU_SCALE = 1.0 / pu_forward(torch.tensor(65504.0)).item()


def pu_inverse(image):
    return torch.where(image <= PU_X0, image / PU_A,
                       torch.where(image <= PU_X1, torch.pow((image - PU_D) / PU_B, 1.0 / PU_C),
                                   torch.exp((image - PU_G) / PU_E) - PU_F))


class ArdaOidnSmall(nn.Module):
    """The released color-only small network, with the original parameter names."""

    def __init__(self):
        super().__init__()
        channels = {"enc_conv0": (3, 32), "enc_conv1": (32, 32),
                    "enc_conv2": (32, 32), "enc_conv3": (32, 32), "enc_conv4": (32, 32),
                    "enc_conv5a": (32, 32), "enc_conv5b": (32, 32),
                    "dec_conv4a": (64, 64), "dec_conv4b": (64, 64),
                    "dec_conv3a": (96, 64), "dec_conv3b": (64, 64),
                    "dec_conv2a": (96, 64), "dec_conv2b": (64, 32),
                    "dec_conv1a": (35, 32), "dec_conv1b": (32, 32), "dec_conv0": (32, 3)}
        for name, (inputs, outputs) in channels.items():
            setattr(self, name, nn.Conv2d(inputs, outputs, 3, padding=1))

    def forward(self, image):
        value = torch.relu(self.enc_conv0(image))
        value = torch.relu(self.enc_conv1(value))
        pool1 = functional.max_pool2d(value, 2, 2)
        pool2 = functional.max_pool2d(torch.relu(self.enc_conv2(pool1)), 2, 2)
        pool3 = functional.max_pool2d(torch.relu(self.enc_conv3(pool2)), 2, 2)
        value = functional.max_pool2d(torch.relu(self.enc_conv4(pool3)), 2, 2)
        value = torch.relu(self.enc_conv5a(value))
        value = torch.relu(self.enc_conv5b(value))
        for stage, skip in ((4, pool3), (3, pool2), (2, pool1), (1, image)):
            value = functional.interpolate(value, scale_factor=2, mode="nearest")
            value = torch.cat((value, skip), dim=1)
            value = torch.relu(getattr(self, f"dec_conv{stage}a")(value))
            value = torch.relu(getattr(self, f"dec_conv{stage}b")(value))
        return self.dec_conv0(value)


class ArdaOidnHdr(nn.Module):
    """Full-frame static multiples-of-16 HDR inference, including black-frame handling."""

    def __init__(self, network):
        super().__init__()
        self.network = network
        self.register_buffer("luminance", torch.tensor([0.212671, 0.715160, 0.072169]).reshape(1, 3, 1, 1))

    def exposure(self, image):
        # At these explicitly supported dimensions the upstream <=16px binning is exactly 16x16.
        luminance = (image * self.luminance).sum(dim=1, keepdim=True)
        bins = functional.avg_pool2d(luminance, 16, 16)
        valid = bins > 1e-8
        count = valid.to(torch.float32).sum(dim=(2, 3), keepdim=True)
        logs = torch.where(valid, torch.log(torch.clamp(bins, min=1e-8)), torch.zeros_like(bins))
        mean = logs.sum(dim=(2, 3), keepdim=True) / torch.clamp(count, min=1)
        return torch.where(count > 0, 0.18 / torch.exp(mean), torch.ones_like(mean))

    def forward(self, noisy_hdr):
        image = torch.clamp(noisy_hdr, min=0)
        exposure = self.exposure(image)
        encoded = pu_forward(image * exposure) * PU_SCALE
        filtered = torch.clamp(self.network(encoded), min=0)
        return pu_inverse(filtered / PU_SCALE) / exposure


def numpy_exposure(image):
    """Independent form of upstream training/color.py autoexposure for preprocessing checks."""
    image = image[0].transpose(1, 2, 0)
    luminance = image[..., 0] * 0.212671 + image[..., 1] * 0.715160 + image[..., 2] * 0.072169
    height, width = luminance.shape
    bins = np.array([luminance[y:y + 16, x:x + 16].mean()
                     for y in range(0, height, 16) for x in range(0, width, 16)])
    bins = bins[bins > 1e-8]
    return 0.18 / np.exp2(np.log2(bins).mean()) if bins.size else 1.0


def export_model(weights, output, height, width, reference=None):
    if min(height, width) < 16 or height % 16 or width % 16:
        raise ValueError("Initial OIDN export requires dimensions >= 16 and divisible by 16")
    network = ArdaOidnSmall().eval()
    network.load_state_dict(read_weights(weights), strict=True)
    model = ArdaOidnHdr(network).eval()
    generator = np.random.default_rng(20261002)
    source = generator.lognormal(-1.0, 1.2, (1, 3, height, width)).astype(np.float32)
    image = torch.from_numpy(source)
    with torch.inference_mode():
        exposure = model.exposure(image).item()
        np.testing.assert_allclose(exposure, numpy_exposure(source), rtol=2e-6)
        assert model.exposure(torch.zeros_like(image)).item() == 1.0
        expected = model(image).numpy()
    if not np.isfinite(expected).all() or (expected < 0).any():
        raise ValueError("Pretrained reference produced invalid linear HDR output")
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(model, (image,), str(output), input_names=["noisy_hdr"],
                      output_names=["denoised_hdr"], opset_version=17, dynamo=False,
                      do_constant_folding=False, external_data=False)
    exported = onnx.load(output)
    state_names = set(model.state_dict())
    exported_state = [item.name for item in exported.graph.initializer if item.name in state_names]
    missing = set(dict(model.named_parameters())) - set(exported_state)
    if missing:
        raise ValueError(f"ONNX capture did not preserve named model parameters: {sorted(missing)}")
    state_metadata = exported.metadata_props.add()
    state_metadata.key = "arda.state_dict"
    state_metadata.value = json.dumps(exported_state)
    onnx.checker.check_model(exported, full_check=True)
    onnx.save(exported, output)
    if reference:
        np.savez(reference, noisy_hdr=source, denoised_hdr=expected)
    provenance = {"model": "OIDN rt_hdr_small", "spatial_only": True,
                  "source_revision": SOURCE_REVISION, "weights_revision": WEIGHT_REVISION,
                  "weights_sha256": WEIGHT_SHA256, "weights_url": WEIGHT_URL,
                  "shape_nchw": list(source.shape), "precision": "float32",
                  "preprocessing": "nonnegative linear HDR, 16x16 luminance bins, geometric-mean exposure, normalized PU",
                  "postprocessing": "nonnegative encoded output, inverse PU, inverse exposure",
                  "reference_exposure": exposure, "torch_version": torch.__version__,
                  "onnx_version": onnx.__version__, "opset": 17,
                  "runtime_dependencies": "none; compile ONNX to a CUDA graph descriptor and separate state dict",
                  "state_dict_names": exported_state}
    output.with_suffix(".provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"onnx": str(output), "shape": list(source.shape), "nodes": len(exported.graph.node),
                      "parameters": sum(parameter.numel() for parameter in model.parameters()),
                      "reference": str(reference) if reference else None}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weights", type=Path, default=Path(".cache/model-export/oidn/rt_hdr_small.tza"))
    parser.add_argument("--download", action="store_true", help="Download only the pinned 634 KB pretrained checkpoint")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--width", type=int, default=64)
    parser.add_argument("--reference", type=Path, help="Write fixed-seed input and CPU reference output for CUDA parity")
    args = parser.parse_args()
    if args.download and not args.weights.exists():
        args.weights.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.weights.with_suffix(".download")
        urllib.request.urlretrieve(WEIGHT_URL, temporary)
        read_weights(temporary)  # Validate the hash and archive before installing the checkpoint.
        temporary.replace(args.weights)
    export_model(args.weights, args.output, args.height, args.width, args.reference)


if __name__ == "__main__":
    main()
