# SPDX-License-Identifier: AGPL-3.0-or-later

"""T6: validate the contract corpus, or captured JSON/JSONL frames offline."""

import argparse
import io
import json
import sys
from decimal import Decimal
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.validators import extend
from referencing import Registry, Resource

ROOT = Path(__file__).resolve().parents[1]
SCHEMAS = ROOT / "protocol/schemas"
EXAMPLES = ROOT / "protocol/examples"
TRANSPORTS = {"device": "ws-device.json", "agent": "ws-agent.json", "cdc": "cdc.json"}
FORMATS = FormatChecker(["date", "date-time", "uri", "ipv4", "ipv6"])


@FORMATS.checks("wifi-ssid")
def wifi_ssid(value: Any) -> bool:
    return not isinstance(value, str) or (
        "\0" not in value and 1 <= len(value.encode("utf-8")) <= 32
    )


@FORMATS.checks("wifi-passphrase")
def wifi_passphrase(value: Any) -> bool:
    return not isinstance(value, str) or (
        "\0" not in value and len(value.encode("utf-8")) <= 63
    )


def is_integer(checker: Any, value: Any) -> bool:
    if isinstance(value, Decimal):
        return value == value.to_integral_value()
    return Draft202012Validator.TYPE_CHECKER.is_type(value, "integer")


Validator = extend(
    Draft202012Validator,
    version="nuka-t6",
    type_checker=Draft202012Validator.TYPE_CHECKER.redefine("integer", is_integer),
)


def invalid_constant(value: str) -> None:
    raise ValueError("Non-finite numbers are not JSON")


def load_json(raw: str) -> Any:
    # Decimal keeps the protocol's 0.1 precision exact during multipleOf checks.
    return json.loads(raw, parse_float=Decimal, parse_constant=invalid_constant)


def field_path(parts: Any) -> str:
    return "$" + "".join(
        f"[{part}]" if isinstance(part, int) else f".{part}" for part in parts
    )


def report(error: Any, label: str) -> int:
    if error.context:
        return sum(report(child, label) for child in error.context)
    if error.validator == "required":
        missing = set(error.validator_value) - error.instance.keys()
        for name in sorted(missing):
            print(
                f"{label}: {field_path([*error.absolute_path, name])}: required field missing"
            )
        return len(missing)
    # Do not echo frame values: CDC captures can contain credentials.
    print(f"{label}: {field_path(error.absolute_path)}: failed {error.validator}")
    return 1


def declared_fields(node: dict[str, Any], resolver: Any, prefix: str = "d") -> set[str]:
    if "$ref" in node:
        resolved = resolver.lookup(node["$ref"])
        return declared_fields(resolved.contents, resolved.resolver, prefix)
    fields: set[str] = set()
    for name, child in node.get("properties", {}).items():
        path = f"{prefix}.{name}"
        fields.add(path)
        fields.update(declared_fields(child, resolver, path))
    if "items" in node:
        fields.update(declared_fields(node["items"], resolver, prefix + "[]"))
    return fields


def present_fields(value: Any, prefix: str = "d") -> set[str]:
    fields: set[str] = set()
    if isinstance(value, dict):
        for name, child in value.items():
            path = f"{prefix}.{name}"
            fields.add(path)
            fields.update(present_fields(child, path))
    elif isinstance(value, list):
        for child in value:
            fields.update(present_fields(child, prefix + "[]"))
    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths", nargs="*", type=Path, help="JSON files or JSONL captures"
    )
    parser.add_argument(
        "--stdin", action="store_true", help="Read captured JSONL from stdin"
    )
    parser.add_argument(
        "--transport", choices=TRANSPORTS, help="Transport for captures"
    )
    parser.add_argument(
        "--schema",
        help="Explicit schema relative to protocol/schemas, e.g. rest/host.json",
    )
    args = parser.parse_args()
    documents = {
        path.relative_to(SCHEMAS).as_posix(): load_json(
            path.read_text(encoding="utf-8")
        )
        for path in sorted(SCHEMAS.rglob("*.json"))
    }
    for name, document in documents.items():
        Draft202012Validator.check_schema(document)
    registry = Registry().with_resources(
        (document["$id"], Resource.from_contents(document))
        for document in documents.values()
    )

    # Resolve every reference, including branches not reached by a sample.
    def resolve_refs(value: Any, resolver: Any) -> None:
        if isinstance(value, dict):
            if "$ref" in value:
                resolver.lookup(value["$ref"])
            for child in value.values():
                resolve_refs(child, resolver)
        elif isinstance(value, list):
            for child in value:
                resolve_refs(child, resolver)

    for document in documents.values():
        resolve_refs(document, registry.resolver(document["$id"]))
    validators = {
        name: Validator(
            document,
            registry=registry,
            format_checker=FORMATS,
        )
        for name, document in documents.items()
    }
    failures = 0
    checked = 0
    seen: dict[str, set[str]] = {group: set() for group in TRANSPORTS}

    def validate(raw: str, label: str, name: str, transport: str | None) -> None:
        nonlocal failures, checked
        checked += 1
        try:
            value = load_json(raw)
        except (json.JSONDecodeError, ValueError):
            print(f"{label}: $: invalid JSON")
            failures += 1
            return
        if transport in {"device", "cdc"} and len(raw.encode("utf-8")) > 8192:
            print(f"{label}: $: frame exceeds 8192 UTF-8 bytes")
            failures += 1
        if transport == "cdc" and (not raw.endswith("\n") or raw.count("\n") != 1):
            print(f"{label}: $: CDC frame must be one JSON line terminated by LF")
            failures += 1
        validator = validators[name]
        if transport in TRANSPORTS and isinstance(value, dict):
            branches = documents[name]["oneOf"]
            selected = next(
                (
                    branch
                    for branch in branches
                    if branch["properties"]["t"]["const"] == value.get("t")
                ),
                None,
            )
            if selected is None:
                print(f"{label}: $.t: undocumented message type")
                failures += 1
                return
            seen[transport].add(value["t"])
            # Keep the original base URI while limiting diagnostics to this t.
            validator = validator.evolve(
                schema={**documents[name], "oneOf": [selected]}
            )
        failures += sum(report(error, label) for error in validator.iter_errors(value))
        if transport == "cdc" and isinstance(value, dict) and value.get("t") == "ok":
            variant = Path(label).name
            response = {
                "ok.info.json": "info_response",
                "ok.diag.json": "diag_response",
                "ok.import.json": "import_response",
            }.get(variant)
            if response:
                failures += sum(
                    report(error, label)
                    for error in validators["cdc.json"]
                    .evolve(schema={"$ref": f"#/$defs/{response}"})
                    .iter_errors(value["d"])
                )

    corpus = not args.paths and not args.stdin
    paths = (
        sorted(
            path for path in EXAMPLES.rglob("*") if path.suffix in {".json", ".jsonl"}
        )
        if corpus
        else args.paths
    )
    for path in paths:
        group = args.transport or path.parent.name
        if args.schema:
            name = args.schema
            transport = args.transport
        elif group in TRANSPORTS:
            name, transport = TRANSPORTS[group], group
        elif group == "rest":
            name, transport = "rest/" + path.name.split(".")[0] + ".json", None
        elif group == "backup":
            name, transport = "vault-backup.json", None
        elif group == "provisioning":
            name, transport = "provisioning.json", None
        else:
            parser.error("External captures need --transport or --schema")
        raw = path.read_bytes().decode("utf-8")
        if path.suffix == ".jsonl":
            for line_no, line in enumerate(raw.splitlines(keepends=True), 1):
                validate(line, f"{path.name}:{line_no}", name, transport)
        else:
            label = (
                path.relative_to(ROOT).as_posix()
                if path.is_relative_to(ROOT)
                else path.name
            )
            validate(raw, label, name, transport)
    if args.stdin:
        name = args.schema or TRANSPORTS[args.transport or "device"]
        for line_no, line in enumerate(
            io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8", newline=""), 1
        ):
            validate(
                line,
                f"stdin:{line_no}",
                name,
                args.transport or (None if args.schema else "device"),
            )
    if corpus:
        for name, path in (
            ("ws-device.json", "device/ota.check.json"),
            ("cdc.json", "cdc/info.json"),
            ("provisioning.json", "provisioning/provisioning.json"),
        ):
            frame = load_json((EXAMPLES / path).read_text(encoding="utf-8"))
            cases = (
                ({"extra": "a\0b"}, False),
                ({"extra\0key": "text"}, False),
                ({"extra": [{"key\0": "text"}]}, False),
                ({"extra": {"nested": ["\0"]}}, False),
                ({"extra": r"\u0000"}, True),
            )
            for index, (extra, expected) in enumerate(cases):
                if validators[name].is_valid({**frame, **extra}) != expected:
                    print(f"{name}: no-NUL boundary case {index + 1} failed")
                    failures += 1
        for name, path, field in (
            ("ws-device.json", "device/hello.json", ("d", "dev_id")),
            ("ws-agent.json", "agent/hello.json", ("d", "agent_id")),
            ("cdc.json", "cdc/prov.set.json", ("d", "hub_id")),
            ("rest/ai-account.json", "rest/ai-account.json", ("id",)),
            ("rest/media.json", "rest/media.json", ("src_id",)),
        ):
            frame = load_json((EXAMPLES / path).read_text(encoding="utf-8"))
            target = frame
            for key in field[:-1]:
                target = target[key]
            cases = (
                ("x", True),
                ("x" * 32, True),
                ("example\n", False),
                ("x" * 32 + "\n", False),
                ("", False),
                ("x" * 33, False),
            )
            for index, (value, expected) in enumerate(cases):
                target[field[-1]] = value
                if validators[name].is_valid(frame) != expected:
                    print(f"{name}: strict-ID boundary case {index + 1} failed")
                    failures += 1
        for group, name in TRANSPORTS.items():
            documented = {
                branch["properties"]["t"]["const"]
                for branch in documents[name]["oneOf"]
            }
            for missing in sorted(documented - seen[group]):
                print(f"examples/{group}: missing sample for {missing}")
                failures += 1
        cfg = load_json((EXAMPLES / "device/cfg.full.json").read_text(encoding="utf-8"))
        for field, cases in (
            ("home_face", (("clock", True), ("assistant", True), ("pet", False))),
            (
                "volume",
                ((0, True), (100, True), (-1, False), (101, False), (True, False)),
            ),
            ("assistant", ((True, True), (False, True), (1, False))),
            (
                "key3_long",
                (({"action": "talk"}, True), ({"action": "macro"}, False)),
            ),
        ):
            for index, (value, expected) in enumerate(cases):
                frame = {**cfg, "d": {**cfg["d"], field: value}}
                if validators["ws-device.json"].is_valid(frame) != expected:
                    print(
                        f"ws-device.json: cfg.{field} boundary case {index + 1} failed"
                    )
                    failures += 1
        chat = {"dev_id": "nuka_example", "text": "hello"}
        for name, cases in (
            (
                "rest/assistant-chat-request.json",
                (
                    (chat, True),
                    ({**chat, "text": ""}, False),
                    ({**chat, "text": "x" * 16385}, False),
                    ({**chat, "file_ids": ["f_example", "f_example"]}, False),
                    ({**chat, "file_ids": [f"f_{index}" for index in range(9)]}, False),
                    ({**chat, "dev_id": "nuka_example\n"}, False),
                ),
            ),
            (
                "rest/assistant-memory-create.json",
                (
                    ({"dev_id": "nuka_example", "text": "x" * 2000}, True),
                    ({"dev_id": "nuka_example", "text": "x" * 2001}, False),
                    ({"dev_id": "nuka_example", "text": ""}, False),
                ),
            ),
            (
                "assistant-config.json",
                (
                    ({}, True),
                    ({"enabled": True}, False),
                    (
                        {
                            "enabled": True,
                            "llm": {
                                "base_url": "http://192.0.2.10:11434/v1",
                                "model": "example",
                            },
                        },
                        True,
                    ),
                    ({"llm": {"base_url": "file:///example"}}, False),
                    ({"llm": {"base_url": "https://user@llm.example.com/v1"}}, False),
                    ({"llm": {"context_window": 1023}}, False),
                    ({"llm": {"context_window": 1048577}}, False),
                ),
            ),
            (
                "rest/config.json",
                (
                    ({"security": {"remote_admin": False}}, True),
                    ({"security": {"remote_admin": True}}, False),
                    (
                        {
                            "security": {
                                "remote_admin": True,
                                "cf_access": {
                                    "enabled": True,
                                    "team_domain": "<TEAM>.cloudflareaccess.com",
                                    "aud": "<AUD>",
                                },
                            }
                        },
                        True,
                    ),
                ),
            ),
        ):
            for index, (value, expected) in enumerate(cases):
                if validators[name].is_valid(value) != expected:
                    print(f"{name}: assistant boundary case {index + 1} failed")
                    failures += 1
        for name, path in (
            ("rest/assistant-status.json", "rest/assistant-status.json"),
            ("rest/assistant-chat-request.json", "rest/assistant-chat-request.json"),
            ("rest/assistant-history.json", "rest/assistant-history.json"),
            ("rest/assistant-memory-create.json", "rest/assistant-memory-create.json"),
            ("rest/assistant-memories.json", "rest/assistant-memories.json"),
            ("rest/assistant-files.json", "rest/assistant-files.json"),
            ("rest/assistant-pending.json", "rest/assistant-pending.json"),
            ("rest/config.json", "rest/config.assistant.json"),
        ):
            expected = declared_fields(
                documents[name], registry.resolver(documents[name]["$id"])
            )
            actual = present_fields(
                load_json((EXAMPLES / path).read_text(encoding="utf-8"))
            )
            for missing in sorted(expected - actual):
                print(f"{path}: $.{missing}: full sample omits documented field")
                failures += 1
        events = {
            load_json(path.read_text(encoding="utf-8"))["event"]
            for path in (EXAMPLES / "rest").glob("assistant-event.*.json")
        }
        for branch in documents["rest/assistant-event.json"]["oneOf"]:
            event = branch["properties"]["event"]["const"]
            if event not in events:
                print(f"examples/rest: missing assistant SSE sample for {event}")
                failures += 1
        device = documents["ws-device.json"]
        for branch in device["oneOf"]:
            topic = branch["properties"]["t"]["const"]
            if not topic.startswith("vm.") and topic != "cfg":
                continue
            for variant in ("full", "min"):
                path = EXAMPLES / "device" / f"{topic}.{variant}.json"
                if not path.is_file():
                    print(f"examples/device: missing {topic}.{variant}.json")
                    failures += 1
            full = EXAMPLES / "device" / f"{topic}.full.json"
            if full.is_file():
                expected = declared_fields(
                    branch["properties"]["d"], registry.resolver(device["$id"])
                )
                actual = present_fields(
                    load_json(full.read_text(encoding="utf-8"))["d"]
                )
                for missing in sorted(expected - actual):
                    print(
                        f"{full.name}: $.{missing}: full sample omits documented field"
                    )
                    failures += 1
        validate(
            (ROOT / "protocol/wmo_icon_map.json").read_text(encoding="utf-8"),
            "protocol/wmo_icon_map.json",
            "wmo-icon-map.json",
            None,
        )
    if checked == 0:
        print("T6: no frames supplied")
        return 1
    print(f"T6: {len(documents)} schemas, {checked} payloads, {failures} failures")
    return int(failures > 0)


if __name__ == "__main__":
    raise SystemExit(main())
