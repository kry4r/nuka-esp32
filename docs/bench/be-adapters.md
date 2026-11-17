# BE adapters and alert verification

Date: 2026-10-17. Scope: HUB-08–10, HUB-12–13, HUB-15–17, and the weather part of HUB-22. HUB-14's runtime notification pipeline and HUB-22's pet/time logic are integrated by the main BE owner.

## Implemented boundaries

- Collectors: psutil, NVML, Windows LHM, Linux hwmon/DRM sysfs, and Apple Silicon macmon. Optional metrics are omitted when the upstream reports them unsupported. A Windows read-only storage device query joins volume usage with psutil physical-disk I/O counters.
- Sources: ServerStatus, public Uptime Kuma status pages, Beszel/PocketBase login or token and pagination, per-node Netdata, node_exporter text/Prometheus instant vectors, generic JSONPath with unit scaling. Merge priority follows configured order and name aliases.
- AI: L1 status/signals, CPAMP L2 quota/header snapshots and usage, exact L3 recipe URLs, stable opaque account IDs, masked labels, stale timestamps, request budget and per-account minimum interval. Both transport and wrapped 401/403 stop queued requests under a provider lock. 429/5xx/timeouts back off exponentially; retry performs one L1 request. All traffic is allowlisted; no usage queue or write-management request is permitted.
- Alerts: the CONFIG expression grammar without eval, field whitelist/missing-field false, derived resource percentages, built-ins, debounce, repeat, one recovery notification.
- Weather: geocoding cache, Open-Meteo current/three future days, WMO icon/day-night mapping and canonical Celsius protocol fields.

All input fixtures are synthetic. No real CPA/CPAMP, server probe, or weather instance was contacted. HTTP fixtures run through httpx.MockTransport; this is upstream-boundary substitution rather than nested application mocks.

## Evidence and remaining checks

| Item | Result |
|---|---|
| Windows psutil two samples, separated by 1 s | Passed: CPU, memory/swap, one mounted disk with read/write rates, network rates, online and UTC last_seen; no personal values recorded. |
| Unsupported-platform probes on this Windows machine | Passed: macmon, linux_sensors, amd_sysfs return disabled with the platform reason. LHM probe only establishes platform applicability; no LHM server was contacted. |
| NVML | Passed: initially disabled with `Install the nvidia extra`; after installing the optional extra, the one-hour Windows observation reads physical NVIDIA telemetry with health ok. No hardware identifiers are included in this report. |
| T2 authentication/allowlist/backoff | Passed on 2026-10-18 with T1–T5 (74 cases total). Concurrent outer and wrapped 401/403 stop all queued requests; allowlist and timeout/backoff cases pass. |
| T3 per-adapter fixtures | Passed: one fixture per server source, LHM, macmon, CPA L1, CPAMP L2, each L3 recipe, and Open-Meteo. |
| T4 parser/debounce/recovery/repeat | Passed, including malicious/unknown expressions and one recovery. The intentionally invalid __import__ case emits the expected unknown-field warning. |
| T6 updated REST Server contract | Passed: 65 schemas / 136 sample payloads. Optional mem_pct/disk_pct are explicitly authorized in DECISIONS. |
| SPDX | Passed at handoff: 192 source files, zero failures. |
| Personal-value scan | Owned plugin/tests/guides files passed; repository scan found two unrelated flash-guide version literals, reported to the parent owner. |
| Linux/macOS, physical AMD/Apple sensors | Not run: corresponding platform/hardware unavailable to this agent. Requires platform acceptance, not additional tests. |

HUB-08's three-platform acceptance and HUB-09's real-hardware field completeness are not claimed complete by fixture results. Command: hub/.venv/Scripts/python.exe -m pytest hub/tests/test_t1_zones.py hub/tests/test_t2_ai.py hub/tests/test_t3_adapters.py hub/tests/test_t4_alerts.py hub/tests/test_t5_approval.py -q. Result: 74 passed.

## Upstream shape references

These versions were reviewed as source/documentation targets; none was deployed or contacted. Fixtures contain the minimal relevant shape, with example identifiers and measurements.

| Adapter | Reference / fixture focus |
|---|---|
| ServerStatus | [cppla client](https://github.com/cppla/ServerStatus/blob/master/clients/client-linux.py), client format 1.1.0: KiB memory, MiB disk, bytes/s network. |
| Uptime Kuma | [public internal API](https://github.com/louislam/uptime-kuma/wiki/Internal-API), release [2.5.6](https://github.com/louislam/uptime-kuma/releases/tag/2.5.6): page groups, heartbeats, 24 h ratio. |
| Beszel | [v0.21.0 Info](https://github.com/henrygd/beszel/blob/v0.21.0/internal/entities/system/system.go): CPU and memory/disk percentages. Aggregate bandwidth cannot establish separate upload/download; capacities are absent from systems info. |
| Netdata | [v2.12.1](https://github.com/netdata/netdata/tree/v2.12.1): v1 info and four one-point charts. |
| node_exporter | [v1.12.1](https://github.com/prometheus/node_exporter/tree/v1.12.1): text counters/gauges; Prometheus mode uses instant vector responses. |
| generic_json | Project-owned JSONPath fixture; explicit scale conversion, no upstream version. |
| LHM | [official project](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor): nested Children/SensorId/Value tree; custom rule precedes Intel/AMD defaults. Fixture format only, no release compatibility claim. |
| macmon | [v0.10.0 pipe format](https://github.com/vladkens/macmon/blob/v0.10.0/readme.md): gpu_scaled_ratio to percent, MHz, watts, nullable temperatures. |
| CPA/CPAMP | [CPA](https://github.com/router-for-me/CLIProxyAPI), [CPAMP v1.14.7](https://github.com/seakee/CPA-Manager-Plus/tree/v1.14.7): auth-files, read-only quota query, header snapshots, retained usage details. Codex/Claude/Antigravity recipes follow SPEC §7.6.4 and synthetic response shapes. |
| Open-Meteo | [forecast API](https://open-meteo.com/en/docs): current/daily fields, WMO weather codes, and Celsius normalization. |

An L1 signal without an upstream observed_at retains the time first seen when its values stay unchanged. This is an observation age, not a claim about the actual upstream header timestamp. A full daily usage summary is omitted if retained details cannot establish its failure count. These limitations preserve missing-data semantics rather than fabricating fresh or zero values.

## Guides and integration handoff

Guides 04, 05, 06, 07-ai-quota, and 07-weather have Chinese/English pairs with platform commands, expected results, config snippets, removal, and diagnostics. Doctor reads the running Hub plugin-health snapshot using a short-lived local admin token, revoked immediately; with Hub offline it performs availability/secret-presence probes only. Use authenticated `/api/v1/ai` for breaker state. `/api/v1/health` contains only protocol/service health.

Plugin functions: `build_collectors(config.collectors, client, secrets)`, `build_sources(config.servers, secrets, client)`, `build_providers(config.ai, secrets, client)`, and `Weather(config.weather, client)`. AI plugin name equals configured provider name. `on_error(error)` is synchronous and adds timeout backoff; Scheduler owns the only plugin exception handler. AI sample returns normalized accounts and one provider-health entry. AlertEngine takes an async notification callback and evaluates `(scope, items, now)`.
