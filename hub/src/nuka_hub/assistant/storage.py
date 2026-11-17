# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import hashlib
import json
import secrets
import shutil
import sqlite3
from collections.abc import AsyncIterator
from contextlib import closing
from pathlib import Path

from nuka_hub.core.history import History, iso

DDL = """
CREATE TABLE IF NOT EXISTS assistant_sessions(
 id TEXT PRIMARY KEY,dev_id TEXT NOT NULL,created_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS assistant_turns(
 seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE,dev_id TEXT,session_id TEXT,
 complete INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS assistant_messages(
 seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE,dev_id TEXT,session_id TEXT,
 turn_id TEXT,role TEXT,text TEXT,created_at TEXT,tool_call_id TEXT,tool_name TEXT,
 tool_state TEXT,file_ids TEXT,payload TEXT);
CREATE TABLE IF NOT EXISTS assistant_memories(
 seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE,dev_id TEXT,text TEXT,
 source_message_id TEXT,created_at TEXT,updated_at TEXT);
CREATE TABLE IF NOT EXISTS assistant_files(
 seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE,dev_id TEXT,name TEXT,
 size INTEGER,sha256 TEXT,created_at TEXT);
CREATE INDEX IF NOT EXISTS assistant_message_scope ON assistant_messages(dev_id,session_id,seq);
CREATE INDEX IF NOT EXISTS assistant_turn_scope ON assistant_turns(dev_id,session_id,seq);
CREATE INDEX IF NOT EXISTS assistant_memory_scope ON assistant_memories(dev_id,seq);
CREATE INDEX IF NOT EXISTS assistant_file_scope ON assistant_files(dev_id,seq);
"""


class MissingResource(LookupError):
    pass


class UploadError(ValueError):
    pass


class UploadTooLarge(UploadError):
    pass


def new_id(prefix: str) -> str:
    return prefix + secrets.token_hex(10)


def public_message(row: dict) -> dict:
    result = {
        key: row[key]
        for key in (
            "id",
            "dev_id",
            "session_id",
            "role",
            "text",
            "created_at",
            "tool_call_id",
            "tool_name",
            "tool_state",
        )
        if row[key] is not None
    }
    if row["file_ids"]:
        result["file_ids"] = json.loads(row["file_ids"])
    return result


def resource(row: dict) -> dict:
    return {key: value for key, value in row.items() if key != "seq" and value is not None}


class AssistantStorage:
    def __init__(self, history: History, data: Path):
        self.history = history
        self.directory = data / "assistant-files"
        self.directory.mkdir(parents=True, exist_ok=True)
        self.history.connection.executescript(DDL)
        self.file_lock = asyncio.Lock()

    async def session(self, dev_id: str, session_id: str) -> bool:
        return bool(
            await self.history.sql(
                "SELECT id FROM assistant_sessions WHERE id=? AND dev_id=?", (session_id, dev_id)
            )
        )

    async def start(self, dev_id: str, session_id: str, turn_id: str) -> None:
        def insert():
            with self.history.connection as db:
                db.execute(
                    "INSERT OR IGNORE INTO assistant_sessions VALUES (?,?,?)",
                    (session_id, dev_id, iso()),
                )
                db.execute(
                    "INSERT INTO assistant_turns(id,dev_id,session_id) VALUES (?,?,?)",
                    (turn_id, dev_id, session_id),
                )

        await self.history.work(insert)

    async def add_message(
        self,
        dev_id: str,
        session_id: str,
        turn_id: str,
        role: str,
        text: str,
        *,
        identity: str | None = None,
        payload: dict | None = None,
        file_ids: list[str] | None = None,
        tool_call_id: str | None = None,
        tool_name: str | None = None,
        tool_state: str | None = None,
    ) -> str:
        identity = identity or new_id("m_")
        await self.history.sql(
            "INSERT INTO assistant_messages(id,dev_id,session_id,turn_id,role,text,created_at,"
            "tool_call_id,tool_name,tool_state,file_ids,payload) VALUES (?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET payload=excluded.payload",
            (
                identity,
                dev_id,
                session_id,
                turn_id,
                role,
                text,
                iso(),
                tool_call_id,
                tool_name,
                tool_state,
                json.dumps(file_ids) if file_ids else None,
                json.dumps(payload, ensure_ascii=False) if payload else None,
            ),
        )
        return identity

    async def complete(self, turn_id: str) -> None:
        await self.history.sql("UPDATE assistant_turns SET complete=1 WHERE id=?", (turn_id,))

    async def context(self, dev_id: str, session_id: str, turns: int) -> list[list[dict]]:
        rows = await self.history.sql(
            "SELECT id FROM assistant_turns WHERE dev_id=? AND session_id=? AND complete=1 "
            "ORDER BY seq DESC LIMIT ?",
            (dev_id, session_id, turns),
        )
        groups = []
        for row in reversed(rows):
            messages = await self.history.sql(
                "SELECT payload FROM assistant_messages WHERE dev_id=? AND turn_id=? "
                "AND payload IS NOT NULL ORDER BY seq",
                (dev_id, row["id"]),
            )
            groups.append([json.loads(item["payload"]) for item in messages])
        return groups

    async def page(
        self,
        kind: str,
        dev_id: str,
        limit: int,
        before: str,
        *,
        session_id: str = "",
        query: str = "",
    ) -> dict:
        table = {
            "history": "assistant_messages",
            "memories": "assistant_memories",
            "files": "assistant_files",
        }[kind]
        where, values = ["dev_id=?"], [dev_id]
        if session_id:
            where.append("session_id=?")
            values.append(session_id)
        if query:
            where.append("instr(lower(text),lower(?))>0")
            values.append(query)
        if before:
            cursor = await self.history.sql(
                f"SELECT seq FROM {table} WHERE id=? AND " + " AND ".join(where),
                (before, *values),
            )
            if not cursor:
                raise MissingResource("Cursor not found")
            where.append("seq<?")
            values.append(cursor[0]["seq"])
        rows = await self.history.sql(
            f"SELECT * FROM {table} WHERE " + " AND ".join(where) + " ORDER BY seq DESC LIMIT ?",
            (*values, limit + 1),
        )
        format_row = public_message if kind == "history" else resource
        result = {"items": [format_row(row) for row in rows[:limit]]}
        if len(rows) > limit:
            result["next_before"] = rows[limit - 1]["id"]
        return result

    async def export(self, dev_id: str, session_id: str) -> AsyncIterator[bytes]:
        where = "dev_id=?" + (" AND session_id=?" if session_id else "")
        values = (dev_id, session_id) if session_id else (dev_id,)
        maximum = (
            await self.history.sql(
                f"SELECT COALESCE(MAX(seq),0) AS stop FROM assistant_messages WHERE {where}", values
            )
        )[0]["stop"]
        cursor = 0
        while True:
            rows = await self.history.sql(
                f"SELECT * FROM assistant_messages WHERE {where} AND seq>? AND seq<=? "
                "ORDER BY seq LIMIT 100",
                (*values, cursor, maximum),
            )
            if not rows:
                return
            for row in rows:
                yield (json.dumps(public_message(row), ensure_ascii=False) + "\n").encode()
            cursor = rows[-1]["seq"]

    async def delete_session(self, dev_id: str, session_id: str) -> None:
        def remove():
            with self.history.connection as db:
                db.execute(
                    "DELETE FROM assistant_memories WHERE dev_id=? AND source_message_id IN "
                    "(SELECT id FROM assistant_messages WHERE dev_id=? AND session_id=?)",
                    (dev_id, dev_id, session_id),
                )
                for table in ("assistant_messages", "assistant_turns"):
                    db.execute(
                        f"DELETE FROM {table} WHERE dev_id=? AND session_id=?", (dev_id, session_id)
                    )
                db.execute(
                    "DELETE FROM assistant_sessions WHERE dev_id=? AND id=?", (dev_id, session_id)
                )

        await self.history.work(remove)

    async def source(self, dev_id: str, message_id: str) -> None:
        if not await self.history.sql(
            "SELECT id FROM assistant_messages WHERE id=? AND dev_id=?", (message_id, dev_id)
        ):
            raise MissingResource("Source message not found")

    async def add_memory(self, dev_id: str, text: str, source_message_id: str | None) -> dict:
        identity, created = new_id("mem_"), iso()
        await self.history.sql(
            "INSERT INTO assistant_memories(id,dev_id,text,source_message_id,"
            "created_at,updated_at) "
            "VALUES (?,?,?,?,?,?)",
            (identity, dev_id, text, source_message_id, created, created),
        )
        result = {
            "id": identity,
            "dev_id": dev_id,
            "text": text,
            "created_at": created,
            "updated_at": created,
        }
        if source_message_id:
            result["source_message_id"] = source_message_id
        return result

    async def delete_memory(self, dev_id: str, identity: str) -> None:
        rows = await self.history.sql(
            "DELETE FROM assistant_memories WHERE id=? AND dev_id=? RETURNING id",
            (identity, dev_id),
        )
        if not rows:
            raise MissingResource("Memory not found")

    async def retrieve(self, dev_id: str, query: str) -> list[dict]:
        rows = await self.history.sql(
            "SELECT * FROM assistant_memories WHERE dev_id=? "
            "ORDER BY (instr(lower(text),lower(?))>0) DESC,seq DESC LIMIT 8",
            (dev_id, query),
        )
        result, count = [], 0
        for row in rows:
            if count + len(row["text"]) > 8000:
                break
            result.append({"id": row["id"], "text": row["text"]})
            count += len(row["text"])
        return result

    async def file(self, dev_id: str, identity: str) -> dict:
        rows = await self.history.sql(
            "SELECT * FROM assistant_files WHERE id=? AND dev_id=?", (identity, dev_id)
        )
        if not rows:
            raise MissingResource("File not found")
        return resource(rows[0])

    async def upload(
        self, dev_id: str, name: str, chunks: AsyncIterator[bytes], maximum: int
    ) -> dict:
        identity = new_id("f_")
        temporary = self.directory / (identity + ".part")
        destination = self.directory / identity
        committed, size, digest = False, 0, hashlib.sha256()
        prefix = bytearray()
        async with self.file_lock:
            try:
                with temporary.open("xb") as handle:
                    async for chunk in chunks:
                        size += len(chunk)
                        if size > maximum:
                            raise UploadTooLarge("File exceeds configured limit")
                        prefix.extend(chunk[: max(0, 4 - len(prefix))])
                        if prefix == b"NVB1":
                            raise UploadError("Vault backups must use the USB CLI")
                        digest.update(chunk)
                        write = asyncio.create_task(asyncio.to_thread(handle.write, chunk))
                        try:
                            await asyncio.shield(write)
                        except asyncio.CancelledError:
                            try:
                                await write
                            finally:
                                raise

                async def commit():
                    nonlocal committed
                    await asyncio.to_thread(temporary.replace, destination)
                    created = iso()
                    await self.history.sql(
                        "INSERT INTO assistant_files(id,dev_id,name,size,sha256,created_at) "
                        "VALUES (?,?,?,?,?,?)",
                        (identity, dev_id, name, size, digest.hexdigest(), created),
                    )
                    committed = True
                    return {
                        "id": identity,
                        "dev_id": dev_id,
                        "name": name,
                        "size": size,
                        "sha256": digest.hexdigest(),
                        "created_at": created,
                    }

                transaction = asyncio.create_task(commit())
                try:
                    return await asyncio.shield(transaction)
                except asyncio.CancelledError:
                    try:
                        await transaction
                    finally:
                        raise
            finally:
                temporary.unlink(missing_ok=True)
                if not committed:
                    destination.unlink(missing_ok=True)

    async def delete_file(self, dev_id: str, identity: str) -> None:
        async with self.file_lock:
            await self.file(dev_id, identity)

            def remove():
                with self.history.connection as db:
                    (self.directory / identity).unlink()
                    db.execute(
                        "DELETE FROM assistant_files WHERE id=? AND dev_id=?", (identity, dev_id)
                    )

            await self.history.work(remove)

    async def backup(self, destination: Path) -> None:
        async with self.file_lock:

            def snapshot():
                destination.mkdir(parents=True, exist_ok=False)
                with closing(sqlite3.connect(destination / "nuka.sqlite3")) as target:
                    self.history.connection.backup(target)
                    rows = target.execute("SELECT id FROM assistant_files").fetchall()
                attachments = destination / "assistant-files"
                attachments.mkdir()
                for row in rows:
                    shutil.copy2(self.directory / row[0], attachments / row[0])

            await self.history.work(snapshot)
