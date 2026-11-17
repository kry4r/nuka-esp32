# SPDX-License-Identifier: AGPL-3.0-or-later
import asyncio
import ipaddress
import socket

import psutil
from zeroconf import IPVersion, ServiceInfo
from zeroconf.asyncio import AsyncZeroconf

from . import __version__


def addresses(host: str) -> list[str]:
    if host not in {"0.0.0.0", "::"}:
        return [host]
    up = {name for name, stats in psutil.net_if_stats().items() if stats.isup}
    # Link-local addresses (e.g. APIPA on adapters without DHCP) are unreachable for devices.
    return list(
        {
            entry.address.split("%", 1)[0]
            for name, values in psutil.net_if_addrs().items()
            if name in up
            for entry in values
            if entry.family in {socket.AF_INET, socket.AF_INET6}
            and not (address := ipaddress.ip_address(entry.address.split("%", 1)[0])).is_loopback
            and not address.is_link_local
        }
    )


async def advertise(runtime):
    info = ServiceInfo(
        "_nuka._tcp.local.",
        runtime.config.hub.name + "._nuka._tcp.local.",
        port=runtime.config.hub.port,
        parsed_addresses=await asyncio.to_thread(addresses, runtime.config.hub.host),
        server=runtime.hub_id + ".local.",
        properties={
            "id": runtime.hub_id,
            "ver": __version__,
            "proto": "1",
            "tls": "0",
            "ws": "/ws/device",
        },
    )
    zeroconf = AsyncZeroconf(ip_version=IPVersion.All)
    try:
        await zeroconf.async_register_service(info)
        await asyncio.Future()
    finally:
        await zeroconf.async_unregister_service(info)
        await zeroconf.async_close()
