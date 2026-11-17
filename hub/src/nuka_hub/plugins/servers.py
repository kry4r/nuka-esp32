# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import hashlib
import time
from datetime import UTC, datetime
from typing import Annotated, Any

import httpx
from jsonpath_ng.ext import parse as jsonpath
from prometheus_client.parser import text_string_to_metric_families
from pydantic import BaseModel, BeforeValidator, ConfigDict, Field, TypeAdapter

from nuka_hub.core.plugin import Health


def stable_id(source: str, value: str) -> str:
    return "s_" + hashlib.sha256(f"{source}:{value}".encode()).hexdigest()[:20]


class Boundary(BaseModel):
    model_config = ConfigDict(extra="ignore")


def status_uptime(value: str) -> int:
    # Upstream formatUptime: "<days> 天" once a day has passed, otherwise "HH:MM:SS".
    if value.endswith(" 天"):
        return int(value.removesuffix(" 天")) * 86400
    hours, minutes, seconds = (int(part) for part in value.split(":"))
    return hours * 3600 + minutes * 60 + seconds


class StatusServer(Boundary):
    name: str
    alias: str = ""
    online4: bool
    online6: bool
    # Offline entries carry only identity and the online flags.
    cpu: float | None = Field(default=None, ge=0, le=100)
    memory_used: int | None = Field(default=None, ge=0)
    memory_total: int | None = Field(default=None, ge=0)
    hdd_used: int | None = Field(default=None, ge=0)
    hdd_total: int | None = Field(default=None, ge=0)
    network_rx: float | None = Field(default=None, ge=0)
    network_tx: float | None = Field(default=None, ge=0)
    load_1: float | None = Field(default=None, ge=0)
    load_5: float | None = Field(default=None, ge=0)
    load_15: float | None = Field(default=None, ge=0)
    uptime: Annotated[int, BeforeValidator(status_uptime)] | None = None


class StatusResponse(Boundary):
    servers: list[StatusServer]


class KumaMonitor(Boundary):
    id: int
    name: str


class KumaGroup(Boundary):
    monitorList: list[KumaMonitor]


class KumaPage(Boundary):
    publicGroupList: list[KumaGroup]


class Heartbeat(Boundary):
    status: int
    ping: float | None = None


class KumaBeats(Boundary):
    heartbeatList: dict[str, list[Heartbeat]]
    uptimeList: dict[str, float]


class BeszelInfo(Boundary):
    cpu: float = Field(ge=0, le=100)
    mp: float = Field(ge=0, le=100)
    dp: float = Field(ge=0, le=100)
    u: int = Field(default=0, ge=0)


class BeszelSystem(Boundary):
    id: str
    name: str
    status: str
    info: BeszelInfo | None = None


class BeszelPage(Boundary):
    items: list[BeszelSystem]
    totalPages: int


class Chart(Boundary):
    labels: list[str]
    data: list[list[float | None]]


class NodeInfo(Boundary):
    hostname: str


class PromResult(Boundary):
    metric: dict[str, str]
    value: tuple[float, str]


class PromData(Boundary):
    resultType: str
    result: list[PromResult]


class PromResponse(Boundary):
    status: str
    data: PromData


class GenericItem(Boundary):
    name: str
    online: bool
    cpu_pct: float | None = Field(default=None, ge=0, le=100)
    mem_pct: float | None = Field(default=None, ge=0, le=100)
    disk_pct: float | None = Field(default=None, ge=0, le=100)
    mem_used_b: int | None = Field(default=None, ge=0)
    mem_total_b: int | None = Field(default=None, ge=0)
    disk_used_b: int | None = Field(default=None, ge=0)
    disk_total_b: int | None = Field(default=None, ge=0)
    net_up_bps: float | None = Field(default=None, ge=0)
    net_down_bps: float | None = Field(default=None, ge=0)
    latency_ms: float | None = Field(default=None, ge=0)
    uptime_pct: float | None = Field(default=None, ge=0, le=100)
    uptime_s: int | None = Field(default=None, ge=0)
    load: list[float] | None = Field(default=None, min_length=3, max_length=3)


class ServerSource:
    kind = "server"
    doc = "05-server-sources"

    def __init__(self, config: Any, secrets: Any, client: httpx.AsyncClient):
        self.config, self.client = config, client
        self.name = config.name
        self.interval_s, self.timeout_s = config.interval, config.timeout
        self.headers = {
            key: secrets.get(value.removeprefix("secret:"))
            if value.startswith("secret:")
            else value
            for key, value in config.headers.items()
        }
        self.credential = secrets.get(config.auth_secret) if config.auth_secret else ""
        self.basic: tuple[str, str] | None = None
        if self.credential:
            if config.type == "node_exporter":
                username, password = self.credential.split(":", 1)
                self.basic = (username, password)
            elif config.type != "beszel":
                self.headers["Authorization"] = "Bearer " + self.credential

    async def probe(self) -> Health:
        return Health(self.name, self.kind, doc=self.doc)

    async def get(self, path: str = "", **kwargs: Any) -> httpx.Response:
        response = await self.client.get(
            self.config.url.rstrip("/") + path,
            headers=self.headers,
            auth=self.basic,
            timeout=self.timeout_s,
            **kwargs,
        )
        response.raise_for_status()
        return response

    def server(self, name: str, online: bool, identity: str | None = None) -> dict:
        return {
            "id": stable_id(self.name, identity or name),
            "name": name,
            "src": self.config.type,
            "src_name": self.name,
            "online": online,
            "last_seen": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
            "alerts": [],
        }


class ServerStatus(ServerSource):
    async def sample(self) -> list[dict]:
        payload = StatusResponse.model_validate((await self.get()).json())
        result = []
        for item in payload.servers:
            server = self.server(item.alias or item.name, item.online4 or item.online6, item.name)
            if not server["online"]:
                result.append(server)
                continue
            server.update(
                cpu_pct=round(item.cpu, 1),
                mem_used_b=item.memory_used * 1024,
                mem_total_b=item.memory_total * 1024,
                disk_used_b=item.hdd_used * 1024**2,
                disk_total_b=item.hdd_total * 1024**2,
                net_down_bps=item.network_rx,
                net_up_bps=item.network_tx,
                uptime_s=item.uptime,
            )
            if item.load_5 is not None and item.load_15 is not None:
                server["load"] = [item.load_1, item.load_5, item.load_15]
            result.append(server)
        return result


class UptimeKuma(ServerSource):
    async def sample(self) -> list[dict]:
        slug = self.config.options["slug"]
        page = KumaPage.model_validate((await self.get("/api/status-page/" + slug)).json())
        beats = KumaBeats.model_validate(
            (await self.get("/api/status-page/heartbeat/" + slug)).json()
        )
        result = []
        for group in page.publicGroupList:
            for monitor in group.monitorList:
                history = beats.heartbeatList[str(monitor.id)]
                beat = history[-1]
                server = self.server(monitor.name, beat.status == 1, str(monitor.id))
                if beat.ping is not None:
                    server["latency_ms"] = beat.ping
                key = f"{monitor.id}_24"
                if key in beats.uptimeList:
                    server["uptime_pct"] = round(beats.uptimeList[key] * 100, 1)
                result.append(server)
        return result


class Beszel(ServerSource):
    async def probe(self) -> Health:
        if not self.credential:
            return Health(
                self.name, self.kind, "disabled", "Beszel credential is missing", self.doc
            )
        if self.credential.startswith("token:"):
            self.headers["Authorization"] = self.credential.removeprefix("token:")
        else:
            username, password = self.credential.split(":", 1)
            response = await self.client.post(
                self.config.url.rstrip("/") + "/api/collections/users/auth-with-password",
                json={"identity": username, "password": password},
                timeout=self.timeout_s,
            )
            response.raise_for_status()
            token = TypeAdapter(dict[str, Any]).validate_python(response.json())["token"]
            self.headers["Authorization"] = TypeAdapter(str).validate_python(token)
        return Health(self.name, self.kind, doc=self.doc)

    async def sample(self) -> list[dict]:
        result, page_number, total_pages = [], 1, 1
        while page_number <= total_pages:
            response = await self.get(
                "/api/collections/systems/records", params={"page": page_number, "perPage": 200}
            )
            page = BeszelPage.model_validate(response.json())
            for item in page.items:
                server = self.server(item.name, item.status == "up", item.id)
                if item.info is not None:
                    server.update(
                        cpu_pct=round(item.info.cpu, 1),
                        mem_pct=round(item.info.mp, 1),
                        disk_pct=round(item.info.dp, 1),
                        uptime_s=item.info.u,
                    )
                result.append(server)
            total_pages, page_number = page.totalPages, page_number + 1
        return result


class Netdata(ServerSource):
    async def sample(self) -> list[dict]:
        info = NodeInfo.model_validate((await self.get("/api/v1/info")).json())
        server = self.server(info.hostname, True)
        charts = {}
        for name in ("system.cpu", "system.ram", "disk_space._", "system.net"):
            response = await self.get(
                "/api/v1/data", params={"chart": name, "points": 1, "after": -5}
            )
            chart = Chart.model_validate(response.json())
            charts[name] = dict(zip(chart.labels[1:], chart.data[0][1:], strict=True))
        cpu, ram, disk, net = (
            charts[name] for name in ("system.cpu", "system.ram", "disk_space._", "system.net")
        )
        server["cpu_pct"] = round(
            sum(value for key, value in cpu.items() if key != "idle" and value is not None), 1
        )
        server.update(
            mem_used_b=int(ram["used"] * 1024**2),
            mem_total_b=int(sum(ram.values()) * 1024**2),
            disk_used_b=int(disk["used"] * 1024**3),
            disk_total_b=int(sum(disk.values()) * 1024**3),
            net_up_bps=abs(net["sent"]) * 1000 / 8,
            net_down_bps=abs(net["received"]) * 1000 / 8,
        )
        return [server]


class NodeExporter(ServerSource):
    def __init__(self, config: Any, secrets: Any, client: httpx.AsyncClient):
        super().__init__(config, secrets, client)
        self.previous: dict[str, tuple[float, float, float]] = {}
        self.previous_net: dict[str, tuple[float, float, float]] = {}

    async def sample(self) -> list[dict]:
        if self.config.options.get("mode", "exporter") == "prometheus":
            return await self.prometheus()
        response = await self.get("/metrics")
        metrics: dict[str, list[Any]] = {}
        for family in text_string_to_metric_families(response.text):
            for sample in family.samples:
                metrics.setdefault(sample.name, []).append(sample)
        server = self.server(self.name, True)
        cpu = metrics["node_cpu_seconds_total"]
        total, idle = (
            sum(s.value for s in cpu),
            sum(s.value for s in cpu if s.labels["mode"] == "idle"),
        )
        now = time.monotonic()
        if self.name in self.previous:
            previous_total, previous_idle, _ = self.previous[self.name]
            delta = total - previous_total
            if delta > 0:
                server["cpu_pct"] = round((1 - (idle - previous_idle) / delta) * 100, 1)
        self.previous[self.name] = total, idle, now
        values = {name: samples[0].value for name, samples in metrics.items() if len(samples) == 1}
        server["mem_total_b"] = int(values["node_memory_MemTotal_bytes"])
        server["mem_used_b"] = server["mem_total_b"] - int(values["node_memory_MemAvailable_bytes"])
        roots = {
            s.name: s.value
            for name in ("node_filesystem_size_bytes", "node_filesystem_avail_bytes")
            for s in metrics.get(name, [])
            if s.labels["mountpoint"] == "/"
        }
        if roots:
            server["disk_total_b"] = int(roots["node_filesystem_size_bytes"])
            server["disk_used_b"] = server["disk_total_b"] - int(
                roots["node_filesystem_avail_bytes"]
            )
        if "node_boot_time_seconds" in values:
            server["uptime_s"] = int(time.time() - values["node_boot_time_seconds"])
        if all(name in values for name in ("node_load1", "node_load5", "node_load15")):
            server["load"] = [values[name] for name in ("node_load1", "node_load5", "node_load15")]
        if (
            "node_network_transmit_bytes_total" in metrics
            and "node_network_receive_bytes_total" in metrics
        ):
            sent, received = (
                sum(s.value for s in metrics[name] if s.labels["device"] != "lo")
                for name in (
                    "node_network_transmit_bytes_total",
                    "node_network_receive_bytes_total",
                )
            )
            if self.name in self.previous_net:
                old_sent, old_received, old_at = self.previous_net[self.name]
                server.update(
                    net_up_bps=max(0, sent - old_sent) / (now - old_at),
                    net_down_bps=max(0, received - old_received) / (now - old_at),
                )
            self.previous_net[self.name] = sent, received, now
        return [server]

    async def prometheus(self) -> list[dict]:
        queries = {
            "cpu_pct": (
                '100 * (1 - avg by(instance) (rate(node_cpu_seconds_total{mode="idle"}[5m])))'
            ),
            "mem_total_b": "node_memory_MemTotal_bytes",
            "mem_available_b": "node_memory_MemAvailable_bytes",
            "disk_total_b": 'node_filesystem_size_bytes{mountpoint="/"}',
            "disk_available_b": 'node_filesystem_avail_bytes{mountpoint="/"}',
            "net_up_bps": (
                'sum by(instance) (rate(node_network_transmit_bytes_total{device!="lo"}[5m]))'
            ),
            "net_down_bps": (
                'sum by(instance) (rate(node_network_receive_bytes_total{device!="lo"}[5m]))'
            ),
            "uptime_s": "time() - node_boot_time_seconds",
            "online": "up",
        }
        label = self.config.options.get("instance_label", "instance")
        # Grouping uses the same configured identity label across all queries.
        if label != "instance":
            queries = {
                field: query.replace("by(instance)", f"by({label})")
                for field, query in queries.items()
            }
        servers: dict[str, dict] = {}
        for field, query in queries.items():
            payload = PromResponse.model_validate(
                (await self.get("/api/v1/query", params={"query": query})).json()
            )
            if payload.status != "success" or payload.data.resultType != "vector":
                raise ValueError("Prometheus requires a successful instant vector")
            for item in payload.data.result:
                name = item.metric[label]
                if field == "online" and name not in servers:
                    continue
                server = servers.setdefault(name, self.server(name, True))
                value = float(item.value[1])
                server[field] = value == 1 if field == "online" else value
        for server in servers.values():
            for prefix in ("mem", "disk"):
                available = prefix + "_available_b"
                if available in server and prefix + "_total_b" in server:
                    server[prefix + "_used_b"] = int(
                        server[prefix + "_total_b"] - server.pop(available)
                    )
                    server[prefix + "_total_b"] = int(server[prefix + "_total_b"])
            if "cpu_pct" in server:
                server["cpu_pct"] = round(server["cpu_pct"], 1)
            if "uptime_s" in server:
                server["uptime_s"] = int(server["uptime_s"])
        return list(servers.values())


class GenericJson(ServerSource):
    def __init__(self, config: Any, secrets: Any, client: httpx.AsyncClient):
        super().__init__(config, secrets, client)
        self.items = jsonpath(config.options["items"])
        self.fields = {}
        for name, mapping in config.options["fields"].items():
            path, scale = (
                (mapping, 1)
                if isinstance(mapping, str)
                else (mapping["path"], mapping.get("scale", 1))
            )
            self.fields[name] = jsonpath(path), scale

    async def sample(self) -> list[dict]:
        payload = (await self.get()).json()
        result = []
        for match in self.items.find(payload):
            mapped = {}
            for name, (expression, scale) in self.fields.items():
                found = expression.find(match.value)
                if found:
                    value = found[0].value
                    if name == "online" and isinstance(value, str):
                        if value not in {"up", "down", "true", "false", "1", "0"}:
                            raise ValueError("Unsupported online value")
                        value = value in {"up", "true", "1"}
                    elif scale != 1:
                        value *= scale
                    mapped[name] = value
            item = GenericItem.model_validate(mapped).model_dump(exclude_none=True)
            server = self.server(item.pop("name"), item.pop("online"))
            for name in ("cpu_pct", "mem_pct", "disk_pct", "uptime_pct"):
                if name in item:
                    item[name] = round(item[name], 1)
            server.update(item)
            result.append(server)
        return result


def merge_servers(groups: list[list[dict]], config: Any) -> list[dict]:
    if config.by == "none":
        return [server for group in groups for server in group]
    aliases = {
        alias: canonical
        for canonical, names in config.aliases.items()
        for alias in [canonical, *names]
    }
    merged: dict[str, dict] = {}
    for group in groups:
        for server in group:
            name = aliases.get(server["name"], server["name"])
            if name not in merged:
                merged[name] = {**server, "name": name, "id": stable_id("merged", name)}
            else:
                for field, value in server.items():
                    merged[name].setdefault(field, value)
    return list(merged.values())


def build_sources(config: Any, secrets: Any, client: httpx.AsyncClient) -> list[ServerSource]:
    kinds = {
        "serverstatus": ServerStatus,
        "uptime_kuma": UptimeKuma,
        "beszel": Beszel,
        "netdata": Netdata,
        "node_exporter": NodeExporter,
        "generic_json": GenericJson,
    }
    return [kinds[source.type](source, secrets, client) for source in config.source]
