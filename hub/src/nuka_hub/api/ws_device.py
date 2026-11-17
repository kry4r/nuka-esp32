# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import contextlib
import hashlib
import json
import time
from collections import defaultdict
from dataclasses import dataclass, field

from fastapi import WebSocket, WebSocketDisconnect
from pydantic import ValidationError

from nuka_hub import __version__
from nuka_hub.core.agents import PermissionGone
from nuka_hub.core.clock import device_time
from nuka_hub.core.history import iso
from nuka_hub.core.models import DEVICE_MODELS, DeviceEnvelope, DeviceHello, Envelope
from nuka_hub.core.wire import contains_nul
from nuka_hub.vm.render import host, server, view

from .ws_frames import FrameError, receive_text, validation_code

TOPICS = ("hosts", "servers", "ai", "agents", "notify", "media", "weather", "pet")
LIMITS = {"hosts": 6, "servers": 12, "ai": 8, "agents": 6, "notify": 20, "media": 3}


@dataclass
class Connection:
    ws: WebSocket
    dev_id: str
    lang: str
    topics: set[str]
    screen: bool
    fw: str
    nukapack: str
    card: str = "clock"
    low_power: bool = False
    dnd: bool = False
    detail: dict | None = None
    dismissed: set[str] = field(default_factory=set)
    sent: dict[str, float] = field(default_factory=lambda: defaultdict(float))
    revisions: dict[str, int] = field(default_factory=lambda: defaultdict(int))
    changed: set[str] = field(default_factory=set)
    events: asyncio.Queue = field(default_factory=asyncio.Queue)
    force: bool = False
    battery_level: int = 0


class DeviceHub:
    def __init__(self, runtime):
        self.runtime = runtime
        self.connections: dict[str, Connection] = {}
        for topic in TOPICS:
            runtime.bus.subscribe(topic, self.changed)

    async def changed(self, topic, value):
        for connection in self.connections.values():
            connection.changed.add(topic)

    @staticmethod
    def frame(kind: str, data: dict, request_id: int | None = None):
        result = {"v": 1, "t": kind, "ts": int(time.time()), "d": data}
        if request_id is not None:
            result["id"] = request_id
        return result

    async def send(self, ws: WebSocket, kind: str, data: dict, request_id=None):
        message = self.frame(kind, data, request_id)
        if contains_nul(message):
            raise FrameError(1008)
        encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
        # Only list truncation is allowed: a missing sensor remains missing.
        key = "accounts" if kind == "vm.ai" else "sessions" if kind == "vm.agents" else "items"
        while len(encoded.encode("utf-8")) > 8192 and key in data and data[key]:
            data[key].pop()
            data["more"] = data.get("more", 0) + 1
            encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > 8192:
            raise ValueError(f"{kind} exceeds device frame limit")
        await ws.send_text(encoded)

    async def event(self, kind: str, data: dict, dev_ids: list[str] | None = None):
        for connection in self.connections.values():
            if dev_ids is not None and connection.dev_id not in dev_ids:
                continue
            if kind == "notify.push" and not connection.screen and data["level"] != "critical":
                continue
            if (
                kind == "notify.push"
                and (connection.dnd or self.runtime.notifications.quiet())
                and data["level"] != "critical"
            ):
                continue
            payload = (
                self.runtime.notifications.localized(data, connection.lang)
                if kind == "notify.push"
                else data
            )
            connection.events.put_nowait((kind, payload, None))

    def interval(self, topic: str, connection: Connection) -> float:
        overrides = self.runtime.config.devices.push_overrides
        if topic == "hosts":
            interval = (
                overrides.get("hosts_active", 1)
                if connection.card == "hosts"
                else overrides.get("hosts_inactive", 5)
            )
        elif topic == "servers":
            interval = (
                overrides.get("servers_active", 5)
                if connection.card == "servers"
                else overrides.get("servers_inactive", 15)
            )
        elif topic == "ai":
            interval = overrides.get("ai_min", 10)
        elif topic == "detail":
            interval = overrides.get("detail", 5)
        else:
            interval = 0
        return (
            max(interval, overrides.get("low_power_min", 5))
            if connection.low_power and topic in {"hosts", "servers", "detail"}
            else interval
        )

    async def snapshot(self, connection: Connection, topic: str):
        value = self.runtime.state.get(topic)
        if topic == "notify":
            value = [
                self.runtime.notifications.localized(item, connection.lang)
                for item in value
                if item["nid"] not in self.runtime.notifications.web_only
            ]
        data = view(topic, value, connection.lang, connection.dismissed)
        key = "accounts" if topic == "ai" else "sessions" if topic == "agents" else "items"
        if topic in LIMITS and len(data[key]) > LIMITS[topic]:
            data["more"] = len(data[key]) - LIMITS[topic]
            data[key] = data[key][: LIMITS[topic]]
        kind = "vm." + topic
        connection.revisions[kind] += 1
        data["rev"] = connection.revisions[kind]
        await self.send(connection.ws, kind, data)
        connection.sent[topic] = time.monotonic()
        connection.changed.discard(topic)

    async def detail(self, connection):
        topic, identity = connection.detail["topic"], connection.detail["id"]
        items = self.runtime.state.get(topic)
        item = next((item for item in items if item["id"] == identity), None)
        if item is None:
            return
        rendered = host(item) if topic == "hosts" else server(item)
        rows = []
        for key, value in rendered.items():
            if key in {"id", "name", "online"}:
                continue
            rows.append(
                {
                    "k": key,
                    "v_txt": json.dumps(value, ensure_ascii=False)
                    if isinstance(value, (dict, list))
                    else str(value),
                }
            )
        history = await self.runtime.history.query(
            topic, identity, ["cpu.pct", "gpu.pct", "mem.pct"], "24h"
        )
        spark = {
            key.split(".")[0]: values[-48:] for key, values in history["series"].items() if values
        }
        kind = "vm.host_detail" if topic == "hosts" else "vm.server_detail"
        connection.revisions[kind] += 1
        data = {
            "rev": connection.revisions[kind],
            "id": identity,
            "rows": rows,
            "spark": spark,
            "spark_span_txt": "24h",
        }
        await self.send(connection.ws, kind, data)
        connection.sent["detail"] = time.monotonic()

    async def push(self, connection: Connection):
        while True:
            while not connection.events.empty():
                kind, data, request_id = connection.events.get_nowait()
                await self.send(connection.ws, kind, data, request_id)
            if connection.screen or connection.force:
                now = time.monotonic()
                for topic in sorted(connection.topics):
                    periodic = topic in {"hosts", "servers"}
                    if connection.force or (
                        (periodic or topic in connection.changed)
                        and now - connection.sent[topic] >= self.interval(topic, connection)
                    ):
                        await self.snapshot(connection, topic)
                if connection.detail and now - connection.sent["detail"] >= self.interval(
                    "detail", connection
                ):
                    await self.detail(connection)
                connection.force = False
            if self.runtime.demo and self.runtime.demo_state == "offline":
                await connection.ws.close(1001)
                return
            await asyncio.sleep(0.05)

    async def handle(self, connection: Connection, frame: Envelope):
        kind, data = frame.t, frame.d
        response = None
        if kind == "sub":
            connection.topics = set(data["topics"])
            connection.force = True
        elif kind == "state":
            connection.force = data["screen"] and not connection.screen
            connection.screen, connection.card = data["screen"], data["card"]
            connection.low_power = data.get("low_power", False)
            connection.dnd = data.get("dnd", False)
            await self.runtime.history.sql(
                "UPDATE devices SET last_seen=?,last_state_json=? WHERE dev_id=?",
                (iso(), json.dumps(data), connection.dev_id),
            )
            bat = data["bat"]
            level = (
                (2 if bat["pct"] < 10 else 1 if bat["pct"] < 20 else 0)
                if (bat["src"] == "bat" and bat["chg"] == "none")
                else 0
            )
            if level > connection.battery_level:
                await self.runtime.notify(
                    {
                        "source": "device_battery",
                        "level": "critical" if level == 2 else "warn",
                        "title": f"{connection.dev_id}：设备电量 {bat['pct']:.0f}%",
                        "dedupe_key": "battery:" + connection.dev_id,
                    },
                    web_only=True,
                )
            connection.battery_level = level
            await self.runtime.history.sql(
                "INSERT INTO device_battery VALUES (?,?,?,?,?,?)",
                (connection.dev_id, time.time(), bat["pct"], bat["mv"], bat["chg"], bat["src"]),
            )
            await self.registry()
        elif kind == "sub.detail":
            connection.detail = data if data["topic"] is not None else None
            connection.sent["detail"] = 0
        elif kind == "perm.decide":
            try:
                self.runtime.agents.decide(data["rid"], data["behavior"], "device")
                response = ("ok", {})
            except PermissionGone:
                response = ("err", {"code": "perm_gone", "msg": "Permission request expired"})
        elif kind == "ai.refresh":
            now = time.monotonic()
            if now - self.runtime.last_ai_refresh < 60:
                response = ("err", {"code": "rate_limited", "msg": "One refresh per minute"})
            else:
                self.runtime.last_ai_refresh = now
                for plugin in self.runtime.ai_plugins:
                    self.runtime.scheduler.request(plugin, "refresh")
                response = ("ok", {})
        elif kind == "macro.get":
            response = (
                "macro.set",
                {
                    "rev": self.runtime.macros_rev,
                    "macros": [m.model_dump() for m in self.runtime.config.devices.macro],
                },
            )
        elif kind == "ota.check":
            from nuka_hub.ota import offer

            connection.events.put_nowait(("ok", {}, frame.id))
            for item in self.runtime.ota_manifests():
                current = connection.fw if item["kind"] == "app" else connection.nukapack
                if item["ver"] != current:
                    await offer(self.runtime, item, [connection.dev_id])
        elif kind == "notify.dismiss":
            if data.get("all"):
                connection.dismissed.update(
                    item["nid"] for item in self.runtime.state.get("notify")
                )
            else:
                connection.dismissed.add(data["nid"])
            connection.changed.add("notify")
        elif kind == "pomo":
            self.runtime.pomo[connection.dev_id] = data
            if data["state"] == "done":
                self.runtime.celebrate_until = int(time.time()) + 3
            await self.runtime.pet()
        elif kind in {"ota.result", "ota.progress"}:
            await self.runtime.history.audit("ota", connection.dev_id, data)
        elif kind == "media.cmd":
            await self.runtime.media_command(data["src_id"], data["cmd"])
        if response:
            connection.events.put_nowait((*response, frame.id))

    async def registry(self):
        rows = await self.runtime.history.sql("SELECT * FROM devices")
        devices = []
        for row in rows:
            item = {
                key: row[key]
                for key in ("dev_id", "name", "fw", "last_seen")
                if row[key] is not None
            }
            item["online"] = row["dev_id"] in self.connections
            if item["online"]:
                item["ip"] = self.connections[row["dev_id"]].ws.client.host
            if row["last_state_json"]:
                item["state"] = json.loads(row["last_state_json"])
            devices.append(item)
        await self.runtime.set("devices", devices)

    async def connect(self, ws: WebSocket):
        await ws.accept(
            subprotocol="nuka.v1" if "nuka.v1" in ws.scope.get("subprotocols", []) else None
        )
        authorization = ws.headers.get("authorization", "")
        token = authorization.removeprefix("Bearer ")
        authenticated = await self.runtime.tokens.authenticate(token, "device")
        if authenticated is None:
            if "x-nuka-pair" in ws.headers:
                await self.pair(ws)
                return
            rows = await self.runtime.history.sql(
                "SELECT revoked FROM tokens WHERE sha256=?",
                (hashlib.sha256(token.encode()).hexdigest(),),
            )
            await ws.close(4003 if rows and rows[0]["revoked"] else 4001)
            return
        connection = None
        sender = None
        receiver = None
        try:
            raw = await receive_text(ws, 8192)
            frame = DeviceEnvelope.model_validate_json(raw)
            hello = DeviceHello.model_validate(frame.d)
            if frame.t != "hello" or frame.id is None:
                await ws.close(1008)
                return
            dev_id = hello.dev_id
            old = self.connections.get(dev_id)
            if old:
                with contextlib.suppress(WebSocketDisconnect):
                    await old.ws.close(4008)
            connection = Connection(
                ws, dev_id, hello.lang, set(hello.topics), hello.screen, hello.fw, hello.nukapack
            )
            self.connections[dev_id] = connection
            rows = await self.runtime.history.sql(
                "SELECT name FROM devices WHERE dev_id=?", (dev_id,)
            )
            name = rows[0]["name"] if rows else dev_id
            await self.runtime.history.sql(
                (
                    "INSERT INTO devices VALUES (?,?,?,?,?,?) ON CONFLICT(dev_id) "
                    "DO UPDATE SET fw=excluded.fw,last_seen=excluded.last_seen,"
                    "token_id=excluded.token_id"
                ),
                (dev_id, name, hello.fw, iso(), None, authenticated["id"]),
            )
            await self.registry()
            welcome = {
                "hub_id": self.runtime.hub_id,
                "hub_name": self.runtime.config.hub.name,
                "hub_ver": __version__,
                "proto": 1,
                "dev_name": name,
                "lang": connection.lang,
                "time": device_time(self.runtime.tz_posix),
                "features": self.runtime.features(),
                "macros_rev": self.runtime.macros_rev,
            }
            await self.send(ws, "welcome", welcome, frame.id)
            if self.runtime.demo and self.runtime.demo_state == "loading":
                await asyncio.sleep(3)
            for topic in sorted(connection.topics):
                await self.snapshot(connection, topic)
            cfg = self.runtime.config.devices.model_dump(
                exclude={"macro", "push_overrides", "default_lang"}, exclude_none=True
            )
            cfg["lang"] = self.runtime.config.devices.default_lang or self.runtime.config.hub.lang
            await self.send(ws, "cfg", cfg)
            sender = asyncio.create_task(self.push(connection))
            while True:
                receiver = asyncio.create_task(receive_text(ws, 8192))
                await asyncio.wait([receiver, sender], return_when=asyncio.FIRST_COMPLETED)
                if sender.done():
                    await sender
                    return
                raw = await receiver
                frame = DeviceEnvelope.model_validate_json(raw)
                if frame.t not in DEVICE_MODELS:
                    continue
                frame.d = DEVICE_MODELS[frame.t].model_validate(frame.d).wire()
                if (
                    frame.t in {"perm.decide", "ai.refresh", "macro.get", "ota.check"}
                    and frame.id is None
                ):
                    await ws.close(1008)
                    return
                await self.handle(connection, frame)
        except ValidationError as error:
            await ws.close(validation_code(error))
        except FrameError as error:
            await ws.close(error.code)
        except WebSocketDisconnect:
            pass
        finally:
            if receiver and not receiver.done():
                receiver.cancel()
                with contextlib.suppress(asyncio.CancelledError, WebSocketDisconnect):
                    await receiver
            if sender:
                sender.cancel()
                with contextlib.suppress(asyncio.CancelledError, WebSocketDisconnect):
                    await sender
            if connection and self.connections.get(connection.dev_id) is connection:
                del self.connections[connection.dev_id]
                await self.registry()

    async def pair(self, ws: WebSocket):
        parts = ws.headers["x-nuka-pair"].split(":")
        import re

        if (
            len(parts) != 2
            or not re.fullmatch(r"[a-z0-9_-]{1,32}", parts[0])
            or not re.fullmatch(r"\d{6}", parts[1])
        ):
            await ws.close(4004)
            return
        try:
            raw = await receive_text(ws, 8192)
            frame = DeviceEnvelope.model_validate_json(raw)
            hello = DeviceHello.model_validate(frame.d)
        except ValidationError as error:
            await ws.close(validation_code(error))
            return
        except FrameError as error:
            await ws.close(error.code)
            return
        except WebSocketDisconnect:
            return
        if frame.t != "hello" or frame.id is None:
            await ws.close(1008)
            return
        if hello.dev_id != parts[0]:
            await ws.close(4004)
            return
        item = {
            "ws": ws,
            "code": parts[1],
            "expires": time.time() + 300,
            "attempts": 0,
            "rejected": False,
            "fw": hello.fw,
            "done": asyncio.Event(),
        }
        self.runtime.pending_pairs[hello.dev_id] = item
        await self.send(ws, "pair.wait", {})
        waiter = asyncio.create_task(item["done"].wait())
        receiver = asyncio.create_task(receive_text(ws, 8192))
        try:
            async with asyncio.timeout(300):
                await asyncio.wait([waiter, receiver], return_when=asyncio.FIRST_COMPLETED)
                if receiver.done():
                    await receiver
                    await ws.close(1008)
                else:
                    await ws.close(4004 if item["rejected"] else 1000)
        except TimeoutError:
            await ws.close(4004)
        except FrameError as error:
            await ws.close(error.code)
        except WebSocketDisconnect:
            pass
        finally:
            for task in (waiter, receiver):
                if not task.done():
                    task.cancel()
                    with contextlib.suppress(asyncio.CancelledError):
                        await task
            # A newer pairing attempt for the same device may have replaced this entry.
            if self.runtime.pending_pairs.get(hello.dev_id) is item:
                del self.runtime.pending_pairs[hello.dev_id]
