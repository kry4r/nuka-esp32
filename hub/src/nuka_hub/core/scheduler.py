# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import contextlib
import random
from collections import deque
from dataclasses import asdict
from datetime import UTC, datetime, timedelta

from .plugin import Health, Plugin


class Scheduler:
    def __init__(self, on_sample, on_health):
        self.on_sample = on_sample
        self.on_health = on_health
        self.tasks: dict[str, asyncio.Task] = {}
        self.health: dict[str, Health] = {}
        self.commands: dict[str, deque[str]] = {}
        self.wake: dict[str, asyncio.Event] = {}

    async def run(self, plugin: Plugin) -> None:
        key = plugin.kind + ":" + plugin.name
        probed = False
        while True:
            self.wake[key].clear()
            try:
                if not probed:
                    health = await plugin.probe()
                    self.health[key] = health
                    await self.on_health()
                    if health.health == "disabled":
                        if plugin.kind == "ai":
                            await self.on_sample(plugin, plugin.result())
                        return
                    probed = True
                async with asyncio.timeout(plugin.timeout_s):
                    operation = self.commands[key].popleft() if self.commands[key] else "sample"
                    result = await getattr(plugin, operation)()
                await self.on_sample(plugin, result)
                provider_health = result["providers"][0]["health"] if plugin.kind == "ai" else "ok"
                status = (
                    "error"
                    if provider_health == "auth_failed"
                    else "degraded"
                    if provider_health == "backoff"
                    else provider_health
                )
                self.health[key] = Health(
                    plugin.name,
                    plugin.kind,
                    status,
                    "" if status == "ok" else provider_health,
                    plugin.doc,
                )
            except Exception as error:
                if plugin.kind == "ai":
                    plugin.on_error(error)
                    await self.on_sample(plugin, plugin.result())
                retry = datetime.now(UTC) + timedelta(seconds=plugin.interval_s)
                detail = (
                    f"{type(error).__name__}; requests stopped; update secret or retry"
                    if plugin.kind == "ai" and plugin.breaker.state == "auth_failed"
                    else f"{type(error).__name__}; next retry {retry.isoformat()}"
                )
                self.health[key] = Health(
                    plugin.name,
                    plugin.kind,
                    "error",
                    detail,
                    plugin.doc,
                )
            await self.on_health()
            if not self.commands[key]:
                with contextlib.suppress(TimeoutError):
                    async with asyncio.timeout(plugin.interval_s * random.uniform(0.95, 1.05)):
                        await self.wake[key].wait()

    def request(self, plugin: Plugin, operation: str) -> None:
        key = plugin.kind + ":" + plugin.name
        self.commands[key].append(operation)
        self.wake[key].set()

    def start(self, plugins: list[Plugin]) -> None:
        for plugin in plugins:
            key = plugin.kind + ":" + plugin.name
            self.commands[key] = deque()
            self.wake[key] = asyncio.Event()
            self.tasks[key] = asyncio.create_task(self.run(plugin), name=plugin.name)

    async def stop(self, names: set[str] | None = None) -> None:
        for name in list(self.tasks) if names is None else names:
            task = self.tasks.pop(name)
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
            self.health.pop(name, None)
            del self.commands[name], self.wake[name]

    def listing(self) -> list[dict]:
        return [
            {k: v for k, v in asdict(health).items() if v != ""} for health in self.health.values()
        ]
