# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Generate real registry/codec NBT blobs from minecraft-data loginPacket.json.

Reads /tmp/opencode/login_*.json (downloaded from PrismarineJS/minecraft-data),
filters registries to the limbo-minimal set, converts NBT JSON to binary, and
emits src/protocol/codec_data.h/.cpp with byte arrays.

Kept registries: dimension_type (overworld only), worldgen/biome (plains only),
chat_type (all entries), damage_type (all entries). Vanilla entry IDs preserved,
so chunk biome palettes and JoinGame dimension indices stay consistent.

Reuse map for versions without own data dir (documented skew):
  761->1.19.4, 765->1.20.2(elements), 769->1.21.3, 770/771/772->1.21.9,
  758->1.18, 756->1.17, 736->1.16, 753/754->1.16.2 shapes, 338->1.12.2 IDs.
Usage: python3 tools/gen_codec.py   (run from repo root)
"""
import json
import os
import struct

SRC = "/tmp/opencode"
OUT_H = "src/protocol/codec_data.h"
OUT_CPP = "src/protocol/codec_data.cpp"

TAG = {"end": 0, "byte": 1, "short": 2, "int": 3, "long": 4, "float": 5,
       "double": 6, "byteArray": 7, "string": 8, "list": 9, "compound": 10,
       "intArray": 11, "longArray": 12}


def enc(node, anonymous):
    """Encode one NBT JSON node. anonymous=True omits name (root/list elements)."""
    t = node["type"]
    v = node["value"]
    if t in ("byte", "short", "int", "long", "float", "double", "string") \
            and isinstance(v, list) and len(v) == 2 and isinstance(v[0], int):
        # minecraft-data `option` convention: [present, value]; absent => omit tag.
        if not v[0]:
            return b""
        v = v[1]
        node = dict(node)
        node["value"] = v
    out = bytearray()
    out.append(TAG[t])
    if not anonymous:
        name = node.get("name", "").encode()
        out += struct.pack(">H", len(name)) + name
    if t == "compound":
        for child_name, child in v.items():
            child = dict(child)
            child["name"] = child_name
            out += enc(child, False)
        out.append(0)
    elif t == "list":
        et, items = v["type"], v["value"]
        out.append(TAG[et])
        out += struct.pack(">i", len(items))
        for it in items:
            out += enc_payload(et, it)
    elif t == "string":
        b = v.encode()
        out += struct.pack(">H", len(b)) + b
    elif t == "byte":
        out += struct.pack(">b", v)
    elif t == "short":
        out += struct.pack(">h", v)
    elif t == "int":
        out += struct.pack(">i", v)
    elif t == "long":
        out += struct.pack(">q", v)
    elif t == "float":
        out += struct.pack(">f", v)
    elif t == "double":
        out += struct.pack(">d", v)
    elif t == "byteArray":
        out += struct.pack(">i", len(v)) + bytes(v)
    elif t == "intArray":
        out += struct.pack(">i", len(v))
        for x in v:
            out += struct.pack(">i", x)
    elif t == "longArray":
        out += struct.pack(">i", len(v))
        for x in v:
            out += struct.pack(">q", x)
    else:
        raise ValueError(t)
    return bytes(out)


def enc_payload(t, node):
    """Encode list element payload (no type byte, no name)."""
    if isinstance(node, dict) and "value" not in node:
        node = {"type": "compound", "value": node}
    v = node["value"] if isinstance(node, dict) else node
    if t == "compound":
        out = bytearray()
        for child_name, child in v.items():
            child = dict(child)
            child["name"] = child_name
            out += enc(child, False)
        out.append(0)
        return bytes(out)
    if t == "string":
        b = v.encode()
        return struct.pack(">H", len(b)) + b
    if t == "int":
        return struct.pack(">i", v)
    if t == "byte":
        return struct.pack(">b", v)
    raise ValueError("list-elem " + t)


def load(fn):
    with open(os.path.join(SRC, fn)) as f:
        return json.load(f)


def reg_entry(dc, reg, entry_name):
    for e in dc["value"][reg]["value"]["value"]["value"]["value"]:
        if e["name"]["value"] == entry_name:
            return e
    raise KeyError(entry_name)


def compound_node(children):
    return {"type": "compound", "value": children}


KEEP_CHAT_DMG = True

BLOBS = {}  # name -> bytes
META = {}   # name -> (plains_id, overworld_id)


def filtered_compound(dc, with_chat_dmg):
    regs = normalize(dc)
    want = ["minecraft:dimension_type", "minecraft:worldgen/biome"]
    if with_chat_dmg:
        want += ["minecraft:chat_type", "minecraft:damage_type"]
    children = {}
    for reg in want:
        if reg not in regs:
            continue
        entries = regs[reg]
        if reg == "minecraft:worldgen/biome":
            entries = [e for e in entries if e[0] == "minecraft:plains"]
        elif reg == "minecraft:dimension_type":
            entries = [e for e in entries if e[0] == "minecraft:overworld"]
        children[reg] = {"type": "compound", "value": {
            "type": {"type": "string", "value": reg},
            "value": {"type": "list", "value": {"type": "compound", "value": [
                {"name": {"type": "string", "value": n},
                 "id": {"type": "int", "value": i},
                 "element": el} for n, i, el in entries]}},
        }}
    return compound_node(children)


def record_ids(dc, key):
    regs = normalize(dc)
    plains = next(i for n, i, e in regs["minecraft:worldgen/biome"] if n == "minecraft:plains")
    over = next(i for n, i, e in regs["minecraft:dimension_type"] if n == "minecraft:overworld")
    META[key] = (plains, over)


def normalize(dc):
    """Return {reg: [(name, id, elementNode)]} for either codec shape."""
    out = {}
    if "value" in dc:
        regs = dc["value"]
        for reg, node in regs.items():
            es = node["value"]["value"]["value"]["value"]
            out[reg] = [(e["name"]["value"], e["id"]["value"], e["element"]) for e in es]
    else:
        for reg, node in dc.items():
            if not isinstance(node, dict) or "entries" not in node:
                continue
            items = []
            for e in node["entries"]:
                items.append((e["key"], e.get("id", 0), e["value"]))
            out[reg] = items
    return out


def build():
    # --- JoinGame full codecs (anonymous root compound) ---
    jobs = [
        ("j116", "login_1.16.json", None),          # single-dimension shape, as-is
        ("j1162", "login_1.16.2.json", False),
        ("j117", "login_1.17.json", False),
        ("j118", "login_1.18.json", False),
        ("j119_759", "login_1.19.json", True),
        ("j119_760", "login_1.19.2.json", True),
        ("j119_7612", "login_1.19.4.json", True),
        ("j120_763", "login_1.20.json", True),
    ]
    for name, fn, filtered in jobs:
        d = load(fn)
        dc = d["dimensionCodec"]
        if filtered is None:
            BLOBS[name] = enc(dc, True)
        else:
            BLOBS[name] = enc(filtered_compound(dc, filtered), True)
        if filtered is not None:
            record_ids(dc, name)
            over_el = next(el for n, i, el in normalize(dc)["minecraft:dimension_type"]
                            if n == "minecraft:overworld")
            BLOBS["dim_" + name[1:]] = enc(over_el, True)
    # --- 764/765 config: one compound body ---
    for name, fn in [("c764", "login_1.20.2.json"), ("c765", "login_1.20.2.json")]:
        dc = load(fn)["dimensionCodec"]
        BLOBS[name] = enc(filtered_compound(dc, True), True)
        record_ids(dc, name)
    # --- 766+ config: one RegistryData body PER registry ---
    for name, fn in [("e766", "login_1.20.5.json"), ("e767", "login_1.21.1.json"),
                     ("e769", "login_1.21.3.json"), ("e770", "login_1.21.9.json"),
                     ("e773", "login_1.21.9.json"), ("e774", "login_1.21.11.json"),
                     ("e775", "login_26.1.json")]:
        dc = load(fn)["dimensionCodec"]
        regs = normalize(dc)
        for reg in ["minecraft:dimension_type", "minecraft:worldgen/biome",
                    "minecraft:chat_type", "minecraft:damage_type"]:
            if reg not in regs:
                continue
            entries = regs[reg]
            if reg == "minecraft:worldgen/biome":
                entries = [e for e in entries if e[0] == "minecraft:plains"]
            elif reg == "minecraft:dimension_type":
                entries = [e for e in entries if e[0] == "minecraft:overworld"]
            body = bytearray()
            rid = reg.encode()
            body += _varint(len(rid)) + rid
            body += _varint(len(entries))
            for ename, eid, element in entries:
                key = ename.encode()
                body += _varint(len(key)) + key
                body.append(1)  # present
                body += enc(element, True)
            BLOBS[f"{name}_{reg.split(':')[-1].split('/')[-1]}"] = bytes(body)
        plains = next(i for n, i, e in regs["minecraft:worldgen/biome"] if n == "minecraft:plains")
        over = next(i for n, i, e in regs["minecraft:dimension_type"] if n == "minecraft:overworld")
        META[name] = (plains, over)


def _varint(n):
    out = bytearray()
    u = n & 0xFFFFFFFF
    while True:
        b = u & 0x7F
        u >>= 7
        out.append(b | 0x80 if u else b)
        if not u:
            break
    return bytes(out)


def emit():
    build()
    # Keep the proprietary notice in generated output so a regeneration never
    # drops it. Must be the first thing written, before #pragma once / includes.
    notice = (
        "// limbo-c++ — proprietary software, all rights reserved.\n"
        "// Copyright (c) 2026 Paranthaman\n"
        "// See LICENSE. No permission is granted to copy, modify, or redistribute\n"
        "// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.\n"
        "//\n"
    )
    with open(OUT_H, "w") as f:
        f.write(notice)
        f.write("#pragma once\n// GENERATED by tools/gen_codec.py — do not hand-edit.\n"
                "// Real registry/codec NBT from minecraft-data loginPacket.json\n"
                "// (filtered: overworld + plains + chat + damage; vanilla IDs kept).\n"
                "#include <cstddef>\n#include <cstdint>\n\nnamespace limbo::codec {\n\n"
                "struct Blob { const uint8_t* data; size_t size; };\n")
        for name in BLOBS:
            f.write(f"Blob {name}();\n")
        f.write("int plainsId(const char* target);\nint overworldId(const char* target);\n"
                "}  // namespace limbo::codec\n")
    # "nbt" (named) fields — JoinGame codecs and standalone dimensions —
    # need the empty root name; "anonymousNbt" blobs stay nameless.
    for name in list(BLOBS):
        if name.startswith("j") or name.startswith("dim_"):
            BLOBS[name] = BLOBS[name][:1] + b"\x00\x00" + BLOBS[name][1:]
    with open(OUT_CPP, "w") as f:
        f.write(notice)
        f.write('#include "protocol/codec_data.h"\n#include <cstring>\n\nnamespace limbo::codec {\n')
        for name, data in BLOBS.items():
            f.write(f"\nstatic const uint8_t k_{name}[] = {{\n")
            for i in range(0, len(data), 12):
                f.write("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",\n")
            f.write(f"}};\nBlob {name}() {{ return {{k_{name}, sizeof(k_{name})}}; }}\n")
        f.write("\nint plainsId(const char* t) {\n")
        for k, (plains, over) in META.items():
            f.write(f'  if (!std::strcmp(t, "{k}")) return {plains};\n')
        f.write("  return 0;\n}\nint overworldId(const char* t) {\n")
        for k, (plains, over) in META.items():
            f.write(f'  if (!std::strcmp(t, "{k}")) return {over};\n')
        f.write("  return 0;\n}\n}  // namespace limbo::codec\n")
    total = sum(len(v) for v in BLOBS.values())
    print(f"emitted {len(BLOBS)} blobs, {total} bytes")
    for k, (plains, over) in META.items():
        print(f"  {k}: plains={plains} overworld={over}")


if __name__ == "__main__":
    emit()
