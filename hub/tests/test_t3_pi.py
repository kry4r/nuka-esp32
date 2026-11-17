# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
import shutil
from pathlib import Path

import pytest

from nuka_hub.assistant.pi_engine import ENGINE, PiEngine


@pytest.mark.skipif(
    shutil.which("node") is None or not (ENGINE.parent / "node_modules").is_dir(),
    reason="Pi optional runtime requires Node and npm ci",
)
async def test_t3_pi_llm_fixture():
    fixture = json.loads(
        Path(__file__).with_name("fixtures").joinpath("pi_llm.json").read_text(encoding="utf-8")
    )
    requests = []
    calls = []

    async def respond(reader, writer):
        head = (await reader.readuntil(b"\r\n\r\n")).decode("ascii")
        headers = dict(line.split(": ", 1) for line in head.split("\r\n")[1:] if ": " in line)
        body = json.loads(await reader.readexactly(int(headers["content-length"])))
        requests.append(body)
        assert head.startswith("POST /v1/chat/completions ")
        assert "authorization" not in {name.lower() for name in headers}
        step = fixture["steps"][len(requests) - 1]
        chunks = [
            {
                "id": "fake",
                "choices": [{"index": 0, "delta": step["delta"], "finish_reason": None}],
            },
            {
                "id": "fake",
                "choices": [{"index": 0, "delta": {}, "finish_reason": step["finish_reason"]}],
            },
        ]
        data = "".join("data: " + json.dumps(chunk) + "\n\n" for chunk in chunks)
        payload = (data + "data: [DONE]\n\n").encode()
        writer.write(
            b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n"
            + f"Content-Length: {len(payload)}\r\n\r\n".encode()
            + payload
        )
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    async def execute(name, arguments):
        calls.append((name, arguments))
        return {"result": "ok"}

    server = await asyncio.start_server(respond, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    engine = PiEngine()
    engine.check_ready()
    async with server, asyncio.timeout(15):
        events = [
            event
            async for event in engine.turn(
                config={
                    "base_url": f"http://127.0.0.1:{port}/v1",
                    "model": "fixture",
                    "context_window": 32768,
                    "max_output_tokens": 128,
                    "temperature": 0.7,
                },
                api_key=None,
                system_prompt="Use only the registered fixture tool.",
                messages=[],
                text="Read the fixture status.",
                tools=[
                    {
                        "name": "read_status",
                        "description": "Read a fixture status.",
                        "parameters": {
                            "type": "object",
                            "properties": {"topic": {"type": "string"}},
                            "required": ["topic"],
                            "additionalProperties": False,
                        },
                    }
                ],
                execute=execute,
            )
        ]
    assert calls == [("read_status", {"topic": "ai"})]
    assert len(requests) == 2
    result = next(message for message in requests[1]["messages"] if message["role"] == "tool")
    assert result["tool_call_id"] == "fake_call"
    assert json.loads(result["content"]) == {"result": "ok"}
    assert (
        "".join(event["text"] for event in events if event["type"] == "delta")
        == "Fixture complete."
    )
    messages = [event["message"] for event in events if event["type"] == "message"]
    assert [message["role"] for message in messages] == [
        "user",
        "assistant",
        "toolResult",
        "assistant",
    ]
