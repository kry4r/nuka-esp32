# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import json
import sqlite3
import time
from datetime import UTC, datetime
from pathlib import Path

DDL = """
CREATE TABLE IF NOT EXISTS metric_points(host_id TEXT,ts REAL,key TEXT,value REAL);
CREATE TABLE IF NOT EXISTS server_points(
 server_id TEXT,ts REAL,cpu REAL,mem REAL,disk REAL,online INTEGER);
CREATE TABLE IF NOT EXISTS device_battery(
 dev_id TEXT,ts REAL,pct REAL,mv INTEGER,charging TEXT,src TEXT);
CREATE TABLE IF NOT EXISTS ai_windows(
 account_id TEXT,ts REAL,kind TEXT,used_pct REAL,reset_at TEXT,source TEXT);
CREATE TABLE IF NOT EXISTS notifications(
 nid TEXT PRIMARY KEY,ts REAL,level TEXT,source TEXT,title TEXT,body TEXT,
 dedupe_key TEXT,payload TEXT);
CREATE TABLE IF NOT EXISTS audit(id TEXT PRIMARY KEY,ts REAL,kind TEXT,actor TEXT,detail TEXT);
CREATE TABLE IF NOT EXISTS tokens(
 id TEXT PRIMARY KEY,scope TEXT,name TEXT,sha256 TEXT UNIQUE,created TEXT,last_used TEXT,
 revoked INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS devices(
 dev_id TEXT PRIMARY KEY,name TEXT,fw TEXT,last_seen TEXT,last_state_json TEXT,token_id TEXT);
CREATE INDEX IF NOT EXISTS metric_lookup ON metric_points(host_id,ts,key);
CREATE INDEX IF NOT EXISTS server_lookup ON server_points(server_id,ts);
"""


def iso(epoch: float | None = None) -> str:
    return (
        datetime.fromtimestamp(time.time() if epoch is None else epoch, UTC)
        .isoformat()
        .replace("+00:00", "Z")
    )


class History:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(path, check_same_thread=False)
        self.connection.row_factory = sqlite3.Row
        self.connection.execute("PRAGMA journal_mode=WAL")
        self.connection.executescript(DDL)
        self.lock = asyncio.Lock()

    async def work(self, action):
        async with self.lock:
            task = asyncio.create_task(asyncio.to_thread(action))
            try:
                return await asyncio.shield(task)
            except asyncio.CancelledError:
                # A sqlite worker keeps running after cancellation; retain its lock until it ends.
                try:
                    await task
                finally:
                    raise

    async def sql(self, statement: str, values: tuple = ()) -> list[dict]:
        def execute():
            cursor = self.connection.execute(statement, values)
            rows = [dict(row) for row in cursor.fetchall()]
            self.connection.commit()
            return rows

        return await self.work(execute)

    async def audit(self, kind: str, actor: str, detail: dict) -> None:
        import secrets

        await self.sql(
            "INSERT INTO audit VALUES (?,?,?,?,?)",
            ("au_" + secrets.token_hex(6), time.time(), kind, actor, json.dumps(detail)),
        )

    async def metrics(self, host: dict) -> None:
        values = {}
        if "cpu" in host:
            values.update(
                {"cpu." + k: v for k, v in host["cpu"].items() if isinstance(v, (int, float))}
            )
        if host.get("gpus"):
            values.update(
                {"gpu." + k: v for k, v in host["gpus"][0].items() if isinstance(v, (int, float))}
            )
        if "mem" in host and "used_b" in host["mem"] and "total_b" in host["mem"]:
            values["mem.pct"] = round(host["mem"]["used_b"] / host["mem"]["total_b"] * 100, 1)
        observed = time.time()
        for key, value in values.items():
            await self.sql(
                "INSERT INTO metric_points VALUES (?,?,?,?)", (host["id"], observed, key, value)
            )

    async def query(self, kind: str, identity: str, keys: list[str], span: str) -> dict:
        seconds = {"1h": 3600, "24h": 86400, "30d": 2592000}[span]
        if kind == "hosts":
            rows = await self.sql(
                "SELECT ts,key,value FROM metric_points WHERE host_id=? AND ts>? ORDER BY ts",
                (identity, time.time() - seconds),
            )
            groups = {}
            for row in rows:
                groups.setdefault(row["ts"], {})[row["key"]] = row["value"]
        else:
            rows = await self.sql(
                "SELECT * FROM server_points WHERE server_id=? AND ts>? ORDER BY ts",
                (identity, time.time() - seconds),
            )
            groups = {
                row["ts"]: {"cpu.pct": row["cpu"], "mem.pct": row["mem"], "disk.pct": row["disk"]}
                for row in rows
            }
        points = [
            (ts, row)
            for ts, row in groups.items()
            if all(key in row and row[key] is not None for key in keys)
        ]
        return {
            "ts": [iso(ts) for ts, _ in points],
            "series": {key: [row[key] for _, row in points] for key in keys},
        }

    async def server(self, server: dict) -> None:
        mem = server.get("mem_pct")
        if "mem_used_b" in server and "mem_total_b" in server:
            mem = round(server["mem_used_b"] / server["mem_total_b"] * 100, 1)
        disk = server.get("disk_pct")
        if "disk_used_b" in server and "disk_total_b" in server:
            disk = round(server["disk_used_b"] / server["disk_total_b"] * 100, 1)
        await self.sql(
            "INSERT INTO server_points VALUES (?,?,?,?,?,?)",
            (server["id"], time.time(), server.get("cpu_pct"), mem, disk, server["online"]),
        )

    async def cleanup(self, config) -> None:
        now = time.time()
        cutoff = now - config.raw_retention

        def downsample():
            connection = self.connection
            metric = connection.execute(
                (
                    "SELECT host_id,CAST(ts/300 AS INTEGER)*300,key,AVG(valu"
                    "e) FROM metric_points WHERE ts<? GROUP BY host_id,CAST("
                    "ts/300 AS INTEGER),key"
                ),
                (cutoff,),
            ).fetchall()
            server = connection.execute(
                (
                    "SELECT server_id,CAST(ts/300 AS INTEGER)*300,AVG(cpu),A"
                    "VG(mem),AVG(disk),MIN(online) FROM server_points WHERE "
                    "ts<? GROUP BY server_id,CAST(ts/300 AS INTEGER)"
                ),
                (cutoff,),
            ).fetchall()
            with connection:
                connection.execute("DELETE FROM metric_points WHERE ts<?", (cutoff,))
                connection.execute("DELETE FROM server_points WHERE ts<?", (cutoff,))
                connection.executemany("INSERT INTO metric_points VALUES (?,?,?,?)", metric)
                connection.executemany("INSERT INTO server_points VALUES (?,?,?,?,?,?)", server)

        async with self.lock:
            await asyncio.to_thread(downsample)
        await self.sql(
            "DELETE FROM metric_points WHERE ts<?", (now - config.downsampled_retention,)
        )
        await self.sql(
            "DELETE FROM server_points WHERE ts<?", (now - config.downsampled_retention,)
        )
        await self.sql("DELETE FROM device_battery WHERE ts<?", (now - 2592000,))
        await self.sql("DELETE FROM ai_windows WHERE ts<?", (now - 2592000,))
        await self.sql("DELETE FROM audit WHERE ts<?", (now - config.audit_retention,))

    async def close(self):
        await asyncio.to_thread(self.connection.close)
