# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import base64
import contextlib
import json
import platform
import time

import httpx
from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed

from . import __version__
from .core.config import data_path
from .core.models import AgentWelcome, DeviceMediaCmd, Envelope
from .core.runtime import identity
from .core.scheduler import Scheduler
from .core.secrets import Secrets
from .media import Media


async def run_agent(config, args, path):
    from .plugins.collectors import build_collectors

    folder = data_path(config)
    folder.mkdir(parents=True, exist_ok=True)
    agent_id = identity(folder, "agent-id", "ag_")
    store = Secrets(path.parent)
    url = args.hub or config.agent.hub_url
    token = args.token or await asyncio.to_thread(store.get, config.agent.token_secret)
    if not url or not token:
        raise ValueError("Agent requires agent.hub_url and an agent token secret")
    headers = {"Authorization": "Bearer " + token}
    if config.agent.cf_client_id_secret:
        if not url.startswith("wss://"):
            raise ValueError("Cloudflare credentials require WSS")
        headers.update(
            {
                "CF-Access-Client-Id": await asyncio.to_thread(
                    store.get, config.agent.cf_client_id_secret
                ),
                "CF-Access-Client-Secret": await asyncio.to_thread(
                    store.get, config.agent.cf_client_secret_secret
                ),
            }
        )
    delay = 1
    while True:
        try:
            async with (
                httpx.AsyncClient(timeout=10) as client,
                connect(url, additional_headers=headers) as ws,
            ):
                name = config.agent.name or config.collectors.host_name or config.hub.name

                def frame(kind, data, request_id=None):
                    message = {"v": 1, "t": kind, "ts": int(time.time()), "d": data}
                    if request_id is not None:
                        message["id"] = request_id
                    return json.dumps(message, separators=(",", ":"))

                await ws.send(
                    frame(
                        "hello",
                        {
                            "agent_id": agent_id,
                            "name": name,
                            "os": platform.system().lower(),
                            "ver": __version__,
                            "proto": 1,
                            "caps": ["host", "media"] if config.agent.send_media else ["host"],
                        },
                        1,
                    )
                )
                envelope = Envelope.model_validate_json(await ws.recv())
                welcome = AgentWelcome.model_validate(envelope.d)
                if envelope.t != "welcome" or envelope.id != 1:
                    raise ValueError("Agent welcome required")
                parts, sent_covers = {}, {}
                media = Media(client, agent_id, name, {})

                async def health():
                    pass

                async def sample(plugin, value):
                    if plugin.kind == "collector":
                        parts[plugin.name] = value
                    else:
                        items = []
                        for item in value:
                            outgoing = dict(item)
                            outgoing["at"] = int(time.time())
                            cover = outgoing.pop("cover", None)
                            if cover and sent_covers.get(item["src_id"]) != cover:
                                digest = cover.rsplit("/", 1)[1].removesuffix(".jpg")
                                outgoing["cover_b64"] = base64.b64encode(
                                    media.covers[digest]
                                ).decode("ascii")
                                sent_covers[item["src_id"]] = cover
                            items.append(outgoing)
                        await ws.send(frame("media.state", {"items": items}))

                async def hosts():
                    while True:
                        if parts:
                            host = {"name": name, "online": True, "os": platform.system().lower()}
                            for key in {key for part in parts.values() for key in part} - {
                                "last_seen"
                            }:
                                priority = config.collectors.priority.get(
                                    key, config.collectors.enabled
                                )
                                source = next(
                                    name for name in priority if key in parts.get(name, {})
                                )
                                host[key] = parts[source][key]
                                if isinstance(host[key], dict):
                                    host[key] = dict(host[key])
                                    for subkey in {
                                        field
                                        for part in parts.values()
                                        if key in part
                                        for field in part[key]
                                    }:
                                        order = config.collectors.priority.get(
                                            key + "." + subkey, config.collectors.enabled
                                        )
                                        selected = next(
                                            name
                                            for name in order
                                            if subkey in parts.get(name, {}).get(key, {})
                                        )
                                        host[key][subkey] = parts[selected][key][subkey]
                            await ws.send(frame("host.sample", host))
                        await asyncio.sleep(welcome.intervals.host_fast)

                scheduler = Scheduler(sample, health)
                plugins = await asyncio.to_thread(
                    build_collectors, config.collectors, client, store
                )
                for plugin in plugins:
                    plugin.interval_s = (
                        welcome.intervals.host_fast
                        if plugin.name == "psutil"
                        else welcome.intervals.host_slow
                    )
                    if plugin.name == "psutil":
                        plugin.disk_interval = welcome.intervals.disk
                if config.agent.send_media:
                    plugins.append(media)
                scheduler.start(plugins)
                sender = asyncio.create_task(hosts())
                delay = 1
                try:
                    async for raw in ws:
                        message = Envelope.model_validate_json(raw)
                        if message.t == "media.cmd":
                            data = DeviceMediaCmd.model_validate(message.d)
                            await media.command(data.src_id, data.cmd)
                finally:
                    sender.cancel()
                    with contextlib.suppress(asyncio.CancelledError):
                        await sender
                    await scheduler.stop()
                    await media.close()
        except (OSError, TimeoutError, ConnectionClosed):
            await asyncio.sleep(delay)
            delay = min(delay * 2, 30)
