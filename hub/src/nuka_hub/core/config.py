# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import difflib
import ipaddress
import os
import re
import socket
import tomllib
from pathlib import Path
from typing import Annotated, Any, Literal
from urllib.parse import urlsplit
from zoneinfo import ZoneInfoNotFoundError

from platformdirs import user_config_path, user_data_path
from pydantic import BaseModel, BeforeValidator, ConfigDict, Field, ValidationError, model_validator


def seconds(value: str | int | float) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    match = re.fullmatch(r"(\d+(?:\.\d+)?)(ms|s|m|h|d)", value)
    if match is None:
        raise ValueError("duration requires ms, s, m, h or d")
    return float(match[1]) * {"ms": 0.001, "s": 1, "m": 60, "h": 3600, "d": 86400}[match[2]]


Duration = Annotated[float, BeforeValidator(seconds), Field(gt=0)]


class Model(BaseModel):
    model_config = ConfigDict(extra="forbid", validate_default=True, populate_by_name=True)


class Hub(Model):
    name: str = Field(default_factory=socket.gethostname)
    host: str = "0.0.0.0"
    port: int = Field(default=8790, ge=1, le=65535)
    public_url: str = ""
    lang: Literal["zh-CN", "en"] = "zh-CN"
    timezone: str = "auto"
    data_dir: str = ""
    log_level: Literal["debug", "info", "warning", "error"] = "info"

    @model_validator(mode="after")
    def timezone_rule(self) -> Hub:
        from .clock import posix_timezone

        try:
            rule = posix_timezone(self.timezone)
        except ZoneInfoNotFoundError as error:
            raise ValueError("unknown IANA timezone") from error
        if len(rule) > 127 or any(not " " <= character <= "~" for character in rule):
            raise ValueError(
                "timezone requires a printable ASCII POSIX TZ rule of at most 127 bytes"
            )
        return self


class Mdns(Model):
    enabled: bool = True


class CfAccess(Model):
    enabled: bool = False
    team_domain: str = ""
    aud: str = ""
    require_for_ws: bool = True


class Security(Model):
    lan_cidrs: list[str] = [
        "10.0.0.0/8",
        "172.16.0.0/12",
        "192.168.0.0/16",
        "100.64.0.0/10",
        "fc00::/7",
        "fe80::/10",
    ]
    trusted_proxies: list[str] = []
    public_detect_headers: list[str] = ["Cf-Connecting-Ip", "Cf-Ray", "X-Forwarded-For"]
    web_approve_permissions: bool = False
    allow_lan_hooks: bool = False
    session_days: int = Field(default=30, gt=0)
    remote_admin: bool = False
    cf_access: CfAccess = Field(default_factory=CfAccess)

    @model_validator(mode="after")
    def networks(self) -> Security:
        for value in [*self.lan_cidrs, *self.trusted_proxies]:
            ipaddress.ip_network(value)
        if self.remote_admin and not (
            self.cf_access.enabled and self.cf_access.team_domain and self.cf_access.aud
        ):
            raise ValueError("remote_admin requires Cloudflare Access team_domain and aud")
        return self


class Psutil(Model):
    fast_interval: Duration = 1
    disk_interval: Duration = 60


class Nvml(Model):
    interval: Duration = 1
    gpu_indices: list[int] = []


class SensorMap(Model):
    field: str = Field(
        pattern=(
            "^(?:cpu\\.(?:temp_c|power_w|freq_mhz|pct)|gpus\\[\\d+\\]\\.("
            "?:pct|temp_c|power_w|fan_pct|clock_mhz|mem_used_b|mem_t"
            "otal_b)|temps\\[\\d+\\]\\.c|fans\\[\\d+\\]\\.rpm)$"
        )
    )
    sensor_id: str = ""
    name_regex: str = ""

    @model_validator(mode="after")
    def selector(self) -> SensorMap:
        if not self.sensor_id and not self.name_regex:
            raise ValueError("sensor_id or name_regex is required")
        if self.name_regex:
            try:
                re.compile(self.name_regex)
            except re.error as error:
                raise ValueError("invalid sensor name_regex") from error
        return self


class Lhm(Model):
    url: str = "http://127.0.0.1:8085/data.json"
    user: str = ""
    password_secret: str = ""
    interval: Duration = 2
    map: list[SensorMap] = []


class Macmon(Model):
    path: str = "macmon"
    interval: Duration = 2


class LinuxSensors(Model):
    interval: Duration = 2
    cpu_temp_labels: list[str] = ["Tctl", "Package id 0", "k10temp", "coretemp"]


class Collectors(Model):
    enabled: list[Literal["psutil", "nvml", "lhm", "macmon", "linux_sensors", "amd_sysfs"]] = [
        "psutil",
        "nvml",
        "lhm",
        "macmon",
        "linux_sensors",
        "amd_sysfs",
    ]
    host_name: str = ""
    psutil: Psutil = Field(default_factory=Psutil)
    nvml: Nvml = Field(default_factory=Nvml)
    lhm: Lhm = Field(default_factory=Lhm)
    macmon: Macmon = Field(default_factory=Macmon)
    linux_sensors: LinuxSensors = Field(default_factory=LinuxSensors)
    priority: dict[str, list[str]] = {
        "cpu.temp_c": ["lhm", "linux_sensors", "macmon"],
        "gpus": ["nvml", "lhm", "amd_sysfs", "macmon"],
    }


class Source(Model):
    type: Literal[
        "serverstatus", "uptime_kuma", "beszel", "netdata", "node_exporter", "generic_json"
    ]
    name: str
    url: str
    interval: Duration = 5
    timeout: Duration = 5
    auth_secret: str = ""
    headers: dict[str, str] = {}
    options: dict[str, Any] = {}

    @model_validator(mode="after")
    def options_boundary(self) -> Source:
        if self.type == "node_exporter" and "instance_label" in self.options:
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", self.options["instance_label"]) is None:
                raise ValueError("invalid Prometheus instance_label")
        return self


class Merge(Model):
    by: Literal["name", "none"] = "name"
    aliases: dict[str, list[str]] = {}


class Servers(Model):
    source: list[Source] = []
    merge: Merge = Field(default_factory=Merge)

    @model_validator(mode="after")
    def source_names(self) -> Servers:
        if len({source.name for source in self.source}) != len(self.source):
            raise ValueError("server source names must be unique")
        return self


class Rule(Model):
    name: str
    scope: Literal["hosts", "servers", "ai"] = "servers"
    when: str
    for_: Duration = Field(default=60, alias="for")
    level: Literal["info", "warn", "critical"] = "warn"
    title: str = ""

    @model_validator(mode="after")
    def expression(self) -> Rule:
        from nuka_hub.plugins.alerts import Expression

        Expression(self.when, self.scope)
        return self


class Alerts(Model):
    builtin: dict[str, bool] = {
        "server_offline": True,
        "cpu_high": True,
        "mem_high": True,
        "disk_high": True,
        "ai_exhausted": True,
        "ai_reauth": True,
    }
    repeat: Duration = 1800
    rule: list[Rule] = []

    @model_validator(mode="before")
    @classmethod
    def builtin_defaults(cls, value):
        if "builtin" in value:
            value["builtin"] = {**cls.model_fields["builtin"].default, **value["builtin"]}
        return value


class Account(Model):
    label: str = ""
    hidden: bool = False


class Provider(Model):
    type: Literal["cpamp", "cpa"]
    name: str
    base_url: str
    key_secret: str
    passive_interval: Duration = 60
    snapshot_interval: Duration = 120
    active_enabled: bool = True
    active_min_interval: Annotated[float, BeforeValidator(seconds), Field(ge=300)] = 600
    max_rpm: int = Field(default=20, ge=1, le=20)
    accounts: dict[str, Account] = {}


class Ai(Model):
    stale_after: Duration = 1800
    manual_refresh_per_min: int = Field(default=1, ge=1, le=1)
    label_mode: Literal["masked_email", "index"] = "masked_email"
    provider: list[Provider] = []

    @model_validator(mode="after")
    def provider_names(self) -> Ai:
        if len({provider.name for provider in self.provider}) != len(self.provider):
            raise ValueError("AI provider names must be unique")
        return self


class Agents(Model):
    enabled_sources: list[Literal["claude", "codex", "gemini"]] = ["claude", "codex", "gemini"]
    permission_timeout: Annotated[float, BeforeValidator(seconds), Field(gt=0, lt=90)] = 60
    show_project: Literal["basename", "hidden"] = "basename"
    collect_last_message: bool = True
    idle_after: Duration = 600
    purge_after: Duration = 7200


class Media(Model):
    enabled: bool = True


class Weather(Model):
    enabled: bool = False
    provider: Literal["open_meteo"] = "open_meteo"
    lat: float | None = Field(default=None, ge=-90, le=90)
    lon: float | None = Field(default=None, ge=-180, le=180)
    city: str = ""
    units: Literal["metric", "imperial"] = "metric"
    interval: Duration = 900

    @model_validator(mode="after")
    def location(self) -> Weather:
        if self.enabled and not self.city and (self.lat is None or self.lon is None):
            raise ValueError("weather requires explicit lat/lon or city")
        return self


class Notify(Model):
    quiet_hours: str = ""
    dedupe_window: Duration = 300
    keep: int = Field(default=500, gt=0)
    hook_rate_per_min: int = Field(default=30, gt=0)
    hook_burst: int = Field(default=10, gt=0)

    @model_validator(mode="after")
    def quiet_period(self) -> Notify:
        if (
            self.quiet_hours
            and re.fullmatch(
                r"(?:[01]\d|2[0-3]):[0-5]\d-(?:[01]\d|2[0-3]):[0-5]\d", self.quiet_hours
            )
            is None
        ):
            raise ValueError("quiet_hours requires HH:MM-HH:MM")
        return self


AppId = Literal[
    "clock",
    "hosts",
    "servers",
    "ai",
    "buddy",
    "pet",
    "pomodoro",
    "media",
    "weather",
    "vault",
    "settings",
]


class KeyLong(Model):
    action: Literal["talk", "dnd", "always_on", "pomodoro", "macro", "none"] = "talk"
    slot: int | None = Field(default=None, ge=0, le=11)

    @model_validator(mode="after")
    def macro(self) -> KeyLong:
        if (self.action == "macro") != (self.slot is not None):
            raise ValueError("slot is required only for macro action")
        return self


class Macro(Model):
    slot: int = Field(ge=0, le=11)
    label: str
    kind: Literal["keys", "text"]
    value: str = Field(max_length=256)


class Devices(Model):
    default_lang: Literal["", "zh-CN", "en"] = ""
    push_overrides: dict[str, Duration] = {}
    key3_long: KeyLong = Field(default_factory=KeyLong)
    key3_double_card: AppId = "buddy"
    key3_clock_style: bool = True
    home_face: Literal["clock", "assistant"] = "clock"
    volume: int | None = Field(default=None, ge=0, le=100)
    assistant: bool = False
    t_dim: int = Field(default=20, gt=0, le=86400)
    t_off: int = Field(default=10, gt=0, le=86400)
    macro: list[Macro] = Field(default=[], max_length=12)

    @model_validator(mode="after")
    def intervals(self) -> Devices:
        allowed = {
            "hosts_active",
            "hosts_inactive",
            "servers_active",
            "servers_inactive",
            "detail",
            "ai_min",
            "low_power_min",
        }
        if set(self.push_overrides) - allowed:
            raise ValueError("unknown push_overrides key")
        for key, minimum in (("ai_min", 10), ("low_power_min", 5)):
            if key in self.push_overrides and self.push_overrides[key] < minimum:
                raise ValueError(f"{key} must be >= {minimum}s")
        if len({m.slot for m in self.macro}) != len(self.macro):
            raise ValueError("duplicate macro slot")
        return self


class Ota(Model):
    dir: str = ""
    auto_offer: bool = False


class Store(Model):
    raw_retention: Duration = 86400
    downsampled_retention: Duration = 2592000
    audit_retention: Duration = 15552000


class Web(Model):
    enabled: bool = True
    allow_config_edit: bool = True


class Agent(Model):
    hub_url: str = ""
    token_secret: str = "agent.token"
    name: str = ""
    send_media: bool = True
    cf_client_id_secret: str = ""
    cf_client_secret_secret: str = ""

    @model_validator(mode="after")
    def cf_pair(self) -> Agent:
        if bool(self.cf_client_id_secret) != bool(self.cf_client_secret_secret):
            raise ValueError("both Cloudflare secret references are required")
        return self


class AssistantLlm(Model):
    base_url: str = ""
    model: str = ""
    key_secret: str = ""
    temperature: float = Field(default=0.7, ge=0, le=2)
    context_window: int = Field(default=32768, ge=1024, le=1048576)

    @model_validator(mode="after")
    def origin(self) -> AssistantLlm:
        if self.base_url:
            url = urlsplit(self.base_url)
            if (
                url.scheme not in {"http", "https"}
                or not url.hostname
                or url.username
                or url.password
            ):
                raise ValueError("base_url requires HTTP/HTTPS without login credentials")
        return self


class AssistantHistory(Model):
    enabled: bool = True
    context_turns: int = Field(default=20, ge=1)


class AssistantMemory(Model):
    enabled: bool = True


class AssistantUploads(Model):
    max_bytes: int = Field(default=67108864, ge=1)


class Assistant(Model):
    enabled: bool = False
    name: str = Field(default="Nuka", min_length=1, max_length=64)
    system_prompt: str = Field(default="You are Nuka. Reply briefly.", max_length=16384)
    lang: Literal["zh-CN", "en"] = "zh-CN"
    max_output_tokens: int = Field(default=1024, ge=1)
    llm: AssistantLlm = Field(default_factory=AssistantLlm)
    history: AssistantHistory = Field(default_factory=AssistantHistory)
    memory: AssistantMemory = Field(default_factory=AssistantMemory)
    uploads: AssistantUploads = Field(default_factory=AssistantUploads)

    @model_validator(mode="after")
    def budgets(self) -> Assistant:
        if self.enabled and not (self.llm.base_url and self.llm.model):
            raise ValueError("enabled assistant requires llm.base_url and llm.model")
        if self.max_output_tokens >= self.llm.context_window:
            raise ValueError("max_output_tokens must be smaller than llm.context_window")
        return self


class Config(Model):
    hub: Hub = Field(default_factory=Hub)
    mdns: Mdns = Field(default_factory=Mdns)
    security: Security = Field(default_factory=Security)
    collectors: Collectors = Field(default_factory=Collectors)
    servers: Servers = Field(default_factory=Servers)
    alerts: Alerts = Field(default_factory=Alerts)
    ai: Ai = Field(default_factory=Ai)
    agents: Agents = Field(default_factory=Agents)
    media: Media = Field(default_factory=Media)
    weather: Weather = Field(default_factory=Weather)
    notify: Notify = Field(default_factory=Notify)
    devices: Devices = Field(default_factory=Devices)
    ota: Ota = Field(default_factory=Ota)
    store: Store = Field(default_factory=Store)
    web: Web = Field(default_factory=Web)
    agent: Agent = Field(default_factory=Agent)
    assistant: Assistant = Field(default_factory=Assistant)


class ConfigError(ValueError):
    def __init__(self, details: list[dict]):
        self.details = details
        super().__init__("; ".join(f"line {d['line']}: {d['msg']}" for d in details))


def config_path(value: str | None = None) -> Path:
    return Path(
        value
        or os.environ.get("NUKA_CONFIG")
        or user_config_path("nuka", appauthor=False, roaming=True) / "config.toml"
    )


def data_path(config: Config) -> Path:
    if config.hub.data_dir:
        return Path(config.hub.data_dir)
    directory = user_data_path("nuka", appauthor=False)
    return directory / "data" if os.name == "nt" else directory


def parse_config(text: str, *, environment: bool = True) -> Config:
    try:
        raw = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        line = (
            int(re.search(r"line (\d+)", str(error))[1])
            if "line " in str(error)
            else len(text.splitlines())
        )
        raise ConfigError([{"line": max(1, line), "msg": str(error)}]) from error
    if environment:
        for key, value in os.environ.items():
            if not key.startswith("NUKA__"):
                continue
            parts = key.removeprefix("NUKA__").lower().split("__")
            target = raw
            for part in parts[:-1]:
                target = target.setdefault(part, {})
            try:
                target[parts[-1]] = tomllib.loads(f"value = {value}")["value"]
            except tomllib.TOMLDecodeError:
                target[parts[-1]] = value
    try:
        return Config.model_validate(raw)
    except ValidationError as error:
        details = []
        for issue in error.errors(include_input=False, include_url=False):
            parts = [str(p) for p in issue["loc"]]
            key = parts[-1] if parts else ""
            line = next(
                (
                    n
                    for key in reversed(parts)
                    for n, row in enumerate(text.splitlines(), 1)
                    if re.match(rf'\s*"?{re.escape(key)}"?\s*=', row)
                ),
                1,
            )
            message = f"{'.'.join(parts)}: {issue['msg']}"
            if issue["type"] == "extra_forbidden":
                model = Config
                for part in parts[:-1]:
                    field = model.model_fields.get(part)
                    if (
                        field is None
                        or not isinstance(field.annotation, type)
                        or not issubclass(field.annotation, BaseModel)
                    ):
                        break
                    model = field.annotation
                closest = difflib.get_close_matches(key, model.model_fields, n=1)
                if closest:
                    message += f"; did you mean {closest[0]}?"
            details.append({"line": line, "msg": message})
        raise ConfigError(details) from error


def load_config(path: Path) -> Config:
    return parse_config(path.read_text(encoding="utf-8"))
