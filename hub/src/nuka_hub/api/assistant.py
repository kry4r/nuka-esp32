# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import json
import re
from typing import Annotated
from urllib.parse import unquote

from fastapi import APIRouter, HTTPException, Query, Request
from fastapi.responses import FileResponse, StreamingResponse
from pydantic import AfterValidator, BaseModel, ConfigDict, Field, model_validator

from nuka_hub.assistant.storage import UploadError, UploadTooLarge


def identifier(value: str) -> str:
    if not re.fullmatch(r"[a-z0-9_-]{1,32}", value):
        raise ValueError("requires a nuka ID")
    return value


Identifier = Annotated[str, AfterValidator(identifier)]
PageLimit = Annotated[int, Query(ge=1, le=100)]


class Chat(BaseModel):
    model_config = ConfigDict(extra="forbid")
    dev_id: Identifier
    text: str = Field(min_length=1, max_length=16384)
    session_id: Identifier | None = None
    file_ids: list[Identifier] = Field(default_factory=list, max_length=8)

    @model_validator(mode="after")
    def unique_files(self) -> Chat:
        if len(set(self.file_ids)) != len(self.file_ids):
            raise ValueError("file_ids must be unique")
        return self


class MemoryCreate(BaseModel):
    model_config = ConfigDict(extra="forbid")
    dev_id: Identifier
    text: str = Field(min_length=1, max_length=2000)
    source_message_id: Identifier | None = None


def filename(encoded: str) -> str:
    if re.search(r"%(?![0-9a-fA-F]{2})", encoded):
        raise UploadError("Invalid filename encoding")
    try:
        name = unquote(encoded, encoding="utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise UploadError("Filename must be UTF-8") from error
    if (
        not name
        or name in {".", ".."}
        or len(name.encode()) > 255
        or any(char in name for char in "\0\r\n/\\")
    ):
        raise UploadError("Invalid filename")
    if name.casefold().endswith(".nukavault"):
        raise UploadError("Vault backups must use the USB CLI")
    return name


def assistant_router(runtime) -> APIRouter:
    router = APIRouter(prefix="/api/v1/assistant")
    service = runtime.assistant

    @router.get("/status")
    async def status():
        config = runtime.config.assistant
        return {
            "enabled": config.enabled,
            "history_enabled": config.history.enabled,
            "memory_enabled": config.memory.enabled,
        }

    @router.post("/chat")
    async def chat(value: Chat):
        await service.device(value.dev_id)
        stream = await service.open(value.dev_id, value.text, value.session_id, value.file_ids)

        async def events():
            try:
                async for event in stream:
                    yield (
                        "event: "
                        + event["event"]
                        + "\ndata: "
                        + json.dumps(event["data"], ensure_ascii=False)
                        + "\n\n"
                    ).encode()
            finally:
                await stream.aclose()

        return StreamingResponse(
            events(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-store", "X-Accel-Buffering": "no"},
        )

    @router.get("/history")
    async def history(
        dev_id: Identifier,
        session_id: Identifier | None = None,
        limit: PageLimit = 50,
        before: Identifier | None = None,
    ):
        await service.device(dev_id)
        if session_id:
            await service.owns_session(dev_id, session_id)
        return await service.store.page(
            "history", dev_id, limit, before or "", session_id=session_id or ""
        )

    @router.delete("/history")
    async def delete_history(dev_id: Identifier, session_id: Identifier):
        await service.device(dev_id)
        await service.owns_session(dev_id, session_id)
        await service.cancel(session_id)
        await service.store.delete_session(dev_id, session_id)
        service.transient.pop(session_id, None)
        return {}

    @router.get("/history/export")
    async def export(dev_id: Identifier, session_id: Identifier | None = None):
        await service.device(dev_id)
        if session_id:
            await service.owns_session(dev_id, session_id)
        return StreamingResponse(
            service.store.export(dev_id, session_id or ""),
            media_type="application/x-ndjson",
            headers={
                "Content-Disposition": 'attachment; filename="nuka-history.jsonl"',
                "X-Content-Type-Options": "nosniff",
                "Cache-Control": "no-store",
            },
        )

    @router.get("/memories")
    async def memories(
        dev_id: Identifier,
        q: str = Query(default="", max_length=2000),
        limit: PageLimit = 50,
        before: Identifier | None = None,
    ):
        await service.device(dev_id)
        return await service.store.page("memories", dev_id, limit, before or "", query=q)

    @router.post("/memories", status_code=202)
    async def remember(value: MemoryCreate):
        await service.device(value.dev_id)
        if value.source_message_id:
            await service.store.source(value.dev_id, value.source_message_id)
        # C2-02 supplies the physical confirmation transport; no substitute authorization.
        raise HTTPException(409, "Device confirmation transport is not available")

    @router.delete("/memories/{identity}")
    async def forget(identity: Identifier, dev_id: Identifier):
        await service.device(dev_id)
        await service.store.delete_memory(dev_id, identity)
        return {}

    @router.get("/files")
    async def files(dev_id: Identifier, limit: PageLimit = 50, before: Identifier | None = None):
        await service.device(dev_id)
        return await service.store.page("files", dev_id, limit, before or "")

    @router.post("/files", status_code=201)
    async def upload(request: Request, dev_id: Identifier):
        await service.device(dev_id)
        maximum = runtime.config.assistant.uploads.max_bytes
        if request.headers.get("content-type", "").split(";")[0] != "application/octet-stream":
            raise UploadError("Use application/octet-stream")
        name = filename(request.headers.get("x-filename", ""))
        length = request.headers.get("content-length")
        if length is not None:
            if not re.fullmatch(r"[0-9]+", length):
                raise UploadError("Invalid Content-Length")
            if int(length) > maximum:
                raise UploadTooLarge("File exceeds configured limit")
        return await service.store.upload(dev_id, name, request.stream(), maximum)

    @router.get("/files/{identity}/download")
    async def download(identity: Identifier, dev_id: Identifier):
        await service.device(dev_id)
        value = await service.store.file(dev_id, identity)
        return FileResponse(
            service.store.directory / value["id"],
            filename=value["name"],
            media_type="application/octet-stream",
            headers={"X-Content-Type-Options": "nosniff", "Cache-Control": "no-store"},
        )

    @router.delete("/files/{identity}")
    async def delete_file(identity: Identifier, dev_id: Identifier):
        await service.device(dev_id)
        await service.store.delete_file(dev_id, identity)
        return {}

    return router
