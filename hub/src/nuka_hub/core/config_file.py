# SPDX-License-Identifier: AGPL-3.0-or-later
from collections.abc import MutableMapping, MutableSequence

import tomlkit
from tomlkit.exceptions import ConvertError, ParseError

from .config import ConfigError


def update_document(target: MutableMapping, values: dict) -> None:
    for key in list(target):
        if key not in values:
            del target[key]
    for key, value in values.items():
        previous = target.get(key)
        if previous == value:
            continue
        if isinstance(value, dict) and isinstance(previous, MutableMapping):
            update_document(previous, value)
        elif (
            isinstance(value, list)
            and value
            and all(isinstance(item, dict) for item in value)
            and isinstance(previous, MutableSequence)
        ):
            while len(previous) > len(value):
                del previous[-1]
            for index, item in enumerate(value):
                if index < len(previous):
                    update_document(previous[index], item)
                else:
                    previous.append(item)
        else:
            target[key] = value


def structured_toml(original: str, values: dict) -> str:
    try:
        document = tomlkit.parse(original)
    except ParseError as error:
        raise ConfigError([{"line": error.line, "msg": str(error)}]) from error
    try:
        update_document(document, values)
    except ConvertError as error:
        raise ConfigError(
            [{"line": 1, "msg": "Configuration contains a value TOML cannot represent"}]
        ) from error
    return tomlkit.dumps(document)
