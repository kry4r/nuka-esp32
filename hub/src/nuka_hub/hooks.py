# SPDX-License-Identifier: AGPL-3.0-or-later
import base64
import json
from pathlib import Path

EVENTS = {
    "claude": [
        "PermissionRequest",
        "Notification",
        "Stop",
        "StopFailure",
        "PreToolUse",
        "PostToolUse",
        "UserPromptSubmit",
        "SessionStart",
        "SessionEnd",
    ],
    "codex": ["SessionStart", "PreToolUse", "PostToolUse", "Stop", "SessionEnd"],
    "gemini": [
        "SessionStart",
        "BeforeTool",
        "AfterTool",
        "AfterAgent",
        "SessionEnd",
        "Notification",
    ],
}


def hook_url(config) -> str:
    if config.agent.hub_url:
        from urllib.parse import urlsplit

        url = urlsplit(config.agent.hub_url)
        return ("https" if url.scheme == "wss" else "http") + "://" + url.netloc
    return f"http://localhost:{config.hub.port}"


def hooks(action: str, source: str, config, home: Path | None = None):
    directory = (home or Path.home()) / ("." + source)
    path = directory / ("hooks.json" if source == "codex" else "settings.json")
    backup = directory / (path.name + ".nuka-original")
    if action == "uninstall":
        original = json.loads(backup.read_text(encoding="utf-8"))
        if original["existed"]:
            path.write_bytes(base64.b64decode(original["bytes"]))
        else:
            path.unlink()
        backup.unlink()
        print("Original hook configuration restored.")
        return
    if backup.exists():
        raise ValueError("nuka hooks are already installed; uninstall before reinstalling")
    directory.mkdir(parents=True, exist_ok=True)
    raw = path.read_bytes() if path.exists() else b""
    existing = json.loads(raw.decode("utf-8-sig")) if raw else {}
    backup.write_text(
        json.dumps({"existed": path.exists(), "bytes": base64.b64encode(raw).decode()}),
        encoding="utf-8",
    )
    settings = existing.setdefault("hooks", {})
    for event in EVENTS[source]:
        if source == "claude":
            handler = {
                "type": "http",
                "url": hook_url(config) + "/hook/claude",
                "timeout": 90 if event == "PermissionRequest" else 2,
                "headers": {"Authorization": "Bearer $NUKA_HOOK_TOKEN"},
                "allowedEnvVars": ["NUKA_HOOK_TOKEN"],
            }
        else:
            handler = {
                "type": "command",
                "command": f"nuka-hook {source} {event}",
                "timeout": 2000 if source == "gemini" else 2,
            }
        settings.setdefault(event, []).append({"hooks": [handler]})
    path.write_text(json.dumps(existing, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Hooks installed; original file backed up.")
    if source == "codex":
        print("Open /hooks in Codex and review/trust the installed hook definitions.")
