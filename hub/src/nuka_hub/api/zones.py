# SPDX-License-Identifier: AGPL-3.0-or-later
from ipaddress import ip_address, ip_network


def zone_for(address: str, headers, config) -> str:
    source = ip_address(address)
    public_headers = any(header.lower() in headers for header in config.public_detect_headers)
    if source.is_loopback and not public_headers:
        return "local"
    if source.is_loopback and public_headers:
        return "public"
    if (
        any(source in ip_network(network) for network in config.trusted_proxies)
        and "x-forwarded-for" in headers
    ):
        source = ip_address(headers["x-forwarded-for"].split(",")[0].strip())
    return "lan" if any(source in ip_network(network) for network in config.lan_cidrs) else "public"


def remote_management_path(path: str) -> bool:
    return path in {"/config", "/assistant"} or path.startswith(
        ("/api/v1/config", "/api/v1/secrets", "/api/v1/assistant/")
    )


def permitted(path: str, method: str, zone: str, config, *, remote_verified: bool = False) -> bool:
    if path.startswith("/hook/"):
        return zone == "local" or (zone == "lan" and config.allow_lan_hooks)
    if path.startswith("/api/v1/perm/") or path == "/approve":
        return zone != "public" and config.web_approve_permissions
    if remote_management_path(path):
        return zone != "public" or (
            config.remote_admin and config.cf_access.enabled and remote_verified
        )
    sensitive = path.startswith(("/api/v1/pair/", "/api/v1/usb/")) or path == "/devices/pair"
    return not sensitive or zone != "public"
