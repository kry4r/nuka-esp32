# SPDX-License-Identifier: AGPL-3.0-or-later
from importlib.resources import files
from pathlib import Path
from time import time
from zoneinfo import TZPATH, ZoneInfo

from tzlocal import get_localzone_name


def device_time(tz_posix: str) -> dict:
    epoch = int(time())
    if not 946684800 <= epoch <= 4102444799:
        raise ValueError("System clock must be between 2000 and 2099 for device synchronization")
    return {"epoch": epoch, "tz_posix": tz_posix}


def timezone_name(value: str) -> str:
    return get_localzone_name() if value == "auto" else value


def posix_timezone(value: str) -> str:
    name = timezone_name(value)
    ZoneInfo(name)
    system = next(
        (Path(folder) / name for folder in TZPATH if (Path(folder) / name).is_file()), None
    )
    raw = (
        system.read_bytes()
        if system
        else files("tzdata.zoneinfo").joinpath(*name.split("/")).read_bytes()
    )
    result = raw.rsplit(b"\n", 2)[-2].decode("ascii")
    if not result:
        raise ValueError(f"timezone {name} has no POSIX TZ rule")
    return result
