# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
from collections import defaultdict
from collections.abc import Callable


class Bus:
    def __init__(self):
        self.listeners: dict[str, set[Callable]] = defaultdict(set)

    def subscribe(self, topic: str, callback: Callable) -> None:
        self.listeners[topic].add(callback)

    def unsubscribe(self, topic: str, callback: Callable) -> None:
        self.listeners[topic].remove(callback)

    async def publish(self, topic: str, value: object) -> None:
        await asyncio.gather(*(callback(topic, value) for callback in tuple(self.listeners[topic])))
