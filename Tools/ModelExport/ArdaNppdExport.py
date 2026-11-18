"""Export the official NPPD small_2_spp inference model, with explicit temporal state.

All downloads and PyTorch execution happen offline. The emitted ONNX is subsequently
compiled by ArdaIreeExport; no model framework is needed by the deployed package.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import urllib.request
import zlib

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from ArdaModelPackage import check


NPPD_COMMIT = "3b7ac8452370eb5c74a8f3339c2cd1a843fa33d3"
CHECKPOINT_URL = "https://neural-partitioning-pyramids.mpi-inf.mpg.de/data/nppd_pretrained.zip"
CHECKPOINT_MEMBER = "outputs/small_2_spp/ckpt_epoch/epoch=47-val_loss=0.294069.ckpt"
CHECKPOINT_SHA256 = "037ac6628c96689daeee0172674a0dc2a3b11c44050bbf1200a4eb6620da3fb7"
SOURCE_HASHES = {
    "partitioning_pyramid.py": "76b5e4a92604b6e81c3d693515aa7a3c6afcb27f2add5b66081c32f934cc504d",
    "networks/convunet.py": "4f26dc53b11d67e71eaeb517bd3c3c7b36504097e1fea84c7b37117b57394c8a",
    "model.py": "f4ff54e066f2d41a5dbd83baee91f14e02e7f35bc838dcd7c68e319399789b70",
    "util.py": "00a93b8d0b1dd412e97d457513d4946596d0e851a736b0f04689d2fae2689388"}


def open_range(first, last):
    request = urllib.request.Request(CHECKPOINT_URL, headers={"Range": f"bytes={first}-{last}"})
    response = urllib.request.urlopen(request, timeout=60)
    check(response.status == 206, "Checkpoint server must support partial downloads")
    check(response.headers.get("Content-Range", "").startswith(f"bytes {first}-{last}/"),
          "Checkpoint server returned the wrong byte range")
    return response


def download_checkpoint(destination):
    """Download only one ZIP member, with bounded decompression and ZIP CRC checking."""
    if destination.exists():
        check(hashlib.sha256(destination.read_bytes()).hexdigest() == CHECKPOINT_SHA256, "Checkpoint SHA256 mismatch")
        return
    request = urllib.request.Request(CHECKPOINT_URL, method="HEAD")
    with urllib.request.urlopen(request, timeout=60) as response:
        total = int(response.headers["Content-Length"])
    tail_start = max(0, total - 65557)
    with open_range(tail_start, total - 1) as response:
        tail = response.read()
    eocd = tail.rfind(b"PK\x05\x06")
    check(eocd >= 0, "Checkpoint ZIP directory is missing")
    fields = struct.unpack_from("<4s4H2IH", tail, eocd)
    count, directory_size, directory_offset = fields[4:7]
    check(fields[1:3] == (0, 0) and directory_size < 65536, "Unsupported checkpoint ZIP layout")
    with open_range(directory_offset, directory_offset + directory_size - 1) as response:
        directory = response.read()
    cursor, found = 0, None
    for _ in range(count):
        fields = struct.unpack_from("<4s6H3I5H2I", directory, cursor)
        check(fields[0] == b"PK\x01\x02", "Invalid checkpoint ZIP entry")
        name = directory[cursor + 46:cursor + 46 + fields[10]].decode()
        if name == CHECKPOINT_MEMBER:
            found = fields
        cursor += 46 + fields[10] + fields[11] + fields[12]
    check(found is not None and found[4] == 8 and found[9] < 250_000_000, "Expected small checkpoint was not found")
    offset = found[16]
    with open_range(offset, offset + 29) as response:
        local = struct.unpack("<4s5H3I2H", response.read())
    check(local[0] == b"PK\x03\x04", "Invalid checkpoint local header")
    start = offset + 30 + local[-2] + local[-1]
    temporary = destination.with_suffix(".download")
    decompressor, size, crc = zlib.decompressobj(-15), 0, 0
    print(f"Downloading selected pretrained checkpoint: {found[8]:,} compressed bytes", flush=True)
    with open_range(start, start + found[8] - 1) as response, temporary.open("wb") as output:
        while chunk := response.read(1024 * 1024):
            decoded = decompressor.decompress(chunk, found[9] - size + 1)
            size += len(decoded)
            check(size <= found[9], "Checkpoint exceeds advertised size")
            crc = zlib.crc32(decoded, crc)
            output.write(decoded)
        decoded = decompressor.flush()
        size += len(decoded)
        crc = zlib.crc32(decoded, crc)
        output.write(decoded)
    check(decompressor.eof and size == found[9] and crc == found[7], "Checkpoint ZIP integrity check failed")
    check(hashlib.sha256(temporary.read_bytes()).hexdigest() == CHECKPOINT_SHA256, "Checkpoint SHA256 mismatch")
    temporary.replace(destination)


def source_file(directory, relative):
    path = directory / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    url = f"https://raw.githubusercontent.com/balintio/nppd/{NPPD_COMMIT}/src/{relative}"
    if not path.exists():
        urllib.request.urlretrieve(url, path)
    check(hashlib.sha256(path.read_bytes()).hexdigest() == SOURCE_HASHES[relative], "Pinned NPPD source SHA256 mismatch")
    return path


def source_module(directory, relative):
    path = source_file(directory, relative)
    specification = importlib.util.spec_from_file_location("nppd_" + path.stem, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def original_step_reference(model, inputs, source):
    """Execute the pinned authors' inference methods without importing training code."""
    context = {"torch": torch, "F": F, "lrange": lambda start, stop: list(range(start, stop)),
               "tensor_like": lambda like, data: torch.tensor(data, dtype=like.dtype, device=like.device)}
    utility = ast.parse(source_file(source, "util.py").read_text())
    methods = [item for item in utility.body if isinstance(item, ast.FunctionDef)
               and item.name in ("normalize_radiance", "clip_logp1")]
    projective = source / "noisebase_projective.py"
    if not projective.exists():
        urllib.request.urlretrieve("https://raw.githubusercontent.com/balintio/noisebase/"
            "63056ed5a8583a047a2f3fffdbbbcf5f111e967c/noisebase/torch/projective.py", projective)
    check(hashlib.sha256(projective.read_bytes()).hexdigest() ==
          "822d5bd45b28e59225d3a9d8263bd47479e7f78fad186919f93410146d89d4ad", "Noisebase source SHA256 mismatch")
    methods += [item for item in ast.parse(projective.read_text()).body if isinstance(item, ast.FunctionDef)
                and item.name == "backproject_pixel_centers"]
    original = ast.parse(source_file(source, "model.py").read_text())
    model_class = next(item for item in original.body if isinstance(item, ast.ClassDef) and item.name == "Model")
    methods += [item for item in model_class.body if isinstance(item, ast.FunctionDef) and item.name == "step"]
    exec(compile(ast.Module(body=methods, type_ignores=[]), str(source / "model.py"), "exec"), context)
    frame = {name: value for name, value in inputs.items() if name not in ("temporal", "previous_crop_offset")}
    frame["prev_camera"] = {"crop_offset": inputs["previous_crop_offset"]}
    return context["step"](model, frame, inputs["temporal"])


class NppdInference(nn.Module):
    """Inference-only Model.step adapter; spatial and temporal state stay tensor ports.

    Adapted from NPPD src/model.py:53-112 under Apache2.0 (ArdaNppd.LICENSE.txt).
    Changes: remove training framework/losses, expose flat tensor inputs and state.
    Reprojection coordinates follow Noisebase
    63056ed5a8583a047a2f3fffdbbbcf5f111e967c/noisebase/torch/projective.py.
    """

    def __init__(self, source, height, width):
        super().__init__()
        self.height, self.width = height, width
        self.encoder = nn.Sequential(nn.Conv2d(10, 32, 1), nn.LeakyReLU(0.3),
            nn.Conv2d(32, 32, 1), nn.LeakyReLU(0.3), nn.Conv2d(32, 32, 1))
        self.filter = source_module(source, "partitioning_pyramid.py").PartitioningPyramid()
        self.weight_predictor = source_module(source, "networks/convunet.py").ConvUNet(70, self.filter.inputs)

    @staticmethod
    def normalize(value):
        return value / (torch.mean(value, tuple(range(1, value.dim())), keepdim=True) + 1e-8)

    def forward(self, color, depth, normal, diffuse, motion, crop_offset, previous_crop_offset, temporal):
        height, width = self.height, self.width
        pixel_grid = torch.stack(torch.meshgrid(torch.arange(height, dtype=color.dtype),
            torch.arange(width, dtype=color.dtype), indexing="ij"))
        pixel_pos = pixel_grid + torch.mean(motion, -1) - previous_crop_offset[..., None, None] + crop_offset[..., None, None]
        pixel_xy = torch.permute(torch.flip(pixel_pos, (1,)), (0, 2, 3, 1)) + 0.5
        grid = pixel_xy / torch.tensor([width, height], dtype=color.dtype) * 2 - 1
        reprojected = F.grid_sample(temporal, grid, mode="bilinear", padding_mode="zeros", align_corners=False)
        previous_color, previous_output, previous_feature = reprojected[:, :3], reprojected[:, 3:6], reprojected[:, 6:]
        normalized_color = torch.log(torch.clamp_min(self.normalize(color), 0) + 1)
        encoder_input = torch.cat((depth, normal, diffuse, normalized_color), 1)
        feature = self.encoder(torch.permute(encoder_input, (0, 4, 1, 2, 3)).flatten(0, 1))
        feature = torch.mean(feature.reshape(1, 2, 32, height, width), 1)
        color = torch.mean(color, -1)
        predictor_color = torch.log(torch.clamp_min(self.normalize(torch.cat((previous_color, color), 1)), 0) + 1)
        weights = self.weight_predictor(torch.cat((predictor_color, previous_feature, feature), 1))
        t_lambda = torch.sigmoid(weights[0][:, self.filter.t_lambda_index, None])
        color = t_lambda * previous_color + (1 - t_lambda) * color
        feature = t_lambda * previous_feature + (1 - t_lambda) * feature
        output = self.filter(weights, color, previous_output)
        return output, torch.cat((color, output, feature), 1)


def load_inference_model(directory, height, width):
    directory.mkdir(parents=True, exist_ok=True)
    checkpoint = directory / "small_2_spp.ckpt"
    download_checkpoint(checkpoint)
    source = directory / "source"
    model = NppdInference(source, height, width).eval()
    license_path = source / "LICENSE"
    if not license_path.exists():
        urllib.request.urlretrieve(f"https://raw.githubusercontent.com/balintio/nppd/{NPPD_COMMIT}/LICENSE", license_path)
    checkpoint_data = torch.load(checkpoint, map_location="cpu", weights_only=True)
    state = checkpoint_data["state_dict"]
    # The Lightning checkpoint also stores perceptual training-loss network weights.
    inference_weights = {name: value for name, value in state.items()
                         if name.startswith("encoder.") or name.startswith("weight_predictor.")}
    model.load_state_dict(inference_weights, strict=True)
    return model, source, checkpoint


def export(directory, height, width):
    check(height >= 64 and width >= 64 and height % 32 == width % 32 == 0,
          "NPPD static pilot dimensions must be at least64 and multiples of32")
    model, source, checkpoint = load_inference_model(directory, height, width)
    generator = torch.Generator().manual_seed(1729)
    sample_shape = (1, 3, height, width, 2)
    inputs = {
        "color": torch.rand(sample_shape, generator=generator) * 2,
        "depth": torch.rand((1, 1, height, width, 2), generator=generator),
        "normal": torch.rand(sample_shape, generator=generator) * 2 - 1,
        "diffuse": torch.rand(sample_shape, generator=generator),
        "motion": torch.zeros((1, 2, height, width, 2)),
        "crop_offset": torch.zeros((1, 2)), "previous_crop_offset": torch.zeros((1, 2)),
        "temporal": torch.zeros((1, 38, height, width))}
    # A nonzero prior frame exercises temporal reprojection rather than a cold-start-only path.
    with torch.no_grad():
        _, inputs["temporal"] = model(*inputs.values())
        inputs["motion"][:] = 0.25
        expected = model(*inputs.values())
        original = original_step_reference(model, inputs, source)
        for adapted, authored in zip(expected, original[:2]):
            torch.testing.assert_close(adapted, authored, rtol=0, atol=0)
        next_inputs = dict(inputs)
        next_inputs["temporal"] = expected[1]
        next_expected = model(*next_inputs.values())
    stem = directory / f"nppd_small_2spp_{height}x{width}"
    torch.onnx.export(model, tuple(inputs.values()), str(stem) + ".onnx", dynamo=False,
        opset_version=17, input_names=list(inputs), output_names=["denoised_hdr", "next_temporal"],
        do_constant_folding=False)
    import onnx
    exported = onnx.load(str(stem) + ".onnx")
    state_names = set(model.state_dict())
    exported_state = [item.name for item in exported.graph.initializer if item.name in state_names]
    missing = set(dict(model.named_parameters())) - set(exported_state)
    if missing:
        raise ValueError(f"ONNX capture did not preserve named model parameters: {sorted(missing)}")
    state_metadata = exported.metadata_props.add()
    state_metadata.key = "arda.state_dict"
    state_metadata.value = json.dumps(exported_state)
    onnx.checker.check_model(exported, full_check=True)
    onnx.save(exported, str(stem) + ".onnx")
    np.savez(str(stem) + "_reference.npz", **{name: value.numpy() for name, value in inputs.items()},
             denoised_hdr=expected[0].numpy(), next_temporal=expected[1].numpy())
    raw_directory = Path(str(stem) + "_raw")
    raw_directory.mkdir(exist_ok=True)
    raw_manifest = []
    for name, value in list(inputs.items()) + [("denoised_hdr", expected[0]), ("next_temporal", expected[1])]:
        array = value.numpy().astype("<f4")
        array.tofile(raw_directory / (name + ".bin"))
        raw_manifest.append({"name": name, "role": "input" if name in inputs else "output",
                             "file": name + ".bin", "dtype": "float32", "shape": list(array.shape), "byte_size": array.nbytes})
    for name, value in zip(("denoised_hdr", "next_temporal"), next_expected):
        value.numpy().astype("<f4").tofile(raw_directory / (name + "_frame2.bin"))
    (raw_directory / "tensors.json").write_text(json.dumps(raw_manifest, indent=2) + "\n")
    provenance = {"project": "https://github.com/balintio/nppd", "commit": NPPD_COMMIT,
        "checkpoint_url": CHECKPOINT_URL, "checkpoint_member": CHECKPOINT_MEMBER,
        "checkpoint_sha256": hashlib.sha256(checkpoint.read_bytes()).hexdigest(),
        "parameters": sum(value.numel() for value in model.parameters()), "state_dict_names": exported_state,
        "height": height, "width": width, "samples_per_pixel": 2,
        "temporal_channels": 38, "license": "source/LICENSE", "torch": torch.__version__,
        "original_model_step_parity": "bitwise exact on the exported reference case",
        "source_sha256": {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in source.rglob("*.py")}}
    stem.with_suffix(".provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(f"Exported {stem}.onnx with {provenance['parameters']:,} pretrained parameters")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(".cache/model-export/nppd"))
    parser.add_argument("--height", type=int, default=64)
    parser.add_argument("--width", type=int, default=64)
    args = parser.parse_args()
    export(args.output_dir, args.height, args.width)


if __name__ == "__main__":
    main()
