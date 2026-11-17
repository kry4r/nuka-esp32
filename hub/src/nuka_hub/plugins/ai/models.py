# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import hashlib
from datetime import UTC, datetime
from typing import Any

from pydantic import BaseModel, ConfigDict, Field


class Boundary(BaseModel):
    model_config = ConfigDict(extra="ignore")


class ApiCallResponse(Boundary):
    status_code: int = Field(ge=100, le=599)
    body: str


class Quota(Boundary):
    signals: dict[str, Any] = {}
    observed_at: datetime | None = None


class AuthFile(Boundary):
    auth_index: str
    provider: str
    status: str
    email: str = ""
    disabled: bool = False
    unavailable: bool = False
    next_retry_after: datetime | None = None
    id_token: dict[str, Any] = {}
    quota: Quota = Field(default_factory=Quota)
    project_id: str = ""


class AuthFiles(Boundary):
    files: list[AuthFile]


class RateWindow(Boundary):
    used_percent: float = Field(ge=0, le=100)
    reset_at: int | None = None
    limit_window_seconds: int


class CodexLimit(Boundary):
    primary_window: RateWindow | None = None
    secondary_window: RateWindow | None = None


class CodexUsage(Boundary):
    rate_limit: CodexLimit
    credits: dict[str, Any] | None = None


class ClaudeWindow(Boundary):
    utilization: float = Field(ge=0, le=100)
    resets_at: datetime | None = None


class ClaudeExtraUsage(Boundary):
    is_enabled: bool
    utilization: float | None = Field(default=None, ge=0, le=100)


class ClaudeUsage(Boundary):
    five_hour: ClaudeWindow | None = None
    seven_day: ClaudeWindow | None = None
    seven_day_opus: ClaudeWindow | None = None
    seven_day_sonnet: ClaudeWindow | None = None
    extra_usage: ClaudeExtraUsage | None = None


class Bucket(Boundary):
    remainingFraction: float = Field(ge=0, le=1)
    resetTime: datetime | None = None
    modelId: str


class BucketGroup(Boundary):
    buckets: list[Bucket]


class AntigravityUsage(Boundary):
    groups: list[BucketGroup]


def iso(value: datetime | float | int) -> str:
    stamp = datetime.fromtimestamp(value, UTC) if isinstance(value, (float, int)) else value
    return stamp.astimezone(UTC).isoformat().replace("+00:00", "Z")


def account_id(provider: str, index: str) -> str:
    return "a_" + hashlib.sha256(f"{provider}:{index}".encode()).hexdigest()[:20]


def window(
    kind: str,
    used: float,
    source: str,
    observed: datetime,
    stale_after: float,
    reset: datetime | int | None = None,
    label: str = "",
) -> dict:
    result = {
        "kind": kind,
        "used_pct": round(used, 1),
        "source": source,
        "observed_at": iso(observed),
        "stale": (datetime.now(UTC) - observed).total_seconds() > stale_after,
    }
    if reset is not None:
        result["reset_at"] = iso(reset)
    if label:
        result["label"] = label
    return result


def parse_active(
    kind: str, payload: Any, stale_after: float, observed: datetime
) -> tuple[list[dict], dict | None]:
    windows, credits = [], None
    if kind == "codex":
        usage = CodexUsage.model_validate(payload)
        for rate in (usage.rate_limit.primary_window, usage.rate_limit.secondary_window):
            if rate is not None:
                name = {18000: "5h", 86400: "daily", 604800: "weekly", 2592000: "monthly"}.get(
                    rate.limit_window_seconds, "other"
                )
                windows.append(
                    window(
                        name,
                        rate.used_percent,
                        "active",
                        observed,
                        stale_after,
                        rate.reset_at,
                        str(rate.limit_window_seconds) + "s" if name == "other" else "",
                    )
                )
        if usage.credits is not None and "balance" in usage.credits:
            credits = {"balance": float(usage.credits["balance"]), "currency": "USD"}
    elif kind == "claude":
        usage = ClaudeUsage.model_validate(payload)
        for key, name, label in (
            ("five_hour", "5h", ""),
            ("seven_day", "weekly", ""),
            ("seven_day_opus", "other", "Opus weekly"),
            ("seven_day_sonnet", "other", "Sonnet weekly"),
        ):
            rate = getattr(usage, key)
            if rate is not None:
                windows.append(
                    window(
                        name,
                        rate.utilization,
                        "active",
                        observed,
                        stale_after,
                        rate.resets_at,
                        label,
                    )
                )
        if (
            usage.extra_usage is not None
            and usage.extra_usage.is_enabled
            and usage.extra_usage.utilization is not None
        ):
            windows.append(
                window("monthly", usage.extra_usage.utilization, "active", observed, stale_after)
            )
    else:
        usage = AntigravityUsage.model_validate(payload)
        for group in usage.groups:
            for bucket in group.buckets:
                windows.append(
                    window(
                        "other",
                        (1 - bucket.remainingFraction) * 100,
                        "active",
                        observed,
                        stale_after,
                        bucket.resetTime,
                        bucket.modelId,
                    )
                )
    return windows, credits
