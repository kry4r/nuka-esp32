# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

from typing import Protocol

import httpx
from pydantic import BaseModel, ConfigDict, Field, HttpUrl


class SearchResult(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    title: str = Field(max_length=2048)
    url: HttpUrl
    snippet: str = Field(max_length=16384)


class Search(Protocol):
    async def search(self, query: str) -> list[SearchResult]: ...


class SearxItem(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    title: str
    url: HttpUrl
    content: str = ""


class SearxResponse(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    results: list[SearxItem]


class BraveItem(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    title: str
    url: HttpUrl
    description: str = ""


class BraveWeb(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    results: list[BraveItem]


class BraveResponse(BaseModel):
    model_config = ConfigDict(extra="ignore", strict=True)
    web: BraveWeb | None = None


class SearxngSearch:
    def __init__(
        self, client: httpx.AsyncClient, *, base_url: str, max_results: int, key: str = ""
    ):
        self.client, self.base_url = client, base_url.rstrip("/")
        self.max_results, self.key = max_results, key

    async def search(self, query: str) -> list[SearchResult]:
        response = await self.client.get(
            self.base_url + "/search",
            params={"q": query, "format": "json"},
            headers={"Authorization": f"Bearer {self.key}"} if self.key else {},
        )
        response.raise_for_status()
        data = SearxResponse.model_validate(response.json())
        return [
            SearchResult(title=item.title[:2048], url=item.url, snippet=item.content[:16384])
            for item in data.results[: self.max_results]
        ]


class BraveSearch:
    def __init__(self, client: httpx.AsyncClient, *, base_url: str, key: str, max_results: int):
        self.client, self.base_url = client, base_url.rstrip("/")
        self.key, self.max_results = key, max_results

    async def search(self, query: str) -> list[SearchResult]:
        response = await self.client.get(
            self.base_url + "/web/search",
            params={"q": query, "count": self.max_results},
            headers={"X-Subscription-Token": self.key, "Accept": "application/json"},
        )
        response.raise_for_status()
        data = BraveResponse.model_validate(response.json())
        if data.web is None:
            return []
        return [
            SearchResult(title=item.title[:2048], url=item.url, snippet=item.description[:16384])
            for item in data.web.results[: self.max_results]
        ]
