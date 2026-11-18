#!/usr/bin/env python3
"""Build and launch Cornell Box with an exported AI model and checkpoint."""

from RunCornellBox import main


if __name__ == "__main__":
    raise SystemExit(main(denoising=True))
