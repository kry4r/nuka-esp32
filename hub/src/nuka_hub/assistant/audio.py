# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import io
import os
import subprocess
import wave


def pcm_to_wav(pcm: bytes, sample_rate: int) -> bytes:
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(sample_rate)
        audio.writeframes(pcm)
    return output.getvalue()


class AudioDecoder:
    """Decode provider audio in memory; the output is mono signed little-endian PCM."""

    def __init__(self, sample_rate: int, command: str = "ffmpeg"):
        self.sample_rate, self.command = sample_rate, command

    async def decode(self, audio: bytes) -> bytes:
        process = await asyncio.create_subprocess_exec(
            self.command,
            "-hide_banner",
            "-loglevel",
            "error",
            "-nostdin",
            "-i",
            "pipe:0",
            "-vn",
            "-ac",
            "1",
            "-ar",
            str(self.sample_rate),
            "-f",
            "s16le",
            "pipe:1",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            **({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}),
        )
        try:
            output, _ = await process.communicate(audio)
        except asyncio.CancelledError:
            if process.returncode is None:
                process.kill()
            await process.wait()
            raise
        if process.returncode:
            # A decoder may echo input metadata; do not expose it in API errors or logs.
            raise ValueError("Audio decoding failed")
        return output
