# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import contextlib
import copy
import hashlib
import json
import logging
import random
import secrets
import time
from datetime import datetime
from importlib.resources import files
from pathlib import Path
from zoneinfo import ZoneInfo

import httpx

from .bus import Bus
from .clock import device_time, posix_timezone, timezone_name
from .config import Config, ConfigError, data_path, load_config
from .history import History, iso
from .scheduler import Scheduler
from .secrets import Secrets
from .state import StateStore
from .tokens import Tokens


def identity(folder: Path, filename: str, prefix: str) -> str:
    path = folder / filename
    if path.exists():
        return path.read_text(encoding="ascii").strip()
    value = prefix + secrets.token_hex(8)
    path.write_text(value, encoding="ascii")
    return value


class Runtime:
    def __init__(self, config: Config, path: Path, demo: bool = False, demo_state: str = "normal"):
        self.config, self.path, self.demo, self.demo_state = config, path, demo, demo_state
        self.data = data_path(config)
        self.data.mkdir(parents=True, exist_ok=True)
        self.hub_id = identity(self.data, "hub-id", "hub_")
        self.host_id = identity(self.data, "host-id", "h_")
        self.tz_posix = posix_timezone(config.hub.timezone)
        self.timezone = ZoneInfo(timezone_name(config.hub.timezone))
        self.history = History(self.data / "nuka.sqlite3")
        self.tokens = Tokens(self.history)
        self.secrets = Secrets(path.parent)
        self.bus, self.state = Bus(), StateStore()
        self.scheduler = Scheduler(self.sample, self.plugin_health)
        self.client = httpx.AsyncClient(timeout=10, follow_redirects=False)
        self.tasks: list[asyncio.Task] = []
        self.devices = None
        self.collector_values = {}
        self.server_values = {}
        self.ai_values = {}
        self.ai_plugins = []
        self.agent_connections = {}
        self.media_adapter = None
        self.ota_dir = Path(config.ota.dir) if config.ota.dir else self.data / "ota"
        self.ota_dir.mkdir(parents=True, exist_ok=True)
        self.last_ai_refresh = 0.0
        self.macros_rev = int(
            hashlib.sha256(
                json.dumps([m.model_dump() for m in config.devices.macro], sort_keys=True).encode()
            ).hexdigest()[:7],
            16,
        )
        self.pomo: dict[str, dict] = {}
        self.pending_pairs: dict[str, dict] = {}
        self.cover_cache: dict[str, bytes] = {}
        self.celebrate_until = 0
        self.token_milestone = None
        self.media_ids: set[str] = set()
        self.media_activity: dict[str, float] = {}
        self.offered: set[tuple[str, str, str]] = set()
        self.mtime_ns = path.stat().st_mtime_ns if path.exists() else 0
        self.start_time = time.time()
        self.config_lock = asyncio.Lock()
        from nuka_hub.assistant.service import AssistantService

        self.assistant = AssistantService(self)

    def features(self) -> dict:
        if self.demo:
            return {
                name: self.demo_state != "unconfigured"
                for name in ("ai", "servers", "agents", "media", "weather")
            }
        return {
            "ai": bool(self.config.ai.provider),
            "servers": bool(self.config.servers.source),
            "agents": bool(self.config.agents.enabled_sources),
            "media": self.config.media.enabled,
            "weather": self.config.weather.enabled,
        }

    async def set(self, topic: str, value):
        if topic == "media":
            previous = {item["src_id"]: item for item in self.state.get("media")}
            now = time.monotonic()
            for item in value:
                source = item["src_id"]
                old = previous.get(source)
                if old is None or any(
                    item.get(key) != old.get(key)
                    for key in ("state", "title", "artist", "album", "cover", "dur_s")
                ):
                    self.media_activity[source] = now
            self.media_activity = {
                item["src_id"]: self.media_activity[item["src_id"]] for item in value
            }
            value = sorted(
                value, key=lambda item: self.media_activity[item["src_id"]], reverse=True
            )
        if self.state.set(topic, value):
            await self.bus.publish(topic, value)

    async def plugin_health(self):
        await self.set("plugins", self.scheduler.listing())

    async def sample(self, plugin, value):
        if plugin.kind == "collector":
            self.collector_values[plugin.name] = value
            result = {
                "id": self.host_id,
                "name": self.config.collectors.host_name or self.config.hub.name,
                "online": True,
                "role": "hub",
                "last_seen": iso(),
            }
            for name in self.config.collectors.enabled:
                for key, field in self.collector_values.get(name, {}).items():
                    if isinstance(field, dict):
                        target = result.setdefault(key, {})
                        for subkey, subvalue in field.items():
                            priority = self.config.collectors.priority.get(key + "." + subkey, [])
                            winner = next(
                                (
                                    source
                                    for source in priority
                                    if subkey in self.collector_values.get(source, {}).get(key, {})
                                ),
                                name,
                            )
                            if winner == name:
                                target[subkey] = subvalue
                            elif subkey not in target:
                                target[subkey] = self.collector_values[winner][key][subkey]
                    elif key not in result:
                        priority = self.config.collectors.priority.get(key, [])
                        winner = next(
                            (
                                source
                                for source in priority
                                if key in self.collector_values.get(source, {})
                            ),
                            name,
                        )
                        result[key] = self.collector_values[winner][key]
            hosts = [host for host in self.state.get("hosts") if host["id"] != self.host_id]
            await self.set("hosts", [result, *hosts])
            await self.history.metrics(result)
        elif plugin.kind == "server":
            from nuka_hub.plugins.servers import merge_servers

            self.server_values[plugin.name] = value
            merged = merge_servers(list(self.server_values.values()), self.config.servers.merge)
            await self.set("servers", self.notifications.badges(merged))
            for server in value:
                await self.history.server(server)
        elif plugin.kind == "ai":
            self.ai_values[plugin.name] = value
            combined = {"accounts": [], "providers": []}
            for item in self.ai_values.values():
                combined["accounts"].extend(item["accounts"])
                combined["providers"].extend(item["providers"])
                if "summary" in item:
                    summary = combined.setdefault(
                        "summary", {"requests": 0, "tokens": 0, "failures": 0}
                    )
                    for key in summary:
                        summary[key] += item["summary"][key]
            await self.set("ai", combined)
            for account in value["accounts"]:
                for window in account["windows"]:
                    await self.history.sql(
                        "INSERT INTO ai_windows VALUES (?,?,?,?,?,?)",
                        (
                            account["id"],
                            time.time(),
                            window["kind"],
                            window["used_pct"],
                            window.get("reset_at"),
                            window["source"],
                        ),
                    )
            total = combined.get("summary", {}).get("tokens", 0) // 50000
            if self.token_milestone is not None and total > self.token_milestone:
                self.celebrate_until = int(time.time()) + 3
            self.token_milestone = total
        elif plugin.kind == "media":
            await self.set(
                "media",
                [item for item in self.state.get("media") if item["src_id"] not in self.media_ids]
                + value,
            )
            self.media_ids = {item["src_id"] for item in value}
        elif plugin.kind == "weather":
            await self.set(plugin.kind, value)
        if plugin.kind in {"collector", "server", "ai"} and not self.demo:
            scope = {"collector": "hosts", "server": "servers", "ai": "ai"}[plugin.kind]
            items = self.state.get(scope)
            await self.alerts.evaluate(
                scope, items["accounts"] if scope == "ai" else items, time.time()
            )
        await self.pet()

    async def pet(self):
        agents, ai = self.state.get("agents"), self.state.get("ai")
        # SPEC 6.8.6: attention > hungry > celebrate > busy > idle > sleep;
        # charging and dizzy are device-local overlays.
        mood = "idle"
        if agents["pending"]:
            mood = "attention"
        elif any(
            a["status"] == "exhausted" or any(w["used_pct"] > 90 for w in a["windows"])
            for a in ai["accounts"]
        ):
            mood = "hungry"
        elif time.time() < self.celebrate_until:
            await self.set("pet", {"mood": "celebrate", "until": self.celebrate_until})
            return
        elif any(s["state"] in {"thinking", "tool"} for s in agents["sessions"]) or any(
            p["state"] == "running" and p["phase"] == "focus" for p in self.pomo.values()
        ):
            mood = "busy"
        elif not agents["sessions"] or datetime.now(self.timezone).hour < 6:
            mood = "sleep"
        await self.set("pet", {"mood": mood})

    async def demo_tick(self):
        seed = json.loads(files("nuka_hub.data").joinpath("demo.json").read_text(encoding="utf-8"))
        for topic in ("hosts", "servers", "ai", "agents", "notify", "media", "weather", "plugins"):
            value = copy.deepcopy(seed[topic])
            if self.demo_state == "unconfigured":
                value = (
                    {"accounts": [], "providers": []}
                    if topic == "ai"
                    else {"sessions": [], "pending": []}
                    if topic == "agents"
                    else {}
                    if topic == "weather"
                    else []
                )
            await self.set(topic, value)
        await self.pet()
        step = 0
        while True:
            await asyncio.sleep(1)
            if self.demo_state == "unconfigured":
                continue
            step += 1
            hosts = copy.deepcopy(self.state.get("hosts"))
            for host in hosts:
                host["cpu"]["pct"] = round(
                    min(100, max(0, host["cpu"]["pct"] + random.uniform(-4, 4))), 1
                )
                host["last_seen"] = iso()
                host["online"] = self.demo_state != "offline"
            await self.set("hosts", hosts)
            if step % 10 == 0:
                ai = copy.deepcopy(self.state.get("ai"))
                for account in ai["accounts"]:
                    for window in account["windows"]:
                        window["used_pct"] = round(
                            min(100, window["used_pct"] + random.uniform(0, 1)), 1
                        )
                await self.set("ai", ai)
            if step % 20 == 0:
                await self.notify(
                    {
                        "title": "演示告警",
                        "body": "仅使用合成演示数据",
                        "level": "warn",
                        "source": "demo",
                    }
                )
            if step % 30 == 0:
                self.tasks.append(
                    asyncio.create_task(
                        self.agents.permission(
                            {
                                "session_id": "demo",
                                "tool_name": "Bash",
                                "tool_input": {"command": "echo example"},
                                "cwd": "example",
                            }
                        )
                    )
                )
            await self.pet()

    async def maintenance(self):
        elapsed = 0
        while True:
            await asyncio.sleep(1)
            elapsed += 1
            await self.agents.expire()
            if elapsed % 30 == 0:
                await self.set("notify", await self.notifications.listing())
                await self.set("servers", self.notifications.badges(self.state.get("servers")))
            if self.config.ota.auto_offer and self.devices:
                from nuka_hub.ota import offer

                for item in self.ota_manifests():
                    for dev_id in self.devices.connections:
                        key = (dev_id, item["kind"], item["sha256"])
                        if key not in self.offered:
                            await offer(self, item, [dev_id])
                            self.offered.add(key)
            if elapsed % 3600 == 0:
                await self.history.cleanup(self.config.store)
            if elapsed % 21600 == 0 and self.devices:
                await self.devices.event("time", device_time(self.tz_posix))
            if self.path.exists() and self.path.stat().st_mtime_ns != self.mtime_ns:
                try:
                    await self.reload()
                except ConfigError:
                    self.mtime_ns = self.path.stat().st_mtime_ns
                    logging.getLogger("nuka").error(
                        "配置重载失败，请在管理页检查行号；运行配置保留"
                    )

    async def reload(self):
        new = load_config(self.path)
        self.mtime_ns = self.path.stat().st_mtime_ns
        if new.assistant != self.config.assistant:
            await self.assistant.close()
            self.config.assistant = new.assistant
        for section in ("hub", "security"):
            old_values, new_values = (
                getattr(self.config, section).model_dump(),
                getattr(new, section).model_dump(),
            )
            if section == "hub":
                old_values.pop("timezone")
                new_values.pop("timezone")
            if old_values != new_values:
                logging.getLogger("nuka").warning("%s 配置已保存，重启 Hub 后生效", section)
        sections = {
            name
            for name in ("servers", "alerts", "ai", "agents", "weather", "notify")
            if getattr(new, name) != getattr(self.config, name)
        }
        kinds = {"servers": "server", "ai": "ai", "weather": "weather"}
        names = {
            name
            for name in self.scheduler.tasks
            if name.split(":", 1)[0] in {kinds[s] for s in sections if s in kinds}
        }
        await self.scheduler.stop(names)
        for section in sections:
            setattr(self.config, section, getattr(new, section))
        if "servers" in sections:
            self.server_values.clear()
            await self.set("servers", [])
        if "ai" in sections:
            self.ai_values.clear()
            await self.set("ai", {"accounts": [], "providers": []})
        if sections and not self.demo:
            await self.start_plugins(sections)
        await self.plugin_health()
        if new.hub.timezone != self.config.hub.timezone:
            self.config.hub.timezone = new.hub.timezone
            self.tz_posix = posix_timezone(new.hub.timezone)
            self.timezone = ZoneInfo(timezone_name(new.hub.timezone))
            await self.devices.event("time", device_time(self.tz_posix))

    async def start_plugins(self, sections: set[str] | None = None):
        from nuka_hub.plugins.ai import build_providers
        from nuka_hub.plugins.alerts import AlertEngine
        from nuka_hub.plugins.collectors import build_collectors
        from nuka_hub.plugins.servers import build_sources
        from nuka_hub.plugins.weather import Weather

        if sections is None or "alerts" in sections:
            self.alerts = AlertEngine(self.config.alerts, self.notify)
        plugins = []
        if sections is None:
            plugins.extend(
                await asyncio.to_thread(
                    build_collectors, self.config.collectors, self.client, self.secrets
                )
            )
        if sections is None or "servers" in sections:
            plugins.extend(
                await asyncio.to_thread(
                    build_sources, self.config.servers, self.secrets, self.client
                )
            )
        if sections is None or "ai" in sections:
            self.ai_plugins = await asyncio.to_thread(
                build_providers, self.config.ai, self.secrets, self.client, self.timezone
            )
            plugins.extend(self.ai_plugins)
        if self.config.weather.enabled and (sections is None or "weather" in sections):
            plugins.append(Weather(self.config.weather, self.client))
        self.scheduler.start(plugins)

    def ota_manifests(self) -> list[dict]:
        from pydantic import TypeAdapter

        from .models import RestOtaManifest

        return [
            item.wire()
            for path in sorted(self.ota_dir.glob("*/manifest.json"))
            for item in TypeAdapter(list[RestOtaManifest]).validate_json(path.read_bytes())
        ]

    async def media_command(self, src_id: str, command: str):
        for connection in self.agent_connections.values():
            if src_id in connection["media_ids"]:
                await connection["ws"].send_json(
                    self.devices.frame("media.cmd", {"src_id": src_id, "cmd": command})
                )
                return
        if self.media_adapter:
            await self.media_adapter.command(src_id, command)

    async def secret_changed(self, name: str):
        from nuka_hub.plugins.ai import build_providers
        from nuka_hub.plugins.servers import build_sources

        if self.demo:
            return
        for config, field, secret, kind, build, args in (
            (self.config.ai, "provider", "key_secret", "ai", build_providers, (self.timezone,)),
            (self.config.servers, "source", "auth_secret", "server", build_sources, ()),
        ):
            values = [value for value in getattr(config, field) if getattr(value, secret) == name]
            if not values:
                continue
            names = {value.name for value in values}
            await self.scheduler.stop({kind + ":" + plugin for plugin in names})
            plugins = await asyncio.to_thread(
                build,
                config.model_copy(update={field: values}),
                self.secrets,
                self.client,
                *args,
            )
            if kind == "ai":
                replacements = {plugin.name: plugin for plugin in plugins}
                self.ai_plugins = [
                    replacements.get(plugin.name, plugin) for plugin in self.ai_plugins
                ]
            self.scheduler.start(plugins)

    async def start(self):
        from .agents import Agents
        from .notifications import Notifications

        self.agents = Agents(self)
        self.notifications = Notifications(self)
        await self.set("notify", await self.notifications.listing())
        await self.set("pet", {"mood": "sleep"})
        self.tasks.append(asyncio.create_task(self.maintenance()))
        if self.demo:
            self.tasks.append(asyncio.create_task(self.demo_tick()))
            await asyncio.sleep(0)
        else:
            await self.start_plugins()
            if self.config.mdns.enabled:
                from nuka_hub.discovery import advertise

                self.tasks.append(asyncio.create_task(advertise(self)))
            if self.config.media.enabled:
                from nuka_hub.media import Media

                self.media_adapter = Media(
                    self.client, self.hub_id, self.config.hub.name, self.cover_cache
                )
                self.scheduler.start([self.media_adapter])

    async def notify(self, value: dict, *, web_only: bool = False) -> str:
        return await self.notifications.send(value, web_only=web_only)

    async def stop(self):
        await self.assistant.close()
        await self.scheduler.stop()
        if self.media_adapter:
            await self.media_adapter.close()
        for task in self.tasks:
            task.cancel()
        for task in self.tasks:
            with contextlib.suppress(asyncio.CancelledError):
                await task
        await self.client.aclose()
        await self.history.close()
