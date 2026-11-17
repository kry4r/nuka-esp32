# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import ctypes
import json
import os
import shutil
import socket
import sys
from dataclasses import asdict
from pathlib import Path

import httpx
from serial.tools import list_ports
from zeroconf.asyncio import AsyncServiceInfo, AsyncZeroconf

from .core.plugin import Health


async def firewall(port: int) -> tuple[str, str]:
    if sys.platform == "win32":
        script = (
            "$ErrorActionPreference='Stop'; "
            "$enabled = @(Get-NetFirewallProfile | Where-Object Enabled -eq $true).Count -gt 0; "
        )
        script += (
            "$rules = Get-NetFirewallRule -Enabled True -Direction Inbound -Action Allow "
            "| Get-NetFirewallPortFilter; "
        )
        script += (
            "$allowed = @($rules | Where-Object { $_.Protocol -in @('TCP',6,'Any') -and "
            f"($_.LocalPort -contains '{port}' -or $_.LocalPort -contains 'Any') "
            "}).Count -gt 0; "
        )
        script += "@{enabled=$enabled;allowed=$allowed}|ConvertTo-Json -Compress"
        powershell = (
            Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        )
        command = [str(powershell), "-NoProfile", "-NonInteractive", "-Command", script]
    elif sys.platform == "darwin":
        command = ["/usr/libexec/ApplicationFirewall/socketfilterfw", "--getglobalstate"]
    elif shutil.which("ufw"):
        command = ["ufw", "status"]
    elif shutil.which("nft"):
        command = ["nft", "list", "ruleset"]
    else:
        return "degraded", "No supported firewall inspection tool found; review inbound TCP rules"
    process = await asyncio.create_subprocess_exec(
        *command,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.DEVNULL,
        creationflags=0x08000000 if sys.platform == "win32" else 0,
    )
    try:
        async with asyncio.timeout(10):
            output, _ = await process.communicate()
    except TimeoutError:
        process.kill()
        await process.wait()
        return "degraded", "Firewall inspection timed out; no rule was changed"
    if process.returncode:
        return "degraded", "Firewall inspection needs administrator permission; no rule was changed"
    if sys.platform == "win32":
        state = json.loads(output.decode("utf-8-sig"))
        if not state["enabled"]:
            return "ok", "Windows Firewall is disabled"
        return (
            ("ok", "Inbound TCP allow rule found")
            if state["allowed"]
            else ("degraded", "No inbound TCP allow rule found for the configured port")
        )
    text = output.decode()
    disabled = "State = 0" in text if sys.platform == "darwin" else "Status: inactive" in text
    return (
        ("ok", "Firewall is disabled")
        if disabled
        else (
            "degraded",
            "Firewall rules inspected; confirm inbound TCP access for the configured port",
        )
    )


async def doctor(runtime):
    from .plugins.ai import build_providers
    from .plugins.collectors import build_collectors
    from .plugins.servers import build_sources
    from .plugins.weather import Weather

    plugins = await asyncio.to_thread(
        build_collectors, runtime.config.collectors, runtime.client, runtime.secrets
    )
    plugins += await asyncio.to_thread(
        build_sources, runtime.config.servers, runtime.secrets, runtime.client
    )
    plugins += await asyncio.to_thread(
        build_providers, runtime.config.ai, runtime.secrets, runtime.client, runtime.timezone
    )
    if runtime.config.weather.enabled:
        plugins.append(Weather(runtime.config.weather, runtime.client))
    if runtime.config.media.enabled:
        from .media import Media

        plugins.append(
            Media(runtime.client, runtime.hub_id, runtime.config.hub.name, runtime.cover_cache)
        )

    def report(name, health, detail, doc):
        print(
            json.dumps(
                {"name": name, "kind": "system", "health": health, "detail": detail, "doc": doc},
                ensure_ascii=False,
            )
        )

    usb = await asyncio.to_thread(
        lambda: [
            port for port in list_ports.comports() if port.vid == 0x303A and port.pid == 0x4001
        ]
    )
    report(
        "usb",
        "ok" if usb else "disabled",
        "nuka USB CDC available"
        if usb
        else "No nuka USB CDC device; check USB cable and native USB mode",
        "guides/03-pair-device",
    )
    try:
        response = await runtime.client.get(
            f"http://localhost:{runtime.config.hub.port}/api/v1/health"
        )
        response.raise_for_status()
        data = response.json()
        connected = data["ok"] is True and data["proto"] == 1
        if connected:
            token = await runtime.tokens.create("admin", "doctor")
            try:
                response = await runtime.client.get(
                    f"http://localhost:{runtime.config.hub.port}/api/v1/plugins",
                    headers={"Authorization": "Bearer " + token["token"]},
                )
                response.raise_for_status()
                from pydantic import TypeAdapter

                from .core.models import RestPlugin

                current = [
                    asdict(Health(**item.wire()))
                    for item in TypeAdapter(list[RestPlugin]).validate_python(
                        response.json()["items"]
                    )
                ]
            finally:
                await runtime.tokens.revoke(token["id"])
    except (OSError, ValueError, KeyError, httpx.HTTPError):
        connected = False
    probes = []
    if not connected:
        for plugin in plugins:
            try:
                result = asdict(await plugin.probe())
            except Exception as error:
                result = {
                    "name": plugin.name,
                    "kind": plugin.kind,
                    "health": "error",
                    "detail": type(error).__name__,
                    "doc": plugin.doc,
                }
            probes.append(result)
    for result in current if connected else probes:
        print(json.dumps(result, ensure_ascii=False))
    report(
        "hub_http",
        "ok" if connected else "degraded",
        "Hub health reachable"
        if connected
        else "Hub not reachable; start nuka hub and check hub.port",
        "guides/12-troubleshooting",
    )
    probe = socket.socket()
    try:
        probe.bind((runtime.config.hub.host, runtime.config.hub.port))
        detail, health = "Port available", "ok"
    except OSError:
        detail, health = (
            ("Port is owned by the running Hub", "ok")
            if connected
            else ("Port occupied by another process", "error")
        )
    finally:
        probe.close()
    report("port", health, detail, "guides/02-install-hub")
    if runtime.config.mdns.enabled:
        discovery = AsyncZeroconf()
        try:
            info = AsyncServiceInfo(
                "_nuka._tcp.local.", runtime.config.hub.name + "._nuka._tcp.local."
            )
            found = await info.async_request(discovery.zeroconf, 1500)
            valid = found and info.properties.get(b"id") == runtime.hub_id.encode()
            report(
                "mdns",
                "ok" if valid else "degraded",
                "Paired Hub discovered over _nuka._tcp"
                if valid
                else "Hub discovery failed; check UDP multicast/firewall or use the paired URL",
                "guides/09-public-access",
            )
        finally:
            await discovery.async_close()
    else:
        report("mdns", "disabled", "mDNS disabled in config", "guides/09-public-access")
    firewall_health, firewall_detail = await firewall(runtime.config.hub.port)
    report(
        "firewall",
        firewall_health,
        firewall_detail,
        "guides/02-install-hub-"
        + (
            "windows"
            if sys.platform == "win32"
            else "macos"
            if sys.platform == "darwin"
            else "linux"
        ),
    )
    if sys.platform == "win32":
        admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
        if not admin:
            report(
                "lhm_privilege",
                "degraded",
                "LibreHardwareMonitor needs administrator privileges; run LHM elevated",
                "guides/04-hardware-sensors",
            )
