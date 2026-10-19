#!/usr/bin/env python3
"""Create the CI provenance package for the exact native runtime object/archive."""
from __future__ import annotations
import argparse, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path
import struct


def record(path: Path) -> dict:
    data = path.read_bytes()
    st = path.stat()
    return {"path": str(path.resolve()), "bytes": st.st_size,
            "sha256": hashlib.sha256(data).hexdigest(), "mtime_ns": st.st_mtime_ns}


def run(argv: list[str]) -> dict:
    try:
        p = subprocess.run(argv, text=True, capture_output=True, check=False)
        return {"argv": [str(x) for x in argv], "status": p.returncode,
                "stdout": p.stdout, "stderr": p.stderr}
    except OSError as exc:
        return {"argv": [str(x) for x in argv], "status": None,
                "stdout": "", "stderr": str(exc)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--object", type=Path, required=True)
    ap.add_argument("--archive", type=Path, required=True)
    ap.add_argument("--package", type=Path, required=True)
    ap.add_argument("--compiler", type=Path, required=True)
    ap.add_argument("--profile", required=True)
    ap.add_argument("--object-format", required=True)
    ap.add_argument("--flag", dest="flags", action="append", default=[])
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--ar", default="ar")
    ap.add_argument("--readobj", default="")
    ap.add_argument("--license", type=Path)
    args = ap.parse_args()
    for p in (args.source, args.object, args.archive):
        if not p.is_file():
            raise SystemExit(f"missing runtime input: {p}")
    package = args.package
    package.mkdir(parents=True, exist_ok=True)
    copied = {}
    for src, name in ((args.object, args.object.name), (args.archive, args.archive.name)):
        dst = package / name
        shutil.copy2(src, dst)
        copied[name] = record(dst)
    if args.license and args.license.is_file():
        dst = package / "LICENSE"
        shutil.copy2(args.license, dst)
        copied["LICENSE"] = record(dst)

    compiler = str(args.compiler)
    tools = {
        "version": run([compiler, "--version"]),
        "dumpmachine": run([compiler, "-dumpmachine"]),
        "ar_members": run([args.ar, "t", str(args.archive)]),
        "nm": run([args.nm, "-g", str(args.object)]),
    }
    if args.readobj:
        tools["readobj"] = run([args.readobj, "--file-headers", str(args.object)])
    else:
        tools["readelf"] = run(["readelf", "-h", "-S", str(args.object)])
    inspection = tools.get("readobj", tools.get("readelf"))
    inspection_text = inspection["stdout"] + inspection["stderr"]
    format_token = "COFF" if args.object_format.startswith("COFF") else "ELF"
    for name, result in tools.items():
        if result['status'] != 0:
            raise SystemExit(f"runtime preparation audit failed: {name}: {result}")
    data = args.object.read_bytes()
    if format_token == "ELF":
        valid = data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<HH', data, 16) == (1, 62)
    else:
        valid = len(data) >= 20 and struct.unpack_from('<H', data)[0] == 0x8664 and struct.unpack_from('<H', data, 16)[0] == 0
    if not valid or format_token not in inspection_text.upper():
        raise SystemExit("runtime object is not relocatable native x64 in the selected format")
    if 'claw.runtime.println.i32' not in tools['nm']['stdout']:
        raise SystemExit("runtime object lacks the audited Int32 ABI symbol")
    manifest = {
        "format": "claw-runtime-provenance-1",
        "targetprofile": args.profile, "profile": args.profile,
        "object_format": args.object_format,
        "source_abi": "void(i32) claw.runtime.println.i32; source hash is the ABI identity",
        "source": record(args.source), "object": record(args.object),
        "archive": record(args.archive), "package": copied,
        "compiler": {"path": str(args.compiler.resolve()), "version": tools["version"],
                      "dumpmachine": tools["dumpmachine"]},
        "cflags": args.flags,
        "symbolaudit": {"archive_members": tools["ar_members"], "nm": tools["nm"]},
        "archive_members": tools["ar_members"], "symbols": tools["nm"],
        "object_inspection": inspection,
        "objectformat_verified": inspection["status"] == 0 and format_token in inspection_text.upper(),
    }
    (package / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"manifest": str((package / 'runtime-manifest.json').resolve()),
                      "archive": str(args.archive.resolve()), "object": str(args.object.resolve())}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"manifest generation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
