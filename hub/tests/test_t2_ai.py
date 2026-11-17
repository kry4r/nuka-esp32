# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
from pathlib import Path

import httpx
import pytest

from nuka_hub.plugins.ai.breaker import AuthFailed, Breaker, CircuitOpen, SafeClient
from nuka_hub.plugins.ai.recipes import render


@pytest.mark.parametrize("status", [401, 403])
async def test_t2_other_provider_secret_keeps_auth_failed(status, tmp_path):
    from nuka_hub.core.config import Config
    from nuka_hub.core.runtime import Runtime

    calls = []
    fixture = json.loads((Path(__file__).parent / "fixtures/cpa.json").read_text())

    def respond(request):
        calls.append(request.url.host)
        return (
            httpx.Response(status)
            if request.url.host == "blocked.example.com"
            else httpx.Response(200, json=fixture)
        )

    config = Config.model_validate(
        {
            "hub": {"name": "example-hub", "timezone": "Etc/UTC", "data_dir": str(tmp_path)},
            "mdns": {"enabled": False},
            "collectors": {"enabled": []},
            "media": {"enabled": False},
            "ai": {
                "provider": [
                    {
                        "name": host,
                        "type": "cpa",
                        "base_url": f"https://{host}.example.com",
                        "key_secret": f"t2-isolation-{host}",
                        "passive_interval": 0.05,
                        "active_enabled": False,
                    }
                    for host in ("blocked", "other")
                ]
            },
        }
    )
    runtime = Runtime(config, tmp_path / "config.toml")
    await runtime.client.aclose()
    runtime.client = httpx.AsyncClient(transport=httpx.MockTransport(respond))
    for host in ("blocked", "other"):
        runtime.secrets.set(f"t2-isolation-{host}", "<YOUR_TOKEN>", file=True)
    try:
        await runtime.start()
        async with asyncio.timeout(5):
            while (
                calls.count("blocked.example.com") != 1 or not runtime.state.get("ai")["providers"]
            ):
                await asyncio.sleep(0.01)
        blocked, other = runtime.ai_plugins
        blocked_task = runtime.scheduler.tasks["ai:blocked"]
        await runtime.secret_changed("t2-isolation-other")
        await asyncio.sleep(0.2)
        assert runtime.ai_plugins[0] is blocked
        assert runtime.ai_plugins[1] is not other
        assert runtime.scheduler.tasks["ai:blocked"] is blocked_task
        assert blocked.breaker.state == "auth_failed"
        assert calls.count("blocked.example.com") == 1
        runtime.secrets.path.write_text("", encoding="utf-8")
        await runtime.secret_changed("t2-isolation-blocked")
        async with asyncio.timeout(5):
            while (
                next(
                    item["health"]
                    for item in runtime.state.get("ai")["providers"]
                    if item["name"] == "blocked"
                )
                != "disabled"
            ):
                await asyncio.sleep(0.01)
        assert calls.count("blocked.example.com") == 1
    finally:
        await runtime.stop()


@pytest.mark.parametrize("status", [401, 403])
@pytest.mark.parametrize("wrapped", [False, True])
async def test_t2_auth_failure_stops_all_requests(status, wrapped):
    calls = []

    def respond(request):
        calls.append(request)
        return (
            httpx.Response(200, json={"status_code": status, "body": "{}"})
            if wrapped
            else httpx.Response(status)
        )

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        breaker = Breaker(20)
        safe = SafeClient("https://cpa.example.com", "<YOUR_TOKEN>", client, breaker)
        method, path = (
            ("POST", "/v0/management/api-call") if wrapped else ("GET", "/v0/management/auth-files")
        )
        options = (
            {"payload": render("codex", "example-index", "example-account")} if wrapped else {}
        )
        results = await asyncio.gather(
            *(safe.request(method, path, **options) for _ in range(8)),
            return_exceptions=True,
        )
        assert len(calls) == 1
        assert isinstance(results[0], AuthFailed)
        assert all(isinstance(error, CircuitOpen) for error in results[1:])
        assert breaker.state == "auth_failed"
        breaker.retry()
        with pytest.raises(AuthFailed):
            await safe.request(method, path, **options)
        assert len(calls) == 2


@pytest.mark.parametrize(
    "method,path",
    [
        ("GET", "/v0/management/usage-queue"),
        ("DELETE", "/v0/management/auth-files"),
        ("POST", "/v0/management/quota-snapshots"),
        ("PUT", "/v0/management/config"),
        ("GET", "https://other.example.com/v0/management/auth-files"),
    ],
)
async def test_t2_management_allowlist(method, path):
    calls = []
    async with httpx.AsyncClient(
        transport=httpx.MockTransport(lambda request: calls.append(request))
    ) as client:
        safe = SafeClient("https://cpa.example.com", "<YOUR_TOKEN>", client, Breaker(20))
        with pytest.raises(ValueError):
            await safe.request(method, path)
    assert not calls


async def test_t2_recipe_url_allowlist():
    calls = []
    payload = render("codex", "example-index", "example-account")
    payload["url"] = "https://other.example.com/usage"
    async with httpx.AsyncClient(
        transport=httpx.MockTransport(lambda request: calls.append(request))
    ) as client:
        safe = SafeClient("https://cpa.example.com", "<YOUR_TOKEN>", client, Breaker(20))
        with pytest.raises(ValueError):
            await safe.request("POST", "/v0/management/api-call", payload=payload)
    assert not calls


def test_t2_backoff_and_request_budget():
    breaker = Breaker(2)
    for failures, delay in enumerate((1, 2, 4, 8), 1):
        breaker.response(429, 100)
        assert breaker.retry_at == 100 + delay
        assert breaker.failures == failures
    for _ in range(20):
        breaker.backoff(100)
    assert breaker.retry_at == 1900
    breaker.retry()
    breaker.before(2000)
    breaker.before(2001)
    with pytest.raises(CircuitOpen):
        breaker.before(2002)
