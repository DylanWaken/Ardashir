"""Export an ordinary PyTorch module through the public Arda Python API.

Install the local package first: python -m pip install -e Tools/ModelExport
This small untrained network demonstrates packaging, not denoising quality.
"""

import argparse
from pathlib import Path

import torch

import arda


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path(".cache/model-export/torch-api"))
    parser.add_argument("--arch", nargs="+", default=["sm_80", "sm_86"])
    parser.add_argument("--verify", action="store_true", help="Compare every variant with PyTorch using the CUDA driver")
    args = parser.parse_args()

    torch.manual_seed(7)
    model = torch.nn.Sequential(
        torch.nn.Conv2d(3, 8, kernel_size=3, padding=1),
        torch.nn.ReLU(),
        torch.nn.Conv2d(8, 3, kernel_size=1),
    ).half()
    # The model is captured once; the descriptor and initial checkpoint are separate.
    result = arda.export(
        model,
        (torch.ones(1, 3, 32, 32, dtype=torch.float16),),
        args.output_dir,
        target_archs=args.arch,
        input_names=["image"],
        output_names=["filtered_image"],
        name="image_filter",
        verify=args.verify,
        rtol=0.005,
        atol=0.002,
    )
    # After training or torch.load(..., weights_only=True), publish another state
    # for these same compiled descriptors without invoking the compiler again.
    checkpoint = arda.export_state_dict(model.state_dict(), result,
                                        args.output_dir / "image_filter.alternate.ardastate")
    print(result.manifest_path)
    print(checkpoint)


if __name__ == "__main__":
    main()
