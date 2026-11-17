# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import base64
import getpass
import hashlib
import json
import secrets
import struct

import serial
from serial.tools import list_ports

from .core.runtime import Runtime
from .core.usb_models import (
    CdcEnvelope,
    CdcError,
    DeviceInfo,
    ImportResult,
    OtpauthImport,
    Provisioning,
    VaultChunk,
)
from .core.wire import contains_nul


def device_port() -> str:
    ports = [port for port in list_ports.comports() if port.vid == 0x303A and port.pid == 0x4001]
    if len(ports) != 1:
        raise ValueError("Connect exactly one nuka-esp32 USB device; see the pairing guide")
    return ports[0].device


class CDC:
    def __init__(self):
        self.serial = serial.Serial(device_port(), 115200, timeout=65)
        self.request_id = 0

    async def send(self, kind: str, data: dict) -> int:
        self.request_id += 1
        frame = {"t": kind, "id": self.request_id, "d": data}
        if contains_nul(frame):
            raise ValueError("CDC strings and object keys must not contain NUL")
        raw = (
            json.dumps(
                frame,
                separators=(",", ":"),
                ensure_ascii=False,
            )
            + "\n"
        ).encode()
        if len(raw) > 8192:
            raise ValueError("CDC request exceeds 8192 bytes")
        await asyncio.to_thread(self.serial.write, raw)
        return self.request_id

    async def receive(self, request_id: int):
        while True:
            raw = await asyncio.to_thread(self.serial.read_until, b"\n", 8193)
            if not raw:
                raise TimeoutError("Device confirmation timed out")
            if len(raw) > 8192 or not raw.endswith(b"\n"):
                raise ValueError("Invalid CDC frame length")
            frame = CdcEnvelope.model_validate_json(raw)
            if frame.id != request_id:
                continue
            if frame.t == "err":
                error = CdcError.model_validate(frame.d)
                raise ValueError("Device rejected request: " + error.code)
            if frame.t == "vault.chunk":
                frame.d = VaultChunk.model_validate(frame.d).wire()
            return frame.wire()

    async def request(self, kind: str, data: dict):
        request_id = await self.send(kind, data)
        response = await self.receive(request_id)
        if response["t"] != "ok":
            raise ValueError("Expected CDC acknowledgement")
        if kind == "info":
            return DeviceInfo.model_validate(response["d"]).wire()
        if kind in {"vault.import.end", "vault.import.otpauth"}:
            return ImportResult.model_validate(response["d"]).wire()
        return response["d"]

    def close(self):
        self.serial.close()


async def pair(config, path):
    cdc = CDC()
    runtime = Runtime(config, path)
    token = None
    try:
        info = await cdc.request("info", {})
        networks = []
        while len(networks) < 3:
            ssid = input("Wi-Fi SSID: ")
            password = getpass.getpass("Wi-Fi password: ")
            networks.append({"ssid": ssid, "pass": password})
            if input("Add another Wi-Fi network? (y/N): ").lower() != "y":
                break
        hubs = [value.strip() for value in input("Hub WS URLs (comma separated): ").split(",")]
        token = await runtime.tokens.create("device", info["dev_id"])
        name = input("Device name: ").strip() or info["dev_id"]
        code = str(secrets.randbelow(1000000)).zfill(6)
        data = {
            "wifi": networks,
            "hubs": hubs,
            "hub_id": runtime.hub_id,
            "hub_name": config.hub.name,
            "dev_token": token["token"],
            "code": code,
        }
        if input("Configure Cloudflare Access service token? (y/N): ").lower() == "y":
            data["cf_access"] = {
                "origin": input("Access HTTPS origin: ").strip(),
                "client_id": getpass.getpass("Access client ID: "),
                "client_secret": getpass.getpass("Access client secret: "),
            }
        data = Provisioning.model_validate(data).wire()
        print("Confirm the Hub and code on the device: " + code)
        await cdc.request("prov.set", data)
        await runtime.history.sql(
            "INSERT OR REPLACE INTO devices VALUES (?,?,?,?,?,?)",
            (info["dev_id"], name, info["fw"], None, None, token["id"]),
        )
        await runtime.history.audit("pair", "usb", {"dev_id": info["dev_id"]})
        print("Provisioning accepted. Waiting for device connection…")
        for _ in range(60):
            await asyncio.sleep(1)
            rows = await runtime.history.sql(
                "SELECT last_seen FROM devices WHERE dev_id=?", (info["dev_id"],)
            )
            if rows[0]["last_seen"]:
                print("Device connected successfully.")
                return
        raise TimeoutError("Device accepted provisioning but has not connected; run nuka doctor")
    except (ValueError, TimeoutError, serial.SerialException):
        if token:
            await runtime.tokens.revoke(token["id"])
        raise
    finally:
        cdc.close()
        await runtime.stop()


def derive(passphrase: bytearray, salt: bytes) -> bytearray:
    return bytearray(
        hashlib.scrypt(passphrase, salt=salt, n=32768, r=8, p=1, dklen=32, maxmem=64 * 1024 * 1024)
    )


async def vault(args):
    cdc = CDC()
    password, key = bytearray(), bytearray()
    try:
        info = await cdc.request("info", {})
        if not info["unlocked"] or not info["usb_admin"]:
            raise ValueError(
                "Unlock Vault and enable its two-minute USB management mode on the device"
            )
        if args.action == "import" and args.otpauth:
            uris = [
                line.strip()
                for line in args.file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            ]
            data = OtpauthImport.model_validate({"uris": uris}).wire()
            result = await cdc.request("vault.import.otpauth", data)
            print(json.dumps(result))
            return
        if args.action == "import":
            encrypted = args.file.read_bytes()
            if (
                len(encrypted) < 55
                or encrypted[:4] != b"NVB1"
                or encrypted[4:11] != struct.pack("<BIBB", 1, 32768, 8, 1)
            ):
                raise ValueError("Invalid NVB1 v1 backup header")
            salt = encrypted[11:27]
        else:
            salt = secrets.token_bytes(16)
        password = bytearray(getpass.getpass("Backup passphrase: ").encode("utf-8"))
        key = await asyncio.to_thread(derive, password, salt)
        password[:] = bytes(len(password))
        if args.action == "export":
            request_id = await cdc.send(
                "vault.export",
                {
                    "key_b64": base64.b64encode(key).decode(),
                    "salt_b64": base64.b64encode(salt).decode(),
                },
            )
            chunks = []
            sequence, last = 0, False
            while True:
                frame = await cdc.receive(request_id)
                if frame["t"] == "ok":
                    if not last:
                        raise ValueError("Export ended before final chunk")
                    break
                data = frame["d"]
                if frame["t"] != "vault.chunk" or last or data["seq"] != sequence:
                    raise ValueError("Invalid export chunk order")
                chunks.append(base64.b64decode(data["b64"], validate=True))
                sequence += 1
                last = data["last"]
            args.file.write_bytes(b"".join(chunks))
            print("Encrypted backup written.")
        else:
            await cdc.request(
                "vault.import.begin",
                {"key_b64": base64.b64encode(key).decode(), "size": len(encrypted)},
            )
            for sequence, offset in enumerate(range(0, len(encrypted), 4096)):
                await cdc.request(
                    "vault.import.chunk",
                    {
                        "seq": sequence,
                        "b64": base64.b64encode(encrypted[offset : offset + 4096]).decode(),
                    },
                )
            result = await cdc.request("vault.import.end", {})
            print(json.dumps(result))
    finally:
        password[:] = bytes(len(password))
        key[:] = bytes(len(key))
        cdc.close()
