# SPDX-License-Identifier: AGPL-3.0-or-later
import hashlib
import secrets
import time

from .history import iso


class Tokens:
    def __init__(self, history):
        self.history = history
        self.sessions: dict[str, dict] = {}

    async def create(self, scope: str, name: str) -> dict:
        token = secrets.token_urlsafe(32)
        result = {
            "id": "tk_" + secrets.token_hex(6),
            "scope": scope,
            "name": name,
            "created": iso(),
        }
        await self.history.sql(
            "INSERT INTO tokens(id,scope,name,sha256,created) VALUES (?,?,?,?,?)",
            (
                result["id"],
                scope,
                name,
                hashlib.sha256(token.encode()).hexdigest(),
                result["created"],
            ),
        )
        await self.history.audit("token", "cli", {"id": result["id"], "action": "create"})
        return {**result, "token": token}

    async def authenticate(self, token: str, scope: str | set[str]) -> dict | None:
        rows = await self.history.sql(
            "SELECT * FROM tokens WHERE sha256=?", (hashlib.sha256(token.encode()).hexdigest(),)
        )
        if not rows or rows[0]["revoked"]:
            return None
        row = rows[0]
        scopes = {scope} if isinstance(scope, str) else scope
        if row["scope"] not in scopes:
            return None
        await self.history.sql("UPDATE tokens SET last_used=? WHERE id=?", (iso(), row["id"]))
        return row

    async def listing(self):
        rows = await self.history.sql(
            "SELECT id,scope,name,created,last_used FROM tokens WHERE revoked=0"
        )
        return [{k: v for k, v in row.items() if v is not None} for row in rows]

    async def revoke(self, identity: str):
        await self.history.sql("UPDATE tokens SET revoked=1 WHERE id=?", (identity,))
        for key, session in list(self.sessions.items()):
            if session["token_id"] == identity:
                del self.sessions[key]
        await self.history.audit("token", "admin", {"id": identity, "action": "revoke"})

    def session(self, token_id: str, days: int) -> tuple[str, str]:
        cookie, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(24)
        self.sessions[hashlib.sha256(cookie.encode()).hexdigest()] = {
            "token_id": token_id,
            "csrf": csrf,
            "expires": time.time() + days * 86400,
        }
        return cookie, csrf

    async def get_session(self, cookie: str) -> dict | None:
        session = self.sessions.get(hashlib.sha256(cookie.encode()).hexdigest())
        if session is None or session["expires"] <= time.time():
            return None
        rows = await self.history.sql(
            "SELECT revoked FROM tokens WHERE id=?", (session["token_id"],)
        )
        return session if rows and not rows[0]["revoked"] else None

    def logout(self, cookie: str):
        self.sessions.pop(hashlib.sha256(cookie.encode()).hexdigest(), None)
