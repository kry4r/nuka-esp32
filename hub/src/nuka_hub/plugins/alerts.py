# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import json
import operator
import re
import warnings
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

TOKEN = re.compile(
    r"\s*(?:(\|\||&&|==|!=|>=|<=|[!><()])|(-?\d+(?:\.\d+)?)|"
    r'("(?:[^"\\]|\\.)*")|([A-Za-z_]\w*(?:\.\w+|\[\d+\])*))'
)
MISSING = object()
COMPARE = {
    "==": operator.eq,
    "!=": operator.ne,
    ">": operator.gt,
    ">=": operator.ge,
    "<": operator.lt,
    "<=": operator.le,
}
KNOWN = {
    "hosts": (
        "(?:id|name|online|os|role|last_seen|cpu\\.(?:pct|temp_c|"
        "freq_mhz|power_w)|cpu\\.per_core\\[\\d+\\]|gpus\\[\\d+\\]\\.(?:"
        "name|pct|temp_c|power_w|mem_used_b|mem_total_b|fan_pct|"
        "clock_mhz)|mem\\.(?:pct|used_b|total_b|swap_used_b|swap_"
        "total_b)|disks\\[\\d+\\]\\.(?:pct|mount|used_b|total_b|read"
        "_bps|write_bps)|net\\.(?:up_bps|down_bps)|bat\\.(?:pct|ch"
        "arging)|fans\\[\\d+\\]\\.(?:label|rpm)|temps\\[\\d+\\]"
        "\\.(?:label|c)|plugins(?:\\.[A-Za-z_]\\w*)*)"
    ),
    "servers": (
        "(?:id|name|online|src|src_name|last_seen|cpu_pct|mem_pc"
        "t|disk_pct|mem_used_b|mem_total_b|disk_used_b|disk_tota"
        "l_b|net_up_bps|net_down_bps|uptime_s|latency_ms|uptime_"
        "pct|load\\[\\d+\\]|alerts\\[\\d+\\]\\.(?:rule|level|since))"
    ),
    "ai": (
        "(?:id|provider_kind|label|plan|status|recover_"
        "at|windows\\.(?:5h|weekly)\\.used_pct|windows\\[\\d+\\]\\.(?:"
        "kind|label|used_pct|stale|source|reset_at|observed_at)|credits\\.(?:balanc"
        "e|currency)|today\\.(?:requests|tokens|failures))"
    ),
}
BUILTIN_TITLES = {
    "zh-CN": {
        "server_offline": "服务器离线",
        "cpu_high": "CPU 占用过高",
        "mem_high": "内存占用过高",
        "disk_high": "磁盘占用过高",
        "ai_exhausted": "AI 配额耗尽",
        "ai_reauth": "AI 账号需要重新登录",
    },
    "en": {
        "server_offline": "Server offline",
        "cpu_high": "CPU usage high",
        "mem_high": "Memory usage high",
        "disk_high": "Disk usage high",
        "ai_exhausted": "AI quota exhausted",
        "ai_reauth": "AI account needs sign-in",
    },
}


def normalize_field(field: str) -> str:
    return re.sub(r"^(?:host|server|ai)\.", "", field)


def field_value(item: dict, field: str) -> Any:
    value: Any = item
    for key, index in re.findall(r"([A-Za-z_0-9]+)|\[(\d+)\]", normalize_field(field)):
        if index:
            if not isinstance(value, list) or int(index) >= len(value):
                return MISSING
            value = value[int(index)]
        elif isinstance(value, list) and key in {"5h", "weekly"}:
            value = next((window for window in value if window["kind"] == key), MISSING)
        elif isinstance(value, dict):
            value = value.get(key, MISSING)
        else:
            return MISSING
    return value


class Expression:
    def __init__(self, source: str, scope: str = "servers"):
        self.tokens: list[tuple[str, Any]] = []
        position = 0
        while position < len(source.rstrip()):
            match = TOKEN.match(source, position)
            if match is None:
                raise ValueError(f"Invalid alert expression at character {position + 1}")
            symbol, number, string, field = match.groups()
            if symbol:
                self.tokens.append((symbol, symbol))
            elif number:
                self.tokens.append(("literal", float(number)))
            elif string:
                self.tokens.append(("literal", json.loads(string)))
            elif field in {"true", "false"}:
                self.tokens.append(("literal", field == "true"))
            else:
                normalized = normalize_field(field)
                if re.fullmatch(KNOWN[scope], normalized) is None:
                    warnings.warn(f"Unknown alert field: {field}", stacklevel=2)
                    self.tokens.append(("literal", MISSING))
                else:
                    self.tokens.append(("field", normalized))
            position = match.end()
        self.index = 0
        self.tree = self.parse_or()
        if self.index != len(self.tokens):
            raise ValueError("Unexpected alert expression token")

    def accept(self, *kinds: str) -> tuple[str, Any] | None:
        if self.index < len(self.tokens) and self.tokens[self.index][0] in kinds:
            value = self.tokens[self.index]
            self.index += 1
            return value
        return None

    def parse_or(self) -> tuple:
        node = self.parse_and()
        while self.accept("||"):
            node = ("||", node, self.parse_and())
        return node

    def parse_and(self) -> tuple:
        node = self.parse_cmp()
        while self.accept("&&"):
            node = ("&&", node, self.parse_cmp())
        return node

    def parse_cmp(self) -> tuple:
        if self.accept("!"):
            return ("!", self.parse_cmp())
        if self.accept("("):
            node = self.parse_or()
            if not self.accept(")"):
                raise ValueError("Unclosed alert expression parenthesis")
            return node
        token = self.accept("field", "literal")
        if token is None:
            raise ValueError("Expected an alert field or literal")
        comparison = self.accept(*COMPARE)
        if comparison:
            right = self.accept("field", "literal")
            if right is None:
                raise ValueError("Expected right side of alert comparison")
            return (comparison[0], token, right)
        return token

    def matches(self, item: dict) -> bool:
        def visit(node: tuple) -> Any:
            op = node[0]
            if op == "literal":
                return node[1]
            if op == "field":
                return field_value(item, node[1])
            left = visit(node[1])
            if op == "&&":
                if left is MISSING or not left:
                    return False
                right = visit(node[2])
                return right is not MISSING and bool(right)
            if op == "||":
                if left is not MISSING and left:
                    return True
                right = visit(node[2])
                return right is not MISSING and bool(right)
            if left is MISSING:
                return MISSING
            if op == "!":
                return not left
            right = visit(node[2])
            if right is MISSING:
                return MISSING
            return COMPARE[op](left, right)

        value = visit(self.tree)
        return value is not MISSING and bool(value)


def derived(item: dict) -> dict:
    result = dict(item)
    if "mem" in item and item["mem"].get("total_b", 0) > 0 and "used_b" in item["mem"]:
        result["mem"] = {**item["mem"], "pct": item["mem"]["used_b"] * 100 / item["mem"]["total_b"]}
    if "disks" in item:
        result["disks"] = [
            {**disk, "pct": disk["used_b"] * 100 / disk["total_b"]}
            if disk.get("total_b", 0) > 0 and "used_b" in disk
            else dict(disk)
            for disk in item["disks"]
        ]
    for prefix in ("mem", "disk"):
        if (
            prefix + "_pct" not in result
            and item.get(prefix + "_total_b", 0) > 0
            and prefix + "_used_b" in item
        ):
            result[prefix + "_pct"] = item[prefix + "_used_b"] * 100 / item[prefix + "_total_b"]
    return result


@dataclass
class Rule:
    name: str
    scope: str
    expression: Expression
    duration: float
    level: str
    title: str


@dataclass
class Trigger:
    since: float
    sent_at: float | None = None


class AlertEngine:
    def __init__(self, config: Any, notify_callback: Callable[[dict], Awaitable[str]]):
        self.repeat, self.notify = config.repeat, notify_callback
        self.rules = [
            Rule(
                rule.name,
                rule.scope,
                Expression(rule.when, rule.scope),
                rule.for_,
                rule.level,
                rule.title,
            )
            for rule in config.rule
        ]
        defaults = [
            ("server_offline", "servers", "online == false", 60, "critical"),
            ("cpu_high", "servers", "cpu_pct > 90", 300, "warn"),
            ("cpu_high", "hosts", "cpu.pct > 90", 300, "warn"),
            ("mem_high", "servers", "mem_pct > 90", 300, "warn"),
            ("mem_high", "hosts", "mem.pct > 90", 300, "warn"),
            ("disk_high", "servers", "disk_pct > 90", 0, "warn"),
            ("ai_exhausted", "ai", 'status == "exhausted"', 0, "warn"),
            ("ai_reauth", "ai", 'status == "reauth"', 0, "critical"),
        ]
        for name, scope, expression, duration, level in defaults:
            if config.builtin[name]:
                self.rules.append(
                    Rule(name, scope, Expression(expression, scope), duration, level, "")
                )
        self.triggers: dict[tuple[str, str, str], Trigger] = {}
        self.disk_hosts = config.builtin["disk_high"]

    async def evaluate(self, scope: str, items: list[dict], now: float) -> None:
        for item in items:
            values = derived(item)
            rules = [rule for rule in self.rules if rule.scope == scope]
            if scope == "hosts" and self.disk_hosts and item.get("disks"):
                expression = " || ".join(
                    f"disks[{index}].pct > 90" for index, _ in enumerate(item["disks"])
                )
                rules.append(Rule("disk_high", scope, Expression(expression, scope), 0, "warn", ""))
            for rule in rules:
                key = rule.name, scope, item["id"]
                matches = rule.expression.matches(values)
                trigger = self.triggers.get(key)
                if matches:
                    if trigger is None:
                        trigger = self.triggers[key] = Trigger(now)
                    if now - trigger.since >= rule.duration and (
                        trigger.sent_at is None or now - trigger.sent_at >= self.repeat
                    ):
                        name = item.get("name", item.get("label", item["id"]))
                        titles = {
                            lang: f"{name}: {labels.get(rule.name, rule.name)}"
                            for lang, labels in BUILTIN_TITLES.items()
                        }
                        title = rule.title or titles["zh-CN"]
                        title = re.sub(
                            r"\{([^{}]+)\}", lambda match: str(field_value(values, match[1])), title
                        )
                        payload = {
                            "source": "alerts",
                            "level": rule.level,
                            "title": title,
                            "dedupe_key": ":".join(key),
                        }
                        if not rule.title:
                            payload["_titles"] = titles
                        await self.notify(payload)
                        trigger.sent_at = now
                elif trigger is not None:
                    if trigger.sent_at is not None:
                        name = item.get("name", item.get("label", item["id"]))
                        titles = {
                            lang: f"{name}: {labels.get(rule.name, rule.name)} "
                            + ("已恢复" if lang == "zh-CN" else "recovered")
                            for lang, labels in BUILTIN_TITLES.items()
                        }
                        await self.notify(
                            {
                                "source": "alerts",
                                "level": "info",
                                "title": titles["zh-CN"],
                                "_titles": titles,
                                "dedupe_key": ":".join(key) + ":recovered",
                            }
                        )
                    del self.triggers[key]
