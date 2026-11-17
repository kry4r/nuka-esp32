# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import hashlib
import json
import secrets
import time
from pathlib import PurePath

from .history import iso


class PermissionGone(ValueError):
    pass


class Agents:
    def __init__(self, runtime):
        self.runtime = runtime
        self.sessions: dict[str, dict] = {}
        self.seen: dict[str, float] = {}
        self.pending: dict[str, dict] = {}
        self.waiters: dict[str, asyncio.Future] = {}

    @staticmethod
    def sid(value: str) -> str:
        return "c_" + hashlib.sha256(value.encode()).hexdigest()[:20]

    async def publish(self):
        await self.runtime.set(
            "agents",
            {"sessions": list(self.sessions.values()), "pending": list(self.pending.values())},
        )

    async def event(self, source: str, event: str, payload: dict, host: str):
        sid = self.sid(payload.get("session_id", host + source))
        states = {
            "SessionStart": "idle",
            "UserPromptSubmit": "thinking",
            "PreToolUse": "tool",
            "BeforeTool": "tool",
            "PostToolUse": "thinking",
            "AfterTool": "thinking",
            "Stop": "done",
            "AfterAgent": "done",
            "StopFailure": "error",
            "Notification": "idle",
            "PermissionRequest": "waiting_permission",
        }
        if event == "SessionEnd":
            self.sessions.pop(sid, None)
            self.seen.pop(sid, None)
            await self.publish()
            return
        if event not in states:
            return
        session = {
            "sid": sid,
            "source": source,
            "host": host,
            "state": states[event],
            "since": iso(),
        }
        if self.runtime.config.agents.show_project == "basename" and payload.get("cwd"):
            session["project"] = PurePath(payload["cwd"].replace("\\", "/")).name
        if payload.get("tool_name"):
            session["tool"] = payload["tool_name"]
        message = payload.get("last_assistant_message") or payload.get("message")
        if self.runtime.config.agents.collect_last_message and message:
            session["last_message"] = message[:140]
        self.sessions[sid] = session
        self.seen[sid] = time.time()
        if session["state"] == "done":
            self.runtime.celebrate_until = int(time.time()) + 3
            await self.runtime.notify(
                {
                    "title": "任务完成"
                    if self.runtime.config.hub.lang == "zh-CN"
                    else "Agent task complete",
                    "_titles": {"zh-CN": "任务完成", "en": "Agent task complete"},
                    "body": session.get("last_message", ""),
                    "source": source,
                    "dedupe_key": "agent:" + sid,
                }
            )
        await self.publish()
        await self.runtime.pet()

    async def permission(self, payload: dict) -> dict:
        await self.event("claude", "PermissionRequest", payload, self.runtime.config.hub.name)
        if not self.runtime.devices or not self.runtime.devices.connections:
            await self.runtime.history.audit(
                "perm", "terminal", {"result": "terminal", "sid": self.sid(payload["session_id"])}
            )
            return {}
        now = time.time()
        rid = "r_" + secrets.token_hex(8)
        item = {
            "rid": rid,
            "sid": self.sid(payload["session_id"]),
            "src": "claude",
            "host": self.runtime.config.hub.name,
            "tool": payload.get("tool_name", "Tool"),
            "summary": json.dumps(payload.get("tool_input", {}), ensure_ascii=False)[:120],
            "created": iso(now),
            "expires": iso(now + self.runtime.config.agents.permission_timeout),
        }
        if self.runtime.config.agents.show_project == "basename" and payload.get("cwd"):
            item["project"] = PurePath(payload["cwd"].replace("\\", "/")).name
        self.pending[rid] = item
        self.waiters[rid] = asyncio.get_running_loop().create_future()
        await self.publish()
        await self.runtime.devices.event(
            "perm.req",
            {k: v for k, v in item.items() if k not in {"created", "expires"}}
            | {"expires_at": int(now + self.runtime.config.agents.permission_timeout)},
        )
        try:
            async with asyncio.timeout(self.runtime.config.agents.permission_timeout):
                behavior, by = await self.waiters[rid]
        except TimeoutError:
            behavior, by = "timeout", "timeout"
        finally:
            self.waiters.pop(rid, None)
            self.pending.pop(rid, None)
        await self.publish()
        await self.runtime.devices.event("perm.done", {"rid": rid, "result": behavior, "by": by})
        await self.runtime.history.audit("perm", by, {"rid": rid, "result": behavior, "by": by})
        if behavior == "timeout":
            return {}
        decision = {"behavior": behavior}
        if behavior == "deny":
            decision["message"] = "Denied on nuka-esp32"
        return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}}

    def decide(self, rid: str, behavior: str, by: str):
        future = self.waiters.get(rid)
        if future is None or future.done():
            raise PermissionGone(rid)
        future.set_result((behavior, by))

    async def expire(self):
        now = time.time()
        changed = False
        for sid, seen in list(self.seen.items()):
            session = self.sessions[sid]
            elapsed = now - seen
            if elapsed > self.runtime.config.agents.purge_after:
                del self.sessions[sid]
                del self.seen[sid]
                changed = True
            elif elapsed > self.runtime.config.agents.idle_after or (
                session["state"] == "done" and elapsed > 30
            ):
                if session["state"] != "idle":
                    session.update(state="idle", since=iso())
                    changed = True
        if changed:
            await self.publish()
        if (
            self.runtime.state.get("pet").get("mood") == "celebrate"
            and now >= self.runtime.state.get("pet")["until"]
        ):
            await self.runtime.pet()
