# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import os
import stat
import tomllib
from pathlib import Path

import keyring
from keyring.errors import NoKeyringError, PasswordDeleteError


class Secrets:
    def __init__(self, config_dir: Path):
        self.path = config_dir / "secrets.toml"

    def file_values(self) -> dict[str, str]:
        if not self.path.exists():
            return {}
        if os.name != "nt" and stat.S_IMODE(self.path.stat().st_mode) != 0o600:
            raise PermissionError("secrets.toml must have mode 600")
        values = tomllib.loads(self.path.read_text(encoding="utf-8"))
        if any(not isinstance(v, str) for v in values.values()):
            raise ValueError("secrets.toml requires flat string values")
        return values

    @staticmethod
    def env_name(name: str) -> str:
        return "NUKA_SECRET_" + name.upper().replace(".", "_").replace("-", "_")

    def get(self, name: str) -> str | None:
        value = os.environ.get(self.env_name(name))
        if value is not None:
            return value
        try:
            value = keyring.get_password("nuka-hub", name)
        except NoKeyringError:
            value = None
        return value if value is not None else self.file_values().get(name)

    def set(self, name: str, value: str, *, file: bool = False) -> None:
        if not file:
            keyring.set_password("nuka-hub", name, value)
            return
        import json

        values = self.file_values()
        values[name] = value
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write("# SPDX-License-Identifier: AGPL-3.0-or-later\n")
            stream.write(
                "\n".join(f"{json.dumps(k)} = {json.dumps(v)}" for k, v in values.items()) + "\n"
            )

    def remove(self, name: str) -> None:
        try:
            keyring.delete_password("nuka-hub", name)
        except (NoKeyringError, PasswordDeleteError):
            pass
        values = self.file_values()
        if name in values:
            del values[name]
            import json

            self.path.write_text(
                "\n".join(f"{json.dumps(k)} = {json.dumps(v)}" for k, v in values.items()) + "\n",
                encoding="utf-8",
            )

    def listing(self, names: set[str]) -> list[dict]:
        file_values = self.file_values()
        rows = []
        for name in sorted(names | file_values.keys()):
            if self.env_name(name) in os.environ:
                rows.append({"name": name, "source": "env"})
                continue
            try:
                value = keyring.get_password("nuka-hub", name)
            except NoKeyringError:
                value = None
            if value is not None:
                rows.append({"name": name, "source": "keyring"})
            elif name in file_values:
                rows.append({"name": name, "source": "file"})
        return rows
