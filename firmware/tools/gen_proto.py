# SPDX-License-Identifier: AGPL-3.0-or-later
"""Compile the frozen WS device schemas into C boundary checks."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "protocol/schemas"
nodes = []
functions = []


def quote(value):
    return json.dumps(value, ensure_ascii=True)


def compile_node(schema, file):
    if "$ref" in schema:
        name, _, fragment = schema["$ref"].partition("#")
        target = file.parent / name if name else file
        value = json.loads(target.read_text(encoding="utf-8"))
        for part in fragment.removeprefix("/").split("/") if fragment else []:
            value = value[part]
        return compile_node(value, target)
    key = (json.dumps(schema, sort_keys=True), file)
    if key in nodes:
        return nodes.index(key)
    number = len(nodes)
    nodes.append(key)
    functions.append("")
    lines = [f"static bool check_{number}(const cJSON *v)", "{", "    (void)v;"]
    types = schema.get("type")
    if types:
        types = types if isinstance(types, list) else [types]
        checks = {"object": "cJSON_IsObject(v)", "array": "cJSON_IsArray(v)",
                  "string": "cJSON_IsString(v)", "boolean": "cJSON_IsBool(v)",
                  "integer": "integer(v)", "number": "(cJSON_IsNumber(v) && isfinite(v->valuedouble))", "null": "cJSON_IsNull(v)"}
        lines.append("    if (!(" + " || ".join(checks[t] for t in types) + ")) return false;")
    if "const" in schema:
        lines.append(f"    if (!equal_literal(v, {quote(json.dumps(schema['const']))})) return false;")
    if "enum" in schema:
        lines.append("    if (!(" + " || ".join(f"equal_literal(v, {quote(json.dumps(x))})" for x in schema["enum"]) + ")) return false;")
    for name, op in (("minimum", "<"), ("maximum", ">")):
        if name in schema:
            lines.append(f"    if (v->valuedouble {op} {schema[name]}) return false;")
    if "multipleOf" in schema:
        divisor = schema["multipleOf"]
        lines.append(f"    if (fabs(v->valuedouble / {divisor} - round(v->valuedouble / {divisor})) > 0.000001) return false;")
    if "maxLength" in schema:
        lines.append(f"    if (utf8_length(v->valuestring) > {schema['maxLength']}) return false;")
    if "pattern" in schema:
        lines.append(f"    if (!matches(v->valuestring, {quote(schema['pattern'])})) return false;")
    if schema.get("format") == "date":
        lines.append("    if (!date_valid(v->valuestring)) return false;")
    for name in schema.get("required", []):
        lines.append(f"    if (!cJSON_GetObjectItemCaseSensitive(v, {quote(name)})) return false;")
    for name, child in schema.get("properties", {}).items():
        index = compile_node(child, file)
        lines.append(f"    {{ const cJSON *p = cJSON_GetObjectItemCaseSensitive(v, {quote(name)}); if (p && !check_{index}(p)) return false; }}")
    if "items" in schema:
        index = compile_node(schema["items"], file)
        lines.append(f"    {{ const cJSON *p; cJSON_ArrayForEach(p, v) if (!check_{index}(p)) return false; }}")
    if "maxItems" in schema:
        lines.append(f"    if (cJSON_GetArraySize(v) > {schema['maxItems']}) return false;")
    if schema.get("uniqueItems"):
        lines.append("    { const cJSON *a, *b; cJSON_ArrayForEach(a, v) for (b = a->next; b; b = b->next) if (cJSON_Compare(a, b, true)) return false; }")
    for name, other in schema.get("dependentRequired", {}).items():
        cond = " || ".join(f"!cJSON_GetObjectItemCaseSensitive(v, {quote(x)})" for x in other)
        lines.append(f"    if (cJSON_GetObjectItemCaseSensitive(v, {quote(name)}) && ({cond})) return false;")
    for kind in ("allOf", "anyOf", "oneOf"):
        if kind in schema:
            calls = [f"check_{compile_node(x, file)}(v)" for x in schema[kind]]
            expression = (" && " if kind == "allOf" else " || ").join(calls)
            if kind == "oneOf":
                expression = "(" + " + ".join(f"(int){x}" for x in calls) + ") == 1"
            lines.append(f"    if (!({expression})) return false;")
    if "if" in schema:
        condition = compile_node(schema["if"], file)
        result = compile_node(schema["then"], file)
        lines.append(f"    if (check_{condition}(v) && !check_{result}(v)) return false;")
    if "not" in schema:
        lines.append(f"    if (check_{compile_node(schema['not'], file)}(v)) return false;")
    lines.extend(("    return true;", "}"))
    functions[number] = "\n".join(lines)
    return number


frame = json.loads((BASE / "ws-device.json").read_text(encoding="utf-8"))
dispatch = []
for case in frame["oneOf"]:
    name = case["properties"]["t"]["const"]
    index = compile_node(case["properties"]["d"], BASE / "ws-device.json")
    request = ' && integer(cJSON_GetObjectItemCaseSensitive(root, "id"))' if "id" in case["required"] else ""
    dispatch.append(f'    if (strcmp(type, "{name}") == 0) return check_{index}(data){request};')
output = "// SPDX-License-Identifier: AGPL-3.0-or-later\n// Generated by firmware/tools/gen_proto.py; unknown fields are ignored.\n"
output += "\n".join(f"static bool check_{n}(const cJSON *v);" for n in range(len(nodes))) + "\n"
output += "\n".join(functions)
output += '\nstatic bool validate_data(const char *type, const cJSON *data, const cJSON *root)\n{\n' + "\n".join(dispatch) + '\n    return true;\n}\n'
(ROOT / "firmware/components/svc/proto_checks.inc").write_text(output, encoding="utf-8")
print(f"Generated {len(nodes)} device schema validators")
