# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import asyncio
import csv
import importlib.util
import io
import json
import platform
import re
import shutil
import time
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import httpx
import psutil
from pydantic import BaseModel, ConfigDict, Field

from nuka_hub.core.plugin import Health

DOC = "04-hardware-sensors"


def disk_name(device: str) -> str:
    if platform.system() != "Windows":
        return Path(device).name
    import ctypes
    from ctypes import wintypes

    class DeviceNumber(ctypes.Structure):
        _fields_ = [
            ("type", wintypes.DWORD),
            ("number", wintypes.DWORD),
            ("partition", wintypes.DWORD),
        ]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = (
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    )
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.DeviceIoControl.argtypes = (
        wintypes.HANDLE,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.c_void_p,
    )
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel.CreateFileW("\\\\.\\" + device.rstrip("\\"), 0, 3, None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    number, returned = DeviceNumber(), wintypes.DWORD()
    try:
        # psutil exposes physical disks; the volume's device number joins its counters.
        if not kernel.DeviceIoControl(
            handle,
            0x002D1080,
            None,
            0,
            ctypes.byref(number),
            ctypes.sizeof(number),
            ctypes.byref(returned),
            None,
        ):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)
    return f"PhysicalDrive{number.number}"


class Collector:
    kind = "collector"
    doc = DOC
    timeout_s = 5.0

    def __init__(self, name: str, interval: float):
        self.name = name
        self.interval_s = interval

    def health(self, state: str = "ok", detail: str = "") -> Health:
        return Health(self.name, self.kind, state, detail, self.doc)


class PsutilCollector(Collector):
    def __init__(self, config: Any):
        super().__init__("psutil", config.fast_interval)
        self.disk_interval = config.disk_interval
        self.disk_at = float("-inf")
        self.disks: list[dict] = []
        self.disk_devices: dict[str, str] = {}
        self.previous_net = None
        self.previous_disk = None
        self.previous_at: float | None = None
        psutil.cpu_percent(percpu=True)

    async def probe(self) -> Health:
        return self.health()

    async def sample(self) -> dict:
        return await asyncio.to_thread(self.collect)

    def collect(self) -> dict:
        now = time.monotonic()
        cores = psutil.cpu_percent(percpu=True)
        memory, swap = psutil.virtual_memory(), psutil.swap_memory()
        network = psutil.net_io_counters()
        cpu: dict[str, Any] = {
            "pct": round(sum(cores) / len(cores), 1),
            "per_core": [round(core, 1) for core in cores],
        }
        frequency = psutil.cpu_freq()
        if frequency is not None:
            cpu["freq_mhz"] = frequency.current
        result: dict[str, Any] = {
            "os": platform.system().lower(),
            "online": True,
            "last_seen": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
            "cpu": cpu,
            "mem": {
                "used_b": memory.used,
                "total_b": memory.total,
                "swap_used_b": swap.used,
                "swap_total_b": swap.total,
            },
        }
        if self.previous_at is not None:
            elapsed = now - self.previous_at
            result["net"] = {
                "up_bps": max(0, network.bytes_sent - self.previous_net.bytes_sent) / elapsed,
                "down_bps": max(0, network.bytes_recv - self.previous_net.bytes_recv) / elapsed,
            }
        if now - self.disk_at >= self.disk_interval:
            self.disks = []
            self.disk_devices = {}
            for partition in psutil.disk_partitions():
                if "cdrom" in partition.opts or not partition.fstype:
                    continue
                usage = psutil.disk_usage(partition.mountpoint)
                self.disks.append(
                    {"mount": partition.mountpoint, "used_b": usage.used, "total_b": usage.total}
                )
                self.disk_devices[partition.mountpoint] = disk_name(partition.device)
            self.disk_at = now
        result["disks"] = [dict(disk) for disk in self.disks]
        disk_io = psutil.disk_io_counters(perdisk=True)
        for disk in result["disks"]:
            device = self.disk_devices[disk["mount"]]
            if (
                self.previous_disk is not None
                and device in disk_io
                and device in self.previous_disk
            ):
                disk.update(
                    read_bps=max(
                        0, disk_io[device].read_bytes - self.previous_disk[device].read_bytes
                    )
                    / elapsed,
                    write_bps=max(
                        0, disk_io[device].write_bytes - self.previous_disk[device].write_bytes
                    )
                    / elapsed,
                )
        battery = psutil.sensors_battery()
        if battery is not None:
            result["bat"] = {"pct": round(battery.percent, 1), "charging": battery.power_plugged}
        self.previous_at, self.previous_net, self.previous_disk = now, network, disk_io
        return result


class NvmlCollector(Collector):
    def __init__(self, config: Any):
        super().__init__("nvml", config.interval)
        self.indices = config.gpu_indices
        self.api: Any = None
        self.available: dict[int, set[str]] = {}

    async def probe(self) -> Health:
        if importlib.util.find_spec("pynvml") is None:
            return self.health("disabled", "Install the nvidia extra")
        executable = shutil.which("nvidia-smi")
        if executable is None:
            return self.health("disabled", "NVIDIA driver tools are unavailable")
        process = await asyncio.create_subprocess_exec(
            executable,
            (
                "--query-gpu=index,name,utilization.gpu,temperature.gpu,"
                "memory.used,memory.total,power.draw,fan.speed,clocks.current.graphics"
            ),
            "--format=csv,noheader,nounits",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        stdout, _ = await process.communicate()
        if process.returncode != 0 or not stdout.strip():
            return self.health("disabled", "NVIDIA driver or GPU is unavailable")
        fields = (
            "name",
            "pct",
            "temp_c",
            "mem_used_b",
            "mem_total_b",
            "power_w",
            "fan_pct",
            "clock_mhz",
        )
        for row in csv.reader(io.StringIO(stdout.decode())):
            self.available[int(row[0])] = {
                field
                for field, value in zip(fields, row[1:], strict=True)
                if value.strip() not in {"N/A", "[N/A]", "[Not Supported]"}
            }
        return self.health()

    async def sample(self) -> dict:
        return await asyncio.to_thread(self.collect)

    def collect(self) -> dict:
        if self.api is None:
            import pynvml

            self.api = pynvml
            self.api.nvmlInit()
        api = self.api
        devices = self.indices or range(api.nvmlDeviceGetCount())
        gpus = []
        for index in devices:
            handle = api.nvmlDeviceGetHandleByIndex(index)
            gpu = {
                "name": api.nvmlDeviceGetName(handle),
            }
            readers = {
                "pct": lambda: round(api.nvmlDeviceGetUtilizationRates(handle).gpu, 1),
                "temp_c": lambda: api.nvmlDeviceGetTemperature(handle, api.NVML_TEMPERATURE_GPU),
                "power_w": lambda: api.nvmlDeviceGetPowerUsage(handle) / 1000,
                "fan_pct": lambda: round(api.nvmlDeviceGetFanSpeed(handle), 1),
                "clock_mhz": lambda: api.nvmlDeviceGetClockInfo(handle, api.NVML_CLOCK_GRAPHICS),
            }
            for field, read in readers.items():
                if field in self.available[index]:
                    gpu[field] = read()
            if {"mem_used_b", "mem_total_b"} <= self.available[index]:
                memory = api.nvmlDeviceGetMemoryInfo(handle)
                gpu.update(mem_used_b=memory.used, mem_total_b=memory.total)
            gpus.append(gpu)
        return {"gpus": gpus}


class LinuxSensorsCollector(Collector):
    def __init__(self, config: Any):
        super().__init__("linux_sensors", config.interval)
        self.labels = config.cpu_temp_labels

    async def probe(self) -> Health:
        if platform.system() != "Linux":
            return self.health("disabled", "Linux hwmon is required")
        if not psutil.sensors_temperatures():
            return self.health("disabled", "No hwmon temperature sensors; install lm-sensors")
        return self.health()

    async def sample(self) -> dict:
        return await asyncio.to_thread(self.collect)

    def collect(self) -> dict:
        temperatures = psutil.sensors_temperatures()
        result: dict[str, Any] = {"temps": [], "fans": []}
        for chip, sensors in temperatures.items():
            for sensor in sensors:
                label = sensor.label or chip
                result["temps"].append({"label": label, "c": sensor.current})
                if "cpu" not in result and any(
                    token in label or token in chip for token in self.labels
                ):
                    result["cpu"] = {"temp_c": sensor.current}
        for chip, sensors in psutil.sensors_fans().items():
            result["fans"].extend(
                {"label": sensor.label or chip, "rpm": int(sensor.current)} for sensor in sensors
            )
        return result


class AmdSysfsCollector(Collector):
    def __init__(self):
        super().__init__("amd_sysfs", 1)
        self.devices: list[Path] = []

    async def probe(self) -> Health:
        if platform.system() != "Linux":
            return self.health("disabled", "Linux DRM sysfs is required")
        self.devices = [
            p.parent for p in Path("/sys/class/drm").glob("card*/device/gpu_busy_percent")
        ]
        return self.health() if self.devices else self.health("disabled", "No supported DRM GPU")

    async def sample(self) -> dict:
        return await asyncio.to_thread(self.collect)

    def collect(self) -> dict:
        gpus = []
        for device in self.devices:
            gpu: dict[str, Any] = {
                "name": "AMD/Intel " + device.parent.name,
                "pct": round(float((device / "gpu_busy_percent").read_text()), 1),
            }
            for filename, field in (
                ("mem_info_vram_used", "mem_used_b"),
                ("mem_info_vram_total", "mem_total_b"),
            ):
                path = device / filename
                if path.exists():
                    gpu[field] = int(path.read_text())
            for hwmon in device.glob("hwmon/hwmon*"):
                for filename, field, scale in (
                    ("temp1_input", "temp_c", 0.001),
                    ("power1_average", "power_w", 0.000001),
                    ("freq1_input", "clock_mhz", 0.000001),
                ):
                    path = hwmon / filename
                    if path.exists():
                        gpu[field] = float(path.read_text()) * scale
            gpus.append(gpu)
        return {"gpus": gpus}


class LhmNode(BaseModel):
    model_config = ConfigDict(extra="ignore")
    SensorId: str = ""
    Text: str = ""
    Value: str = ""
    Children: list[LhmNode] = []


def parse_lhm(document: Any, mappings: list[Any]) -> dict:
    tree = LhmNode.model_validate(document)
    result: dict[str, Any] = {}
    builtin = [
        ("cpu.temp_c", r"/(?:amdcpu|intelcpu)/\d+/temperature/\d+", "Tctl|Package"),
        ("cpu.power_w", r"/(?:amdcpu|intelcpu)/\d+/power/0", ".*"),
    ]
    rules = [
        (
            rule.field,
            re.escape(rule.sensor_id) + "$" if rule.sensor_id else ".*",
            rule.name_regex or ".*",
        )
        for rule in mappings
    ]
    rules += builtin

    gpu_nodes: dict[str, dict] = {}
    nodes: list[LhmNode] = []

    def visit(node: LhmNode, gpu_name: str = "") -> None:
        nodes.append(node)
        sensor_kind = re.search(r"/(temperature|fan)/\d+$", node.SensorId)
        number = re.search(r"[-+]?\d+(?:[.,]\d+)?", node.Value)
        if sensor_kind and number:
            group, field = ("temps", "c") if sensor_kind[1] == "temperature" else ("fans", "rpm")
            value = float(number[0].replace(",", "."))
            result.setdefault(group, []).append(
                {"label": node.Text, field: int(value) if field == "rpm" else value}
            )
        gpu_match = re.match(r"^(/gpu-(?:nvidia|amd|intel)/\d+)(?:/(\w+)/(\d+))?$", node.SensorId)
        if gpu_match and gpu_match[2] is None:
            gpu_name = node.Text
        if gpu_match and gpu_match[2] and node.Value:
            field = {
                ("load", "0"): ("pct", 1),
                ("temperature", "0"): ("temp_c", 1),
                ("power", "0"): ("power_w", 1),
                ("clock", "0"): ("clock_mhz", 1),
                ("smalldata", "0"): ("mem_used_b", 1024**2),
                ("smalldata", "2"): ("mem_total_b", 1024**2),
            }.get((gpu_match[2], gpu_match[3]))
            number = re.search(r"[-+]?\d+(?:[.,]\d+)?", node.Value)
            if field is not None and number:
                gpu = gpu_nodes.setdefault(gpu_match[1], {"name": gpu_name})
                value = float(number[0].replace(",", ".")) * field[1]
                gpu[field[0]] = int(value) if field[0].endswith("_b") else round(value, 1)
        for child in node.Children:
            visit(child, gpu_name)

    visit(tree)
    if gpu_nodes:
        result["gpus"] = list(gpu_nodes.values())
    assigned: set[str] = set()
    for target, sensor, name in rules:
        if target in assigned:
            continue
        for node in nodes:
            if node.Value and re.search(sensor, node.SensorId) and re.search(name, node.Text):
                number = re.search(r"[-+]?\d+(?:[.,]\d+)?", node.Value)
                if number:
                    value = float(number[0].replace(",", "."))
                    parts = re.findall(r"([A-Za-z_]\w*)|\[(\d+)\]", target)
                    container = result
                    for key, index in parts[:-1]:
                        container = (
                            container[int(index)] if index else container.setdefault(key, {})
                        )
                    field = parts[-1][0]
                    if field.endswith("_b"):
                        unit = re.search(r"\b(KB|MB|GB|KiB|MiB|GiB)\b", node.Value)
                        if unit:
                            value *= {
                                "KB": 1000,
                                "MB": 1000**2,
                                "GB": 1000**3,
                                "KiB": 1024,
                                "MiB": 1024**2,
                                "GiB": 1024**3,
                            }[unit[1]]
                        value = int(value)
                    elif field == "rpm":
                        value = int(value)
                    container[field] = value
                    assigned.add(target)
                    break
    return result


class LhmCollector(Collector):
    def __init__(self, config: Any, client: httpx.AsyncClient, secrets: Any = None):
        super().__init__("lhm", config.interval)
        self.config, self.client = config, client
        self.auth = None
        if config.password_secret:
            self.auth = (config.user, secrets.get(config.password_secret))

    async def probe(self) -> Health:
        if platform.system() != "Windows":
            return self.health("disabled", "LibreHardwareMonitor requires Windows")
        return self.health()

    async def sample(self) -> dict:
        response = await self.client.get(self.config.url, auth=self.auth)
        response.raise_for_status()
        return parse_lhm(response.json(), self.config.map)


class MacmonSample(BaseModel):
    model_config = ConfigDict(extra="ignore")
    cpu_power: float = Field(ge=0)
    gpu_power: float = Field(ge=0)
    gpu_freq_mhz: float
    gpu_scaled_ratio: float = Field(ge=0, le=1)
    temp: dict[str, float | None] = {}


def parse_macmon(document: Any) -> dict:
    sample = MacmonSample.model_validate(document)
    result = {
        "cpu": {"power_w": sample.cpu_power},
        "gpus": [
            {
                "name": "Apple GPU",
                "pct": round(sample.gpu_scaled_ratio * 100, 1),
                "clock_mhz": sample.gpu_freq_mhz,
                "power_w": sample.gpu_power,
            }
        ],
    }
    if sample.temp.get("cpu_temp_avg") is not None:
        result["cpu"]["temp_c"] = sample.temp["cpu_temp_avg"]
    if sample.temp.get("gpu_temp_avg") is not None:
        result["gpus"][0]["temp_c"] = sample.temp["gpu_temp_avg"]
    return result


class MacmonCollector(Collector):
    def __init__(self, config: Any):
        super().__init__("macmon", config.interval)
        self.path = config.path

    async def probe(self) -> Health:
        if platform.system() != "Darwin" or platform.machine() != "arm64":
            return self.health("disabled", "macmon requires Apple Silicon")
        if shutil.which(self.path) is None:
            return self.health("disabled", "Install macmon")
        return self.health()

    async def sample(self) -> dict:
        process = await asyncio.create_subprocess_exec(
            self.path,
            "pipe",
            "--samples",
            "1",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, _ = await process.communicate()
        if process.returncode:
            raise RuntimeError("macmon sampling failed")
        return parse_macmon(json.loads(stdout.splitlines()[0]))


def build_collectors(
    config: Any, client: httpx.AsyncClient, secrets: Any = None
) -> list[Collector]:
    constructors = {
        "psutil": lambda: PsutilCollector(config.psutil),
        "nvml": lambda: NvmlCollector(config.nvml),
        "lhm": lambda: LhmCollector(config.lhm, client, secrets),
        "macmon": lambda: MacmonCollector(config.macmon),
        "linux_sensors": lambda: LinuxSensorsCollector(config.linux_sensors),
        "amd_sysfs": AmdSysfsCollector,
    }
    return [constructors[name]() for name in config.enabled]
