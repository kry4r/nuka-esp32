# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from pydantic import BaseModel


class ToolDenied(ValueError):
    pass


@dataclass
class Tool:
    name: str
    description: str
    arguments: type[BaseModel]
    execute: Callable[[BaseModel], Awaitable[dict[str, Any]]]
    sensitive: bool = False

    def specification(self) -> dict:
        return {
            "name": self.name,
            "description": self.description,
            "parameters": self.arguments.model_json_schema(),
        }


class ToolGate:
    def __init__(
        self,
        tools: list[Tool],
        confirm: Callable[[str, dict], Awaitable[bool]] | None = None,
        timeout: float = 60,
    ):
        self.tools = {tool.name: tool for tool in tools}
        self.confirm, self.timeout = confirm, timeout

    async def execute(self, name: str, arguments: dict) -> dict:
        if name not in self.tools:
            raise ToolDenied("Tool is not enabled")
        tool = self.tools[name]
        values = tool.arguments.model_validate(arguments)
        if tool.sensitive:
            if self.confirm is None:
                raise ToolDenied("Device confirmation is unavailable")
            try:
                async with asyncio.timeout(self.timeout):
                    allowed = await self.confirm(name, values.model_dump())
            except TimeoutError as error:
                raise ToolDenied("Device confirmation expired") from error
            if not allowed:
                raise ToolDenied("Device denied the tool")
        # Process a cancellation queued alongside the physical decision before any side effect.
        await asyncio.sleep(0)
        return await tool.execute(values)
