# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import time
from datetime import UTC, datetime, tzinfo
from typing import Any

import httpx
from pydantic import Field

from .cpa import CPA
from .models import Boundary, window


class SnapshotWindow(Boundary):
    window_kind: str
    provider_window_id: str
    used_percent: float | None = Field(default=None, ge=0, le=100)
    remaining_percent: float | None = Field(default=None, ge=0, le=100)
    observed_at_ms: int
    cycle_end_ms: int | None = None


class SnapshotAccount(Boundary):
    row_key: str
    windows: list[SnapshotWindow]


class Snapshots(Boundary):
    items: list[SnapshotAccount]


class HeaderWindow(Boundary):
    used_percent: float | None = Field(default=None, ge=0, le=100)
    reset_at_ms: int | None = None
    window_minutes: int | None = None


class HeaderQuota(Boundary):
    primary: HeaderWindow | None = None
    secondary: HeaderWindow | None = None


class HeaderMetadata(Boundary):
    quota: HeaderQuota | None = None


class HeaderSnapshot(Boundary):
    auth_index: str | None = None
    timestamp_ms: int
    response_metadata: HeaderMetadata | None = None


class HeaderSnapshots(Boundary):
    items: list[HeaderSnapshot]


class DetailTokens(Boundary):
    total_tokens: int = Field(ge=0)


class UsageDetail(Boundary):
    timestamp: datetime
    failed: bool
    tokens: DetailTokens


class ModelUsage(Boundary):
    details: list[UsageDetail]


class ApiUsage(Boundary):
    models: dict[str, ModelUsage]


class Usage(Boundary):
    total_requests: int = Field(ge=0)
    apis: dict[str, ApiUsage] = {}


class CPAMP(CPA):
    def __init__(
        self,
        config: Any,
        ai_config: Any,
        secrets: Any,
        client: httpx.AsyncClient,
        timezone: tzinfo,
    ):
        super().__init__(config, ai_config, secrets, client, timezone)
        self.interval_s = min(config.passive_interval, config.snapshot_interval)
        self.snapshot_at = float("-inf")

    async def extra(self) -> None:
        if time.monotonic() - self.snapshot_at < self.config.snapshot_interval:
            return
        if self.accounts:
            targets = [
                {
                    "row_key": identity,
                    "provider": auth.provider,
                    "account": {"auth_index": auth.auth_index},
                }
                for identity, auth in self.auth_files.items()
            ]
            response = Snapshots.model_validate(
                await self.http.request(
                    "POST",
                    "/v0/management/quota-snapshots/query",
                    payload={"accounts": targets},
                )
            )
            for item in response.items:
                rates = []
                for rate in item.windows:
                    if rate.used_percent is None and rate.remaining_percent is None:
                        continue
                    kind = {
                        "five_hour": "5h",
                        "rolling_24h": "daily",
                        "weekly": "weekly",
                        "monthly": "monthly",
                    }.get(rate.window_kind, "other")
                    used = (
                        rate.used_percent
                        if rate.used_percent is not None
                        else 100 - rate.remaining_percent
                    )
                    rates.append(
                        window(
                            kind,
                            used,
                            "snapshot",
                            datetime.fromtimestamp(rate.observed_at_ms / 1000, UTC),
                            self.ai_config.stale_after,
                            int(rate.cycle_end_ms / 1000) if rate.cycle_end_ms else None,
                            rate.provider_window_id if kind == "other" else "",
                        )
                    )
                if rates:
                    self.accounts[item.row_key]["windows"] = rates
        headers = HeaderSnapshots.model_validate(
            await self.http.request("GET", "/v0/management/monitoring/header-snapshots")
        )
        indices = {auth.auth_index: identity for identity, auth in self.auth_files.items()}
        for snapshot in sorted(headers.items, key=lambda entry: entry.timestamp_ms):
            if (
                snapshot.auth_index not in indices
                or snapshot.response_metadata is None
                or snapshot.response_metadata.quota is None
            ):
                continue
            account = self.accounts[indices[snapshot.auth_index]]
            quota = snapshot.response_metadata.quota
            rates = []
            for rate in (quota.primary, quota.secondary):
                if rate is None or rate.used_percent is None:
                    continue
                kind = {300: "5h", 1440: "daily", 10080: "weekly", 43200: "monthly"}.get(
                    rate.window_minutes, "other"
                )
                rates.append(
                    window(
                        kind,
                        rate.used_percent,
                        "snapshot",
                        datetime.fromtimestamp(snapshot.timestamp_ms / 1000, UTC),
                        self.ai_config.stale_after,
                        int(rate.reset_at_ms / 1000) if rate.reset_at_ms else None,
                        str(rate.window_minutes) + "m" if kind == "other" else "",
                    )
                )
            if rates and (
                not account["windows"]
                or rates[0]["observed_at"] > account["windows"][0]["observed_at"]
            ):
                account["windows"] = rates
        usage = Usage.model_validate(await self.http.request("GET", "/v0/management/usage"))
        details = [
            detail
            for api in usage.apis.values()
            for model in api.models.values()
            for detail in model.details
        ]
        today = datetime.now(self.timezone).date()
        details_today = [
            detail
            for detail in details
            if detail.timestamp.astimezone(self.timezone).date() == today
        ]
        # A retained subset cannot establish today's complete counts.
        self.summary = (
            {
                "requests": len(details_today),
                "tokens": sum(detail.tokens.total_tokens for detail in details_today),
                "failures": sum(detail.failed for detail in details_today),
            }
            if len(details) == usage.total_requests
            else None
        )
        self.snapshot_at = time.monotonic()
