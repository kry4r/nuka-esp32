# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import base64
from collections.abc import Callable
from typing import Any, Protocol

import httpx
from pydantic import BaseModel, ConfigDict, Field

from .audio import AudioDecoder


class Tts(Protocol):
    async def synthesize(self, text: str) -> bytes: ...


class GeneratedAudio(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    audio: str = Field(min_length=1)


class CloudflareAudio(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    success: bool
    result: GeneratedAudio


class OpenAiTts:
    def __init__(
        self,
        client: httpx.AsyncClient,
        decoder: AudioDecoder,
        *,
        base_url: str,
        model: str,
        key: str,
        voice: str,
        speed: float,
    ):
        self.client, self.decoder = client, decoder
        self.base_url, self.model, self.key = base_url.rstrip("/"), model, key
        self.voice, self.speed = voice, speed

    async def synthesize(self, text: str) -> bytes:
        response = await self.client.post(
            self.base_url + "/audio/speech",
            headers={"Authorization": f"Bearer {self.key}"} if self.key else {},
            json={
                "model": self.model,
                "input": text,
                "voice": self.voice,
                "speed": self.speed,
                "response_format": "wav",
            },
        )
        response.raise_for_status()
        return await self.decoder.decode(response.content)


class CloudflareTts:
    """MeloTTS REST response: MP3 bytes or the Workers AI JSON audio envelope."""

    def __init__(
        self,
        client: httpx.AsyncClient,
        decoder: AudioDecoder,
        *,
        base_url: str,
        model: str,
        key: str,
        language: str,
    ):
        self.client, self.decoder = client, decoder
        self.base_url, self.model, self.key = base_url.rstrip("/"), model, key
        self.language = language

    async def synthesize(self, text: str) -> bytes:
        response = await self.client.post(
            self.base_url + "/run/" + self.model,
            headers={"Authorization": f"Bearer {self.key}"},
            json={"prompt": text, "lang": self.language},
        )
        response.raise_for_status()
        if response.headers.get("content-type", "").split(";", 1)[0] == "application/json":
            envelope = CloudflareAudio.model_validate(response.json())
            if not envelope.success:
                raise ValueError("Cloudflare speech synthesis failed")
            audio = base64.b64decode(envelope.result.audio, validate=True)
        else:
            audio = response.content
        return await self.decoder.decode(audio)


class EdgeTts:
    def __init__(
        self,
        decoder: AudioDecoder,
        *,
        voice: str,
        speed: float,
        communicate: Callable[..., Any] | None = None,
    ):
        self.decoder, self.voice, self.speed = decoder, voice, speed
        self.communicate = communicate

    async def synthesize(self, text: str) -> bytes:
        communicate = self.communicate
        if communicate is None:
            from edge_tts import Communicate

            communicate = Communicate
        rate = f"{round((self.speed - 1) * 100):+d}%"
        audio = bytearray()
        async for chunk in communicate(text, self.voice, rate=rate).stream():
            if chunk["type"] == "audio":
                audio.extend(chunk["data"])
        return await self.decoder.decode(bytes(audio))
