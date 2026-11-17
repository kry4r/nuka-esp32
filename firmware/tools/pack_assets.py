# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build ABI-1 nukapack, constrained to one 3 MiB bank."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ABI = 1
BANK_SIZE = 3 * 1024 * 1024
HEADER = struct.Struct("<4sHHII32s")
ENTRY = struct.Struct("<IIII")


def name_hash(name: str) -> int:
    value = 2166136261
    for byte in name.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest", type=Path, default=ROOT / "build/assets/assets.json"
    )
    parser.add_argument("--output", type=Path, default=ROOT / "build/nukapack.bin")
    parser.add_argument("--version", default="0.1.0")
    args = parser.parse_args()
    if not args.version or any(not 32 <= ord(char) <= 126 for char in args.version):
        parser.error("version must be nonempty printable ASCII")
    source = json.loads(args.manifest.read_text(encoding="utf-8"))
    assets = [
        dict(asset, data=(args.manifest.parent / asset["file"]).read_bytes())
        for asset in source
    ]
    assets.append({"name": "version", "type": 0, "data": args.version.encode() + b"\0"})
    hashes = [name_hash(asset["name"]) for asset in assets]
    if len(hashes) != len(set(hashes)):
        raise SystemExit("Resource names have a hash collision")
    offset = HEADER.size + ENTRY.size * len(assets)
    index = bytearray()
    payload = bytearray()
    for asset, hashed in zip(assets, hashes, strict=True):
        aligned = (offset + 3) & ~3
        payload.extend(b"\0" * (aligned - offset))
        data = asset["data"]
        index.extend(ENTRY.pack(hashed, aligned, len(data), asset["type"]))
        payload.extend(data)
        offset = aligned + len(data)
    body = index + payload
    data = (
        HEADER.pack(
            b"NUKA",
            ABI,
            len(assets),
            HEADER.size,
            offset,
            hashlib.sha256(body).digest(),
        )
        + body
    )
    if len(data) > BANK_SIZE:
        raise SystemExit(f"Resource pack {len(data)} exceeds bank capacity {BANK_SIZE}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    metadata = {
        "version": args.version,
        "abi": ABI,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(
        f"nukapack ABI {ABI}: {len(data)}/{BANK_SIZE} bytes, SHA-256 {metadata['sha256']}"
    )


if __name__ == "__main__":
    main()
