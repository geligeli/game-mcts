"""End-to-end smoke test: connects to each configured MCP server over stdio
(like the CLI would), lists its tools, and calls one tool per server.

Run:  mcp_servers/.venv/bin/python mcp_servers/smoke_test.py
"""

import asyncio
import json
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

REPO_ROOT = Path(__file__).resolve().parents[1]
CONFIG = json.loads((REPO_ROOT / ".kimi-code/mcp.json").read_text())["mcpServers"]

# One cheap tool call per server: (tool_name, arguments).
CALLS = {
    "risk-engine": ("risk_new_game", {"num_players": 2, "seed": 1}),
    # arena_rules needs no arena running; every other arena tool does, and a
    # smoke test should not require a live tournament server.
    "arena": ("arena_rules", {}),
}


async def check_server(name: str, cfg: dict) -> bool:
    params = StdioServerParameters(
        command=cfg["command"],
        args=cfg.get("args", []),
        cwd=cfg.get("cwd"),
    )
    timeout = cfg.get("toolTimeoutMs", 60000) / 1000
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await asyncio.wait_for(session.initialize(), timeout=120)
            tools = await session.list_tools()
            tool_names = [t.name for t in tools.tools]
            print(f"[{name}] tools: {tool_names}")
            tool_name, args = CALLS[name]
            if tool_name not in tool_names:
                print(f"[{name}] FAIL: expected tool {tool_name} missing")
                return False
            result = await asyncio.wait_for(
                session.call_tool(tool_name, args), timeout=timeout
            )
            text = "\n".join(
                c.text for c in result.content if c.type == "text"
            )
            if result.isError:
                print(f"[{name}] FAIL calling {tool_name}:\n{text[:1000]}")
                return False
            print(f"[{name}] {tool_name} ->\n{text[:600]}")
            return True


async def main() -> int:
    ok = True
    for name, cfg in CONFIG.items():
        try:
            ok &= await check_server(name, cfg)
        except Exception as e:
            print(f"[{name}] FAIL: {type(e).__name__}: {e}")
            ok = False
        print("---")
    print("SMOKE TEST:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
