# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import argparse
import asyncio
import getpass
import json
import socket
import sys
import tempfile
from contextlib import nullcontext as _existing_data
from pathlib import Path

from . import __version__
from .core.config import Config, config_path, load_config
from .core.runtime import Runtime


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(prog="nuka", description="nuka device companion")
    root.add_argument("--version", action="version", version=__version__)
    commands = root.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init", help="Create configuration and administrator token")
    init.add_argument("--config")
    init.add_argument(
        "--section", choices=["hub", "collectors", "ai", "servers", "weather", "agents", "public"]
    )
    for name in (
        "hub",
        "agent",
        "doctor",
        "pair",
        "token",
        "secret",
        "service",
        "hooks",
        "tunnel",
        "vault",
        "ota",
        "sim-device",
    ):
        command = commands.add_parser(name)
        command.add_argument("--config")
        if name == "hub":
            command.add_argument("--demo", action="store_true")
            command.add_argument(
                "--demo-state",
                choices=["normal", "loading", "offline", "unconfigured"],
                default="normal",
            )
        elif name == "agent":
            command.add_argument("--hub")
            command.add_argument("--token")
        elif name == "sim-device":
            command.add_argument("--hub")
            command.add_argument("--token")
            command.add_argument("--count", type=int, default=1)
            command.add_argument("--duration", type=float)
            command.add_argument("--approve", choices=["allow", "deny"])
        elif name == "secret":
            command.add_argument("action", choices=["set", "get", "rm"])
            command.add_argument("name")
            command.add_argument("--file", action="store_true")
        elif name == "token":
            command.add_argument("action", choices=["create", "list", "revoke"])
            command.add_argument("identity", nargs="?")
            command.add_argument("--scope", choices=["device", "agent", "hook", "notify", "admin"])
            command.add_argument("--name", default="")
        elif name == "hooks":
            command.add_argument("action", choices=["install", "uninstall"])
            command.add_argument("source", choices=["claude", "codex", "gemini"])
            command.add_argument("--home", type=Path)
        elif name == "service":
            command.add_argument("action", choices=["install", "uninstall", "status"])
            command.add_argument("--role", choices=["hub", "agent"], default="hub")
            command.add_argument("--elevated", action="store_true")
            command.add_argument("--system", action="store_true")
        elif name == "tunnel":
            command.add_argument("action", choices=["setup"])
        elif name == "vault":
            command.add_argument("action", choices=["import", "export"])
            command.add_argument("file", type=Path)
            command.add_argument("--otpauth", action="store_true")
        elif name == "ota":
            command.add_argument("action", choices=["publish"])
            command.add_argument("file", type=Path)
            command.add_argument("--version", required=True)
            command.add_argument("--kind", choices=["app", "assets"], default="app")
            command.add_argument("--notes", default="")
    return root


def ask(prompt: str, default: str = "") -> str:
    return input(prompt + (f" [{default}]" if default else "") + ": ").strip() or default


async def initialize(args):
    path = config_path(args.config)
    if path.exists() and not args.section:
        raise ValueError("Configuration exists; use --section to edit a section")
    path.parent.mkdir(parents=True, exist_ok=True)
    if args.section:
        from .setup import configure_section

        text = await configure_section(path.read_text(encoding="utf-8"), args.section, path)
    else:
        name = ask("Hub name", "nuka-hub")
        language = ask("Language (zh-CN/en)", "zh-CN")
        port = int(ask("Port", "8790"))
        with socket.socket() as probe:
            probe.bind(("", port))
        text = (
            "# SPDX-License-Identifier: AGPL-3.0-or-later\n[hub]\n"
            + f"name = {json.dumps(name)}\nlang = {json.dumps(language)}\nport = {port}\n"
        )
        for section in ("collectors", "ai", "servers", "weather", "agents", "public"):
            if ask("Configure " + section + " now? (y/N)", "n").lower() == "y":
                from .setup import configure_section

                text = await configure_section(text, section, path)
    from .core.config import parse_config

    config = parse_config(text)
    if path.exists():
        import shutil
        import time

        shutil.copy2(path, str(path) + ".bak." + str(time.time_ns()))
    path.write_text(text, encoding="utf-8", newline="\n")
    runtime = Runtime(config, path)
    try:
        if not args.section:
            token = await runtime.tokens.create("admin", "administrator")
            print("Administrator token (shown once): " + token["token"])
        print("Configuration written. Run nuka doctor; see docs/guides/02-install-hub-*.md")
        if not args.section and ask("Install login service? (y/N)", "n").lower() == "y":
            from .service import service

            service(
                argparse.Namespace(role="hub", action="install", elevated=False, system=False), path
            )
    finally:
        await runtime.stop()


async def run(args):
    if args.command == "init":
        await initialize(args)
        return
    path = config_path(args.config)
    if args.command == "sim-device" or (args.command == "hub" and args.demo):
        config = load_config(path) if path.exists() else Config()
    else:
        config = load_config(path)
    if args.command == "hub":
        import uvicorn

        from .api.app import create_app

        with (
            tempfile.TemporaryDirectory(prefix="nuka-demo-")
            if args.demo and not path.exists()
            else _existing_data() as demo_dir
        ):
            if demo_dir:
                config.hub.data_dir = demo_dir
                config.mdns.enabled = False
            runtime = Runtime(config, path, args.demo, args.demo_state)
            if args.demo:
                device = await runtime.tokens.create("device", "simulator")
                admin = await runtime.tokens.create("admin", "demo administrator")
                print("Demo device token: " + device["token"], file=sys.stderr)
                print("Demo administrator token: " + admin["token"], file=sys.stderr)
            server = uvicorn.Server(
                uvicorn.Config(
                    create_app(runtime),
                    host=config.hub.host,
                    port=config.hub.port,
                    log_level=config.hub.log_level,
                    proxy_headers=False,
                    access_log=False,
                )
            )
            await server.serve()
        return
    if args.command == "sim-device":
        from .sim import run_simulators

        await run_simulators(config, args)
        return
    if args.command == "agent":
        from .agent import run_agent

        await run_agent(config, args, path)
        return
    if args.command == "secret":
        from .core.secrets import Secrets

        store = Secrets(path.parent)
        if args.action == "set":
            await asyncio.to_thread(
                store.set, args.name, getpass.getpass("Secret: "), file=args.file
            )
        elif args.action == "get":
            print(await asyncio.to_thread(store.get, args.name))
        else:
            await asyncio.to_thread(store.remove, args.name)
        return
    if args.command == "hooks":
        from .hooks import hooks

        hooks(args.action, args.source, config, args.home)
        return
    if args.command == "service":
        from .service import service

        service(args, path)
        return
    if args.command == "vault":
        from .usb import vault

        await vault(args)
        return
    if args.command == "pair":
        from .usb import pair

        await pair(config, path)
        return
    if args.command == "tunnel":
        from .setup import tunnel

        tunnel(config, path)
        return
    runtime = Runtime(config, path)
    try:
        if args.command == "token":
            if args.action == "create":
                if args.scope is None:
                    raise ValueError("token create requires --scope")
                print(json.dumps(await runtime.tokens.create(args.scope, args.name)))
            elif args.action == "list":
                print(json.dumps(await runtime.tokens.listing()))
            else:
                await runtime.tokens.revoke(args.identity)
        elif args.command == "doctor":
            from .doctor import doctor

            await doctor(runtime)
        elif args.command == "ota":
            from .ota import publish

            await publish(runtime, args)
    finally:
        await runtime.stop()


def main():
    args = parser().parse_args()
    try:
        asyncio.run(run(args))
    except (ValueError, OSError) as error:
        print(f"{type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
