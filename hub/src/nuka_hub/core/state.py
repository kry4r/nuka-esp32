# SPDX-License-Identifier: AGPL-3.0-or-later
from collections import defaultdict


class StateStore:
    def __init__(self):
        self.values: dict[str, object] = {
            "hosts": [],
            "servers": [],
            "ai": {"accounts": [], "providers": []},
            "agents": {"sessions": [], "pending": []},
            "notify": [],
            "devices": [],
            "media": [],
            "weather": {},
            "plugins": [],
        }
        self.revisions: dict[str, int] = defaultdict(int)

    def set(self, topic: str, value: object) -> bool:
        if self.values.get(topic) == value:
            return False
        self.values[topic] = value
        self.revisions[topic] += 1
        return True

    def get(self, topic: str):
        return self.values[topic]

    def snapshot(self) -> dict:
        return dict(self.values)
