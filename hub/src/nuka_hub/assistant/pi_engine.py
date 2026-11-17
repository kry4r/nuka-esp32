# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import contextlib
import json
import os
import shutil
import subprocess
from collections.abc import AsyncIterator, Awaitable, Callable
from pathlib import Path
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

MAX_LINE_BYTES = 1024 * 1024
ENGINE = Path(__file__).with_name("pi") / "engine.mjs"
ToolExecutor = Callable[[str, dict[str, Any]], Awaitable[dict[str, Any]]]


class EngineError(RuntimeError):
    pass


class EngineEvent(BaseModel):
    model_config = ConfigDict(extra="forbid")
    type: Literal["delta", "tool", "message", "done", "error"]
    text: str | None = Field(default=None, max_length=262144)
    call_id: str | None = Field(default=None, max_length=512)
    name: str | None = Field(default=None, max_length=128)
    arguments: dict[str, Any] | None = None
    message: dict[str, Any] | None = None
    code: Literal["provider_error", "engine_error", "invalid_frame"] | None = None


class PiEngine:
    """One owned Pi process per turn; credentials travel only through its stdin."""

    def check_ready(self) -> None:
        self.node = shutil.which("node")
        if self.node is None or not (ENGINE.parent / "node_modules").is_dir():
            raise EngineError("Install Node >=22.19 and run npm ci in the assistant Pi directory")
        version = subprocess.check_output([self.node, "--version"], text=True).strip()
        if tuple(int(part) for part in version.removeprefix("v").split(".")) < (22, 19, 0):
            raise EngineError("Pi requires Node >=22.19")

    async def turn(
        self,
        *,
        config: dict[str, Any],
        api_key: str | None,
        system_prompt: str,
        messages: list[dict[str, Any]],
        text: str,
        tools: list[dict[str, Any]],
        execute: ToolExecutor,
    ) -> AsyncIterator[dict[str, Any]]:
        # The child needs no ambient provider credentials, shell, or personal Pi configuration.
        child_env = {
            key: value
            for key, value in os.environ.items()
            if key.upper() in {"PATH", "SYSTEMROOT", "WINDIR", "TEMP", "TMP", "TMPDIR"}
        }
        process = await asyncio.create_subprocess_exec(
            self.node,
            str(ENGINE),
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
            env=child_env,
            limit=MAX_LINE_BYTES + 1,
            cwd=ENGINE.parent,
        )

        async def send(value: dict[str, Any]) -> None:
            encoded = json.dumps(value, ensure_ascii=False).encode() + b"\n"
            if len(encoded) > MAX_LINE_BYTES:
                raise EngineError("Assistant context exceeds the Pi frame limit")
            process.stdin.write(encoded)
            await process.stdin.drain()

        try:
            await send(
                {
                    "type": "start",
                    "config": config,
                    "api_key": api_key,
                    "system_prompt": system_prompt,
                    "messages": messages,
                    "text": text,
                    "tools": tools,
                }
            )
            while True:
                line = await process.stdout.readline()
                if not line:
                    raise EngineError("Pi process ended before completing the turn")
                event = EngineEvent.model_validate_json(line).model_dump(exclude_none=True)
                if event["type"] == "error":
                    raise EngineError("Pi model request failed")
                if event["type"] == "done":
                    return
                if event["type"] == "tool":
                    # The caller owns the allowlist, argument validation and confirmation gate.
                    yield event
                    result = await execute(event["name"], event["arguments"])
                    await send(
                        {
                            "type": "tool_result",
                            "call_id": event["call_id"],
                            "text": json.dumps(result, ensure_ascii=False),
                            "is_error": False,
                        }
                    )
                else:
                    yield event
        finally:
            process.stdin.close()
            if process.returncode is None:
                with contextlib.suppress(ProcessLookupError):
                    process.terminate()
            with contextlib.suppress(TimeoutError):
                async with asyncio.timeout(0.2):
                    await process.wait()
            if process.returncode is None:
                with contextlib.suppress(ProcessLookupError):
                    process.kill()
                await process.wait()
