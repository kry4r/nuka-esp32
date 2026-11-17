# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import getpass
import json
import shutil
import time
import tomllib
from pathlib import Path
from zoneinfo import ZoneInfo

import httpx
import tomli_w

from .core.clock import timezone_name
from .core.config import Config, parse_config
from .core.secrets import Secrets


def ask(prompt: str, default: str = "") -> str:
    return input(prompt + (f" [{default}]" if default else "") + ": ").strip() or default


def write_config(path: Path, raw: dict) -> str:
    text = "# SPDX-License-Identifier: AGPL-3.0-or-later\n" + tomli_w.dumps(raw)
    parse_config(text, environment=False)
    if path.exists():
        shutil.copy2(path, str(path) + ".bak." + str(time.time_ns()))
    path.write_text(text, encoding="utf-8", newline="\n")
    return text


async def configure_section(text: str, section: str, path: Path) -> str:
    from .plugins.ai import build_providers
    from .plugins.ai.breaker import AuthFailed
    from .plugins.collectors import build_collectors
    from .plugins.servers import build_sources

    raw = tomllib.loads(text)
    store = Secrets(path.parent)
    async with httpx.AsyncClient(timeout=10, follow_redirects=False) as client:
        if section == "hub":
            hub = raw.setdefault("hub", {})
            hub.update(
                name=ask("Hub name", hub.get("name", "nuka-hub")),
                lang=ask("Language", hub.get("lang", "zh-CN")),
                port=int(ask("Port", str(hub.get("port", 8790)))),
            )
        elif section == "collectors":
            config = parse_config(text)
            available = []
            for plugin in await asyncio.to_thread(
                build_collectors, config.collectors, client, store
            ):
                health = await plugin.probe()
                print(json.dumps(health.__dict__, ensure_ascii=False))
                if health.health != "disabled":
                    available.append(plugin.name)
            selected = ask("Enabled collectors (comma separated)", ",".join(available))
            raw.setdefault("collectors", {})["enabled"] = selected.split(",") if selected else []
        elif section == "ai":
            kind = ask("Provider type (cpa/cpamp)", "cpa")
            name = ask("Provider name", "example")
            url = ask("Provider base URL")
            reference = "ai." + name
            await asyncio.to_thread(store.set, reference, getpass.getpass("Management key: "))
            candidate = {"type": kind, "name": name, "base_url": url, "key_secret": reference}
            config = Config.model_validate({"ai": {"provider": [candidate]}})
            provider = (
                await asyncio.to_thread(
                    build_providers,
                    config.ai,
                    store,
                    client,
                    ZoneInfo(timezone_name(config.hub.timezone)),
                )
            )[0]
            # The wizard validates once; a bad key must never be retried.
            try:
                await provider.http.request("GET", "/v0/management/auth-files")
            except AuthFailed as error:
                raise ValueError(
                    "Provider authentication failed; no retry was made. Correct the stored key."
                ) from error
            raw.setdefault("ai", {}).setdefault("provider", []).append(candidate)
            print("One auth-files request succeeded; key stored by name only.")
        elif section == "servers":
            while True:
                candidate = {
                    "type": ask("Source type", "generic_json"),
                    "name": ask("Source name", "example"),
                    "url": ask("Source URL"),
                }
                if candidate["type"] == "generic_json":
                    candidate["options"] = json.loads(
                        ask(
                            "JSONPath options JSON",
                            '{"items":"$.items[*]","fields":{"name":"$.name","online":"$.online"}}',
                        )
                    )
                if ask("Authentication secret name (empty for none)"):
                    reference = "servers." + candidate["name"]
                    candidate["auth_secret"] = reference
                    await asyncio.to_thread(
                        store.set, reference, getpass.getpass("Source credential: ")
                    )
                config = Config.model_validate({"servers": {"source": [candidate]}})
                print(
                    json.dumps(
                        await (
                            await asyncio.to_thread(build_sources, config.servers, store, client)
                        )[0].sample(),
                        ensure_ascii=False,
                    )
                )
                raw.setdefault("servers", {}).setdefault("source", []).append(candidate)
                if ask("Add another source? (y/N)", "n").lower() != "y":
                    break
        elif section == "weather":
            city = ask("City (empty to use coordinates)")
            weather = {"enabled": True, "city": city}
            if not city:
                weather.update(lat=float(ask("Latitude")), lon=float(ask("Longitude")))
            raw["weather"] = weather
        elif section == "agents":
            from .hooks import hooks

            sources = ask("CLI sources (comma separated)", "claude,codex,gemini").split(",")
            raw.setdefault("agents", {})["enabled_sources"] = sources
            for source in sources:
                if (
                    ask(
                        "Install " + source + " hooks in your user configuration? (y/N)", "n"
                    ).lower()
                    == "y"
                ):
                    hooks("install", source, Config.model_validate(raw), None)
        elif section == "public":
            raw = tunnel_config(raw, path)
    result = "# SPDX-License-Identifier: AGPL-3.0-or-later\n" + tomli_w.dumps(raw)
    parse_config(result, environment=False)
    return result


def tunnel_config(raw: dict, path: Path) -> dict:
    host = ask("Public hostname", "hub.example.com")
    name = ask("Cloudflare tunnel name", "nuka-hub")
    credential = ask("Tunnel credentials JSON path")
    port = raw.get("hub", {}).get("port", 8790)
    raw.setdefault("hub", {})["public_url"] = "https://" + host
    enabled = ask("Require Cloudflare Access? (Y/n)", "y").lower() == "y"
    access = {"enabled": enabled}
    if enabled:
        access.update(
            team_domain=ask("Access team domain", "access.example.com"),
            aud=ask("Access application AUD"),
        )
    raw.setdefault("security", {})["cf_access"] = access
    folder = path.parent / "cloudflared"
    folder.mkdir(parents=True, exist_ok=True)
    text = (
        "# SPDX-License-Identifier: AGPL-3.0-or-later\n"
        + "tunnel: "
        + json.dumps(name)
        + "\ncredentials-file: "
        + json.dumps(credential)
        + "\ningress:\n  - hostname: "
        + json.dumps(host)
        + "\n    service: "
        + json.dumps(f"http://localhost:{port}")
        + "\n  - service: http_status:404\n"
    )
    (folder / "config.yml").write_text(text, encoding="utf-8")
    print(
        "Created cloudflared/config.yml. Create a tunnel and DNS"
        " route in Cloudflare, then run cloudflared with this fi"
        "le."
    )
    print(
        "Access: create a self-hosted application for your hostn"
        "ame, set the AUD above, and issue separate Service Toke"
        "ns for devices/Agents. See docs/guides/09-public-access"
        "."
    )
    return raw


def tunnel(config, path: Path):
    write_config(path, tunnel_config(tomllib.loads(path.read_text(encoding="utf-8")), path))
