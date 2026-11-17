# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import base64
import hashlib
import io

from fastapi import WebSocketDisconnect
from pydantic import ValidationError

from nuka_hub.core.history import iso
from nuka_hub.core.models import AgentHello, AgentHostSample, AgentMediaState, Envelope

from .ws_frames import FrameError, receive_text, validation_code


async def agent_connection(runtime, ws):
    await ws.accept()
    token = await runtime.tokens.authenticate(
        ws.headers.get("authorization", "").removeprefix("Bearer "), "agent"
    )
    if token is None:
        await ws.close(4001)
        return
    identity = None
    connection = None
    try:
        frame = Envelope.model_validate_json(await receive_text(ws))
        hello = AgentHello.model_validate(frame.d)
        if frame.t != "hello" or frame.id is None:
            await ws.close(1008)
            return
        identity = "h_" + hashlib.sha256(hello.agent_id.encode()).hexdigest()[:24]
        connection = {"ws": ws, "media_ids": set(), "covers": {}}
        old = runtime.agent_connections.get(identity)
        if old:
            await old["ws"].close(4008)
        runtime.agent_connections[identity] = connection
        await ws.send_json(
            runtime.devices.frame(
                "welcome",
                {
                    "hub_id": runtime.hub_id,
                    "intervals": {
                        "host_fast": runtime.config.collectors.psutil.fast_interval,
                        "host_slow": 2,
                        "disk": runtime.config.collectors.psutil.disk_interval,
                    },
                },
                frame.id,
            )
        )
        while True:
            frame = Envelope.model_validate_json(await receive_text(ws))
            if frame.t == "host.sample":
                sample = AgentHostSample.model_validate(frame.d).wire()
                host = {**sample, "id": identity, "role": "agent", "last_seen": iso()}
                await runtime.set(
                    "hosts",
                    [item for item in runtime.state.get("hosts") if item["id"] != identity]
                    + [host],
                )
                await runtime.history.metrics(host)
            elif frame.t == "media.state":
                sample = AgentMediaState.model_validate(frame.d).wire()
                items = []
                for item in sample["items"]:
                    if "cover_b64" in item:
                        from PIL import Image

                        raw = base64.b64decode(item.pop("cover_b64"), validate=True)
                        image = Image.open(io.BytesIO(raw))
                        if (
                            len(raw) > 20480
                            or image.size != (160, 160)
                            or image.format != "JPEG"
                            or image.info.get("progressive")
                        ):
                            await ws.close(1008)
                            return
                        digest = hashlib.sha256(raw).hexdigest()
                        runtime.cover_cache[digest] = raw
                        item["cover"] = "/api/v1/media/cover/" + digest + ".jpg"
                        connection["covers"][item["src_id"]] = item["cover"]
                    elif item["src_id"] in connection["covers"]:
                        item["cover"] = connection["covers"][item["src_id"]]
                    if "at" in item:
                        item["at"] = iso(item["at"])
                    items.append(item)
                previous = connection["media_ids"]
                await runtime.set(
                    "media",
                    [item for item in runtime.state.get("media") if item["src_id"] not in previous]
                    + items,
                )
                connection["media_ids"] = {item["src_id"] for item in items}
    except FrameError as error:
        await ws.close(error.code)
    except ValidationError as error:
        await ws.close(validation_code(error))
    except (ValueError, OSError):
        await ws.close(1008)
    except WebSocketDisconnect:
        pass
    finally:
        if identity and runtime.agent_connections.get(identity) is connection:
            del runtime.agent_connections[identity]

            async def offline():
                await asyncio.sleep(30)
                if identity in runtime.agent_connections:
                    return
                hosts = [
                    dict(item, online=False) if item["id"] == identity else item
                    for item in runtime.state.get("hosts")
                ]
                await runtime.set("hosts", hosts)

            runtime.tasks.append(asyncio.create_task(offline()))
