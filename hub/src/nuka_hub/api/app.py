# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import html
import json
import secrets
import shutil
import time
from collections import defaultdict, deque
from contextlib import asynccontextmanager
from importlib.metadata import metadata
from importlib.resources import files
from typing import Literal

import jwt
from fastapi import Body, FastAPI, HTTPException, Query, Request, WebSocket
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, Response
from keyring.errors import NoKeyringError
from pydantic import BaseModel, ConfigDict, Field
from starlette.exceptions import HTTPException as StarletteHTTPException

from nuka_hub import __version__
from nuka_hub.assistant.service import AssistantError
from nuka_hub.assistant.storage import MissingResource, UploadError, UploadTooLarge
from nuka_hub.core.agents import PermissionGone
from nuka_hub.core.config import Config, ConfigError, parse_config
from nuka_hub.core.config_file import structured_toml
from nuka_hub.core.history import iso

from .assistant import assistant_router
from .ws_device import DeviceHub
from .zones import permitted, remote_management_path, zone_for


class Input(BaseModel):
    model_config = ConfigDict(extra="ignore")


class Login(Input):
    token: str


class Decide(Input):
    behavior: Literal["allow", "deny"]


class Notify(Input):
    title: str = Field(max_length=120)
    body: str | None = Field(default=None, max_length=2000)
    level: Literal["info", "warn", "critical"] = "info"
    source: str = "notify"
    dedupe_key: str | None = None
    ttl_s: int = Field(default=86400, ge=0)
    server: str | None = None


class TokenCreate(Input):
    name: str
    scope: Literal["admin", "device", "agent", "hook", "notify"]


class PairConfirm(Input):
    dev_id: str = Field(pattern=r"^[a-z0-9_-]{1,32}$")
    code: str = Field(pattern=r"^\d{6}$")
    name: str


class RawConfig(Input):
    toml: str
    mtime: str


class StructuredConfig(BaseModel):
    model_config = ConfigDict(extra="forbid")
    config: dict
    mtime: str


class OtaOffer(Input):
    ver: str
    kind: Literal["app", "assets"]
    dev_ids: list[str] = Field(min_length=1)


class ClaudeHook(Input):
    session_id: str
    hook_event_name: Literal[
        "SessionStart",
        "UserPromptSubmit",
        "PreToolUse",
        "PostToolUse",
        "PermissionRequest",
        "Notification",
        "Stop",
        "StopFailure",
        "SessionEnd",
    ]
    cwd: str | None = None
    tool_name: str | None = None
    tool_input: dict | None = None
    message: str | None = None
    last_assistant_message: str | None = None


class CliHook(Input):
    event: str
    host: str
    payload: dict


def error_response(status: int, code: str, message: str, details=None):
    value = {"code": code, "message": message}
    if details is not None:
        value["details"] = details
    return JSONResponse({"error": value}, status_code=status)


class RateLimit:
    def __init__(self):
        self.events = defaultdict(deque)

    def allow(self, key: str, count: int, seconds: float = 60) -> bool:
        now = time.monotonic()
        bucket = self.events[key]
        while bucket and bucket[0] <= now - seconds:
            bucket.popleft()
        if len(bucket) >= count:
            return False
        bucket.append(now)
        return True


def create_app(runtime) -> FastAPI:
    @asynccontextmanager
    async def lifespan(app):
        runtime.devices = DeviceHub(runtime)
        await runtime.start()
        yield
        await runtime.stop()

    app = FastAPI(title="nuka-hub", version=__version__, lifespan=lifespan)
    app.state.runtime = runtime
    rate = RateLimit()
    jwks = None
    access_domain = ""

    @app.exception_handler(StarletteHTTPException)
    async def http_error(request, error):
        return error_response(
            error.status_code,
            {
                400: "bad_request",
                401: "unauthorized",
                403: "forbidden",
                404: "not_found",
                409: "conflict",
                413: "too_large",
                429: "rate_limited",
                502: "upstream_error",
                503: "unavailable",
            }.get(error.status_code, "internal"),
            str(error.detail),
        )

    @app.exception_handler(RequestValidationError)
    async def validation_error(request, error):
        details = [
            {"line": 1, "msg": ".".join(str(x) for x in item["loc"]) + ": " + item["msg"]}
            for item in error.errors()
        ]
        return error_response(400, "bad_request", "Invalid request", details)

    @app.exception_handler(ConfigError)
    async def config_error(request, error):
        return error_response(400, "bad_request", "Invalid configuration", error.details)

    @app.exception_handler(PermissionGone)
    async def permission_error(request, error):
        return error_response(409, "perm_gone", "Permission request expired")

    @app.exception_handler(json.JSONDecodeError)
    async def json_error(request, error):
        return error_response(400, "bad_request", "Invalid JSON")

    @app.exception_handler(MissingResource)
    async def missing_resource(request, error):
        return error_response(404, "not_found", "Not found")

    @app.exception_handler(UploadTooLarge)
    async def upload_too_large(request, error):
        return error_response(413, "too_large", str(error))

    @app.exception_handler(UploadError)
    async def invalid_upload(request, error):
        return error_response(400, "bad_request", str(error))

    @app.exception_handler(AssistantError)
    async def assistant_error(request, error):
        return error_response(
            error.status,
            {400: "bad_request", 409: "conflict", 503: "unavailable"}[error.status],
            str(error),
        )

    @app.exception_handler(Exception)
    async def internal_error(request, error):
        return error_response(500, "internal", type(error).__name__)

    async def access(headers, zone: str):
        nonlocal jwks, access_domain
        config = runtime.config.security.cf_access
        if zone != "public" or not config.enabled:
            return True
        if jwks is None or access_domain != config.team_domain:
            access_domain = config.team_domain
            jwks = jwt.PyJWKClient("https://" + access_domain + "/cdn-cgi/access/certs")
        token = headers.get("cf-access-jwt-assertion", "")
        try:
            key = await asyncio.to_thread(jwks.get_signing_key_from_jwt, token)
            jwt.decode(
                token,
                key.key,
                algorithms=["RS256"],
                audience=config.aud,
                issuer="https://" + config.team_domain,
                options={"require": ["exp", "aud", "iss"]},
            )
            return True
        except jwt.PyJWTError:
            return False

    @app.middleware("http")
    async def security(request: Request, call_next):
        path, method = request.url.path, request.method
        try:
            zone = zone_for(request.client.host, request.headers, runtime.config.security)
        except ValueError:
            return error_response(400, "bad_request", "Invalid forwarded address")
        request.state.zone = zone
        remote_candidate = (
            zone == "public"
            and remote_management_path(path)
            and runtime.config.security.remote_admin
            and runtime.config.security.cf_access.enabled
        )
        if not remote_candidate and not permitted(path, method, zone, runtime.config.security):
            return error_response(404, "not_found", "Not found")
        access_verified = path == "/api/v1/health" or await access(request.headers, zone)
        if not access_verified:
            return (
                error_response(404, "not_found", "Not found")
                if remote_candidate
                else error_response(403, "forbidden", "Cloudflare Access assertion required")
            )
        if (
            path
            in {
                "/api/v1/health",
                "/api/v1/login",
                "/",
                "/app.js",
                "/assistant.js",
                "/forms.js",
                "/style.css",
                "/theme.css",
            }
            or path.startswith("/docs")
            or path == "/openapi.json"
        ):
            return await call_next(request)
        cookie = request.cookies.get("nuka_session", "")
        session = await runtime.tokens.get_session(cookie)
        if remote_candidate and not permitted(
            path,
            method,
            zone,
            runtime.config.security,
            remote_verified=access_verified and session is not None,
        ):
            return error_response(404, "not_found", "Not found")
        request.state.csrf = session["csrf"] if session else ""
        request.state.auth = session
        if session:
            if method not in {"GET", "HEAD", "OPTIONS"} and not secrets.compare_digest(
                request.headers.get("x-csrf-token", ""), session["csrf"]
            ):
                return error_response(403, "forbidden", "CSRF token required")
        else:
            optional_hook = (
                path in {"/hook/claude", "/hook/codex", "/hook/gemini"} and zone == "local"
            )
            scopes = (
                {"notify", "admin"}
                if path in {"/api/v1/notify", "/hook/server"}
                else {"hook"}
                if path.startswith("/hook/")
                else {"device"}
                if path.startswith("/api/v1/ota/") and method == "GET"
                else {"device", "admin"}
                if path.startswith("/api/v1/media/cover/")
                else {"admin"}
            )
            bearer = request.headers.get("authorization", "").removeprefix("Bearer ")
            if path == "/hook/server" and not bearer:
                bearer = request.query_params.get("token", "")
            authenticated = await runtime.tokens.authenticate(bearer, scopes) if bearer else None
            if authenticated is None and not optional_hook:
                return error_response(401, "unauthorized", "Authentication required")
            request.state.auth = authenticated
        return await call_next(request)

    @app.get("/api/v1/health")
    async def health():
        return {"ok": True, "ver": __version__, "proto": 1}

    @app.post("/api/v1/login")
    async def login(value: Login, request: Request):
        if not rate.allow("login:" + request.client.host, 5):
            raise HTTPException(429, "Five login attempts per minute")
        token = await runtime.tokens.authenticate(value.token, "admin")
        if token is None:
            raise HTTPException(401, "Invalid token")
        cookie, csrf = runtime.tokens.session(token["id"], runtime.config.security.session_days)
        response = JSONResponse({"csrf": csrf})
        response.set_cookie(
            "nuka_session",
            cookie,
            max_age=runtime.config.security.session_days * 86400,
            httponly=True,
            samesite="strict",
            secure=request.url.scheme == "https" or request.state.zone == "public",
        )
        return response

    @app.post("/api/v1/logout")
    async def logout(request: Request):
        runtime.tokens.logout(request.cookies.get("nuka_session", ""))
        response = JSONResponse({})
        response.delete_cookie("nuka_session")
        return response

    @app.get("/api/v1/me")
    async def me(request: Request):
        return {
            "zone": request.state.zone,
            "csrf": request.state.csrf,
            "features": runtime.features(),
            "lang": runtime.config.hub.lang,
        }

    @app.get("/api/v1/state")
    async def state():
        value = runtime.state.snapshot()
        return {k: v for k, v in value.items() if k != "pet"}

    def paginate(items, limit, before, key):
        start = (
            next((i + 1 for i, item in enumerate(items) if item[key] == before), 0) if before else 0
        )
        return {"items": items[start : start + limit]}

    @app.get("/api/v1/hosts")
    async def hosts(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(runtime.state.get("hosts"), limit, before, "id")

    @app.get("/api/v1/servers")
    async def servers(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(runtime.state.get("servers"), limit, before, "id")

    @app.get("/api/v1/hosts/{identity}/history")
    async def host_history(
        identity: str, keys: str = "cpu.pct", span: Literal["1h", "24h", "30d"] = "24h"
    ):
        return await runtime.history.query("hosts", identity, keys.split(","), span)

    @app.get("/api/v1/servers/{identity}/history")
    async def server_history(
        identity: str, keys: str = "cpu.pct", span: Literal["1h", "24h", "30d"] = "24h"
    ):
        return await runtime.history.query("servers", identity, keys.split(","), span)

    @app.get("/api/v1/ai")
    async def ai():
        return runtime.state.get("ai")

    @app.post("/api/v1/ai/refresh", status_code=202)
    async def ai_refresh():
        if time.monotonic() - runtime.last_ai_refresh < 60:
            raise HTTPException(429, "One refresh per minute")
        runtime.last_ai_refresh = time.monotonic()
        for provider in runtime.ai_plugins:
            runtime.scheduler.request(provider, "refresh")
        return {}

    @app.post("/api/v1/ai/providers/{name}/retry")
    async def ai_retry(name: str):
        provider = next((item for item in runtime.ai_plugins if item.name == name), None)
        if provider is None:
            raise HTTPException(404, "Provider not found")
        runtime.scheduler.request(provider, "retry")
        return {}

    @app.get("/api/v1/agents")
    async def agents():
        return runtime.state.get("agents")

    @app.post("/api/v1/perm/{rid}")
    async def decide(rid: str, value: Decide):
        runtime.agents.decide(rid, value.behavior, "web")
        return {}

    @app.get("/api/v1/audit")
    async def audit(kind: str = "", limit: int = Query(default=100, ge=1), before: str = ""):
        rows = await runtime.history.sql("SELECT * FROM audit ORDER BY ts DESC")
        items = []
        for row in rows:
            if kind and row["kind"] != kind:
                continue
            items.append(
                {
                    "id": row["id"],
                    "kind": row["kind"],
                    "ts": iso(row["ts"]),
                    **json.loads(row["detail"]),
                }
            )
        return paginate(items, limit, before, "id")

    @app.get("/api/v1/notifications")
    async def notifications(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(runtime.state.get("notify"), limit, before, "nid")

    async def notify_input(value, request):
        auth = request.state.auth
        key = auth["id"] if auth and "id" in auth else request.client.host
        config = runtime.config.notify
        if not rate.allow("notify:" + key, config.hook_rate_per_min) or not rate.allow(
            "burst:" + key, config.hook_burst, 1
        ):
            return JSONResponse(
                {"error": {"code": "rate_limited", "message": "Notification rate exceeded"}},
                status_code=429,
                headers={"Retry-After": "60"},
            )
        return JSONResponse({"nid": await runtime.notify(value)}, status_code=202)

    @app.post("/api/v1/notify", status_code=202)
    async def notify(value: Notify, request: Request):
        return await notify_input(value.model_dump(exclude_none=True, exclude={"server"}), request)

    @app.api_route("/hook/server", methods=["GET", "POST"])
    async def server_hook(request: Request):
        if request.method == "GET":
            value = dict(request.query_params)
            value.pop("token", None)
        elif request.headers.get("content-type", "").startswith("text/plain"):
            lines = (await request.body()).decode("utf-8").splitlines()
            value = {"title": lines[0] if lines else "", "body": "\n".join(lines[1:])}
        else:
            value = await request.json()
        from pydantic import ValidationError

        try:
            parsed = Notify.model_validate(value)
        except ValidationError as error:
            raise HTTPException(400, "Invalid notification") from error
        return await notify_input(parsed.model_dump(exclude_none=True), request)

    @app.post("/hook/claude")
    async def claude(value: ClaudeHook):
        if "claude" not in runtime.config.agents.enabled_sources:
            return {}
        payload = value.model_dump(exclude_none=True)
        if value.hook_event_name == "PermissionRequest":
            return await runtime.agents.permission(payload)
        await runtime.agents.event(
            "claude", value.hook_event_name, payload, runtime.config.hub.name
        )
        return {}

    @app.post("/hook/codex")
    async def codex(value: CliHook):
        if "codex" not in runtime.config.agents.enabled_sources:
            return {}
        await runtime.agents.event("codex", value.event, value.payload, value.host)
        return {}

    @app.post("/hook/gemini")
    async def gemini(value: CliHook):
        if "gemini" not in runtime.config.agents.enabled_sources:
            return {}
        await runtime.agents.event("gemini", value.event, value.payload, value.host)
        return {}

    @app.get("/api/v1/devices")
    async def devices(limit: int = Query(default=100, ge=1), before: str = ""):
        items = runtime.state.get("devices")
        if runtime.demo and not items:
            seed = files("nuka_hub.data").joinpath("demo.json").read_text(encoding="utf-8")
            items = json.loads(seed)["devices"]
        return paginate(items, limit, before, "dev_id")

    @app.patch("/api/v1/devices/{dev_id}")
    async def rename(dev_id: str, name: str = Body(embed=True)):
        await runtime.history.sql("UPDATE devices SET name=? WHERE dev_id=?", (name, dev_id))
        await runtime.devices.registry()
        return {}

    @app.delete("/api/v1/devices/{dev_id}")
    async def remove_device(dev_id: str):
        rows = await runtime.history.sql("SELECT token_id FROM devices WHERE dev_id=?", (dev_id,))
        if not rows:
            raise HTTPException(404, "Device not found")
        await runtime.tokens.revoke(rows[0]["token_id"])
        if dev_id in runtime.devices.connections:
            await runtime.devices.connections[dev_id].ws.close(4003)
        await runtime.history.sql("DELETE FROM devices WHERE dev_id=?", (dev_id,))
        await runtime.devices.registry()
        return {}

    @app.get("/api/v1/pair/pending")
    async def pair_pending(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(
            [
                {"dev_id": identity, "fw": item["fw"], "expires": iso(item["expires"])}
                for identity, item in runtime.pending_pairs.items()
            ],
            limit,
            before,
            "dev_id",
        )

    @app.post("/api/v1/pair/confirm")
    async def pair_confirm(value: PairConfirm):
        item = runtime.pending_pairs.get(value.dev_id)
        if item is None or item["expires"] <= time.time():
            raise HTTPException(404, "Pair request expired")
        if not secrets.compare_digest(value.code, item["code"]):
            item["attempts"] += 1
            if item["attempts"] >= 5:
                item["rejected"] = True
                item["done"].set()
            raise HTTPException(400, "Incorrect pairing code")
        token = await runtime.tokens.create("device", value.name)
        await runtime.history.sql(
            "INSERT OR REPLACE INTO devices VALUES (?,?,?,?,?,?)",
            (value.dev_id, value.name, item["fw"], iso(), None, token["id"]),
        )
        await runtime.devices.send(
            item["ws"],
            "pair.ok",
            {
                "dev_token": token["token"],
                "hub_id": runtime.hub_id,
                "hub_name": runtime.config.hub.name,
            },
        )
        item["done"].set()
        await runtime.history.audit("pair", "web", {"dev_id": value.dev_id})
        return {}

    @app.get("/api/v1/ota")
    async def ota(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(runtime.ota_manifests(), limit, before, "file")

    @app.post("/api/v1/ota/offer")
    async def offer(value: OtaOffer):
        from nuka_hub.ota import offer as send_offer

        item = next(
            (
                item
                for item in runtime.ota_manifests()
                if item["ver"] == value.ver and item["kind"] == value.kind
            ),
            None,
        )
        if item is None:
            raise HTTPException(404, "OTA version not found")
        await send_offer(runtime, item, value.dev_ids)
        return {}

    @app.get("/api/v1/ota/{filename}")
    async def download(filename: str):
        item = next((item for item in runtime.ota_manifests() if item["file"] == filename), None)
        if item is None:
            raise HTTPException(404, "OTA file not found")
        return FileResponse(
            runtime.ota_dir / item["ver"] / filename, media_type="application/octet-stream"
        )

    @app.get("/api/v1/media/cover/{digest}.jpg")
    async def cover(digest: str):
        if digest not in runtime.cover_cache:
            raise HTTPException(404, "Cover not found")
        return Response(
            runtime.cover_cache[digest],
            media_type="image/jpeg",
            headers={"Cache-Control": "private,max-age=31536000,immutable"},
        )

    @app.post("/api/v1/media/{src_id}/cmd")
    async def media_command(src_id: str, cmd: Literal["toggle", "next", "prev"] = Body(embed=True)):
        if not any(item["src_id"] == src_id for item in runtime.state.get("media")):
            raise HTTPException(404, "Media session not found")
        await runtime.media_command(src_id, cmd)
        return {}

    @app.get("/api/v1/plugins")
    async def plugins(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(runtime.state.get("plugins"), limit, before, "name")

    @app.get("/api/v1/tokens")
    async def tokens(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(await runtime.tokens.listing(), limit, before, "id")

    @app.post("/api/v1/tokens")
    async def create_token(value: TokenCreate):
        return await runtime.tokens.create(value.scope, value.name)

    @app.delete("/api/v1/tokens/{identity}")
    async def delete_token(identity: str):
        await runtime.tokens.revoke(identity)
        return {}

    @app.get("/api/v1/config")
    async def config():
        return runtime.config.model_dump(by_alias=True, exclude_none=True)

    @app.get("/api/v1/config/schema")
    async def config_schema():
        return Config.model_json_schema(by_alias=True)

    async def write_config(text: str, mtime: str):
        if not runtime.config.web.allow_config_edit:
            raise HTTPException(404, "Configuration editing disabled")
        async with runtime.config_lock:
            if mtime != iso(runtime.path.stat().st_mtime_ns / 1e9):
                raise HTTPException(409, "Configuration changed; reload before saving")
            parse_config(text)
            backup = str(runtime.path) + ".bak." + str(time.time_ns())
            await asyncio.to_thread(shutil.copy2, runtime.path, backup)
            temporary = runtime.path.with_name(runtime.path.name + ".tmp")
            try:
                await asyncio.to_thread(temporary.write_text, text, encoding="utf-8", newline="\n")
                await asyncio.to_thread(temporary.replace, runtime.path)
            finally:
                temporary.unlink(missing_ok=True)
            await runtime.reload()
        return {}

    @app.put("/api/v1/config")
    async def structured_config(value: StructuredConfig):
        original = await asyncio.to_thread(runtime.path.read_text, encoding="utf-8")
        return await write_config(structured_toml(original, value.config), value.mtime)

    @app.get("/api/v1/config/raw")
    async def config_raw():
        return {
            "toml": runtime.path.read_text(encoding="utf-8"),
            "path": str(runtime.path),
            "mtime": iso(runtime.path.stat().st_mtime_ns / 1e9),
        }

    @app.put("/api/v1/config/raw")
    async def save_config(value: RawConfig):
        return await write_config(value.toml, value.mtime)

    def secret_names():
        result = set()

        def visit(value):
            if isinstance(value, dict):
                for key, child in value.items():
                    if key.endswith("_secret") and child:
                        result.add(child)
                    else:
                        visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)

        visit(runtime.config.model_dump())
        return result

    @app.get("/api/v1/secrets")
    async def secrets_list(limit: int = Query(default=100, ge=1), before: str = ""):
        return paginate(
            await asyncio.to_thread(runtime.secrets.listing, secret_names()), limit, before, "name"
        )

    @app.put("/api/v1/secrets/{name}")
    async def set_secret(name: str, value: str = Body(embed=True)):
        if not runtime.config.web.allow_config_edit:
            raise HTTPException(404, "Configuration editing disabled")
        try:
            await asyncio.to_thread(runtime.secrets.set, name, value)
        except NoKeyringError:
            await asyncio.to_thread(runtime.secrets.set, name, value, file=True)
        await runtime.secret_changed(name)
        return {}

    @app.delete("/api/v1/secrets/{name}")
    async def delete_secret(name: str):
        if not runtime.config.web.allow_config_edit:
            raise HTTPException(404, "Configuration editing disabled")
        await asyncio.to_thread(runtime.secrets.remove, name)
        await runtime.secret_changed(name)
        return {}

    @app.websocket("/ws/device")
    async def device(ws: WebSocket):
        try:
            zone = zone_for(ws.client.host, ws.headers, runtime.config.security)
        except ValueError:
            await ws.close(1008)
            return
        if (
            zone == "public"
            and runtime.config.security.cf_access.require_for_ws
            and not await access(ws.headers, zone)
        ):
            await ws.close(1008)
            return
        await runtime.devices.connect(ws)

    @app.websocket("/ws/agent")
    async def agent(ws: WebSocket):
        from .ws_agent import agent_connection

        try:
            zone = zone_for(ws.client.host, ws.headers, runtime.config.security)
        except ValueError:
            await ws.close(1008)
            return
        if (
            zone == "public"
            and runtime.config.security.cf_access.require_for_ws
            and not await access(ws.headers, zone)
        ):
            await ws.close(1008)
            return
        await agent_connection(runtime, ws)

    @app.get("/")
    @app.get("/config")
    @app.get("/assistant")
    @app.get("/approve")
    async def web():
        if not runtime.config.web.enabled:
            raise HTTPException(404, "Web disabled")
        source = next(
            value.split(",", 1)[1].strip()
            for value in metadata("nuka-hub").get_all("Project-URL")
            if value.startswith("Source,")
        )
        text = files("nuka_hub").joinpath("web/index.html").read_text(encoding="utf-8")
        return HTMLResponse(
            text.replace("NUKA_VERSION", __version__).replace(
                "NUKA_SOURCE", html.escape(source, quote=True)
            )
        )

    app.include_router(assistant_router(runtime))

    @app.get("/{asset}")
    async def asset(asset: str):
        if asset not in {"app.js", "assistant.js", "forms.js", "style.css", "theme.css"}:
            raise HTTPException(404, "Not found")
        media_type = "text/javascript" if asset.endswith(".js") else "text/css"
        return Response(
            files("nuka_hub").joinpath("web/" + asset).read_text(encoding="utf-8"),
            media_type=media_type,
        )

    return app
