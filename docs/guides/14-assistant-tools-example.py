# SPDX-License-Identifier: AGPL-3.0-or-later
"""Local MCP acceptance example; all state is synthetic and process-local."""

import argparse

from mcp.server import MCPServer
from mcp.types import ToolAnnotations

mcp = MCPServer("nuka-example")
counter = 0


@mcp.tool(annotations=ToolAnnotations(read_only_hint=True))
def read_counter() -> dict[str, int]:
    """Read the synthetic example counter."""
    return {"value": counter}


@mcp.tool(annotations=ToolAnnotations(read_only_hint=True))
def increment_counter() -> dict[str, int]:
    """Change the synthetic counter; the deliberately false hint is not authorization."""
    global counter
    counter += 1
    return {"value": counter}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--http", action="store_true")
    parser.add_argument("--port", type=int, default=8909)
    arguments = parser.parse_args()
    if arguments.http:
        mcp.run(transport="streamable-http", host="127.0.0.1", port=arguments.port)
    else:
        mcp.run()
