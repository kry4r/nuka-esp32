# SPDX-License-Identifier: AGPL-3.0-or-later
import pytest

from nuka_hub.api.zones import permitted, zone_for
from nuka_hub.core.config import Security


@pytest.mark.parametrize(
    "address,headers,expected",
    [
        ("127.0.0.1", {}, "local"),
        ("::1", {}, "local"),
        ("127.0.0.1", {"cf-ray": "fixture"}, "public"),
        ("127.0.0.1", {"cf-connecting-ip": "192.0.2.1"}, "public"),
        ("127.0.0.1", {"x-forwarded-for": "192.0.2.1"}, "public"),
        ("192.0.2.1", {}, "public"),
        ("192.0.2.1", {"x-forwarded-for": "127.0.0.1"}, "public"),
    ],
)
def test_zone(address, headers, expected):
    assert zone_for(address, headers, Security()) == expected


@pytest.mark.parametrize(
    "path,method,local,lan,public",
    [
        ("/", "GET", True, True, True),
        ("/api/v1/state", "GET", True, True, True),
        ("/api/v1/ai/refresh", "POST", True, True, True),
        ("/api/v1/perm/r_example", "POST", True, True, False),
        ("/hook/claude", "POST", True, True, False),
        ("/hook/codex", "POST", True, True, False),
        ("/hook/gemini", "POST", True, True, False),
        ("/hook/server", "GET", True, True, False),
        ("/hook/server", "POST", True, True, False),
        ("/api/v1/notify", "POST", True, True, True),
        ("/ws/device", "GET", True, True, True),
        ("/ws/agent", "GET", True, True, True),
        ("/api/v1/config/raw", "GET", True, True, False),
        ("/api/v1/config/raw", "PUT", True, True, False),
        ("/api/v1/config", "PUT", True, True, False),
        ("/api/v1/config/schema", "GET", True, True, False),
        ("/api/v1/assistant/status", "GET", True, True, False),
        ("/api/v1/assistant/chat", "POST", True, True, False),
        ("/api/v1/assistant/history", "DELETE", True, True, False),
        ("/api/v1/assistant/files", "POST", True, True, False),
        ("/api/v1/usb/devices", "GET", True, True, False),
        ("/api/v1/secrets", "GET", True, True, False),
        ("/api/v1/secrets/example", "PUT", True, True, False),
        ("/api/v1/secrets/example", "DELETE", True, True, False),
        ("/api/v1/pair/pending", "GET", True, True, False),
        ("/api/v1/pair/confirm", "POST", True, True, False),
        ("/api/v1/health", "GET", True, True, True),
    ],
)
def test_access_matrix(path, method, local, lan, public):
    config = Security(allow_lan_hooks=True, web_approve_permissions=True)
    for zone, expected in (("local", local), ("lan", lan), ("public", public)):
        assert permitted(path, method, zone, config) is expected


def test_default_hooks_and_approval_closed():
    config = Security()
    assert not permitted("/hook/claude", "POST", "lan", config)
    assert not permitted("/api/v1/perm/r_example", "POST", "local", config)


def test_trusted_proxy_and_lan():
    config = Security(lan_cidrs=["192.0.2.0/24"], trusted_proxies=["198.51.100.0/24"])
    assert zone_for("198.51.100.1", {"x-forwarded-for": "192.0.2.2"}, config) == "lan"
    assert zone_for("203.0.113.1", {"x-forwarded-for": "192.0.2.2"}, config) == "public"


async def test_http_public_guard_precedes_auth_and_csrf(tmp_path):
    import httpx

    from nuka_hub.api.app import create_app
    from nuka_hub.core.config import Config
    from nuka_hub.core.runtime import Runtime

    config = Config()
    config.hub.data_dir = str(tmp_path)
    config.security.web_approve_permissions = True
    runtime = Runtime(config, tmp_path / "config.toml", demo=True)
    token = await runtime.tokens.create("admin", "fixture")
    transport = httpx.ASGITransport(app=create_app(runtime), client=("127.0.0.1", 12345))
    try:
        async with httpx.AsyncClient(
            transport=transport, base_url="http://hub.example.com"
        ) as client:
            for path in (
                "/hook/claude",
                "/hook/server",
                "/api/v1/perm/r_example",
                "/api/v1/config/raw",
                "/api/v1/secrets",
                "/api/v1/pair/pending",
            ):
                response = await client.get(
                    path, headers={"Cf-Ray": "fixture", "Authorization": "Bearer " + token["token"]}
                )
                assert response.status_code == 404
                assert response.json()["error"]["code"] == "not_found"
            login = await client.post("/api/v1/login", json={"token": token["token"]})
            assert login.status_code == 200
            assert "HttpOnly" in login.headers["set-cookie"]
            assert "SameSite=strict" in login.headers["set-cookie"]
            assert (await client.post("/api/v1/logout")).status_code == 403
            assert (
                await client.post("/api/v1/logout", headers={"X-CSRF-Token": login.json()["csrf"]})
            ).status_code == 200
    finally:
        await runtime.stop()


async def test_invalid_trusted_forwarded_address_is_bad_request(tmp_path):
    import httpx

    from nuka_hub.api.app import create_app
    from nuka_hub.core.config import Config
    from nuka_hub.core.runtime import Runtime

    config = Config()
    config.hub.data_dir = str(tmp_path)
    config.security.trusted_proxies = ["198.51.100.0/24"]
    runtime = Runtime(config, tmp_path / "config.toml", demo=True)
    try:
        transport = httpx.ASGITransport(app=create_app(runtime), client=("198.51.100.1", 12345))
        async with httpx.AsyncClient(
            transport=transport, base_url="http://hub.example.com"
        ) as client:
            response = await client.get("/api/v1/health", headers={"X-Forwarded-For": "invalid"})
            assert response.status_code == 400
    finally:
        await runtime.stop()


async def test_remote_management_requires_verified_access_session_and_csrf(tmp_path, monkeypatch):
    import time
    from types import SimpleNamespace

    import httpx
    import jwt
    from cryptography.hazmat.primitives.asymmetric import rsa

    from nuka_hub.api.app import create_app
    from nuka_hub.core.config import Config
    from nuka_hub.core.runtime import Runtime

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    monkeypatch.setattr(
        jwt.PyJWKClient,
        "get_signing_key_from_jwt",
        lambda self, token: SimpleNamespace(key=key.public_key()),
    )
    config = Config.model_validate(
        {
            "hub": {"data_dir": str(tmp_path)},
            "security": {
                "remote_admin": True,
                "web_approve_permissions": True,
                "cf_access": {
                    "enabled": True,
                    "team_domain": "<TEAM>.cloudflareaccess.com",
                    "aud": "fixture-aud",
                },
            },
        }
    )
    assertion = jwt.encode(
        {
            "aud": "fixture-aud",
            "iss": "https://<TEAM>.cloudflareaccess.com",
            "exp": int(time.time()) + 60,
        },
        key,
        algorithm="RS256",
    )
    initial = Config()
    initial.hub.data_dir = str(tmp_path)
    runtime = Runtime(initial, tmp_path / "config.toml", demo=True)
    app = create_app(runtime)
    runtime.config = config
    admin = await runtime.tokens.create("admin", "fixture")
    headers = {"Cf-Ray": "fixture", "Cf-Access-Jwt-Assertion": assertion}
    try:
        async with httpx.AsyncClient(
            transport=httpx.ASGITransport(app=app, client=("127.0.0.1", 12345)),
            base_url="https://hub.example.com",
            headers=headers,
        ) as client:
            assert (
                await client.get(
                    "/api/v1/assistant/status",
                    headers={"Authorization": "Bearer " + admin["token"]},
                )
            ).status_code == 404
            login = await client.post("/api/v1/login", json={"token": admin["token"]})
            assert login.status_code == 200
            assert (await client.get("/api/v1/assistant/status")).status_code == 200
            for expiry in (None, int(time.time()) - 1):
                claims = {"aud": "fixture-aud", "iss": "https://<TEAM>.cloudflareaccess.com"}
                if expiry is not None:
                    claims["exp"] = expiry
                invalid = jwt.encode(claims, key, algorithm="RS256")
                assert (
                    await client.get(
                        "/api/v1/assistant/status", headers={"Cf-Access-Jwt-Assertion": invalid}
                    )
                ).status_code == 404
            assert (
                await client.get(
                    "/api/v1/assistant/status",
                    headers={"Cf-Access-Jwt-Assertion": "invalid-fixture"},
                )
            ).status_code == 404
            body = {"dev_id": "nuka_example", "text": "fixture"}
            assert (await client.post("/api/v1/assistant/chat", json=body)).status_code == 403
            assert (
                await client.post(
                    "/api/v1/assistant/chat",
                    json=body,
                    headers={"X-CSRF-Token": login.json()["csrf"]},
                )
            ).status_code == 503
            for path in (
                "/hook/claude",
                "/approve",
                "/api/v1/perm/r_fixture",
                "/api/v1/pair/pending",
            ):
                assert (await client.get(path)).status_code == 404
    finally:
        await runtime.stop()
