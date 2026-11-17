# SPDX-License-Identifier: AGPL-3.0-or-later
import base64
import io
import json
import struct
import wave
from pathlib import Path
from types import SimpleNamespace

import httpx
import pytest

from nuka_hub.assistant.asr import CloudflareAsr, OpenAiAsr, SherpaAsr
from nuka_hub.assistant.search import BraveSearch, SearxngSearch
from nuka_hub.assistant.tts import CloudflareTts, EdgeTts, OpenAiTts

FIXTURES = Path(__file__).with_name("fixtures")
PCM = struct.pack("<hhh", -32768, 0, 16384)


def fixture(name):
    return json.loads((FIXTURES / (name + ".json")).read_text(encoding="utf-8"))


class Decoder:
    def __init__(self, encoded):
        self.encoded = encoded

    async def decode(self, audio):
        assert audio == self.encoded
        return PCM


async def test_t3_openai_asr_fixture():
    data = fixture("asr-openai")

    def respond(request):
        assert request.url.path == "/v1/audio/transcriptions"
        assert request.headers["Authorization"] == "Bearer <YOUR_TOKEN>"
        assert b'name="language"\r\n\r\nzh' in request.content
        assert b'filename="speech.wav"' in request.content and b"RIFF" in request.content
        assert PCM in request.content
        return httpx.Response(200, json=data)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = OpenAiAsr(
            client,
            base_url="https://asr.example.com/v1",
            model="example-asr",
            key="<YOUR_TOKEN>",
            language="zh",
        )
        assert await adapter.transcribe(PCM, 16000) == data["text"].strip()


async def test_t3_cloudflare_asr_fixture():
    data = fixture("asr-cloudflare")

    def respond(request):
        assert request.url.path == "/accounts/example/ai/run/@cf/openai/whisper-large-v3-turbo"
        payload = json.loads(request.content)
        assert payload["language"] == "zh" and payload["task"] == "transcribe"
        with wave.open(io.BytesIO(base64.b64decode(payload["audio"])), "rb") as audio:
            assert (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) == (
                16000,
                1,
                2,
            )
            assert audio.readframes(3) == PCM
        return httpx.Response(200, json=data)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = CloudflareAsr(
            client,
            base_url="https://cloud.example.com/accounts/example/ai",
            model="@cf/openai/whisper-large-v3-turbo",
            key="<YOUR_TOKEN>",
            language="zh",
        )
        assert await adapter.transcribe(PCM, 16000) == data["result"]["text"].strip()


async def test_t3_sherpa_asr_fixture():
    np = pytest.importorskip("numpy", reason="Local ASR requires the voice extra")
    data = fixture("asr-sherpa")

    class Recognizer:
        def create_stream(self):
            def accept(rate, samples):
                assert rate == data["sample_rate"]
                np.testing.assert_array_equal(samples, np.array(data["samples"]) / 32768)
                assert samples.dtype == np.float32

            return SimpleNamespace(
                accept_waveform=accept, result=SimpleNamespace(text=data["text"])
            )

        def decode_stream(self, stream):
            pass

    adapter = SherpaAsr(Recognizer)
    assert await adapter.transcribe(PCM, data["sample_rate"]) == data["text"].strip()


async def test_t3_openai_tts_fixture():
    data = fixture("tts-openai")
    encoded = base64.b64decode(data["audio_base64"])

    def respond(request):
        assert request.url.path == "/v1/audio/speech"
        assert "Authorization" not in request.headers
        assert json.loads(request.content) == {
            "model": data["model"],
            "input": data["text"],
            "voice": data["voice"],
            "speed": 1.0,
            "response_format": "wav",
        }
        return httpx.Response(200, content=encoded, headers={"Content-Type": "audio/wav"})

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = OpenAiTts(
            client,
            Decoder(encoded),
            base_url="https://tts.example.com/v1",
            model=data["model"],
            key="",
            voice=data["voice"],
            speed=1.0,
        )
        assert await adapter.synthesize(data["text"]) == PCM


async def test_t3_cloudflare_tts_fixture():
    data = fixture("tts-cloudflare")
    encoded = base64.b64decode(data["result"]["audio"])
    requests = []

    def respond(request):
        requests.append(request)
        assert json.loads(request.content) == {"prompt": "假数据回答。", "lang": "zh"}
        if len(requests) == 1:
            return httpx.Response(200, json=data)
        return httpx.Response(200, content=encoded, headers={"Content-Type": "audio/mpeg"})

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = CloudflareTts(
            client,
            Decoder(encoded),
            base_url="https://cloud.example.com/accounts/example/ai",
            model="@cf/myshell-ai/melotts",
            key="<YOUR_TOKEN>",
            language="zh",
        )
        assert await adapter.synthesize("假数据回答。") == PCM
        assert await adapter.synthesize("假数据回答。") == PCM


async def test_t3_edge_tts_fixture():
    data = fixture("tts-edge")
    encoded = b"ID3fake-mp3"

    class Communicate:
        def __init__(self, text, voice, *, rate):
            assert (text, voice, rate) == (data["text"], data["voice"], "+20%")

        async def stream(self):
            for chunk in data["chunks"]:
                if chunk["type"] == "audio":
                    yield {"type": "audio", "data": base64.b64decode(chunk["data_base64"])}
                else:
                    yield chunk

    adapter = EdgeTts(Decoder(encoded), voice=data["voice"], speed=1.2, communicate=Communicate)
    assert await adapter.synthesize(data["text"]) == PCM


async def test_t3_searxng_fixture():
    data = fixture("search-searxng")

    def respond(request):
        assert request.url.path == "/search"
        assert request.url.params["format"] == "json"
        assert request.url.params["q"] == data["query"]
        return httpx.Response(200, json=data)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = SearxngSearch(client, base_url="https://search.example.com", max_results=1)
        result = await adapter.search(data["query"])
        assert len(result) == 1
        assert result[0].title == "Example result"
        assert str(result[0].url) == "https://docs.example.com/page"
        assert result[0].snippet == "A fake search snippet."


async def test_t3_brave_fixture():
    data = fixture("search-brave")

    def respond(request):
        assert request.url.path == "/res/v1/web/search"
        assert request.headers["X-Subscription-Token"] == "<YOUR_TOKEN>"
        assert request.url.params["count"] == "1"
        return httpx.Response(200, json=data)

    async with httpx.AsyncClient(transport=httpx.MockTransport(respond)) as client:
        adapter = BraveSearch(
            client,
            base_url="https://search.example.com/res/v1",
            key="<YOUR_TOKEN>",
            max_results=1,
        )
        result = await adapter.search("假数据搜索")
        assert len(result) == 1
        assert result[0].title == "Example result"
        assert result[0].snippet == "A fake search snippet."
