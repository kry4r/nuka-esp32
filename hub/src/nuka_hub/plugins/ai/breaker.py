# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import time
from collections import deque
from typing import Any

import httpx

from .models import ApiCallResponse
from .recipes import allowed_call

ALLOWED = {
    ("GET", "/v0/management/auth-files"),
    ("GET", "/v0/management/monitoring/header-snapshots"),
    ("GET", "/v0/management/usage"),
    ("POST", "/v0/management/quota-snapshots/query"),
    ("POST", "/v0/management/api-call"),
}


class CircuitOpen(RuntimeError):
    pass


class AuthFailed(RuntimeError):
    pass


class Breaker:
    def __init__(self, max_rpm: int):
        self.state = "ok"
        self.max_rpm = max_rpm
        self.attempts: deque[float] = deque()
        self.failures = 0
        self.retry_at = 0.0

    def before(self, now: float) -> None:
        if self.state == "auth_failed" or now < self.retry_at:
            raise CircuitOpen("AI provider circuit is open")
        while self.attempts and self.attempts[0] <= now - 60:
            self.attempts.popleft()
        if len(self.attempts) >= self.max_rpm:
            raise CircuitOpen("AI provider request budget is exhausted")
        self.attempts.append(now)

    def response(self, status: int, now: float) -> None:
        if status in {401, 403}:
            self.state = "auth_failed"
            raise AuthFailed("AI provider authentication failed; requests stopped")
        if status == 429 or status >= 500:
            self.backoff(now)
        elif 200 <= status < 300:
            self.failures, self.retry_at, self.state = 0, 0.0, "ok"

    def backoff(self, now: float) -> None:
        self.failures += 1
        self.retry_at = now + min(1800, 2 ** min(self.failures - 1, 11))
        self.state = "backoff"

    def retry(self) -> None:
        self.state, self.failures, self.retry_at = "ok", 0, 0.0


class SafeClient:
    def __init__(self, base_url: str, key: str, client: httpx.AsyncClient, breaker: Breaker):
        self.base_url, self.key, self.client, self.breaker = (
            base_url.rstrip("/"),
            key,
            client,
            breaker,
        )
        self.lock = asyncio.Lock()

    async def request(self, method: str, path: str, *, payload: dict | None = None) -> Any:
        if (method, path) not in ALLOWED:
            raise ValueError("AI management request is outside the read allowlist")
        if path == "/v0/management/api-call" and not allowed_call(payload):
            raise ValueError("AI active call is outside the recipe allowlist")
        async with self.lock:
            self.breaker.before(time.monotonic())
            response = await self.client.request(
                method,
                self.base_url + path,
                json=payload,
                headers={"Authorization": "Bearer " + self.key},
                follow_redirects=False,
            )
            # api-call wraps the recipe's HTTP status in its response body.
            if path != "/v0/management/api-call" or not response.is_success:
                self.breaker.response(response.status_code, time.monotonic())
            response.raise_for_status()
            if path == "/v0/management/api-call":
                result = ApiCallResponse.model_validate(response.json())
                self.breaker.response(result.status_code, time.monotonic())
                if result.status_code < 200 or result.status_code >= 300:
                    raise RuntimeError(f"AI recipe returned HTTP {result.status_code}")
                return result
            return response.json()
