"""Public static input profiles and exported artifact descriptions."""

from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping
from ArdaIreeExport import SUPPORTED_TARGETS


# IREE's built-in targets plus audited explicit SIMD target descriptions for
# 89/90/120. These do not promise every model or a Tensor Core schedule.
SUPPORTED_SMS = SUPPORTED_TARGETS


def supported_architectures() -> tuple[str, ...]:
    """Return the numeric CUDA targets understood by the pinned compiler."""
    return tuple(f"sm_{target}" for target in SUPPORTED_SMS)


@dataclass(frozen=True)
class ExportArtifact:
    """One compiled model descriptor, its initial state, and compiler metadata."""
    target_sm: int
    package_path: Path
    metadata_path: Path
    checkpoint_path: Path

    @property
    def descriptor_path(self) -> Path:
        return self.package_path

    @property
    def architecture(self) -> str:
        return f"sm_{self.target_sm}"


@dataclass(frozen=True)
class ExportResult:
    """An exported bundle. With f=None this object owns its temporary files."""
    name: str
    manifest_path: Path
    onnx_path: Path
    reference_path: Path
    artifacts: tuple[ExportArtifact, ...]
    _files: tuple[str, ...] = field(default=(), repr=False, compare=False)
    _owner: object = field(default=None, repr=False, compare=False)

    @property
    def packages(self) -> Mapping[str, Path]:
        """Compatibility alias for descriptors; these files contain no model weights."""
        return self.descriptors

    @property
    def descriptors(self) -> Mapping[str, Path]:
        return {artifact.architecture: artifact.package_path for artifact in self.artifacts}

    @property
    def checkpoints(self) -> Mapping[str, Path]:
        return {artifact.architecture: artifact.checkpoint_path for artifact in self.artifacts}

    def save(self, directory: str | Path) -> "ExportResult":
        """Publish a copy of the complete bundle and return persistent paths."""
        from ._export import _save_result
        return _save_result(self, directory)

    def close(self) -> None:
        """Release owned temporary files. Saved bundle files are never removed."""
        if self._owner is not None:
            self._owner.cleanup()

    def __enter__(self) -> "ExportResult":
        return self

    def __exit__(self, *_):
        self.close()
