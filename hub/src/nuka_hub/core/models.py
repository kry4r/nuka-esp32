# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

from .wire import contains_nul


class WireModel(BaseModel):
    model_config = ConfigDict(extra="ignore", populate_by_name=True)

    def wire(self) -> dict:
        return self.model_dump(mode="json", by_alias=True, exclude_none=True)


class RestCpu(WireModel):
    pct: float | None = Field(default=None, ge=0, le=100)
    per_core: list[float] | None = Field(default=None)
    temp_c: float | None = Field(default=None)
    freq_mhz: float | None = Field(default=None, ge=0)
    power_w: float | None = Field(default=None, ge=0)


class RestGpu(WireModel):
    name: str
    pct: float | None = Field(default=None, ge=0, le=100)
    temp_c: float | None = Field(default=None)
    mem_used_b: int | None = Field(default=None, ge=0)
    mem_total_b: int | None = Field(default=None, ge=0)
    power_w: float | None = Field(default=None, ge=0)
    fan_pct: float | None = Field(default=None, ge=0, le=100)
    clock_mhz: float | None = Field(default=None, ge=0)


class RestMem(WireModel):
    used_b: int | None = Field(default=None, ge=0)
    total_b: int | None = Field(default=None, ge=0)
    swap_used_b: int | None = Field(default=None, ge=0)
    swap_total_b: int | None = Field(default=None, ge=0)


class RestDisk(WireModel):
    mount: str
    used_b: int | None = Field(default=None, ge=0)
    total_b: int | None = Field(default=None, ge=0)
    read_bps: float | None = Field(default=None, ge=0)
    write_bps: float | None = Field(default=None, ge=0)


class RestNet(WireModel):
    up_bps: float | None = Field(default=None, ge=0)
    down_bps: float | None = Field(default=None, ge=0)


class RestFan(WireModel):
    label: str
    rpm: int = Field(ge=0)


class RestTemp(WireModel):
    label: str
    c: float


class RestBattery(WireModel):
    pct: float = Field(ge=0, le=100)
    charging: bool


class RestHost(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    os: str | None = Field(default=None)
    online: bool
    last_seen: str | None = Field(default=None, pattern="Z$")
    role: Literal["hub", "agent"]
    cpu: RestCpu | None = Field(default=None)
    gpus: list[RestGpu] | None = Field(default=None)
    mem: RestMem | None = Field(default=None)
    disks: list[RestDisk] | None = Field(default=None)
    net: RestNet | None = Field(default=None)
    fans: list[RestFan] | None = Field(default=None)
    temps: list[RestTemp] | None = Field(default=None)
    bat: RestBattery | None = Field(default=None)
    plugins: dict[str, Any] | None = Field(default=None)


class RestServerAlert(WireModel):
    rule: str
    level: Literal["info", "warn", "critical"]
    since: str = Field(pattern="Z$")


class RestServer(WireModel):
    mem_pct: float | None = Field(default=None, ge=0, le=100)
    disk_pct: float | None = Field(default=None, ge=0, le=100)
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    src: str
    src_name: str
    online: bool
    last_seen: str | None = Field(default=None, pattern="Z$")
    cpu_pct: float | None = Field(default=None, ge=0, le=100)
    mem_used_b: int | None = Field(default=None, ge=0)
    mem_total_b: int | None = Field(default=None, ge=0)
    disk_used_b: int | None = Field(default=None, ge=0)
    disk_total_b: int | None = Field(default=None, ge=0)
    load: list[float] | None = Field(default=None, min_length=3, max_length=3)
    net_up_bps: float | None = Field(default=None, ge=0)
    net_down_bps: float | None = Field(default=None, ge=0)
    uptime_s: int | None = Field(default=None, ge=0)
    latency_ms: float | None = Field(default=None, ge=0)
    uptime_pct: float | None = Field(default=None, ge=0, le=100)
    alerts: list[RestServerAlert]


class RestQuotaWindow(WireModel):
    kind: Literal["5h", "daily", "weekly", "monthly", "other"]
    label: str | None = Field(default=None)
    used_pct: float = Field(ge=0, le=100)
    reset_at: str | None = Field(default=None, pattern="Z$")
    source: Literal["passive", "snapshot", "active"]
    observed_at: str = Field(pattern="Z$")
    stale: bool


class RestCredits(WireModel):
    balance: float
    currency: str


class RestUsageToday(WireModel):
    requests: int = Field(ge=0)
    tokens: int = Field(ge=0)
    failures: int = Field(ge=0)


class RestAiAccount(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    provider_kind: Literal["codex", "claude", "antigravity", "kimi", "other"]
    label: str
    plan: str | None = Field(default=None)
    status: Literal["ok", "cooldown", "exhausted", "reauth", "disabled", "unknown"]
    recover_at: str | None = Field(default=None, pattern="Z$")
    windows: list[RestQuotaWindow]
    credits: RestCredits | None = Field(default=None)
    today: RestUsageToday | None = Field(default=None)


class RestProviderHealth(WireModel):
    name: str
    type: Literal["cpa", "cpamp"]
    health: Literal["ok", "degraded", "auth_failed", "backoff", "disabled"]
    last_ok: str | None = Field(default=None, pattern="Z$")
    next_try: str | None = Field(default=None, pattern="Z$")
    detail: str | None = Field(default=None)


class RestAgentSession(WireModel):
    sid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    source: Literal["claude", "codex", "gemini"]
    host: str
    project: str | None = Field(default=None)
    state: Literal["idle", "thinking", "tool", "waiting_permission", "done", "error"]
    tool: str | None = Field(default=None)
    since: str = Field(pattern="Z$")
    last_message: str | None = Field(default=None, max_length=140)


class RestPermRequest(WireModel):
    rid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    sid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    src: Literal["claude", "codex", "gemini"]
    host: str
    project: str | None = Field(default=None)
    tool: str
    summary: str = Field(max_length=120)
    created: str = Field(pattern="Z$")
    expires: str = Field(pattern="Z$")


class RestAction(WireModel):
    kind: Literal["dismiss"]


class RestNotification(WireModel):
    nid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    source: str
    level: Literal["info", "warn", "critical"]
    title: str = Field(max_length=120)
    body: str | None = Field(default=None, max_length=2000)
    ts: str = Field(pattern="Z$")
    dedupe_key: str | None = Field(default=None)
    actions: list[RestAction]
    ttl_s: int = Field(ge=0)


class RestDevice(WireModel):
    dev_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    fw: str
    online: bool
    last_seen: str | None = Field(default=None, pattern="Z$")
    ip: Any | None = Field(default=None)
    state: DeviceState | None = Field(default=None)


class RestPlugin(WireModel):
    kind: Literal["collector", "server", "ai", "media", "weather"]
    name: str
    health: Literal["ok", "degraded", "error", "disabled"]
    detail: str | None = Field(default=None)
    doc: str


class RestToken(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    scope: Literal["admin", "device", "agent", "hook", "notify"]
    created: str = Field(pattern="Z$")
    last_used: str | None = Field(default=None, pattern="Z$")


class RestAudit(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    kind: str
    ts: str = Field(pattern="Z$")
    rid: str | None = Field(default=None, pattern="^[a-z0-9_-]{1,32}$")
    result: str | None = Field(default=None)
    by: str | None = Field(default=None)


class RestPendingPair(WireModel):
    dev_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    fw: str
    expires: str = Field(pattern="Z$")


class RestOtaManifest(WireModel):
    ver: str
    kind: Literal["app", "assets"]
    file: str
    sha256: str = Field(pattern="^[a-f0-9]{64}$")
    size: int = Field(ge=0)
    notes_txt: str | None = Field(default=None)


class RestSecret(WireModel):
    name: str
    source: Literal["env", "keyring", "file"]


class DeviceBattery(WireModel):
    pct: float = Field(ge=0, le=100)
    mv: int = Field(ge=0)
    chg: Literal["none", "charging", "full"]
    src: Literal["usb", "bat"]
    eta_min: int | None = Field(default=None, ge=0)


class DeviceFeatures(WireModel):
    ai: bool
    servers: bool
    agents: bool
    media: bool
    weather: bool


class DeviceTime(WireModel):
    epoch: int = Field(ge=946684800, le=4102444799)
    tz_posix: str = Field(max_length=127)

    @field_validator("tz_posix")
    @classmethod
    def printable_timezone(cls, value: str) -> str:
        if any(not " " <= character <= "~" for character in value):
            raise ValueError("POSIX TZ must contain printable ASCII characters")
        return value


class DeviceHello(WireModel):
    dev_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    fw: str
    nukapack: str
    proto: Literal[1]
    lang: Literal["zh-CN", "en"]
    screen: bool
    caps: list[str]
    topics: list[Literal["hosts", "servers", "ai", "agents", "notify", "media", "weather", "pet"]]
    bat: DeviceBattery


class DeviceWelcome(WireModel):
    hub_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    hub_name: str
    hub_ver: str
    proto: Literal[1]
    dev_name: str
    lang: Literal["zh-CN", "en"]
    time: DeviceTime
    ntp: list[str] | None = Field(default=None)
    features: DeviceFeatures
    macros_rev: int = Field(ge=0)


class DeviceHostCpu(WireModel):
    pct: float | None = Field(default=None, ge=0, le=100)
    temp_c: float | None = Field(default=None)
    freq_mhz: float | None = Field(default=None, ge=0)


class DeviceHostGpu(WireModel):
    name: str | None = Field(default=None)
    pct: float | None = Field(default=None, ge=0, le=100)
    temp_c: float | None = Field(default=None)
    mem_pct: float | None = Field(default=None, ge=0, le=100)
    power_w: float | None = Field(default=None, ge=0)


class DeviceHostMem(WireModel):
    pct: float | None = Field(default=None, ge=0, le=100)
    txt: str | None = Field(default=None)


class DeviceHostNet(WireModel):
    up_txt: str | None = Field(default=None)
    down_txt: str | None = Field(default=None)


class DeviceHostDisk(WireModel):
    label: str
    pct: float = Field(ge=0, le=100)


class DeviceHostBat(WireModel):
    pct: float = Field(ge=0, le=100)
    chg: bool


class DeviceHost(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    online: bool
    os: str | None = Field(default=None)
    cpu: DeviceHostCpu | None = Field(default=None)
    gpu: DeviceHostGpu | None = Field(default=None)
    mem: DeviceHostMem | None = Field(default=None)
    net: DeviceHostNet | None = Field(default=None)
    disks: list[DeviceHostDisk] | None = Field(default=None, max_length=3)
    bat: DeviceHostBat | None = Field(default=None)


class DeviceVmHosts(WireModel):
    rev: int = Field(ge=0)
    items: list[DeviceHost] = Field(max_length=6)
    more: int | None = Field(default=None, ge=0)


class DeviceDetailRow(WireModel):
    k: str
    v_txt: str


class DeviceSpark(WireModel):
    cpu: list[float] | None = Field(default=None, max_length=48)
    gpu: list[float] | None = Field(default=None, max_length=48)
    mem: list[float] | None = Field(default=None, max_length=48)


class DeviceVmHostDetail(WireModel):
    rev: int = Field(ge=0)
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    rows: list[DeviceDetailRow]
    spark: DeviceSpark | None = Field(default=None)
    spark_span_txt: str | None = Field(default=None)


class DeviceVmServerDetail(WireModel):
    rev: int = Field(ge=0)
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    rows: list[DeviceDetailRow]
    spark: DeviceSpark | None = Field(default=None)
    spark_span_txt: str | None = Field(default=None)


class DeviceServer(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    online: bool
    src: str
    cpu_pct: float | None = Field(default=None, ge=0, le=100)
    mem_pct: float | None = Field(default=None, ge=0, le=100)
    disk_pct: float | None = Field(default=None, ge=0, le=100)
    load1: float | None = Field(default=None, ge=0)
    net_txt: str | None = Field(default=None)
    uptime_txt: str | None = Field(default=None)
    alerts: int | None = Field(default=None, ge=0)
    latency_ms: float | None = Field(default=None, ge=0)
    uptime_pct: float | None = Field(default=None, ge=0, le=100)


class DeviceVmServers(WireModel):
    rev: int = Field(ge=0)
    items: list[DeviceServer] = Field(max_length=12)
    more: int | None = Field(default=None, ge=0)


class DeviceQuotaWindow(WireModel):
    kind: Literal["5h", "daily", "weekly", "monthly", "other"]
    label: str | None = Field(default=None)
    used_pct: float = Field(ge=0, le=100)
    reset_at: int | None = Field(default=None, ge=1)
    src: Literal["passive", "snapshot", "active"]
    stale: bool


class DeviceAiSummary(WireModel):
    req_today: int = Field(ge=0)
    tok_today_txt: str
    fail_pct: float = Field(ge=0, le=100)


class DeviceAiAccount(WireModel):
    id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    kind: Literal["codex", "claude", "antigravity", "kimi", "other"]
    label: str
    plan: str | None = Field(default=None)
    status: Literal["ok", "cooldown", "exhausted", "reauth", "disabled", "unknown"]
    recover_at: int | None = Field(default=None, ge=1)
    windows: list[DeviceQuotaWindow]
    credits_txt: str | None = Field(default=None)
    fresh_txt: str | None = Field(default=None)


class DeviceVmAi(WireModel):
    rev: int = Field(ge=0)
    summary: DeviceAiSummary | None = Field(default=None)
    accounts: list[DeviceAiAccount] = Field(max_length=8)
    more: int | None = Field(default=None, ge=0)


class DeviceSession(WireModel):
    sid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    src: Literal["claude", "codex", "gemini"]
    host: str
    project: str | None = Field(default=None)
    state: Literal["idle", "thinking", "tool", "waiting_permission", "done", "error"]
    tool: str | None = Field(default=None)
    since: int = Field(ge=0)
    msg: str | None = Field(default=None, max_length=140)


class DeviceVmAgents(WireModel):
    rev: int = Field(ge=0)
    pending: int = Field(ge=0)
    sessions: list[DeviceSession] = Field(max_length=6)
    more: int | None = Field(default=None, ge=0)


class DeviceNotification(WireModel):
    nid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    level: Literal["info", "warn", "critical"]
    src: str
    title: str = Field(max_length=120)
    body: str | None = Field(default=None, max_length=200)
    ts: int = Field(ge=0)


class DeviceVmNotify(WireModel):
    rev: int = Field(ge=0)
    items: list[DeviceNotification] = Field(max_length=20)
    more: int | None = Field(default=None, ge=0)


class DeviceMediaCan(WireModel):
    toggle: bool
    next: bool
    prev: bool


class DeviceMediaItem(WireModel):
    src_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    host: str
    app: str
    title: str | None = Field(default=None)
    artist: str | None = Field(default=None)
    album: str | None = Field(default=None)
    state: Literal["playing", "paused", "stopped"]
    pos_s: float | None = Field(default=None, ge=0)
    dur_s: float | None = Field(default=None, ge=0)
    at: int | None = Field(default=None, ge=0)
    cover: str | None = Field(default=None, pattern="^/api/v1/media/cover/[a-f0-9]+\\.jpg$")
    can: DeviceMediaCan


class DeviceVmMedia(WireModel):
    rev: int = Field(ge=0)
    items: list[DeviceMediaItem] = Field(max_length=3)
    more: int | None = Field(default=None, ge=0)


class DeviceWeatherNow(WireModel):
    temp_c: float | None = Field(default=None)
    feels_c: float | None = Field(default=None)
    hum_pct: float | None = Field(default=None, ge=0, le=100)
    wind_txt: str | None = Field(default=None)
    icon: (
        Literal[
            "clear_day",
            "clear_night",
            "partly_cloudy",
            "cloudy",
            "fog",
            "drizzle",
            "rain",
            "heavy_rain",
            "snow",
            "sleet",
            "thunder",
            "wind",
        ]
        | None
    ) = Field(default=None)
    desc_txt: str | None = Field(default=None)


class DeviceWeatherDay(WireModel):
    date: str
    hi_c: float | None = Field(default=None)
    lo_c: float | None = Field(default=None)
    icon: (
        Literal[
            "clear_day",
            "clear_night",
            "partly_cloudy",
            "cloudy",
            "fog",
            "drizzle",
            "rain",
            "heavy_rain",
            "snow",
            "sleet",
            "thunder",
            "wind",
        ]
        | None
    ) = Field(default=None)


class DeviceVmWeather(WireModel):
    rev: int = Field(ge=0)
    loc_txt: str | None = Field(default=None)
    updated: int | None = Field(default=None, ge=0)
    now: DeviceWeatherNow | None = Field(default=None)
    days: list[DeviceWeatherDay] | None = Field(default=None, max_length=3)


class DeviceVmPet(WireModel):
    rev: int = Field(ge=0)
    mood: Literal["sleep", "idle", "busy", "attention", "celebrate", "hungry"]
    reason: str | None = Field(default=None)
    until: int | None = Field(default=None, ge=1)


class DeviceSub(WireModel):
    topics: list[Literal["hosts", "servers", "ai", "agents", "notify", "media", "weather", "pet"]]


class DeviceState(WireModel):
    screen: bool
    dnd: bool
    card: Literal[
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
    bat: DeviceBattery
    rssi: int | None = Field(default=None)
    heap_int: int | None = Field(default=None, ge=0)
    heap_int_min: int | None = Field(default=None, ge=0)
    up_s: int | None = Field(default=None, ge=0)
    low_power: bool | None = Field(default=None)


class DeviceSubDetail(WireModel):
    topic: Literal["hosts", "servers", None]
    id: str | None = Field(default=None, pattern="^[a-z0-9_-]{1,32}$")


class DevicePermDecide(WireModel):
    rid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    behavior: Literal["allow", "deny"]


class DeviceMediaCmd(WireModel):
    src_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    cmd: Literal["toggle", "next", "prev"]


class DeviceAiRefresh(WireModel):
    pass


class DeviceNotifyDismiss(WireModel):
    nid: str | None = Field(default=None, pattern="^[a-z0-9_-]{1,32}$")
    all: Literal[True] | None = Field(default=None)


class DevicePomo(WireModel):
    phase: Literal["focus", "short_break", "long_break"]
    state: Literal["running", "paused", "done", "reset"]
    ends_at: int | None = Field(default=None, ge=0)
    round: int = Field(ge=0)


class DeviceMacroGet(WireModel):
    pass


class DeviceOtaCheck(WireModel):
    pass


class DeviceOtaProgress(WireModel):
    ver: str
    kind: Literal["app", "assets"]
    pct: float = Field(ge=0, le=100)


class DeviceOtaResult(WireModel):
    ver: str
    kind: Literal["app", "assets"]
    ok: bool
    reason: str | None = Field(default=None)


class DevicePermReq(WireModel):
    rid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    sid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    src: Literal["claude", "codex", "gemini"]
    host: str
    project: str | None = Field(default=None)
    tool: str
    summary: str = Field(max_length=120)
    expires_at: int = Field(ge=0)


class DevicePermDone(WireModel):
    rid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    result: Literal["allow", "deny", "timeout", "cancelled"]
    by: Literal["device", "web", "timeout", "terminal"]


class DeviceNotifyPush(WireModel):
    nid: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    level: Literal["info", "warn", "critical"]
    src: str
    title: str = Field(max_length=120)
    body: str | None = Field(default=None, max_length=200)
    ts: int = Field(ge=0)
    wake: bool


class DeviceOtaOffer(WireModel):
    ver: str
    kind: Literal["app", "assets"]
    url: str
    sha256: str = Field(pattern="^[a-f0-9]{64}$")
    size: int = Field(ge=0)
    notes_txt: str | None = Field(default=None)


class DeviceMacro(WireModel):
    slot: int = Field(ge=0, le=11)
    label: str
    kind: Literal["keys", "text"]
    value: str


class DeviceMacroSet(WireModel):
    rev: int = Field(ge=0)
    macros: list[DeviceMacro] = Field(max_length=12)


class DeviceKey3Long(WireModel):
    action: Literal["talk", "dnd", "always_on", "pomodoro", "macro", "none"]
    slot: int | None = Field(default=None, ge=0, le=11)


class DeviceCfg(WireModel):
    lang: Literal["zh-CN", "en"] | None = Field(default=None)
    key3_long: DeviceKey3Long | None = Field(default=None)
    key3_double_card: (
        Literal[
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
        | None
    ) = Field(default=None)
    key3_clock_style: bool | None = Field(default=None)
    home_face: Literal["clock", "assistant"] | None = Field(default=None)
    volume: int | None = Field(default=None, ge=0, le=100)
    assistant: bool | None = Field(default=None)
    t_dim: int | None = Field(default=None, ge=1)
    t_off: int | None = Field(default=None, ge=1)


class DevicePairWait(WireModel):
    pass


class DevicePairOk(WireModel):
    dev_token: str
    hub_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    hub_name: str


class DeviceOk(WireModel):
    pass


class DeviceErr(WireModel):
    code: Literal["bad_request", "rate_limited", "perm_gone", "not_found", "forbidden", "internal"]
    msg: str


class AgentHello(WireModel):
    agent_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    name: str
    os: str
    ver: str
    proto: Literal[1]
    caps: list[str]


class AgentIntervals(WireModel):
    host_fast: float = Field(gt=0)
    host_slow: float = Field(gt=0)
    disk: float = Field(gt=0)


class AgentWelcome(WireModel):
    hub_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    intervals: AgentIntervals


class AgentHostSample(WireModel):
    name: str
    os: str | None = Field(default=None)
    online: bool
    last_seen: str | None = Field(default=None, pattern="Z$")
    cpu: RestCpu | None = Field(default=None)
    gpus: list[RestGpu] | None = Field(default=None)
    mem: RestMem | None = Field(default=None)
    disks: list[RestDisk] | None = Field(default=None)
    net: RestNet | None = Field(default=None)
    fans: list[RestFan] | None = Field(default=None)
    temps: list[RestTemp] | None = Field(default=None)
    bat: RestBattery | None = Field(default=None)
    plugins: dict[str, Any] | None = Field(default=None)


class AgentMediaItem(WireModel):
    src_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    host: str
    app: str
    title: str | None = Field(default=None)
    artist: str | None = Field(default=None)
    album: str | None = Field(default=None)
    state: Literal["playing", "paused", "stopped"]
    pos_s: float | None = Field(default=None, ge=0)
    dur_s: float | None = Field(default=None, ge=0)
    at: int | None = Field(default=None, ge=0)
    can: DeviceMediaCan
    cover_b64: str | None = Field(
        default=None, pattern="^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
    )


class AgentMediaState(WireModel):
    items: list[AgentMediaItem]


class Envelope(WireModel):
    v: Literal[1]
    t: str = Field(min_length=1)
    id: int | None = None
    ts: int = Field(ge=0)
    d: dict[str, Any]


class DeviceEnvelope(Envelope):
    @model_validator(mode="before")
    @classmethod
    def no_nul(cls, value):
        if contains_nul(value):
            raise ValueError("Device strings and object keys must not contain NUL")
        return value


RestCpu.model_rebuild()
RestGpu.model_rebuild()
RestMem.model_rebuild()
RestDisk.model_rebuild()
RestNet.model_rebuild()
RestFan.model_rebuild()
RestTemp.model_rebuild()
RestBattery.model_rebuild()
RestHost.model_rebuild()
RestServerAlert.model_rebuild()
RestServer.model_rebuild()
RestQuotaWindow.model_rebuild()
RestCredits.model_rebuild()
RestUsageToday.model_rebuild()
RestAiAccount.model_rebuild()
RestProviderHealth.model_rebuild()
RestAgentSession.model_rebuild()
RestPermRequest.model_rebuild()
RestAction.model_rebuild()
RestNotification.model_rebuild()
RestDevice.model_rebuild()
RestPlugin.model_rebuild()
RestToken.model_rebuild()
RestAudit.model_rebuild()
RestPendingPair.model_rebuild()
RestOtaManifest.model_rebuild()
RestSecret.model_rebuild()
DeviceBattery.model_rebuild()
DeviceFeatures.model_rebuild()
DeviceTime.model_rebuild()
DeviceHello.model_rebuild()
DeviceWelcome.model_rebuild()
DeviceHostCpu.model_rebuild()
DeviceHostGpu.model_rebuild()
DeviceHostMem.model_rebuild()
DeviceHostNet.model_rebuild()
DeviceHostDisk.model_rebuild()
DeviceHostBat.model_rebuild()
DeviceHost.model_rebuild()
DeviceVmHosts.model_rebuild()
DeviceDetailRow.model_rebuild()
DeviceSpark.model_rebuild()
DeviceVmHostDetail.model_rebuild()
DeviceVmServerDetail.model_rebuild()
DeviceServer.model_rebuild()
DeviceVmServers.model_rebuild()
DeviceQuotaWindow.model_rebuild()
DeviceAiSummary.model_rebuild()
DeviceAiAccount.model_rebuild()
DeviceVmAi.model_rebuild()
DeviceSession.model_rebuild()
DeviceVmAgents.model_rebuild()
DeviceNotification.model_rebuild()
DeviceVmNotify.model_rebuild()
DeviceMediaCan.model_rebuild()
DeviceMediaItem.model_rebuild()
DeviceVmMedia.model_rebuild()
DeviceWeatherNow.model_rebuild()
DeviceWeatherDay.model_rebuild()
DeviceVmWeather.model_rebuild()
DeviceVmPet.model_rebuild()
DeviceSub.model_rebuild()
DeviceState.model_rebuild()
DeviceSubDetail.model_rebuild()
DevicePermDecide.model_rebuild()
DeviceMediaCmd.model_rebuild()
DeviceAiRefresh.model_rebuild()
DeviceNotifyDismiss.model_rebuild()
DevicePomo.model_rebuild()
DeviceMacroGet.model_rebuild()
DeviceOtaCheck.model_rebuild()
DeviceOtaProgress.model_rebuild()
DeviceOtaResult.model_rebuild()
DevicePermReq.model_rebuild()
DevicePermDone.model_rebuild()
DeviceNotifyPush.model_rebuild()
DeviceOtaOffer.model_rebuild()
DeviceMacro.model_rebuild()
DeviceMacroSet.model_rebuild()
DeviceKey3Long.model_rebuild()
DeviceCfg.model_rebuild()
DevicePairWait.model_rebuild()
DevicePairOk.model_rebuild()
DeviceOk.model_rebuild()
DeviceErr.model_rebuild()
AgentHello.model_rebuild()
AgentIntervals.model_rebuild()
AgentWelcome.model_rebuild()
AgentHostSample.model_rebuild()
AgentMediaItem.model_rebuild()
AgentMediaState.model_rebuild()

DEVICE_MODELS = {
    "battery": DeviceBattery,
    "features": DeviceFeatures,
    "time": DeviceTime,
    "hello": DeviceHello,
    "welcome": DeviceWelcome,
    "host_cpu": DeviceHostCpu,
    "host_gpu": DeviceHostGpu,
    "host_mem": DeviceHostMem,
    "host_net": DeviceHostNet,
    "host_disk": DeviceHostDisk,
    "host_bat": DeviceHostBat,
    "host": DeviceHost,
    "vm.hosts": DeviceVmHosts,
    "detail_row": DeviceDetailRow,
    "spark": DeviceSpark,
    "vm.host_detail": DeviceVmHostDetail,
    "vm.server_detail": DeviceVmServerDetail,
    "server": DeviceServer,
    "vm.servers": DeviceVmServers,
    "quota_window": DeviceQuotaWindow,
    "ai_summary": DeviceAiSummary,
    "ai_account": DeviceAiAccount,
    "vm.ai": DeviceVmAi,
    "session": DeviceSession,
    "vm.agents": DeviceVmAgents,
    "notification": DeviceNotification,
    "vm.notify": DeviceVmNotify,
    "media_can": DeviceMediaCan,
    "media_item": DeviceMediaItem,
    "vm.media": DeviceVmMedia,
    "weather_now": DeviceWeatherNow,
    "weather_day": DeviceWeatherDay,
    "vm.weather": DeviceVmWeather,
    "vm.pet": DeviceVmPet,
    "sub": DeviceSub,
    "state": DeviceState,
    "sub.detail": DeviceSubDetail,
    "perm.decide": DevicePermDecide,
    "media.cmd": DeviceMediaCmd,
    "ai.refresh": DeviceAiRefresh,
    "notify.dismiss": DeviceNotifyDismiss,
    "pomo": DevicePomo,
    "macro.get": DeviceMacroGet,
    "ota.check": DeviceOtaCheck,
    "ota.progress": DeviceOtaProgress,
    "ota.result": DeviceOtaResult,
    "perm.req": DevicePermReq,
    "perm.done": DevicePermDone,
    "notify.push": DeviceNotifyPush,
    "ota.offer": DeviceOtaOffer,
    "macro": DeviceMacro,
    "macro.set": DeviceMacroSet,
    "key3_long": DeviceKey3Long,
    "cfg": DeviceCfg,
    "pair.wait": DevicePairWait,
    "pair.ok": DevicePairOk,
    "ok": DeviceOk,
    "err": DeviceErr,
}
