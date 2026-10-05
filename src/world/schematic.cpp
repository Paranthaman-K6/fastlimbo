// limbo-c++ — proprietary software, all rights reserved.
// Copyright (c) 2026 Paranthaman
// See LICENSE. No permission is granted to copy, modify, or redistribute
// this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

// Sponge .schem v2 reader (v3 nested shape also accepted).
// Standard NBT (big-endian, u16 names), gzip via third_party/miniz.
// Caps enforced before big allocations: dims <= 256, total <= 1M blocks,
// decompressed <= 8MB, palette <= 4096 entries, NBT depth <= 32.
#include "world/schematic.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "miniz.h"

namespace limbo::schem {
namespace {

constexpr size_t kMaxDecompressed = 8u * 1024u * 1024u;
constexpr int kMaxDim = 256;
constexpr int kMaxBlocks = 1024 * 1024;
constexpr int kMaxPalette = 4096;
constexpr int kMaxDepth = 32;

struct Node {
  uint8_t type = 0;  // NBT tag type (0=absent)
  int64_t ival = 0;
  std::string sval;
  std::vector<uint8_t> bytes;             // ByteArray raw
  std::vector<int32_t> ints;              // IntArray raw
  std::map<std::string, Node> kids;       // Compound children
  uint8_t listElem = 0;                   // List element type
  std::vector<Node> items;                // List items (payload-only nodes)
};

struct Cursor {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;
  bool ok = true;
  size_t nodes = 0;
  bool u8(uint8_t& v) {
    if (pos >= n) { ok = false; return false; }
    v = p[pos++];
    return true;
  }
  bool u16(uint16_t& v) {
    if (pos + 2 > n) { ok = false; return false; }
    v = (uint16_t(p[pos]) << 8) | p[pos + 1];
    pos += 2;
    return true;
  }
  bool i16(int16_t& v) {
    uint16_t u = 0;
    if (!u16(u)) return false;
    v = (int16_t)u;
    return true;
  }
  bool i32(int32_t& v) {
    if (pos + 4 > n) { ok = false; return false; }
    v = (int32_t(p[pos]) << 24) | (int32_t(p[pos + 1]) << 16) |
        (int32_t(p[pos + 2]) << 8) | p[pos + 3];
    pos += 4;
    return true;
  }
  bool i64(int64_t& v) {
    if (pos + 8 > n) { ok = false; return false; }
    v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[pos + i];
    pos += 8;
    return true;
  }
  bool take(size_t k, const uint8_t*& out) {
    if (k > n || pos + k > n) { ok = false; return false; }
    out = p + pos;
    pos += k;
    return true;
  }
};

bool parsePayload(Cursor& c, uint8_t type, Node& node, int depth);

bool parseNamed(Cursor& c, std::string& name, Node& node, int depth) {
  uint8_t type = 0;
  if (!c.u8(type)) return false;
  if (type == 0) {
    node.type = 0;
    name.clear();
    return true;
  }
  uint16_t nl = 0;
  if (!c.u16(nl)) return false;
  const uint8_t* np;
  if (!c.take(nl, np)) return false;
  name.assign((const char*)np, nl);
  node.type = type;
  return parsePayload(c, type, node, depth);
}

bool parsePayload(Cursor& c, uint8_t type, Node& node, int depth) {
  if (++c.nodes > 2000000) { c.ok = false; return false; }
  if (depth > kMaxDepth) { c.ok = false; return false; }
  switch (type) {
    case 1: {
      uint8_t v = 0;
      if (!c.u8(v)) return false;
      node.ival = (int8_t)v;
      return true;
    }
    case 2: {
      int16_t v = 0;
      if (!c.i16(v)) return false;
      node.ival = v;
      return true;
    }
    case 3: {
      int32_t v = 0;
      if (!c.i32(v)) return false;
      node.ival = v;
      return true;
    }
    case 4: {
      int64_t v = 0;
      if (!c.i64(v)) return false;
      node.ival = v;
      return true;
    }
    case 5:
      c.pos += 4;
      if (c.pos > c.n) { c.ok = false; return false; }
      return true;
    case 6:
      c.pos += 8;
      if (c.pos > c.n) { c.ok = false; return false; }
      return true;
    case 7: {
      int32_t len = 0;
      if (!c.i32(len) || len < 0 || (size_t)len > c.n) { c.ok = false; return false; }
      const uint8_t* bp;
      if (!c.take((size_t)len, bp)) return false;
      node.bytes.assign(bp, bp + len);
      return true;
    }
    case 8: {
      uint16_t nl = 0;
      if (!c.u16(nl)) return false;
      const uint8_t* sp;
      if (!c.take(nl, sp)) return false;
      node.sval.assign((const char*)sp, nl);
      return true;
    }
    case 9: {
      uint8_t et = 0;
      if (!c.u8(et)) return false;
      int32_t len = 0;
      if (!c.i32(len) || len < 0 || len > kMaxBlocks) { c.ok = false; return false; }
      node.listElem = et;
      node.items.reserve((size_t)len);
      for (int32_t i = 0; i < len; ++i) {
        Node it;
        it.type = et;
        if (!parsePayload(c, et, it, depth + 1)) return false;
        node.items.push_back(std::move(it));
      }
      return true;
    }
    case 10: {
      while (true) {
        std::string name;
        Node child;
        if (!parseNamed(c, name, child, depth + 1)) return false;
        if (child.type == 0) break;
        node.kids[std::move(name)] = std::move(child);
        if (node.kids.size() > 100000) { c.ok = false; return false; }
      }
      return true;
    }
    case 11: {
      int32_t len = 0;
      if (!c.i32(len) || len < 0 || len > kMaxBlocks) { c.ok = false; return false; }
      node.ints.reserve((size_t)len);
      for (int32_t i = 0; i < len; ++i) {
        int32_t v = 0;
        if (!c.i32(v)) return false;
        node.ints.push_back(v);
      }
      return true;
    }
    case 12: {
      int32_t len = 0;
      if (!c.i32(len) || len < 0 || len > kMaxBlocks) { c.ok = false; return false; }
      for (int32_t i = 0; i < len; ++i) {
        int64_t v = 0;
        if (!c.i64(v)) return false;
        (void)v;
      }
      return true;
    }
    default:
      { c.ok = false; return false; }
  }
}

// Integer-ish getter (Byte/Short/Int/Long all accepted).
bool getInt(const Node& root, const char* key, int64_t& v) {
  auto it = root.kids.find(key);
  if (it == root.kids.end()) return false;
  uint8_t t = it->second.type;
  if (t < 1 || t > 4) return false;
  v = it->second.ival;
  return true;
}

bool readVarInt(const uint8_t* p, size_t n, size_t& pos, int32_t& v) {
  uint32_t val = 0;
  int shift = 0;
  for (int i = 0; i < 5; ++i) {
    if (pos >= n) return false;
    uint8_t b = p[pos++];
    val |= (uint32_t)(b & 0x7F) << shift;
    if (!(b & 0x80)) {
      v = (int32_t)val;
      return true;
    }
    shift += 7;
  }
  return false;
}

}  // namespace

bool pasteBounds(const Schem& s, int& minX, int& minY, int& minZ, int& maxX,
                 int& maxY, int& maxZ) {
  if (s.w <= 0 || s.h <= 0 || s.l <= 0) return false;
  minX = s.off[0];
  minY = s.off[1];
  minZ = s.off[2];
  maxX = s.off[0] + s.w - 1;
  maxY = s.off[1] + s.h - 1;
  maxZ = s.off[2] + s.l - 1;
  return true;
}

std::optional<Schem> loadSchem(const std::string& path, Error& err) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open " + path;
    return std::nullopt;
  }
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fsize <= 0 || fsize > 16 * 1024 * 1024) {
    fclose(f);
    err = "bad file size";
    return std::nullopt;
  }
  std::vector<uint8_t> comp((size_t)fsize);
  if (fread(comp.data(), 1, comp.size(), f) != comp.size()) {
    fclose(f);
    err = "cannot read " + path;
    return std::nullopt;
  }
  fclose(f);

  std::vector<uint8_t> raw;
  size_t rawLen = 0;
  if (!mi::gunzip(comp.data(), comp.size(), raw, rawLen) || raw.empty()) {
    err = "not gzip/deflate or corrupt: " + path;
    return std::nullopt;
  }
  if (raw.size() > kMaxDecompressed) {
    err = "decompressed too large";
    return std::nullopt;
  }

  Cursor c{raw.data(), raw.size()};
  std::string rootName;
  Node root;
  if (!parseNamed(c, rootName, root, 0) || !c.ok || root.type != 10) {
    err = "bad NBT root";
    return std::nullopt;
  }
  const Node* top = &root;
  auto sit = root.kids.find("Schematic");
  if (sit != root.kids.end() && sit->second.type == 10) top = &sit->second;  // v3 nest

  int64_t ver = 0;
  auto vit = top->kids.find("Version");
  if (vit != top->kids.end() && vit->second.type >= 1 && vit->second.type <= 4)
    ver = vit->second.ival;
  if (ver != 2 && ver != 3) {
    err = "unsupported Version (want 2/3)";
    return std::nullopt;
  }

  int64_t w = 0, h = 0, l = 0;
  if (!getInt(*top, "Width", w) || !getInt(*top, "Height", h) || !getInt(*top, "Length", l)) {
    err = "missing Width/Height/Length";
    return std::nullopt;
  }
  if (w <= 0 || w > kMaxDim || h <= 0 || h > kMaxDim || l <= 0 || l > kMaxDim) {
    err = "dimensions out of caps";
    return std::nullopt;
  }
  int64_t total = w * h * l;
  if (total > kMaxBlocks) {
    err = "block count exceeds cap";
    return std::nullopt;
  }

  const Node* blocksRoot = top;
  auto bit = top->kids.find("Blocks");
  if (bit != top->kids.end() && bit->second.type == 10) blocksRoot = &bit->second;  // v3

  auto pit = blocksRoot->kids.find("Palette");
  if (pit == blocksRoot->kids.end() || pit->second.type != 10) {
    err = "missing Palette";
    return std::nullopt;
  }
  std::vector<std::string> names;
  std::vector<int> ids;
  for (const auto& [name, node] : pit->second.kids) {
    if (node.type < 1 || node.type > 4) continue;
    if ((int)names.size() >= kMaxPalette) {
      err = "palette too large";
      return std::nullopt;
    }
    names.push_back(name);
    ids.push_back((int)node.ival);
  }
  if (names.empty()) {
    err = "empty palette";
    return std::nullopt;
  }
  // Order palette by id so blocks[] indices resolve positionally.
  std::vector<std::string> byId(names.size(), "");
  for (size_t i = 0; i < names.size(); ++i) {
    int id = ids[i];
    if (id < 0 || (size_t)id >= byId.size()) {
      err = "palette id out of range";
      return std::nullopt;
    }
    byId[(size_t)id] = names[i];
  }
  for (const auto& nm : byId) {
    if (nm.empty()) {
      err = "sparse palette ids";
      return std::nullopt;
    }
  }

  auto dit = blocksRoot->kids.find("BlockData");
  if (dit == blocksRoot->kids.end()) dit = blocksRoot->kids.find("Blocks");
  if (dit == blocksRoot->kids.end() || dit->second.type != 7) {
    // v3 "Data" inside Blocks compound
    dit = blocksRoot->kids.find("Data");
    if (dit == blocksRoot->kids.end() || dit->second.type != 7) {
      err = "missing BlockData";
      return std::nullopt;
    }
  }
  const std::vector<uint8_t>& bd = dit->second.bytes;
  std::vector<int> blocks;
  blocks.reserve((size_t)total);
  size_t pos = 0;
  while ((int64_t)blocks.size() < total) {
    int32_t v = 0;
    if (!readVarInt(bd.data(), bd.size(), pos, v) || v < 0 ||
        (size_t)v >= byId.size()) {
      err = "bad BlockData varint";
      return std::nullopt;
    }
    blocks.push_back(v);
  }

  int64_t dataVersion = 0;
  getInt(*top, "DataVersion", dataVersion);

  Schem s;
  s.w = (int)w;
  s.h = (int)h;
  s.l = (int)l;
  s.dataVersion = (int)dataVersion;
  s.off = {0, 0, 0};
  auto oit = top->kids.find("Offset");
  if (oit != top->kids.end()) {
    if (oit->second.type == 11 && oit->second.ints.size() == 3) {
      s.off[0] = oit->second.ints[0];
      s.off[1] = oit->second.ints[1];
      s.off[2] = oit->second.ints[2];
    } else if (oit->second.type == 9 && oit->second.items.size() == 3) {
      for (int i = 0; i < 3; ++i) s.off[i] = (int)oit->second.items[i].ival;
    }
  }
  s.palette = std::move(byId);
  s.paletteIds.resize(s.palette.size());
  for (size_t i = 0; i < s.palette.size(); ++i) s.paletteIds[i] = (int)i;
  s.blocks = std::move(blocks);
  err.clear();
  return s;
}

}  // namespace limbo::schem
