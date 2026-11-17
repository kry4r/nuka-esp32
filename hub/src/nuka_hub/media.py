# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import base64
import hashlib
import io
import os
import shutil
import sys
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path
from urllib.parse import unquote, urlparse

from pydantic import BaseModel, Field

from .core.history import iso
from .core.plugin import Health


class MacState(BaseModel):
    title: str | None = None
    artist: str | None = None
    album: str | None = None
    playbackRate: float | None = None
    elapsedTime: float | None = Field(default=None, ge=0)
    duration: float | None = Field(default=None, ge=0)
    artworkData: str | None = None
    clientBundleIdentifier: str | None = None


def cover_image(raw: bytes) -> bytes | None:
    from PIL import Image, ImageOps

    image = ImageOps.fit(Image.open(io.BytesIO(raw)).convert("RGB"), (160, 160))
    output = io.BytesIO()
    image.save(output, "JPEG", quality=80, progressive=False)
    result = output.getvalue()
    return result if len(result) <= 20480 else None


class Media:
    name, kind, doc = "media", "media", "guides/04-hardware-sensors"
    interval_s, timeout_s = 1.0, 8.0

    def __init__(self, client, owner_id: str, host: str, covers: dict[str, bytes]):
        self.client, self.owner_id, self.host, self.covers = client, owner_id, host, covers
        self.sessions = {}
        self.manager = None
        self.bus = None
        self.last_art: dict[str, tuple[str, str]] = {}

    async def probe(self) -> Health:
        if sys.platform == "darwin":
            available = shutil.which("nowplaying-cli") is not None
            detail = "Install nowplaying-cli with Homebrew"
        else:
            package = "winrt-Windows.Media.Control" if sys.platform == "win32" else "dbus-next"
            try:
                version(package)
                available = True
            except PackageNotFoundError:
                available = False
            detail = (
                "Install nuka-hub[media-win,images]"
                if sys.platform == "win32"
                else "Install nuka-hub[media-linux,images] and use the desktop D-Bus session"
            )
            if sys.platform == "linux" and not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
                available, detail = False, "No desktop D-Bus session"
        return Health(
            self.name,
            self.kind,
            "ok" if available else "disabled",
            "" if available else detail,
            self.doc,
        )

    def item(self, app: str, state: str, can: dict) -> dict:
        identity = "m_" + hashlib.sha256((self.owner_id + app).encode()).hexdigest()[:24]
        return {
            "src_id": identity,
            "host": self.host,
            "app": app,
            "state": state,
            "can": can,
            "at": iso(),
        }

    async def artwork(self, item: dict, raw: bytes):
        fingerprint = hashlib.sha256(raw).hexdigest()
        previous = self.last_art.get(item["src_id"])
        if previous and previous[0] == fingerprint:
            item["cover"] = "/api/v1/media/cover/" + previous[1] + ".jpg"
            return
        encoded = await asyncio.to_thread(cover_image, raw)
        if encoded is not None:
            digest = hashlib.sha256(encoded).hexdigest()
            self.covers[digest] = encoded
            self.last_art[item["src_id"]] = fingerprint, digest
            item["cover"] = "/api/v1/media/cover/" + digest + ".jpg"

    async def sample(self) -> list[dict]:
        if sys.platform == "win32":
            return await self.windows()
        if sys.platform == "darwin":
            return await self.macos()
        return await self.linux()

    async def windows(self) -> list[dict]:
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
        from winrt.windows.storage.streams import Buffer, InputStreamOptions

        if self.manager is None:
            self.manager = await GlobalSystemMediaTransportControlsSessionManager.request_async()
        items, sessions = [], {}
        for session in self.manager.get_sessions():
            properties = await session.try_get_media_properties_async()
            playback = session.get_playback_info()
            controls = playback.controls
            state = {4: "playing", 5: "paused"}.get(int(playback.playback_status), "stopped")
            item = self.item(
                session.source_app_user_model_id,
                state,
                {
                    "toggle": controls.is_play_enabled or controls.is_pause_enabled,
                    "next": controls.is_next_enabled,
                    "prev": controls.is_previous_enabled,
                },
            )
            for key in ("title", "artist", "album_title"):
                value = getattr(properties, key)
                if value:
                    item["album" if key == "album_title" else key] = value
            timeline = session.get_timeline_properties()
            item.update(
                pos_s=timeline.position.total_seconds(),
                dur_s=(timeline.end_time - timeline.start_time).total_seconds(),
            )
            if properties.thumbnail:
                stream = await properties.thumbnail.open_read_async()
                try:
                    data = await stream.read_async(
                        Buffer(stream.size), stream.size, InputStreamOptions.NONE
                    )
                    await self.artwork(item, bytes(data))
                finally:
                    stream.close()
            sessions[item["src_id"]] = session
            items.append(item)
        self.sessions = sessions
        return items

    async def macos(self) -> list[dict]:
        process = await asyncio.create_subprocess_exec(
            "nowplaying-cli",
            "get",
            "--json",
            "title",
            "artist",
            "album",
            "playbackRate",
            "elapsedTime",
            "duration",
            "artworkData",
            "clientBundleIdentifier",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        output, _ = await process.communicate()
        if process.returncode:
            raise RuntimeError("nowplaying-cli failed")
        state = MacState.model_validate_json(output)
        if state.title is None:
            self.sessions = {}
            return []
        item = self.item(
            state.clientBundleIdentifier or "nowplaying",
            "playing" if state.playbackRate else "paused",
            {"toggle": True, "next": True, "prev": True},
        )
        item.update(
            {
                key: value
                for key, value in state.model_dump().items()
                if key in {"title", "artist", "album"} and value is not None
            }
        )
        if state.elapsedTime is not None:
            item["pos_s"] = state.elapsedTime
        if state.duration is not None:
            item["dur_s"] = state.duration
        if state.artworkData:
            await self.artwork(item, base64.b64decode(state.artworkData, validate=True))
        self.sessions = {item["src_id"]: "nowplaying-cli"}
        return [item]

    async def linux(self) -> list[dict]:
        from dbus_next.aio import MessageBus

        if self.bus is None:
            self.bus = await MessageBus().connect()
        introspection = await self.bus.introspect("org.freedesktop.DBus", "/org/freedesktop/DBus")
        dbus = self.bus.get_proxy_object(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", introspection
        ).get_interface("org.freedesktop.DBus")
        items, sessions = [], {}
        for name in await dbus.call_list_names():
            if not name.startswith("org.mpris.MediaPlayer2."):
                continue
            path = "/org/mpris/MediaPlayer2"
            description = await self.bus.introspect(name, path)
            proxy = self.bus.get_proxy_object(name, path, description)
            properties = await proxy.get_interface("org.freedesktop.DBus.Properties").call_get_all(
                "org.mpris.MediaPlayer2.Player"
            )
            values = {key: value.value for key, value in properties.items()}
            metadata = {key: value.value for key, value in values["Metadata"].items()}
            item = self.item(
                name.removeprefix("org.mpris.MediaPlayer2."),
                values["PlaybackStatus"].lower(),
                {
                    "toggle": values["CanPlay"] or values["CanPause"],
                    "next": values["CanGoNext"],
                    "prev": values["CanGoPrevious"],
                },
            )
            for upstream, key in (("xesam:title", "title"), ("xesam:album", "album")):
                if upstream in metadata:
                    item[key] = metadata[upstream]
            if "xesam:artist" in metadata:
                item["artist"] = ", ".join(metadata["xesam:artist"])
            if "mpris:length" in metadata:
                item["dur_s"] = metadata["mpris:length"] / 1000000
            if "Position" in values:
                item["pos_s"] = values["Position"] / 1000000
            if "mpris:artUrl" in metadata:
                url = urlparse(metadata["mpris:artUrl"])
                if url.scheme == "file":
                    raw = await asyncio.to_thread(Path(unquote(url.path)).read_bytes)
                elif url.scheme in {"https", "http"}:
                    response = await self.client.get(url.geturl())
                    response.raise_for_status()
                    raw = response.content
                else:
                    raise ValueError("Unsupported MPRIS artwork URL")
                await self.artwork(item, raw)
            sessions[item["src_id"]] = proxy.get_interface("org.mpris.MediaPlayer2.Player")
            items.append(item)
        self.sessions = sessions
        return items

    async def command(self, src_id: str, command: str):
        session = self.sessions[src_id]
        if sys.platform == "win32":
            method = {
                "toggle": session.try_toggle_play_pause_async,
                "next": session.try_skip_next_async,
                "prev": session.try_skip_previous_async,
            }[command]
            await method()
        elif sys.platform == "darwin":
            process = await asyncio.create_subprocess_exec(
                session,
                {"toggle": "togglePlayPause", "next": "next", "prev": "previous"}[command],
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.DEVNULL,
            )
            await process.wait()
            if process.returncode:
                raise RuntimeError("nowplaying-cli command failed")
        else:
            await {
                "toggle": session.call_play_pause,
                "next": session.call_next,
                "prev": session.call_previous,
            }[command]()

    async def close(self):
        if self.bus:
            self.bus.disconnect()
