#!/usr/bin/env python3
"""Generate PiTrac build version strings.

Usage:
  version.py <base_version> [dist_sha]

Rules:
  1. If git metadata is available, use the latest SemVer tag matching
     `pitrac/v*` and include commit distance/hash when not on the tag.
  2. If a dist sha is provided (from version.gen), include it when git is not
     available.
  3. Fall back to base_version.
"""

from __future__ import annotations

import re
import subprocess
import sys


TAG_PREFIX = "pitrac/v"
SEMVER_RE = re.compile(r"^(\d+\.\d+\.\d+)(?:-([0-9A-Za-z.-]+))?$")


def _run_git(args: list[str]) -> str | None:
    try:
        out = subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL, text=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return out.strip()


def _sanitize_sha(value: str) -> str:
    value = value.strip()
    if not value:
        return ""
    # Accept raw hash or values like "gabc1234".
    if value.startswith("g"):
        value = value[1:]
    return value[:12]


def _from_git(base_version: str) -> str | None:
    described = _run_git(
        ["describe", "--tags", "--long", "--dirty", "--abbrev=12", "--match", f"{TAG_PREFIX}[0-9]*"]
    )
    if not described:
        return None

    # Format: pitrac/v1.2.3-0-g<sha>[-dirty]
    dirty = described.endswith("-dirty")
    if dirty:
        described = described[:-6]

    if "-g" not in described:
        return None

    left, sha = described.rsplit("-g", 1)
    if "-" not in left:
        return None

    tag, distance_text = left.rsplit("-", 1)
    if not tag.startswith(TAG_PREFIX):
        return None

    tag_version = tag[len(TAG_PREFIX):]
    if not SEMVER_RE.match(tag_version):
        return None

    try:
        distance = int(distance_text)
    except ValueError:
        return None

    short_sha = _sanitize_sha(sha)
    suffix_dirty = ".dirty" if dirty else ""

    if distance == 0 and not dirty:
        return tag_version

    return f"{tag_version}+{distance}.g{short_sha}{suffix_dirty}"


def _fallback(base_version: str, dist_sha: str) -> str:
    git_sha = _sanitize_sha(_run_git(["rev-parse", "--short=12", "HEAD"]) or "")
    if git_sha:
        return f"{base_version}+0.g{git_sha}"

    dist_sha = _sanitize_sha(dist_sha)
    if dist_sha:
        return f"{base_version}+0.g{dist_sha}"

    return base_version


def main() -> int:
    base_version = sys.argv[1] if len(sys.argv) > 1 else "0.0.1"
    dist_sha = sys.argv[2] if len(sys.argv) > 2 else ""

    if not SEMVER_RE.match(base_version):
        # Keep behavior deterministic if an invalid base version is passed in.
        base_version = "0.0.1"

    resolved = _from_git(base_version)
    if resolved is None:
        resolved = _fallback(base_version, dist_sha)

    print(resolved)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
