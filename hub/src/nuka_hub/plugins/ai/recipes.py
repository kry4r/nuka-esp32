# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import json
import tomllib
from pathlib import Path

RECIPES = tomllib.loads(Path(__file__).with_name("recipes.toml").read_text(encoding="utf-8"))


def render(kind: str, auth_index: str, account_id: str = "", project_id: str = "") -> dict:
    recipe = RECIPES[kind]
    headers = {
        key: value.replace("{account_id}", account_id) for key, value in recipe["headers"].items()
    }
    result = {
        "auth_index": auth_index,
        "method": recipe["method"],
        "url": recipe["url"],
        "header": headers,
    }
    if "body" in recipe:
        result["data"] = json.dumps({"project": project_id}, separators=(",", ":"))
    return result


def allowed_call(payload: dict) -> bool:
    return any(
        payload["method"] == recipe["method"] and payload["url"] == recipe["url"]
        for recipe in RECIPES.values()
    )
