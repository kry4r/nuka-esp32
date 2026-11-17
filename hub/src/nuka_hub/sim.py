# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
import sys
import time

from websockets.asyncio.client import connect


async def simulate(config, args, number: int):
    url = args.hub or f"ws://localhost:{config.hub.port}/ws/device"
    token = args.token or input("Device token: ").strip()
    async with connect(
        url,
        additional_headers={"Authorization": "Bearer " + token},
        subprotocols=["nuka.v1"],
        max_size=8192,
    ) as ws:

        def frame(kind, data, identity=None):
            result = {"v": 1, "t": kind, "ts": int(time.time()), "d": data}
            if identity is not None:
                result["id"] = identity
            return json.dumps(result, ensure_ascii=False)

        await ws.send(
            frame(
                "hello",
                {
                    "dev_id": "sim_" + str(number),
                    "fw": "0.1.0",
                    "nukapack": "0.1.0",
                    "proto": 1,
                    "lang": config.hub.lang,
                    "screen": True,
                    "caps": ["hid", "vault"],
                    "topics": [
                        "hosts",
                        "servers",
                        "ai",
                        "agents",
                        "notify",
                        "media",
                        "weather",
                        "pet",
                    ],
                    "bat": {"pct": 80, "mv": 3900, "chg": "none", "src": "bat"},
                },
                1,
            )
        )
        start = time.monotonic()
        request_id = 2
        details_seen = set()
        server_detail = None
        while args.duration is None or time.monotonic() - start < args.duration:
            remaining = args.duration - (time.monotonic() - start) if args.duration else None
            try:
                async with asyncio.timeout(remaining):
                    raw = await ws.recv()
            except TimeoutError:
                return
            print(raw, flush=True)
            message = json.loads(raw)
            if message["t"] == "welcome":
                await ws.send(
                    frame(
                        "state",
                        {
                            "screen": True,
                            "dnd": False,
                            "card": "hosts",
                            "bat": {"pct": 80, "mv": 3900, "chg": "none", "src": "bat"},
                        },
                    )
                )
            elif message["t"] in {"vm.hosts", "vm.servers"}:
                topic = message["t"].removeprefix("vm.")
                if topic == "servers" and message["d"]["items"]:
                    server_detail = message["d"]["items"][0]["id"]
                if topic == "hosts" and topic not in details_seen and message["d"]["items"]:
                    details_seen.add(topic)
                    await ws.send(
                        frame("sub.detail", {"topic": topic, "id": message["d"]["items"][0]["id"]})
                    )
            elif (
                message["t"] == "vm.host_detail" and server_detail and "servers" not in details_seen
            ):
                details_seen.add("servers")
                await ws.send(frame("sub.detail", {"topic": "servers", "id": server_detail}))
            elif message["t"] == "perm.req":
                behavior = args.approve
                if behavior is None and sys.stdin.isatty():
                    behavior = await asyncio.to_thread(input, "Permission (allow/deny/skip): ")
                if behavior in {"allow", "deny"}:
                    await ws.send(
                        frame(
                            "perm.decide",
                            {"rid": message["d"]["rid"], "behavior": behavior},
                            request_id,
                        )
                    )
                    request_id += 1


async def run_simulators(config, args):
    await asyncio.gather(*(simulate(config, args, number) for number in range(args.count)))
