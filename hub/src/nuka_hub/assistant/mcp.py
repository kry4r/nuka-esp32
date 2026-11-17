# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import hashlib
import json
import os
import re
from collections.abc import AsyncIterator, Mapping
from contextlib import asynccontextmanager
from typing import Any

from pydantic import RootModel, model_validator

from .tools import Tool


def _arguments(schema: dict) -> type[RootModel]:
    from jsonschema import Draft202012Validator
    from jsonschema.exceptions import ValidationError
    from referencing import Registry

    Draft202012Validator.check_schema(schema)
    # Registry() deliberately has no remote retrieval: a server's schema cannot
    # make the Hub fetch arbitrary URLs while validating model-supplied arguments.
    validator = Draft202012Validator(schema, registry=Registry())

    class Arguments(RootModel[dict[str, Any]]):
        @model_validator(mode="after")
        def validate_tool(self):
            try:
                validator.validate(self.root)
            except ValidationError as error:
                raise ValueError("Invalid MCP tool arguments") from error
            return self

        @classmethod
        def model_json_schema(cls, **kwargs):
            return schema

    return Arguments


def _name(server: str, tool: str) -> str:
    original = f"mcp_{server}_{tool}"
    name = re.sub(r"[^A-Za-z0-9_-]", "_", original)
    if name == original and len(name) <= 64:
        return name
    return name[:55] + "_" + hashlib.sha256(original.encode()).hexdigest()[:8]


class McpConnection:
    def __init__(self, name: str, client: Any):
        self.name, self.client = name, client

    async def discover(self) -> list[dict]:
        tools, cursor = [], None
        while True:
            page = await self.client.list_tools(cursor=cursor)
            for tool in page.tools:
                schema = tool.input_schema
                if len(json.dumps(schema).encode()) > 32768:
                    raise ValueError("MCP tool schema exceeds the size limit")
                tools.append(
                    {"name": tool.name, "description": tool.description or "", "schema": schema}
                )
            cursor = page.next_cursor
            if cursor is None:
                return tools

    async def tools(self, enabled: Mapping[str, bool]) -> list[Tool]:
        """Only explicit names are registered; each bool is its admin-set sensitivity."""
        tools = []
        for definition in await self.discover():
            name = definition["name"]
            if name not in enabled:
                continue

            async def execute(arguments, tool_name=name):
                result = await self.client.call_tool(tool_name, arguments.root)
                payload = result.model_dump(mode="json", by_alias=True, exclude_none=True)
                if len(json.dumps(payload).encode()) > 131072:
                    raise ValueError("MCP tool result exceeds the size limit")
                return payload

            tools.append(
                Tool(
                    name=_name(self.name, name),
                    description=definition["description"][:2048],
                    arguments=_arguments(definition["schema"]),
                    execute=execute,
                    sensitive=enabled[name],
                )
            )
        return tools


@asynccontextmanager
async def connect(
    *,
    name: str,
    transport: str,
    command: str = "",
    args: list[str] | None = None,
    url: str = "",
    key: str = "",
    environment: dict[str, str] | None = None,
) -> AsyncIterator[McpConnection]:
    from mcp import Client, StdioServerParameters, stdio_client

    if transport == "stdio":
        server = StdioServerParameters(command=command, args=args or [], env=environment or {})
        # Third-party servers may print their credentials on stderr; never forward
        # that channel into the Hub's logs. The handle is closed with the process.
        with open(os.devnull, "w", encoding="utf-8") as errors:
            async with Client(stdio_client(server, errlog=errors)) as client:
                yield McpConnection(name, client)
    else:
        import httpx2
        from mcp.client.streamable_http import streamable_http_client

        async with httpx2.AsyncClient(
            headers={"Authorization": f"Bearer {key}"} if key else {},
            timeout=httpx2.Timeout(30.0, read=60.0),
        ) as http:
            async with Client(streamable_http_client(url, http_client=http)) as client:
                yield McpConnection(name, client)
