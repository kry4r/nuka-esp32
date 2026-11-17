# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
from types import SimpleNamespace

import pytest

from nuka_hub.core.agents import Agents, PermissionGone
from nuka_hub.core.config import Config
from nuka_hub.core.history import History


@pytest.fixture
def runtime(tmp_path):
    config = Config()
    config.agents.permission_timeout = 0.02
    events = []

    async def event(kind, data):
        events.append((kind, data))

    async def noop(*args):
        pass

    return SimpleNamespace(
        config=config,
        devices=SimpleNamespace(connections={"sim": object()}, event=event),
        set=noop,
        pet=noop,
        history=History(tmp_path / "history.sqlite3"),
        events=events,
    )


@pytest.mark.parametrize("behavior", ["allow", "deny", "timeout", "no_device"])
async def test_approval(runtime, behavior):
    if behavior == "no_device":
        runtime.devices.connections = {}
    manager = Agents(runtime)
    task = asyncio.create_task(
        manager.permission(
            {
                "session_id": "fixture",
                "tool_name": "Bash",
                "tool_input": {"command": "echo fixture"},
            }
        )
    )
    await asyncio.sleep(0)
    if behavior in {"allow", "deny"}:
        rid = next(iter(manager.pending))
        manager.decide(rid, behavior, "device")
        with pytest.raises(PermissionGone):
            manager.decide(rid, "deny", "web")
    result = await task
    if behavior in {"timeout", "no_device"}:
        assert result == {}
    else:
        assert result["hookSpecificOutput"]["decision"]["behavior"] == behavior
    await runtime.history.close()
