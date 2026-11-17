# SPDX-License-Identifier: AGPL-3.0-or-later
import json
import secrets
import time
from datetime import datetime

from nuka_hub.vm.render import notification

from .history import iso


class Notifications:
    def __init__(self, runtime):
        self.runtime = runtime
        self.dedupe: dict[str, tuple[float, str]] = {}
        self.web_only: set[str] = set()
        self.server_alerts: list[tuple[str, dict]] = []
        self.titles: dict[str, dict[str, str]] = {}

    async def listing(self):
        rows = await self.runtime.history.sql(
            "SELECT payload FROM notifications ORDER BY ts DESC LIMIT ?",
            (self.runtime.config.notify.keep,),
        )
        items = []
        self.web_only.clear()
        self.server_alerts.clear()
        self.titles.clear()
        for row in rows:
            item = json.loads(row["payload"])
            expires = datetime.fromisoformat(item["ts"]).timestamp() + item["ttl_s"]
            if item["ttl_s"] and expires <= time.time():
                continue
            if item.pop("_web_only", False):
                self.web_only.add(item["nid"])
            titles = item.pop("_titles", None)
            if titles is not None:
                self.titles[item["nid"]] = titles
            server = item.pop("_server", None)
            if server is not None:
                self.server_alerts.append(
                    (
                        server,
                        {
                            "rule": "hook:" + item["nid"],
                            "level": item["level"],
                            "since": item["ts"],
                        },
                    )
                )
            items.append(item)
        return items

    def localized(self, item: dict, lang: str) -> dict:
        titles = self.titles.get(item["nid"])
        return {**item, "title": titles[lang]} if titles is not None else item

    def badges(self, servers: list[dict]) -> list[dict]:
        result = []
        for server in servers:
            names = {
                server["name"],
                *self.runtime.config.servers.merge.aliases.get(server["name"], []),
            }
            alerts = [alert for alert in server["alerts"] if not alert["rule"].startswith("hook:")]
            alerts += [alert for name, alert in self.server_alerts if name in names]
            result.append({**server, "alerts": alerts})
        return result

    def quiet(self) -> bool:
        value = self.runtime.config.notify.quiet_hours
        if not value:
            return False
        start, end = value.split("-")
        current = datetime.now(self.runtime.timezone).strftime("%H:%M")
        return start <= current < end if start < end else current >= start or current < end

    async def send(self, value: dict, *, web_only: bool = False) -> str:
        now = time.time()
        key = value.get("dedupe_key")
        nid = "n_" + secrets.token_hex(8)
        if (
            key in self.dedupe
            and now - self.dedupe[key][0] < self.runtime.config.notify.dedupe_window
        ):
            nid = self.dedupe[key][1]
        if key:
            self.dedupe[key] = (now, nid)
        item = {
            "nid": nid,
            "source": value.get("source", "notify"),
            "level": value.get("level", "info"),
            "title": value["_titles"][self.runtime.config.hub.lang]
            if "_titles" in value
            else value["title"],
            "ts": iso(now),
            "actions": [{"kind": "dismiss"}],
            "ttl_s": value.get("ttl_s", 86400),
        }
        item.update({k: value[k] for k in ("body", "dedupe_key") if k in value})
        stored = dict(item)
        if "_titles" in value:
            stored["_titles"] = value["_titles"]
        if web_only:
            stored["_web_only"] = True
        if "server" in value:
            stored["_server"] = value["server"]
        await self.runtime.history.sql(
            "INSERT OR REPLACE INTO notifications VALUES (?,?,?,?,?,?,?,?)",
            (
                nid,
                now,
                item["level"],
                item["source"],
                item["title"],
                item.get("body"),
                key,
                json.dumps(stored),
            ),
        )
        await self.runtime.history.sql(
            (
                "DELETE FROM notifications WHERE nid NOT IN (SELECT nid "
                "FROM notifications ORDER BY ts DESC LIMIT ?)"
            ),
            (self.runtime.config.notify.keep,),
        )
        await self.runtime.set("notify", await self.listing())
        await self.runtime.set("servers", self.badges(self.runtime.state.get("servers")))
        if (
            not web_only
            and self.runtime.devices
            and (item["level"] == "critical" or not self.quiet())
        ):
            await self.runtime.devices.event(
                "notify.push", notification(item) | {"wake": item["level"] == "critical"}
            )
        return nid
