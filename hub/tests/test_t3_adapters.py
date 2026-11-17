# SPDX-License-Identifier: AGPL-3.0-or-later
import json
from datetime import datetime, time
from pathlib import Path
from zoneinfo import ZoneInfo

import httpx
import pytest

from nuka_hub.core.config import Ai, Collectors, Provider, Source
from nuka_hub.core.config import Weather as WeatherConfig
from nuka_hub.plugins.ai import build_providers
from nuka_hub.plugins.ai.models import account_id
from nuka_hub.plugins.ai.recipes import allowed_call, render
from nuka_hub.plugins.collectors import parse_lhm, parse_macmon
from nuka_hub.plugins.servers import (
    Beszel,
    GenericJson,
    Netdata,
    NodeExporter,
    ServerStatus,
    UptimeKuma,
)
from nuka_hub.plugins.weather import Weather

FIXTURES = Path(__file__).with_name("fixtures")


class Secrets:
    def get(self, name):
        return "token:<YOUR_TOKEN>" if name == "servers.beszel" else "<YOUR_TOKEN>"


def fixture(name):
    return json.loads((FIXTURES / (name + ".json")).read_text(encoding="utf-8"))


@pytest.mark.parametrize(
    "kind,adapter",
    [
        ("serverstatus", ServerStatus),
        ("uptime_kuma", UptimeKuma),
        ("beszel", Beszel),
        ("netdata", Netdata),
        ("node_exporter", NodeExporter),
        ("generic_json", GenericJson),
    ],
)
async def test_t3_server_fixture(kind, adapter):
    data = fixture(kind)
    calls = []
    options = {"slug": "example"} if kind == "uptime_kuma" else {}
    if kind == "generic_json":
        options = {
            "items": "$.data.hosts[*]",
            "fields": {
                "name": "$.hostname",
                "online": "$.up",
                "cpu_pct": "$.cpu",
                "mem_pct": {"path": "$.mem_ratio", "scale": 100},
                "disk_pct": "$.disk",
            },
        }
    config = Source(
        type=kind,
        name="example",
        url="https://monitor.example.com",
        options=options,
        auth_secret="servers.beszel" if kind == "beszel" else "",
    )

    def respond(request):
        calls.append(request)
        if kind == "uptime_kuma":
            payload = data["heartbeats"] if "/heartbeat/" in request.url.path else data["page"]
        elif kind == "netdata":
            payload = (
                data["info"]
                if request.url.path.endswith("/info")
                else data[request.url.params["chart"]]
            )
        elif kind == "node_exporter":
            text = (
                data["metrics"]
                if len(calls) == 1
                else data["metrics"]
                .replace('idle"} 100', 'idle"} 120')
                .replace('user"} 100', 'user"} 180')
            )
            return httpx.Response(200, text=text)
        else:
            payload = data
        return httpx.Response(200, json=payload)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        source = adapter(config, Secrets(), client)
        assert (await source.probe()).health == "ok"
        result = await source.sample()
        assert len(result) == 1 and result[0]["online"]
        assert result[0]["src"] == kind and result[0]["alerts"] == []
        if kind == "node_exporter":
            assert "cpu_pct" not in result[0]
            assert (await source.sample())[0]["cpu_pct"] == 80.0
        elif kind == "uptime_kuma":
            assert result[0]["latency_ms"] == 21.5
            assert result[0]["uptime_pct"] == 99.9
            assert "cpu_pct" not in result[0]
        else:
            assert result[0]["cpu_pct"] == 12.3
        if kind in {"beszel", "generic_json"}:
            assert result[0]["mem_pct"] == 50
            assert "mem_total_b" not in result[0]
        if kind == "serverstatus":
            assert result[0]["mem_used_b"] == 1024**3
            assert result[0]["disk_used_b"] == 10 * 1024**3
            assert result[0]["uptime_s"] == 3600


def test_t3_lhm_fixture():
    data = fixture("lhm")
    assert parse_lhm(data, [])["cpu"]["temp_c"] == 54
    config = Collectors.model_validate(
        {"lhm": {"map": [{"field": "cpu.temp_c", "sensor_id": "/intelcpu/0/temperature/1"}]}}
    )
    result = parse_lhm(data, config.lhm.map)
    assert result["cpu"]["temp_c"] == 56
    assert result["gpus"][0]["pct"] == 12


def test_t3_macmon_fixture():
    result = parse_macmon(fixture("macmon"))
    assert result["cpu"]["power_w"] == 8.4
    assert result["gpus"][0]["pct"] == 12.5
    assert result["gpus"][0]["clock_mhz"] == 461


async def test_t3_cpa_l1_fixture():
    config = Ai(
        provider=[
            Provider(
                type="cpa",
                name="example",
                base_url="https://cpa.example.com",
                key_secret="ai.example",
                active_enabled=False,
            )
        ]
    )
    async with httpx.AsyncClient(
        transport=httpx.MockTransport(lambda request: httpx.Response(200, json=fixture("cpa")))
    ) as client:
        result = await build_providers(config, Secrets(), client, ZoneInfo("UTC"))[0].sample()
    account = result["accounts"][0]
    assert account["label"] == "a***@example.com"
    assert account["windows"][0]["used_pct"] == 42.5
    assert "email" not in account and "auth_index" not in account


async def test_t3_cpamp_l2_fixture():
    config = Ai(
        provider=[
            Provider(
                type="cpamp",
                name="example",
                base_url="https://cpamp.example.com",
                key_secret="ai.example",
                active_enabled=False,
            )
        ]
    )
    data = fixture("cpamp")
    identity = account_id("example", "example-index")
    data["snapshots"]["items"][0]["row_key"] = identity
    shanghai = ZoneInfo("Asia/Shanghai")
    day = datetime.now(shanghai).date()
    data["usage"] = {
        "total_requests": 2,
        "apis": {
            "POST /v1/example": {
                "models": {
                    "example-model": {
                        "details": [
                            {
                                "timestamp": datetime.combine(
                                    day, time(hour, 30), tzinfo=shanghai
                                ).isoformat(),
                                "failed": False,
                                "tokens": {"total_tokens": 100},
                            }
                            for hour in (0, 23)
                        ]
                    }
                }
            }
        },
    }

    def respond(request):
        payload = (
            fixture("cpa")
            if request.url.path.endswith("auth-files")
            else data[
                "snapshots"
                if request.url.path.endswith("query")
                else "headers"
                if request.url.path.endswith("header-snapshots")
                else "usage"
            ]
        )
        return httpx.Response(200, json=payload)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        result = await build_providers(config, Secrets(), client, ZoneInfo("Asia/Shanghai"))[
            0
        ].sample()
    assert result["accounts"][0]["windows"][0]["source"] == "snapshot"
    assert result["accounts"][0]["windows"][0]["used_pct"] == 42.5
    # Both timestamps are the same Shanghai day, while their UTC dates differ.
    assert result["summary"] == {"requests": 2, "tokens": 200, "failures": 0}


@pytest.mark.parametrize(
    "kind,expected", [("codex", 42.5), ("claude", 42.5), ("antigravity", 25.0)]
)
async def test_t3_l3_fixture(kind, expected):
    calls = []
    config = Ai(
        provider=[
            Provider(
                type="cpa",
                name="example",
                base_url="https://cpa.example.com",
                key_secret="ai.example",
            )
        ]
    )
    auth = fixture("cpa")
    auth["files"][0].update(provider=kind, quota={"signals": {}}, project_id="example-project")

    def respond(request):
        calls.append(request)
        if request.url.path.endswith("auth-files"):
            return httpx.Response(200, json=auth)
        payload = json.loads(request.content)
        assert allowed_call(payload)
        return httpx.Response(200, json={"status_code": 200, "body": json.dumps(fixture(kind))})

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        provider = build_providers(config, Secrets(), client, ZoneInfo("UTC"))[0]
        result = await provider.sample()
        assert result["accounts"][0]["windows"][0]["used_pct"] == expected
        before = len(calls)
        await provider.refresh()
        assert len(calls) == before
    assert (
        render(kind, "example-index", "example-account", "example-project")["header"][
            "Authorization"
        ]
        == "Bearer $TOKEN$"
    )


async def test_t3_weather_fixture():
    config = WeatherConfig(enabled=True, lat=0, lon=0)
    async with httpx.AsyncClient(
        transport=httpx.MockTransport(lambda request: httpx.Response(200, json=fixture("weather")))
    ) as client:
        result = await Weather(config, client).sample()
    assert result["now"]["icon"] == "clear_night"
    assert result["now"]["temp_c"] == 18.5
    assert len(result["days"]) == 3 and result["days"][0]["icon"] == "rain"
    assert result["updated"].endswith("Z")
