# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import base64
from collections.abc import Callable
from pathlib import Path
from threading import Lock
from typing import Any, Protocol

import httpx
from pydantic import BaseModel, ConfigDict

from .audio import pcm_to_wav


class Asr(Protocol):
    async def transcribe(self, pcm: bytes, sample_rate: int) -> str: ...


class Transcript(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    text: str


class CloudflareTranscript(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    success: bool
    result: Transcript


class OpenAiAsr:
    def __init__(
        self, client: httpx.AsyncClient, *, base_url: str, model: str, key: str, language: str
    ):
        self.client, self.base_url, self.model = client, base_url.rstrip("/"), model
        self.key, self.language = key, language

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        data = {"model": self.model, "response_format": "json"}
        if self.language != "auto":
            data["language"] = self.language
        response = await self.client.post(
            self.base_url + "/audio/transcriptions",
            headers={"Authorization": f"Bearer {self.key}"} if self.key else {},
            data=data,
            files={"file": ("speech.wav", pcm_to_wav(pcm, sample_rate), "audio/wav")},
        )
        response.raise_for_status()
        return Transcript.model_validate(response.json()).text.strip()


class CloudflareAsr:
    """Workers AI Whisper turbo (the legacy Whisper model has a different input shape)."""

    def __init__(
        self, client: httpx.AsyncClient, *, base_url: str, model: str, key: str, language: str
    ):
        self.client, self.base_url, self.model = client, base_url.rstrip("/"), model
        self.key, self.language = key, language

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        payload = {
            "audio": base64.b64encode(pcm_to_wav(pcm, sample_rate)).decode("ascii"),
            "task": "transcribe",
        }
        if self.language != "auto":
            payload["language"] = self.language
        response = await self.client.post(
            self.base_url + "/run/" + self.model,
            headers={"Authorization": f"Bearer {self.key}"},
            json=payload,
        )
        response.raise_for_status()
        result = CloudflareTranscript.model_validate(response.json())
        if not result.success:
            raise ValueError("Cloudflare transcription failed")
        return result.result.text.strip()


class SherpaAsr:
    def __init__(self, load_recognizer: Callable[[], Any]):
        self.load_recognizer = load_recognizer
        self.recognizer = None
        self.lock = Lock()

    @classmethod
    def whisper(
        cls, *, encoder: Path, decoder: Path, tokens: Path, language: str, num_threads: int
    ) -> SherpaAsr:
        def load():
            import sherpa_onnx

            return sherpa_onnx.OfflineRecognizer.from_whisper(
                encoder=str(encoder),
                decoder=str(decoder),
                tokens=str(tokens),
                language="" if language == "auto" else language,
                task="transcribe",
                num_threads=num_threads,
                debug=False,
            )

        return cls(load)

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        import numpy as np

        samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0

        def recognize() -> str:
            with self.lock:
                if self.recognizer is None:
                    self.recognizer = self.load_recognizer()
                stream = self.recognizer.create_stream()
                stream.accept_waveform(sample_rate, samples)
                self.recognizer.decode_stream(stream)
                return stream.result.text.strip()

        # Cancelling the await discards native ASR's eventual result immediately; the
        # worker retains its lock until it finishes, without delaying device stop.
        return await asyncio.to_thread(recognize)
