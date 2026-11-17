# SPDX-License-Identifier: AGPL-3.0-or-later
from dataclasses import dataclass
from typing import Any, Protocol


@dataclass
class Health:
    name: str
    kind: str
    health: str = "ok"
    detail: str = ""
    doc: str = ""


class Plugin(Protocol):
    name: str
    kind: str
    interval_s: float
    timeout_s: float
    doc: str

    async def probe(self) -> Health: ...

    async def sample(self) -> Any: ...
