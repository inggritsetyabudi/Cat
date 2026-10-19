#!/usr/bin/env python3
"""Fetch and selectively extract the official LLVM 23.1.3 Linux SDK.

The archive is authenticated before the destination is marked usable.  This is
intentionally not a general archive extractor and never falls back to a system
LLVM installation or an LLVM source build.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import pathlib
import tarfile
import time
import urllib.request

import zstandard

SOURCE_URL = "https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/LLVM-23.1.3-Linux-X64.tar.zst"
EXPECTED_SHA256 = "14d2f701eb68fb799001bdea6231048f3990690fa2f406d563555ff8f744daba"
EXPECTED_SIZE = 1188398549
MAX_DISK = 2_500_000_000
MARKER_NAME = ".llvm-sdk-verified.json"


class HashingReader(io.RawIOBase):
    def __init__(self, response):
        self.response = response
        self.digest = hashlib.sha256()
        self.count = 0

    def readable(self) -> bool:
        return True

    def readinto(self, buffer) -> int:
        block = self.response.read(len(buffer))
        buffer[: len(block)] = block
        self.digest.update(block)
        self.count += len(block)
        return len(block)


def _file_hash(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1 << 20):
            digest.update(block)
    return digest.hexdigest()


def _verified(destination: pathlib.Path) -> bool:
    marker = destination / MARKER_NAME
    try:
        data = json.loads(marker.read_text())
    except (FileNotFoundError, OSError, ValueError):
        return False
    if not isinstance(data, dict):
        return False
    selected = data.get("selected")
    if not isinstance(selected, list) or not selected:
        return False
    try:
        for item in selected:
            relative = pathlib.PurePosixPath(item["path"])
            if relative.is_absolute() or ".." in relative.parts:
                return False
            path = destination / relative
            if not path.is_file() or not path.resolve().is_relative_to(destination):
                return False
            if item.get("symlink") is not None:
                if not path.is_symlink() or os.readlink(path) != item["symlink"]:
                    return False
            elif path.is_symlink() or path.stat().st_size != item["bytes"] or _file_hash(path) != item["sha256"]:
                return False
    except (KeyError, TypeError, OSError, ValueError):
        return False
    return (
        data.get("source") == SOURCE_URL
        and data.get("sha256") == EXPECTED_SHA256
        and data.get("download_bytes") == EXPECTED_SIZE
        and (destination / "bin/llvm-config").is_file()
        and (destination / "include/llvm/Config/llvm-config.h").is_file()
    )


def _safe_parts(name: str) -> tuple[str, ...]:
    path = pathlib.PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise RuntimeError(f"unsafe archive path: {name!r}")
    return path.parts


def _wanted(parts: tuple[str, ...]) -> bool:
    if not parts:
        return False
    if parts[0] == "bin" and parts[1:] == ("llvm-config",):
        return True
    if parts[0] == "include" and len(parts) >= 2 and parts[1] in {"llvm", "llvm-c"}:
        return True
    if parts[0] in {"lib", "lib64"} and len(parts) == 2 and parts[1].startswith("libLLVM") and parts[1].endswith(".a"):
        return True
    # Preserve notices/licenses needed to redistribute the selected SDK.
    lowered = "/".join(parts).lower()
    return pathlib.PurePosixPath(*parts).name.lower() in {
        "license", "license.txt", "license.md", "notice", "notice.txt", "copyright",
    } or any(token in lowered.split("/")[-1] for token in ("license", "notice"))


def fetch(destination: pathlib.Path) -> dict:
    destination = destination.resolve()
    if _verified(destination):
        return json.loads((destination / MARKER_NAME).read_text())
    if destination.exists() and (not destination.is_dir() or any(destination.iterdir())):
        raise RuntimeError("Refusing nonempty unverified SDK destination; use a new or empty directory: " + str(destination))
    destination.mkdir(parents=True, exist_ok=True)
    marker = destination / MARKER_NAME
    partial = destination / ".llvm-sdk-unverified.json"
    selected: list[dict] = []
    links: list[tuple[pathlib.Path, str]] = []
    selected_bytes = 0
    started = time.monotonic()
    response = urllib.request.urlopen(SOURCE_URL, timeout=90)
    reader = HashingReader(response)
    compressed = io.BufferedReader(reader, 1 << 20)
    try:
        with zstandard.ZstdDecompressor(max_window_size=1 << 30).stream_reader(compressed, closefd=False) as stream:
            with tarfile.open(fileobj=stream, mode="r|") as archive:
                root: str | None = None
                for member in archive:
                    raw = _safe_parts(member.name)
                    if not raw:
                        continue
                    if root is None:
                        root = raw[0]
                    if raw[0] != root:
                        raise RuntimeError(f"archive contains multiple roots: {member.name!r}")
                    parts = raw[1:]  # official package root, chosen from the original archive
                    if not parts or not _wanted(parts) or member.isdir():
                        continue
                    if member.size < 0 or selected_bytes + member.size > MAX_DISK:
                        raise RuntimeError("selective SDK extraction exceeds the 2.5 GB disk budget")
                    target = destination.joinpath(*parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    file_digest = None
                    if member.issym():
                        resolved = (target.parent / member.linkname).resolve()
                        if not resolved.is_relative_to(destination):
                            raise RuntimeError(f"unsafe SDK symlink: {member.name} -> {member.linkname}")
                        links.append((target, member.linkname))
                    elif member.islnk():
                        raise RuntimeError(f"hard links are not permitted: {member.name}")
                    elif member.isfile():
                        source = archive.extractfile(member)
                        if source is None:
                            raise RuntimeError(f"unable to read archive member: {member.name}")
                        digest = hashlib.sha256()
                        with target.open("wb") as output:
                            while block := source.read(1 << 20):
                                output.write(block)
                                digest.update(block)
                        file_digest = digest.hexdigest()
                        os.chmod(target, member.mode & 0o777)
                    else:
                        raise RuntimeError(f"unsupported archive member: {member.name}")
                    selected_bytes += member.size
                    selected.append({"path": str(pathlib.PurePosixPath(*parts)), "bytes": member.size, "sha256": file_digest, "symlink": member.linkname if member.issym() else None})
            while stream.read(1 << 20):
                pass
        while compressed.read(1 << 20):
            pass
        actual = reader.digest.hexdigest()
        report = {"source": SOURCE_URL, "sha256": actual, "download_bytes": reader.count, "selected_bytes": selected_bytes, "selected": selected, "elapsed_seconds": round(time.monotonic() - started, 3)}
        if actual != EXPECTED_SHA256 or reader.count != EXPECTED_SIZE:
            partial.write_text(json.dumps(report, indent=2) + "\n")
            raise RuntimeError(f"LLVM SDK checksum/size mismatch: sha256={actual} bytes={reader.count}")
        for path, link in links:
            path.unlink(missing_ok=True)
            path.symlink_to(link)
        report["sha256"] = EXPECTED_SHA256
        marker.write_text(json.dumps(report, indent=2) + "\n")
        partial.unlink(missing_ok=True)
        return report
    except Exception:
        marker.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps(fetch(args.destination), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
