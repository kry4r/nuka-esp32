# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
import socket
import subprocess
import sys
from pathlib import Path

import uvicorn
from websockets.asyncio.client import connect

from nuka_hub.api.app import create_app
from nuka_hub.core.config import Config
from nuka_hub.core.runtime import Runtime

ROOT = Path(__file__).resolve().parents[2]


async def capture_demo(tmp_path):
    config = Config()
    config.hub.data_dir = str(tmp_path)
    config.hub.timezone = "Etc/UTC"
    config.mdns.enabled = False
    runtime = Runtime(config, tmp_path / "config.toml", True)
    token = await runtime.tokens.create("device", "T6 fixture")
    listener = socket.socket()
    listener.bind(("localhost", 0))
    listener.listen()
    port = listener.getsockname()[1]
    server = uvicorn.Server(
        uvicorn.Config(
            create_app(runtime), log_level="error", proxy_headers=False, access_log=False
        )
    )
    task = asyncio.create_task(server.serve(sockets=[listener]))
    while not server.started:
        await asyncio.sleep(0.01)
    frames = []
    seen = set()
    hello = json.loads((ROOT / "protocol/examples/device/hello.json").read_text(encoding="utf-8"))
    ota_check = json.dumps(
        json.loads((ROOT / "protocol/examples/device/ota.check.json").read_text(encoding="utf-8"))
    )
    try:
        async with connect(
            f"ws://localhost:{port}/ws/device",
            additional_headers={"Authorization": "Bearer " + token["token"]},
            subprotocols=["nuka.v1"],
        ) as ws:
            await ws.send(json.dumps(hello))
            hosts, servers = None, None
            async with asyncio.timeout(60):
                async for raw in ws:
                    frames.append(raw)
                    frame = json.loads(raw)
                    seen.add(frame["t"])
                    if frame["t"] == "welcome":
                        frames.append(ota_check)
                        await ws.send(ota_check)
                    elif frame["t"] == "vm.hosts" and frame["d"]["items"]:
                        hosts = frame["d"]["items"][0]["id"]
                        if "vm.host_detail" not in seen:
                            await ws.send(
                                json.dumps(
                                    {
                                        "v": 1,
                                        "t": "sub.detail",
                                        "ts": 0,
                                        "d": {"topic": "hosts", "id": hosts},
                                    }
                                )
                            )
                    elif frame["t"] == "vm.servers" and frame["d"]["items"]:
                        servers = frame["d"]["items"][0]["id"]
                    elif frame["t"] == "vm.host_detail" and servers:
                        await ws.send(
                            json.dumps(
                                {
                                    "v": 1,
                                    "t": "sub.detail",
                                    "ts": 0,
                                    "d": {"topic": "servers", "id": servers},
                                }
                            )
                        )
                    elif frame["t"] == "perm.req":
                        await ws.send(
                            json.dumps(
                                {
                                    "v": 1,
                                    "t": "perm.decide",
                                    "id": 2,
                                    "ts": 0,
                                    "d": {"rid": frame["d"]["rid"], "behavior": "deny"},
                                }
                            )
                        )
    except TimeoutError:
        pass
    finally:
        server.should_exit = True
        await task
    path = tmp_path / "demo-frames.jsonl"
    path.write_text("\n".join(frames) + "\n", encoding="utf-8")
    return path, seen


async def test_t6_demo_sixty_seconds(tmp_path):
    path, seen = await capture_demo(tmp_path)
    assert {
        "vm." + topic
        for topic in (
            "hosts",
            "host_detail",
            "servers",
            "server_detail",
            "ai",
            "agents",
            "notify",
            "media",
            "weather",
            "pet",
        )
    } <= seen
    request_id = json.loads(
        (ROOT / "protocol/examples/device/ota.check.json").read_text(encoding="utf-8")
    )["id"]
    assert any(
        frame["t"] == "ok" and frame.get("id") == request_id
        for frame in map(json.loads, path.read_text(encoding="utf-8").splitlines())
    )
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/check_protocol.py"),
            str(path),
            "--transport",
            "device",
        ],
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr
