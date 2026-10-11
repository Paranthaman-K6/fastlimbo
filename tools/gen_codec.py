# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Generate real registry/codec NBT blobs from minecraft-data loginPacket.json.

Reads /tmp/opencode/login_*.json (downloaded from PrismarineJS/minecraft-data),
selects registries per the rules below, converts NBT JSON to binary, and
emits src/protocol/codec_data.h/.cpp with byte arrays.

Registry selection (R/registry-complete): a real 26.2 vanilla client rejects a
filtered subset with `Registry must be non-empty: minecraft:painting_variant /
minecraft:wolf_variant`, so EVERY registry present in the source is emitted
(JoinGame compounds j*, 764/765 compound c*, one body per registry for 766+)
with at least its FIRST vanilla entry; vanilla entry ids are preserved.
Exceptions: dimension_type keeps ONLY minecraft:overworld and worldgen/biome
keeps ONLY minecraft:plains (chunk biome palettes and the JoinGame dimension
index depend on those vanilla ids — baked constants, unchanged); chat_type and
damage_type keep ALL entries (vanilla Login-acceptance needs the full damage
list); registries EMPTY in the source are skipped (1.19.4 trim_pattern/material).

Reuse map for versions without own data dir (documented skew):
  761->1.19.4, 765->1.20.2(elements), 769->1.21.3, 770/771/772->1.21.9,
  758->1.18, 756->1.17, 736->1.16, 753/754->1.16.2 shapes, 338->1.12.2 IDs.
Usage: python3 tools/gen_codec.py   (run from repo root)
"""
import copy
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


def is_typed(node):
    """True for an NBT JSON node {type, value[, name]}; False for a raw
    compound whose FIELD NAMES include 'type'/'value' (e.g. the
    minecraft:timeline keyframes {ticks, value} in 1.21.11+ sources, or the
    enchantment radius {min, max, type, value})."""
    return (isinstance(node, dict) and "type" in node and "value" in node
            and isinstance(node["type"], str) and node["type"] in TAG
            and set(node) <= {"type", "value", "name"})


def enc_payload(t, node):
    """Encode list element payload (no type byte, no name)."""
    if t == "compound":
        # Raw compound = the element's own field map (already nameless).
        fields = node["value"] if is_typed(node) else node
        out = bytearray()
        for child_name, child in fields.items():
            child = dict(child)
            child["name"] = child_name
            out += enc(child, False)
        out.append(0)
        return bytes(out)
    v = node["value"] if is_typed(node) else node
    if t == "string":
        b = v.encode()
        return struct.pack(">H", len(b)) + b
    if t == "int":
        return struct.pack(">i", v)
    if t == "byte":
        return struct.pack(">b", v)
    if t == "short":
        return struct.pack(">h", v)
    if t == "long":
        return struct.pack(">q", v)
    if t == "float":
        return struct.pack(">f", v)
    if t == "double":
        return struct.pack(">d", v)
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


BLOBS = {}     # name -> bytes
META = {}      # name -> (plains_id, overworld_id)
TARGETS = {}   # eNNN -> [(registry, blob_name)] in source (vanilla) order


def select_entries(reg, entries):
    """Entries emitted for one registry (entries = [(name, id, element)])."""
    if reg == "minecraft:dimension_type":
        return [e for e in entries if e[0] == "minecraft:overworld"]
    if reg == "minecraft:worldgen/biome":
        return [e for e in entries if e[0] == "minecraft:plains"]
    # Every other registry ships its FULL vanilla member list. Vanilla 26.x
    # freeze-validates required members ("Missing element <reg>/<name>") —
    # first-entry-only subsets (trim_material/redstone, jukebox songs,
    # instruments, ...) are rejected at Finish. Only dimension_type (JoinGame
    # dimension index) and biome (chunk palette id) stay filtered.
    return entries


def filtered_compound(dc):
    """Anonymous-root codec compound with EVERY non-empty source registry."""
    regs = normalize(dc)
    children = {}
    for reg, entries in regs.items():
        entries = select_entries(reg, entries)
        if not entries:
            continue  # empty in source -> skip (1.19.4 trim_pattern/material)
        children[reg] = {"type": "compound", "value": {
            "type": {"type": "string", "value": reg},
            "value": {"type": "list", "value": {"type": "compound", "value": [
                {"name": {"type": "string", "value": n},
                 "id": {"type": "int", "value": i},
                 "element": el} for n, i, el in entries]}},
        }}
    return compound_node(children)


def isc(x):
    """True if x is an NBT compound (typed node or raw field map)."""
    if not isinstance(x, dict):
        return False
    if set(x) <= {"type", "value", "name"} and isinstance(x.get("type"), str):
        return x["type"] == "compound"
    return True  # raw field map (e.g. list elements like keyframes)


def cfields(x):
    """Field map of a compound (typed node or raw), else None."""
    if not isinstance(x, dict):
        return None
    if set(x) <= {"type", "value", "name"} and isinstance(x.get("type"), str):
        v = x.get("value")
        return v if x["type"] == "compound" and isinstance(v, dict) else None
    return x


def sval(x):
    """Plain value of a scalar node, else None."""
    if isinstance(x, dict) and set(x) <= {"type", "value", "name"}:
        v = x.get("value")
        return v if not isinstance(v, (dict, list)) else None
    return x if isinstance(x, str) else None


def is_list_node(x):
    return isinstance(x, dict) and x.get("type") == "list"


def raw_items(lst):
    """Element list of a list node (raw maps or typed nodes)."""
    v = lst.get("value", [])
    items = v.get("value", []) if isinstance(v, dict) else v
    return items if isinstance(items, list) else []


def upd_entity_pred(p):
    """26.2 entity-predicate map form (Via RegistryDataRewriter26_2)."""
    F = cfields(p)
    if F is None:
        return
    ts = F.pop("type_specific", None)
    if ts is not None:
        upd_type_specific(F, ts)
    t = F.pop("type", None)
    if t is not None:
        F["entity_type"] = t
    for k in ("vehicle", "passenger", "targeted_entity"):
        if k in F and isc(F[k]):
            upd_entity_pred(F[k])


def upd_type_specific(F, ts):
    TF = cfields(ts)
    if TF is None:
        return
    tnode = TF.pop("type", None)
    if tnode is None:
        return
    t = sval(tnode)
    stripped = t.split(":")[-1] if isinstance(t, str) else ""
    if stripped in ("player", "lightning", "fishing_hook", "raider", "sheep"):
        F["type_specific/" + stripped] = ts
    elif stripped == "slime":
        F["type_specific/cube_mob"] = ts
    # else: dropped (Via has no default branch)


def upd_term(t):
    """Base walk (inverted/terms recursion) + 26.2 top-level entity/damage
    handling. Mirrors base RegistryDataRewriter.updateEnchantmentTerm with
    the RegistryDataRewriter26_2 override applied after super."""
    F = cfields(t)
    if F is None:
        return
    cond = sval(F.get("condition", F.get("type")))
    base = cond.split(":")[-1] if isinstance(cond, str) else ""
    if base == "inverted" and "term" in F and isc(F["term"]):
        upd_term(F["term"])
    elif "terms" in F and is_list_node(F["terms"]):
        for sub in raw_items(F["terms"]):
            upd_term(sub)
    # 26.2 override reads "condition" only (no "type" fallback).
    cond26 = sval(F.get("condition"))
    base26 = cond26.split(":")[-1] if isinstance(cond26, str) else ""
    if base26 == "entity_properties" and "predicate" in F and isc(F["predicate"]):
        upd_entity_pred(F["predicate"])
    elif base26 == "damage_source_properties":
        for k in ("source_entity", "direct_entity"):
            if k in F and isc(F[k]):
                upd_entity_pred(F[k])


def upd_nested_effect(n):
    F = cfields(n)
    if F is None:
        return
    # Effect rewrites (attribute/particle/sound/block mappings) are identity
    # for our 26.1-ids-to-26.2 data path: no renames involved, and 26_2
    # registers no effect rewriters/removals. Only requirements are walked.
    req = F.get("requirements")
    if req is not None:
        upd_term(req)


def upd_enchantment(el):
    """Apply the 26.2 enchantment walk to one entry element."""
    F = cfields(el)
    if F is None:
        return el
    effs = F.get("effects")
    EF = cfields(effs)
    if EF is None:
        return el
    for _, node in EF.items():
        if isc(node):
            upd_nested_effect(node)
        elif is_list_node(node):
            for sub in raw_items(node):
                upd_nested_effect(sub)
    return el


def to26_2(reg, name, el):
    """26.1 -> 26.2 element upgrade. Mirrors ViaVersion
    Protocol1_21_11To26_1 registry handlers line-for-line; only registries
    whose 26.2 codec changed shape are touched, everything else passes
    through byte-identical. Operates on a deep copy; source order kept."""
    el = copy.deepcopy(el)
    if not (isinstance(el, dict) and el.get("type") == "compound"):
        return el
    F = el["value"]  # field map
    def get(k):
        n = F.get(k)
        return n["value"] if isinstance(n, dict) and "value" in n else n
    def put_str(k, v):
        F[k] = {"type": "string", "value": v}
    if reg == "minecraft:cat_variant":
        # addEntityNamePrefix("cat") + addBabyAssetId
        new = get("asset_id").replace("cat/", "cat/cat_")
        F["asset_id"] = {"type": "string", "value": new}
        put_str("baby_asset_id", new + "_baby")
    elif reg in ("minecraft:chicken_variant", "minecraft:cow_variant",
                 "minecraft:pig_variant", "minecraft:frog_variant"):
        # swapEntityNameAffix(affix); chicken/cow/pig also get baby_asset_id
        affix = reg.split(":")[1].split("_")[0]
        a = get("asset_id")
        if isinstance(a, str) and a.endswith("_" + affix):
            a = a[:-(len(affix) + 1)].replace(affix + "/", affix + "/" + affix + "_")
            F["asset_id"] = {"type": "string", "value": a}
        if reg != "minecraft:frog_variant":
            put_str("baby_asset_id", get("asset_id") + "_baby")
    elif reg == "minecraft:wolf_variant":
        # baby_assets: every asset id + "_baby"
        assets = F.get("assets")
        if isinstance(assets, dict) and assets.get("type") == "compound":
            baby = {"type": "compound", "value": {}}
            for k, v in assets["value"].items():
                vv = v["value"] if isinstance(v, dict) else v
                baby["value"][k] = {"type": "string", "value": vv + "_baby"}
            F["baby_assets"] = baby
    elif reg == "minecraft:wolf_sound_variant":
        # adult_sounds/baby_sounds split + step_sound
        sounds = {"type": "compound", "value": dict(F)}
        sounds["value"] = dict(F)
        sounds["value"]["step_sound"] = {"type": "string",
                                         "value": "entity.wolf.step"}
        F.clear()
        F["adult_sounds"] = sounds
        F["baby_sounds"] = copy.deepcopy(sounds)
    elif reg == "minecraft:timeline":
        put_str("clock", "overworld")
    elif reg == "minecraft:enchantment":
        el = upd_enchantment(el)
    return el


_dim205_ow = None


def overworld_125():
    """1.20.5 overworld element (stable pre-26 field shapes), deep-copied."""
    global _dim205_ow
    if _dim205_ow is None:
        dc = load("login_1.20.5.json")["dimensionCodec"]
        regs = normalize(dc)
        _dim205_ow = next(el for n, i, el in regs["minecraft:dimension_type"]
                          if n == "minecraft:overworld")
    return copy.deepcopy(_dim205_ow)


def patch_261_overworld(regs, for26x):
    """Restore dimension fields the slimmed 1.21.11/26.1 snapshots dropped.

    Upstream's newer loginPacket.json files lost the stable pre-26 fields;
    Via's 1_21_11->26_1 translation preserves them, so vanilla requires them.
    Merges absent keys only (never overrides source values), from the 1.20.5
    ground truth with identical shapes. has_ender_dragon_fight=false for
    overworld per Via's dimension handler. for26x additionally normalizes
    visual/ambient_light_color to the int form Via computes for 26.1+
    (1.21.11 native keeps its string form).
    """
    base = cfields(overworld_125())
    for _, _, el in regs.get("minecraft:dimension_type", []):
        F = cfields(el)
        if F is None:
            continue
        for k in ("bed_works", "has_raids", "natural", "piglin_safe",
                  "respawn_anchor_works", "ultrawarm"):
            if k not in F and base is not None and k in base:
                F[k] = copy.deepcopy(base[k])
        if "has_ender_dragon_fight" not in F:
            F["has_ender_dragon_fight"] = {"type": "byte", "value": 0}
        if for26x:
            av = F.get("attributes")
            AF = cfields(av)
            if AF is not None:
                for ak in list(AF.keys()):
                    if ak.split("/")[-1] == "ambient_light_color":
                        AF[ak] = {"type": "int", "value": -16119286}


def record_ids(dc, key):
    regs = normalize(dc)
    plains = next(i for n, i, e in regs["minecraft:worldgen/biome"] if n == "minecraft:plains")
    over = next(i for n, i, e in regs["minecraft:dimension_type"] if n == "minecraft:overworld")
    META[key] = (plains, over)


def emit_table(name, regs, upgrade=None):
    """One RegistryData body per registry, in vanilla (source) order."""
    table = []
    for reg, entries in regs.items():  # source order == vanilla order
        entries = select_entries(reg, entries)
        if not entries:
            continue  # empty in source -> skip
        body = bytearray()
        rid = reg.encode()
        body += _varint(len(rid)) + rid
        body += _varint(len(entries))
        for ename, eid, element in entries:
            if upgrade is not None:
                element = upgrade(reg, ename, element)
            key = ename.encode()
            body += _varint(len(key)) + key
            body.append(1)  # present
            body += enc(element, True)
        blob_name = f"{name}_{reg.split(':')[-1].split('/')[-1]}"
        if blob_name in BLOBS:
            raise SystemExit(f"blob name collision: {blob_name}")
        BLOBS[blob_name] = bytes(body)
        table.append((reg, blob_name))
    if not table:
        raise SystemExit(f"{name}: no registries in source")
    TARGETS[name] = table


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
        ("j1162", "login_1.16.2.json", True),
        ("j117", "login_1.17.json", True),
        ("j118", "login_1.18.json", True),
        ("j119_759", "login_1.19.json", True),
        ("j119_760", "login_1.19.2.json", True),
        ("j119_7612", "login_1.19.4.json", True),
        ("j120_763", "login_1.20.json", True),
    ]
    for name, fn, filtered in jobs:
        d = load(fn)
        dc = d["dimensionCodec"]
        if filtered is None:
            BLOBS[name] = enc(dc, True)  # 1.16 single-registry shape, verbatim
        else:
            BLOBS[name] = enc(filtered_compound(dc), True)
        if filtered is not None:
            record_ids(dc, name)
            over_el = next(el for n, i, el in normalize(dc)["minecraft:dimension_type"]
                            if n == "minecraft:overworld")
            BLOBS["dim_" + name[1:]] = enc(over_el, True)
    # --- 764/765 config: one compound body (every non-empty source registry) ---
    for name, fn in [("c764", "login_1.20.2.json"), ("c765", "login_1.20.2.json")]:
        dc = load(fn)["dimensionCodec"]
        BLOBS[name] = enc(filtered_compound(dc), True)
        record_ids(dc, name)
    # --- 766+ config: one RegistryData body PER registry (all of them) ---
    for name, fn in [("e766", "login_1.20.5.json"), ("e767", "login_1.21.1.json"),
                     ("e769", "login_1.21.3.json"), ("e770", "login_1.21.9.json"),
                     ("e773", "login_1.21.9.json"), ("e774", "login_1.21.11.json"),
                     ("e775", "login_26.1.json")]:
        dc = load(fn)["dimensionCodec"]
        regs = normalize(dc)
        if fn == "login_1.21.11.json":
            patch_261_overworld(regs, for26x=False)  # 1.21.11 native shapes
        elif fn == "login_26.1.json":
            patch_261_overworld(regs, for26x=True)  # 26.1 int color form
        emit_table(name, regs)
        if name == "e775":
            # 26.1 requires the sound-variant registries non-empty (Via
            # sends them to every 26.1 client); 1.21.11-era and older
            # targets never knew these registries.
            append_sound_variants(name)
        plains = next(i for n, i, e in regs["minecraft:worldgen/biome"] if n == "minecraft:plains")
        over = next(i for n, i, e in regs["minecraft:dimension_type"] if n == "minecraft:overworld")
        META[name] = (plains, over)
    # --- e776 (26.2/26.3): same 26.1 source, elements upgraded to 26.2
    # shapes (variant baby assets, sound split, timeline clock). 775 keeps
    # the verbatim 26.1 shapes for native 26.1 clients.
    dc261 = load("login_26.1.json")["dimensionCodec"]
    regs261 = normalize(dc261)
    patch_261_overworld(regs261, for26x=True)
    emit_table("e776", regs261, upgrade=to26_2)
    append_sound_variants("e776")
    plains261 = next(i for n, i, e in regs261["minecraft:worldgen/biome"] if n == "minecraft:plains")
    over261 = next(i for n, i, e in regs261["minecraft:dimension_type"] if n == "minecraft:overworld")
    META["e776"] = (plains261, over261)


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


# --- 26.1+ sound-variant registries ----------------------------------------
# Upstream minecraft-data dropped cat/cow/pig/chicken_sound_variant, but
# vanilla 26.x requires them non-empty ("Registry must be non-empty").
# Ground truth: ViaVersion's sound-variant-registries-26.1.nbt (the exact
# bytes Via sends 1.21.11->26.1 clients), checked in at tools/. Entry ids
# follow file order (Via's entriesFromTag order), matching vanilla's
# network-order assignment.
SOUND_NBT = "tools/sound-variant-registries-26.1.nbt"
_sound_regs = None


def _nbt_node(data, pos, tag):
    """Read one NBT payload, returning a gen_codec JSON node + new pos."""
    if tag == 8:
        n = struct.unpack(">H", data[pos:pos + 2])[0]
        pos += 2
        s = data[pos:pos + n].decode()
        pos += n
        return {"type": "string", "value": s}, pos
    if tag == 10:
        fields = {}
        while True:
            ct = data[pos]
            pos += 1
            if ct == 0:
                return {"type": "compound", "value": fields}, pos
            nl = struct.unpack(">H", data[pos:pos + 2])[0]
            pos += 2
            nm = data[pos:pos + nl].decode()
            pos += nl
            fields[nm], pos = _nbt_node(data, pos, ct)
    raise SystemExit(f"sound NBT: unexpected tag {tag}")


def sound_variant_regs():
    """[(registry, [(name, id, element)])] from Via's NBT (file order)."""
    global _sound_regs
    if _sound_regs is None:
        with open(SOUND_NBT, "rb") as f:
            data = f.read()
        assert data[0] == 10, "sound NBT root must be a compound"
        pos = 1
        nl = struct.unpack(">H", data[pos:pos + 2])[0]
        pos += 2 + nl  # skip root name
        root, pos = _nbt_node(data, pos, 10)
        _sound_regs = []
        for reg, entries in root["value"].items():
            es = []
            for i, (k, v) in enumerate(entries["value"].items()):
                es.append((k, i, v))
            full = reg if ":" in reg else "minecraft:" + reg
            _sound_regs.append((full, es))
    return _sound_regs


def append_sound_variants(target):
    """Append the 4 sound-variant RegistryData bodies to TARGETS[target]."""
    for reg, entries in sound_variant_regs():
        body = bytearray()
        rid = reg.encode()
        body += _varint(len(rid)) + rid
        body += _varint(len(entries))
        for ename, eid, element in entries:
            key = ename.encode()
            body += _varint(len(key)) + key
            body.append(1)  # present
            body += enc(element, True)
        blob_name = f"{target}_{reg.split(':')[-1].split('/')[-1]}"
        if blob_name in BLOBS:
            raise SystemExit(f"blob name collision: {blob_name}")
        BLOBS[blob_name] = bytes(body)
        TARGETS[target].append((reg, blob_name))


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
                "// (every source registry, >=1 vanilla entry; overworld/plains only for\n"
                "// dimension_type / worldgen/biome; vanilla IDs kept).\n"
                "#include <cstddef>\n#include <cstdint>\n\nnamespace limbo::codec {\n\n"
                "struct Blob { const uint8_t* data; size_t size; };\n")
        for name in BLOBS:
            f.write(f"Blob {name}();\n")
        # Per-target table: every RegistryData body in vanilla (source) order.
        f.write("struct RegBlob { const char* registry; Blob blob; };\n")
        for t in TARGETS:
            f.write(f"const RegBlob* {t}_regs(size_t* count);\n")
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
        # Per-target RegistryData tables (vanilla order) for limbo::registry.
        for t, table in TARGETS.items():
            f.write(f"\nstatic const RegBlob k_{t}_regs[] = {{\n")
            for reg, bn in table:
                f.write(f'  {{"{reg}", {{k_{bn}, sizeof(k_{bn})}}}},\n')
            f.write("};\n")
            f.write(f"const RegBlob* {t}_regs(size_t* n) {{\n"
                    f"  *n = sizeof(k_{t}_regs) / sizeof(k_{t}_regs[0]);\n"
                    f"  return k_{t}_regs;\n}}\n")
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
    print("registry bodies per config target:")
    for t, table in TARGETS.items():
        size = sum(len(BLOBS[bn]) for _, bn in table)
        print(f"  {t}: {len(table)} blobs, {size} bytes")
        for reg, bn in table:
            print(f"      {reg:34s} {len(BLOBS[bn]):6d} B")


if __name__ == "__main__":
    emit()
