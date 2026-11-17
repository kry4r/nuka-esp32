# SPDX-License-Identifier: AGPL-3.0-or-later
import hashlib
import json
import shutil

import httpx


async def offer(runtime, item: dict, dev_ids: list[str]):
    origin = runtime.config.hub.public_url.rstrip("/")
    payload = {k: v for k, v in item.items() if k != "file"}
    payload["url"] = origin + "/api/v1/ota/" + item["file"]
    await runtime.devices.event("ota.offer", payload, dev_ids)


async def publish(runtime, args):
    import re

    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.-]+)?", args.version) is None:
        raise ValueError("OTA version must be a semantic version")
    limit = (4 if args.kind == "app" else 3) * 1024 * 1024
    size = args.file.stat().st_size
    if size > limit:
        raise ValueError(f"Image exceeds the {limit // 1048576} MiB {args.kind} partition limit")
    prefix = "nuka-esp32" if args.kind == "app" else "nukapack"
    name = prefix + "-" + args.version + ".bin"
    folder = runtime.ota_dir / args.version
    folder.mkdir(parents=True, exist_ok=True)
    destination = folder / name
    shutil.copyfile(args.file, destination)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    manifest = {
        "ver": args.version,
        "kind": args.kind,
        "file": name,
        "size": size,
        "sha256": digest,
    }
    if args.notes:
        manifest["notes_txt"] = args.notes
    path = folder / "manifest.json"
    items = (
        [item for item in json.loads(path.read_text(encoding="utf-8")) if item["kind"] != args.kind]
        if path.exists()
        else []
    )
    path.write_text(json.dumps([*items, manifest], indent=2), encoding="utf-8")
    await runtime.history.audit("ota", "cli", {"ver": args.version, "kind": args.kind})
    token = await runtime.tokens.create("admin", "ota publish")
    headers = {"Authorization": "Bearer " + token["token"]}
    origin = f"http://localhost:{runtime.config.hub.port}/api/v1"
    try:
        response = await runtime.client.get(origin + "/devices", headers=headers, timeout=3)
        response.raise_for_status()
        devices = [item["dev_id"] for item in response.json()["items"] if item["online"]]
        if devices:
            response = await runtime.client.post(
                origin + "/ota/offer",
                headers=headers,
                json={"ver": args.version, "kind": args.kind, "dev_ids": devices},
                timeout=3,
            )
            response.raise_for_status()
        print(f"OTA 已发布，已通知 {len(devices)} 台在线设备。")
    except httpx.RequestError:
        print("OTA 已发布；Hub 未运行，启动后可在管理页发送更新。")
    finally:
        await runtime.tokens.revoke(token["id"])
