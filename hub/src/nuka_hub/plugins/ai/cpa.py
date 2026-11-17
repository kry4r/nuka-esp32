# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import json
import time
from datetime import UTC, datetime, tzinfo
from typing import Annotated, Any

import httpx
from pydantic import Field, TypeAdapter

from nuka_hub.core.plugin import Health

from .breaker import Breaker, SafeClient
from .models import AuthFile, AuthFiles, account_id, iso, parse_active, window
from .recipes import render

PERCENT = TypeAdapter(Annotated[float, Field(ge=0, le=100)])


class CPA:
    kind = "ai"
    doc = "07-ai-quota"
    timeout_s = 30.0

    def __init__(
        self,
        config: Any,
        ai_config: Any,
        secrets: Any,
        client: httpx.AsyncClient,
        timezone: tzinfo,
    ):
        self.config, self.ai_config = config, ai_config
        self.timezone = timezone
        self.name, self.interval_s = config.name, config.passive_interval
        self.breaker = Breaker(config.max_rpm)
        self.key = secrets.get(config.key_secret)
        self.breaker.state = "ok" if self.key else "disabled"
        self.http = SafeClient(config.base_url, self.key or "", client, self.breaker)
        self.accounts: dict[str, dict] = {}
        self.auth_files: dict[str, AuthFile] = {}
        self.signal_observed: dict[str, tuple[dict, datetime]] = {}
        self.active_at: dict[str, float] = {}
        self.passive_at = float("-inf")
        self.last_ok: datetime | None = None
        self.summary: dict | None = None

    async def probe(self) -> Health:
        return Health(
            self.name,
            self.kind,
            self.breaker.state,
            "" if self.breaker.state == "ok" else "AI management secret is missing",
            self.doc,
        )

    def on_error(self, error: Exception) -> None:
        if isinstance(error, (httpx.TimeoutException, TimeoutError)):
            self.breaker.backoff(time.monotonic())

    def result(self) -> dict:
        now = datetime.now(UTC)
        for account in self.accounts.values():
            for rate in account["windows"]:
                rate["stale"] = (
                    now - datetime.fromisoformat(rate["observed_at"])
                ).total_seconds() > self.ai_config.stale_after
        provider = {"name": self.name, "type": self.config.type, "health": self.breaker.state}
        if self.last_ok is not None:
            provider["last_ok"] = iso(self.last_ok)
        if self.breaker.state == "backoff":
            provider["next_try"] = iso(
                time.time() + max(0, self.breaker.retry_at - time.monotonic())
            )
        result = {
            "accounts": [
                {**account, "breaker_open": self.breaker.state in {"auth_failed", "backoff"}}
                for account in self.accounts.values()
            ],
            "providers": [provider],
        }
        if self.summary is not None:
            result["summary"] = self.summary
        return result

    async def sample(self) -> dict:
        if self.breaker.state == "auth_failed" or time.monotonic() < self.breaker.retry_at:
            return self.result()
        if time.monotonic() - self.passive_at >= self.config.passive_interval:
            await self.passive()
        await self.extra()
        self.result()
        if self.config.active_enabled:
            for identity, account in self.accounts.items():
                if all(rate["stale"] for rate in account["windows"]):
                    await self.active(identity)
        return self.result()

    async def extra(self) -> None:
        pass

    async def passive(self) -> None:
        payload = AuthFiles.model_validate(
            await self.http.request("GET", "/v0/management/auth-files")
        )
        accounts, files = {}, {}
        now = datetime.now(UTC)
        for position, auth in enumerate(payload.files, 1):
            identity = account_id(self.name, auth.auth_index)
            override = self.config.accounts.get(identity)
            if override is not None and override.hidden:
                continue
            kind = (
                auth.provider
                if auth.provider in {"codex", "claude", "antigravity", "kimi"}
                else "other"
            )
            local, _, domain = auth.email.partition("@")
            label = (
                f"{local[:1]}***@{domain}"
                if domain and self.ai_config.label_mode == "masked_email"
                else f"{kind} {position}"
            )
            if override is not None and override.label:
                label = override.label
            previous = self.accounts.get(identity)
            windows = self.passive_windows(identity, auth, now)
            if previous is not None:
                by_kind = {
                    (rate["kind"], rate.get("label", "")): rate for rate in previous["windows"]
                }
                for rate in windows:
                    key = rate["kind"], rate.get("label", "")
                    if key not in by_kind or rate["observed_at"] > by_kind[key]["observed_at"]:
                        by_kind[key] = rate
                windows = list(by_kind.values())
            status = "ok" if auth.status in {"ready", "active", "ok"} else "unknown"
            if any(rate["used_pct"] >= 100 for rate in windows):
                status = "exhausted"
            if auth.unavailable:
                status = "cooldown"
            if auth.status == "reauth":
                status = "reauth"
            if auth.disabled:
                status = "disabled"
            account = {
                "id": identity,
                "provider_kind": kind,
                "label": label,
                "status": status,
                "windows": windows,
            }
            plan = auth.id_token.get("plan_type")
            if plan:
                account["plan"] = plan
            if auth.next_retry_after is not None:
                account["recover_at"] = iso(auth.next_retry_after)
            if previous is not None:
                for name in ("credits", "today"):
                    if name in previous:
                        account[name] = previous[name]
            accounts[identity], files[identity] = account, auth
        self.accounts, self.auth_files = accounts, files
        self.passive_at, self.last_ok = time.monotonic(), now

    def passive_windows(self, identity: str, auth: AuthFile, now: datetime) -> list[dict]:
        signals = {key.lower(): value for key, value in auth.quota.signals.items()}
        if auth.quota.observed_at is not None:
            observed = auth.quota.observed_at
        elif identity in self.signal_observed and self.signal_observed[identity][0] == signals:
            observed = self.signal_observed[identity][1]
        else:
            observed = now
        self.signal_observed[identity] = signals, observed
        result = []
        if auth.provider == "codex":
            for prefix, kind in (("primary", "5h"), ("secondary", "weekly")):
                used = signals.get(f"x-codex-{prefix}-used-percent")
                if used is not None:
                    reset = signals.get(f"x-codex-{prefix}-reset-at")
                    result.append(
                        window(
                            kind,
                            PERCENT.validate_python(used),
                            "passive",
                            observed,
                            self.ai_config.stale_after,
                            int(reset) if reset else None,
                        )
                    )
        elif auth.provider == "claude":
            for prefix, kind in (("5h", "5h"), ("7d", "weekly")):
                used = signals.get(f"anthropic-ratelimit-unified-{prefix}-utilization")
                if used is not None:
                    reset = signals.get(f"anthropic-ratelimit-unified-{prefix}-reset")
                    result.append(
                        window(
                            kind,
                            PERCENT.validate_python(float(used) * 100),
                            "passive",
                            observed,
                            self.ai_config.stale_after,
                            int(reset) if reset else None,
                        )
                    )
        return result

    async def active(self, identity: str) -> None:
        auth = self.auth_files[identity]
        if auth.provider not in {"codex", "claude", "antigravity"} or self.accounts[identity][
            "status"
        ] in {"disabled", "reauth"}:
            return
        now = time.monotonic()
        if now - self.active_at.get(identity, float("-inf")) < self.config.active_min_interval:
            return
        payload = render(
            auth.provider,
            auth.auth_index,
            str(auth.id_token.get("chatgpt_account_id", "")),
            auth.project_id,
        )
        self.active_at[identity] = now
        response = await self.http.request("POST", "/v0/management/api-call", payload=payload)
        windows, credits = parse_active(
            auth.provider, json.loads(response.body), self.ai_config.stale_after, datetime.now(UTC)
        )
        self.accounts[identity]["windows"] = windows
        if credits is not None:
            self.accounts[identity]["credits"] = credits
        if self.accounts[identity]["status"] not in {"disabled", "reauth"}:
            self.accounts[identity]["status"] = (
                "exhausted" if any(rate["used_pct"] >= 100 for rate in windows) else "ok"
            )

    async def refresh(self) -> dict:
        if self.config.active_enabled:
            for identity in self.accounts:
                await self.active(identity)
        else:
            await self.passive()
            await self.extra()
        return self.result()

    async def retry(self) -> dict:
        self.breaker.retry()
        await self.passive()
        return self.result()
