# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
import os
import socket
import ssl
import sys
import threading
import time
from urllib.parse import urlsplit

from .hooks import hook_url


async def forward(source: str, event: str, deadline: float):
    from .core.config import config_path, load_config

    config = load_config(config_path())
    payload = json.load(sys.stdin)
    headers = {}
    if "NUKA_HOOK_TOKEN" in os.environ:
        headers["Authorization"] = "Bearer " + os.environ["NUKA_HOOK_TOKEN"]
    url = urlsplit(hook_url(config))
    body = json.dumps({"event": event, "host": socket.gethostname(), "payload": payload}).encode()
    context = ssl.create_default_context() if url.scheme == "https" else None
    async with asyncio.timeout_at(deadline):
        reader, writer = await asyncio.open_connection(
            url.hostname,
            url.port or (443 if context else 80),
            ssl=context,
            happy_eyeballs_delay=0.05,
        )
        try:
            fields = {
                "Host": url.netloc,
                "Content-Type": "application/json",
                "Content-Length": str(len(body)),
                "Connection": "close",
                **headers,
            }
            if any("\r" in value or "\n" in value for value in fields.values()):
                raise ValueError("HTTP header contains a line break")
            request = f"POST /hook/{source} HTTP/1.1\r\n"
            request += "".join(f"{key}: {value}\r\n" for key, value in fields.items())
            writer.write(request.encode("ascii") + b"\r\n" + body)
            await writer.drain()
            await reader.read(1024)
        finally:
            writer.transport.abort()


def main():
    # SPEC requires this input/network boundary to leave the calling CLI unaffected.
    # Leave process startup and interpreter shutdown inside the two-second budget.
    deadline = time.monotonic() + 0.6
    # Cancelling getaddrinfo cannot stop its worker; asyncio shutdown would wait for DNS.
    exit_timer = threading.Timer(1.0, lambda: os._exit(0))
    exit_timer.daemon = True
    exit_timer.start()
    try:
        source, event = sys.argv[1:]
        if source not in {"codex", "gemini"}:
            return
        asyncio.run(forward(source, event, deadline))
    except (OSError, ValueError, TimeoutError):
        return
    finally:
        exit_timer.cancel()
