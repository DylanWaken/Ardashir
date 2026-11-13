#!/usr/bin/env python3
"""Fail when published documentation links to the Understanding Unreal site.

Ardashir publishes its own documentation tree (Docs) and must not offer entry
points to the migrated Unreal essays, the old Docs/Unreal tree, or the
standalone Understanding Unreal repository.  This check is stdlib-only and
uses no other checkout, so it can run before the Pages artifact is uploaded.
"""

from __future__ import annotations

import argparse
import os
import sys
import urllib.parse
from html.parser import HTMLParser
from pathlib import Path
from typing import List, Optional, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DOCS = REPO_ROOT / "Docs"
LINK_ATTRIBUTES = {"href", "src"}
GITHUB_HOSTS = {"github.com", "www.github.com"}
PAGES_HOSTS = {"dylanwaken.github.io"}
GITHUB_OWNER = "dylanwaken"
UNREAL_REPO = "understanding-unreal"
UNREAL_DIRECTORY = "unreal"


class LinkParser(HTMLParser):
    """Collect href and src attribute values from one HTML document."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.links: List[str] = []

    def handle_starttag(self, tag: str, attrs) -> None:
        self._collect(attrs)

    def handle_startendtag(self, tag: str, attrs) -> None:
        self._collect(attrs)

    def _collect(self, attrs) -> None:
        for name, value in attrs:
            if name in LINK_ATTRIBUTES and value is not None:
                self.links.append(value)


def _url_path_segments(path: str) -> List[str]:
    """Decode and normalize URL path segments, dropping dot segments."""
    decoded = urllib.parse.unquote(path).replace("\\", "/")
    segments: List[str] = []
    for segment in decoded.split("/"):
        folded = segment.casefold()
        if not folded or folded == ".":
            continue
        if folded == "..":
            if segments:
                segments.pop()
            continue
        segments.append(folded)
    return segments


def _split_url(value: str):
    """Parse a value as a URL, including protocol-relative references."""
    candidate = value.replace("\\", "/").strip()
    if candidate.startswith("//"):
        candidate = "https://" + candidate.lstrip("/")
    return urllib.parse.urlsplit(candidate)


def _is_forbidden_url(value: str) -> bool:
    try:
        parsed = _split_url(value)
        host = (parsed.hostname or "").casefold().rstrip(".")
    except ValueError:
        return False
    if not host:
        return False

    segments = _url_path_segments(parsed.path)
    if host in GITHUB_HOSTS:
        if len(segments) >= 2 and segments[0] == GITHUB_OWNER:
            repository = segments[1]
            if repository.endswith(".git"):
                repository = repository[:-4]
            return repository == UNREAL_REPO
        return False
    if host in PAGES_HOSTS:
        return bool(segments) and segments[0] == UNREAL_REPO
    return False


def _is_forbidden_local(value: str, source: Path, docs_root: Path) -> bool:
    """Check whether a relative reference resolves inside Docs/Unreal."""
    candidate = value.strip()
    try:
        parsed = urllib.parse.urlsplit(candidate.replace("\\", "/"))
    except ValueError:
        return False
    if parsed.scheme or parsed.netloc or candidate.startswith("//"):
        return False
    path = urllib.parse.unquote(parsed.path)
    if not path:
        return False
    if path.startswith("/"):
        target = docs_root / path.lstrip("/")
    else:
        target = source.parent / path
    try:
        relative = os.path.relpath(
            os.path.abspath(target), os.path.abspath(docs_root)
        )
    except (OSError, ValueError):
        return False
    parts = Path(relative).parts
    return bool(parts) and parts[0].casefold() == UNREAL_DIRECTORY


def find_offenses(docs_root: Path) -> List[Tuple[Path, str]]:
    """Return every offending (file, URL) pair in sorted file order."""
    findings: List[Tuple[Path, str]] = []
    seen: Set[Tuple[Path, str]] = set()
    for source in sorted(docs_root.rglob("*.html")):
        parser = LinkParser()
        parser.feed(source.read_text(encoding="utf-8", errors="replace"))
        parser.close()
        for value in parser.links:
            url = value.strip()
            if not url:
                continue
            key = (source, url)
            if key in seen:
                continue
            if _is_forbidden_url(url) or _is_forbidden_local(url, source, docs_root):
                seen.add(key)
                findings.append(key)
    return findings


def _display_path(path: Path) -> str:
    try:
        return path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--docs",
        type=Path,
        default=DEFAULT_DOCS,
        help="documentation root to scan (default: <repo>/Docs)",
    )
    args = parser.parse_args(argv)
    docs_root = args.docs.resolve()
    if not docs_root.is_dir():
        parser.error("documentation directory not found: %s" % docs_root)

    findings = find_offenses(docs_root)
    if findings:
        for source, url in findings:
            print("%s: %s" % (_display_path(source), url))
        return 1
    print("No Understanding Unreal links found in %s." % _display_path(docs_root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
