# SPDX-License-Identifier: AGPL-3.0-or-later
import time
from datetime import datetime


def epoch(value: str) -> int:
    return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())


def size(value: float) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024 or unit == "TB":
            return f"{value:.1f} {unit}"
        value /= 1024


def pct(used: float, total: float) -> float:
    return round(used / total * 100, 1)


def host(item: dict) -> dict:
    result = {key: item[key] for key in ("id", "name", "online", "os") if key in item}
    if "cpu" in item:
        result["cpu"] = {k: v for k, v in item["cpu"].items() if k in {"pct", "temp_c", "freq_mhz"}}
    if item.get("gpus"):
        gpu = item["gpus"][0]
        result["gpu"] = {k: v for k, v in gpu.items() if k in {"name", "pct", "temp_c", "power_w"}}
        if "mem_used_b" in gpu and "mem_total_b" in gpu:
            result["gpu"]["mem_pct"] = pct(gpu["mem_used_b"], gpu["mem_total_b"])
    if "mem" in item and "used_b" in item["mem"] and "total_b" in item["mem"]:
        mem = item["mem"]
        result["mem"] = {
            "pct": pct(mem["used_b"], mem["total_b"]),
            "txt": f"{size(mem['used_b'])} / {size(mem['total_b'])}",
        }
    if "net" in item:
        result["net"] = {
            key + "_txt": size(item["net"][key + "_bps"]) + "/s"
            for key in ("up", "down")
            if key + "_bps" in item["net"]
        }
    if "disks" in item:
        result["disks"] = [
            {"label": disk["mount"], "pct": pct(disk["used_b"], disk["total_b"])}
            for disk in item["disks"][:3]
            if "used_b" in disk and "total_b" in disk
        ]
    if "bat" in item:
        result["bat"] = {"pct": item["bat"]["pct"], "chg": item["bat"]["charging"]}
    return result


def server(item: dict) -> dict:
    keys = (
        "id",
        "name",
        "online",
        "src",
        "cpu_pct",
        "mem_pct",
        "disk_pct",
        "latency_ms",
        "uptime_pct",
    )
    result = {key: item[key] for key in keys if key in item}
    result["alerts"] = len(item["alerts"])
    for key in ("mem", "disk"):
        if key + "_used_b" in item and key + "_total_b" in item:
            result[key + "_pct"] = pct(item[key + "_used_b"], item[key + "_total_b"])
    if "load" in item:
        result["load1"] = item["load"][0]
    if "net_up_bps" in item and "net_down_bps" in item:
        result["net_txt"] = f"↑{size(item['net_up_bps'])} ↓{size(item['net_down_bps'])}/s"
    if "uptime_s" in item:
        result["uptime_txt"] = f"{item['uptime_s'] // 86400}d"
    return result


def notification(item: dict) -> dict:
    result = {
        "nid": item["nid"],
        "level": item["level"],
        "src": item["source"],
        "title": item["title"],
        "ts": epoch(item["ts"]),
    }
    if "body" in item:
        result["body"] = item["body"][:200]
    return result


def ai_account(item: dict, lang: str) -> dict:
    result = {
        key: item[key] for key in ("id", "label", "status", "plan", "breaker_open") if key in item
    }
    result["kind"] = item["provider_kind"]
    if "recover_at" in item:
        result["recover_at"] = epoch(item["recover_at"])
    result["windows"] = []
    for window in item["windows"]:
        value = {
            key: window[key] for key in ("kind", "used_pct", "label", "stale") if key in window
        }
        value["src"] = window["source"]
        if lang == "zh-CN" and "label" in value:
            value["label"] = {
                "5h": "5 小时",
                "weekly": "每周",
                "weekly_sonnet": "Sonnet 每周",
                "weekly_opus": "Opus 每周",
            }.get(value["label"], value["label"])
        if "reset_at" in window:
            value["reset_at"] = epoch(window["reset_at"])
        result["windows"].append(value)
    if "credits" in item:
        result["credits_txt"] = f"{item['credits']['balance']:.2f} {item['credits']['currency']}"
    if item["windows"]:
        minutes = max(
            0, int(time.time() - max(epoch(w["observed_at"]) for w in item["windows"])) // 60
        )
        result["fresh_txt"] = f"{minutes} 分钟前" if lang == "zh-CN" else f"{minutes} min ago"
    return result


def view(topic: str, value, lang: str, dismissed: set[str]) -> dict:
    if topic == "hosts":
        return {"items": [host(item) for item in sorted(value, key=lambda x: not x["online"])]}
    if topic == "servers":
        return {
            "items": [
                server(item)
                for item in sorted(value, key=lambda x: (x["online"], -len(x["alerts"])))
            ]
        }
    if topic == "ai":
        result = {"accounts": [ai_account(item, lang) for item in value["accounts"]]}
        if "summary" in value:
            summary = value["summary"]
            result["summary"] = {
                "req_today": summary["requests"],
                "tok_today_txt": size(summary["tokens"])
                .replace(" B", "")
                .replace("KB", "K")
                .replace("MB", "M"),
                "fail_pct": pct(summary["failures"], summary["requests"])
                if summary["requests"]
                else 0,
            }
        return result
    if topic == "agents":
        sessions = []
        for item in value["sessions"]:
            entry = {
                key: item[key] for key in ("sid", "host", "project", "state", "tool") if key in item
            }
            entry.update(src=item["source"], since=epoch(item["since"]))
            if "last_message" in item:
                entry["msg"] = item["last_message"][:140]
            sessions.append(entry)
        return {"pending": len(value["pending"]), "sessions": sessions}
    if topic == "notify":
        return {"items": [notification(item) for item in value if item["nid"] not in dismissed]}
    if topic == "media":
        items = []
        for item in value:
            entry = dict(item)
            if "at" in entry:
                entry["at"] = epoch(entry["at"])
            items.append(entry)
        return {"items": items}
    if topic == "weather":
        result = dict(value)
        if lang == "zh-CN" and "now" in result:
            result["now"] = dict(result["now"])
            result["now"]["desc_txt"] = {
                "clear_day": "晴",
                "clear_night": "晴夜",
                "partly_cloudy": "局部多云",
                "cloudy": "多云",
                "fog": "雾",
                "drizzle": "毛毛雨",
                "rain": "雨",
                "heavy_rain": "大雨",
                "sleet": "雨夹雪",
                "snow": "雪",
                "thunder": "雷暴",
                "unknown": "未知",
            }[result["now"]["icon"]]
        if "updated" in result:
            result["updated"] = epoch(result["updated"])
        return result
    return dict(value)
