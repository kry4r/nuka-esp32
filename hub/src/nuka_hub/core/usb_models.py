# SPDX-License-Identifier: AGPL-3.0-or-later
from typing import Any, Literal

from pydantic import AnyWebsocketUrl, Field, ValidationInfo, field_validator, model_validator

from .models import WireModel
from .wire import contains_nul


class CdcEnvelope(WireModel):
    t: str
    id: int | None = None
    d: dict[str, Any]

    @model_validator(mode="before")
    @classmethod
    def no_nul(cls, value):
        if contains_nul(value):
            raise ValueError("CDC strings and object keys must not contain NUL")
        return value


class CdcError(WireModel):
    code: Literal[
        "bad_request",
        "rate_limited",
        "perm_gone",
        "not_found",
        "forbidden",
        "internal",
        "rejected",
        "timeout",
    ]
    msg: str


class DeviceInfo(WireModel):
    dev_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    fw: str
    nukapack: str
    proto: Literal[1]
    paired: bool
    hub_id: str | None = Field(default=None, pattern="^[a-z0-9_-]{1,32}$")
    mac: str = Field(pattern="^(?:[a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}$")
    unlocked: bool
    usb_admin: bool

    @model_validator(mode="after")
    def pairing(self):
        if self.paired and self.hub_id is None:
            raise ValueError("paired info requires hub_id")
        return self


class VaultChunk(WireModel):
    seq: int = Field(ge=0)
    b64: str = Field(pattern="^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$")
    last: bool


class ImportResult(WireModel):
    added: int = Field(ge=0)
    skipped: int = Field(ge=0)


class Wifi(WireModel):
    ssid: str
    password: str = Field(alias="pass")

    @field_validator("ssid", "password")
    @classmethod
    def wifi_text(cls, value: str, info: ValidationInfo) -> str:
        minimum, maximum = (1, 32) if info.field_name == "ssid" else (0, 63)
        if "\0" in value or not minimum <= len(value.encode("utf-8")) <= maximum:
            raise ValueError(f"Expected {minimum}–{maximum} UTF-8 bytes without NUL")
        return value


class CfCredentials(WireModel):
    origin: str = Field(pattern="^https://[a-zA-Z0-9.-]+(?::[0-9]+)?$")
    client_id: str
    client_secret: str


class Provisioning(WireModel):
    wifi: list[Wifi] = Field(min_length=1, max_length=3)
    hubs: list[AnyWebsocketUrl] = Field(min_length=1)
    hub_id: str = Field(pattern="^[a-z0-9_-]{1,32}$")
    hub_name: str
    dev_token: str
    code: str = Field(pattern="^[0-9]{6}$")
    cf_access: CfCredentials | None = None

    @model_validator(mode="after")
    def hub_urls(self):
        if any(not str(url).isascii() or len(str(url)) > 511 for url in self.hubs):
            raise ValueError("Hub WS URL must be at most 511 ASCII bytes after normalization")
        return self


class OtpauthImport(WireModel):
    uris: list[str] = Field(min_length=1)

    @model_validator(mode="after")
    def totp_uris(self):
        if any(not uri.startswith("otpauth://totp/") for uri in self.uris):
            raise ValueError("Expected otpauth://totp/ URIs")
        return self
