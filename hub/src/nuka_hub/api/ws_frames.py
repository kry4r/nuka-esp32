# SPDX-License-Identifier: AGPL-3.0-or-later
from fastapi import WebSocket, WebSocketDisconnect
from pydantic import ValidationError


class FrameError(ValueError):
    def __init__(self, code: int):
        self.code = code
        super().__init__("Invalid WebSocket frame")


async def receive_text(ws: WebSocket, max_size: int | None = None) -> str:
    message = await ws.receive()
    if message["type"] == "websocket.disconnect":
        raise WebSocketDisconnect(message["code"])
    text = message.get("text")
    if text is None:
        raise FrameError(1008)
    if max_size is not None and len(text.encode("utf-8")) > max_size:
        raise FrameError(1009)
    return text


def validation_code(error: ValidationError) -> int:
    return (
        4002
        if any(
            item["type"] == "literal_error" and item["loc"] in {("v",), ("proto",)}
            for item in error.errors()
        )
        else 1008
    )
