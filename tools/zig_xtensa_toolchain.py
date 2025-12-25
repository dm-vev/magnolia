#!/usr/bin/env python3
"""
Fetch and install a Zig toolchain that supports Espressif Xtensa targets.

This repo's system Zig may list `xtensa` as a target but still lack a codegen
backend. Magnolia's Zig applets therefore use a forked Zig toolchain (similar
to esp-rs) from kassane/zig-espressif-bootstrap.

The toolchain is installed under `build/toolchains/zig-xtensa/` (gitignored).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from urllib.request import Request, urlopen


DEFAULT_TAG = "0.14.0-xtensa-dev"
DEFAULT_REPO = "kassane/zig-espressif-bootstrap"


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    req = Request(url, headers={"User-Agent": "codex-cli"})
    with urlopen(req, timeout=60) as resp, open(dest, "wb") as f:
        shutil.copyfileobj(resp, f)


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _platform_asset_name() -> str:
    sysname = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        arch = "x86_64"
    elif machine in ("aarch64", "arm64"):
        arch = "aarch64"
    else:
        raise SystemExit(f"Unsupported host arch for Zig Xtensa toolchain: {machine}")

    if sysname == "linux":
        return f"zig-relsafe-espressif-{arch}-linux-musl-baseline.tar.xz"
    if sysname == "darwin":
        return f"zig-relsafe-espressif-{arch}-macos-baseline.tar.xz"
    if sysname == "windows":
        return f"zig-relsafe-espressif-{arch}-windows-baseline.zip"
    raise SystemExit(f"Unsupported host OS for Zig Xtensa toolchain: {sysname}")


def _checksum_url(repo: str, tag: str) -> str:
    return f"https://github.com/{repo}/releases/download/{tag}/checksum.256"


def _asset_url(repo: str, tag: str, asset: str) -> str:
    return f"https://github.com/{repo}/releases/download/{tag}/{asset}"


def _parse_checksum_file(path: Path) -> dict[str, str]:
    # Format: "<sha256>  <filename>"
    checksums: dict[str, str] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        digest, name = parts[0], parts[-1]
        if len(digest) == 64:
            checksums[name] = digest.lower()
    return checksums


def ensure_toolchain(install_dir: Path, repo: str, tag: str) -> Path:
    install_dir = install_dir.resolve()
    zig_link = install_dir / "zig"
    if zig_link.exists():
        return zig_link

    asset = _platform_asset_name()
    if asset.endswith(".zip"):
        raise SystemExit("Windows host is not supported by this installer yet (zip extraction missing).")

    with tempfile.TemporaryDirectory(prefix="zig-xtensa-") as td:
        tdir = Path(td)
        checksum_path = tdir / "checksum.256"
        archive_path = tdir / asset

        _download(_checksum_url(repo, tag), checksum_path)
        checksums = _parse_checksum_file(checksum_path)
        expected = checksums.get(asset)
        if not expected:
            raise SystemExit(f"checksum.256 missing entry for {asset}")

        _download(_asset_url(repo, tag, asset), archive_path)
        actual = _sha256(archive_path)
        if actual.lower() != expected.lower():
            raise SystemExit(f"SHA256 mismatch for {asset}: expected {expected}, got {actual}")

        install_dir.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive_path, "r:*") as tf:
            tf.extractall(install_dir)

    # Find extracted zig binary.
    candidates = list(install_dir.rglob("zig"))
    candidates = [p for p in candidates if p.is_file() and os.access(p, os.X_OK)]
    if not candidates:
        raise SystemExit(f"Installed archive but could not find `zig` under {install_dir}")
    candidates.sort(key=lambda p: len(str(p)))
    real_zig = candidates[0]

    try:
        zig_link.symlink_to(real_zig)
    except OSError:
        shutil.copy2(real_zig, zig_link)

    return zig_link


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--install-dir", type=Path, default=_repo_root() / "build" / "toolchains" / "zig-xtensa")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--tag", default=DEFAULT_TAG)
    ap.add_argument("--print-zig", action="store_true", help="Print zig path to stdout and exit")
    args = ap.parse_args(argv)

    zig_path = ensure_toolchain(args.install_dir, args.repo, args.tag)

    if args.print_zig:
        print(str(zig_path))
        return 0

    # Sanity check.
    try:
        subprocess.check_call([str(zig_path), "version"])
    except subprocess.CalledProcessError as exc:
        return exc.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

