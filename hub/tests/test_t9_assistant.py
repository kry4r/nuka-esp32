# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio

import httpx
import pytest
from pydantic import BaseModel, Field

from nuka_hub.api.app import create_app
from nuka_hub.assistant.tools import Tool, ToolDenied, ToolGate
from nuka_hub.core.config import Config
from nuka_hub.core.runtime import Runtime


class Arguments(BaseModel):
    text: str = Field(min_length=1, max_length=2000)


@pytest.mark.parametrize("decision", [None, False, True])
async def test_sensitive_tools_require_physical_allow(decision):
    executed = []

    async def write(arguments):
        executed.append(arguments.text)
        return {"saved": True}

    async def confirm(name, arguments):
        return decision

    gate = ToolGate(
        [Tool("remember", "fixture", Arguments, write, True)],
        confirm if decision is not None else None,
    )
    if decision:
        assert await gate.execute("remember", {"text": "fixture"}) == {"saved": True}
    else:
        with pytest.raises(ToolDenied):
            await gate.execute("remember", {"text": "fixture"})
    assert executed == (["fixture"] if decision else [])
    with pytest.raises(ToolDenied):
        await gate.execute("unregistered", {"text": "fixture"})


@pytest.mark.parametrize("outcome", ["timeout", "cancel", "allow_and_cancel"])
async def test_waiting_tool_never_runs_after_expiry_or_cancel(outcome):
    waiting = asyncio.Event()
    decision = asyncio.get_running_loop().create_future()
    executed = []

    async def confirm(name, arguments):
        waiting.set()
        return await decision

    async def write(arguments):
        executed.append(arguments.text)
        return {}

    gate = ToolGate([Tool("remember", "fixture", Arguments, write, True)], confirm, timeout=0.02)
    task = asyncio.create_task(gate.execute("remember", {"text": "fixture"}))
    await waiting.wait()
    if outcome == "timeout":
        with pytest.raises(ToolDenied):
            await task
    else:
        if outcome == "allow_and_cancel":
            decision.set_result(True)
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task
    assert not executed


async def test_disconnected_stream_cancels_pending_memory_write(tmp_path):
    config = Config.model_validate(
        {
            "hub": {"data_dir": str(tmp_path)},
            "assistant": {
                "enabled": True,
                "llm": {"base_url": "http://192.0.2.1/v1", "model": "fixture"},
            },
        }
    )
    runtime = Runtime(config, tmp_path / "config.toml", demo=True)
    waiting = asyncio.Event()
    stopped = asyncio.Event()

    async def confirm(*args):
        waiting.set()
        return await asyncio.get_running_loop().create_future()

    class FakeEngine:
        def check_ready(self):
            pass

        async def turn(self, **kwargs):
            try:
                yield {
                    "type": "tool",
                    "call_id": "fixture|private-upstream-id",
                    "name": "memory_remember",
                    "arguments": {"text": "fixture fact"},
                }
                await kwargs["execute"]("memory_remember", {"text": "fixture fact"})
            finally:
                stopped.set()

    runtime.assistant.engine = FakeEngine()
    runtime.assistant.confirm = confirm
    try:
        stream = await runtime.assistant.open("nuka_example", "remember fixture", None, [])
        async for event in stream:
            if event["event"] == "tool" and event["data"]["state"] == "waiting_confirmation":
                await waiting.wait()
                break
        await stream.aclose()
        assert stopped.is_set()
        assert not runtime.assistant.active
        assert not await runtime.history.sql("SELECT * FROM assistant_memories")
    finally:
        await runtime.stop()


@pytest.mark.parametrize(
    "path,method",
    [
        ("/api/v1/assistant/chat", "POST"),
        ("/api/v1/assistant/history", "GET"),
        ("/api/v1/assistant/files", "POST"),
        ("/api/v1/assistant/memories", "POST"),
    ],
)
async def test_public_assistant_not_exposed_by_admin_bearer(tmp_path, path, method):
    config = Config()
    config.hub.data_dir = str(tmp_path)
    runtime = Runtime(config, tmp_path / "config.toml", demo=True)
    token = await runtime.tokens.create("admin", "fixture")
    try:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=create_app(runtime), client=("127.0.0.1", 12345)),
            base_url="http://hub.example.com",
        ) as client:
            response = await client.request(
                method,
                path,
                headers={"Cf-Ray": "fixture", "Authorization": "Bearer " + token["token"]},
            )
            assert response.status_code == 404
    finally:
        await runtime.stop()
