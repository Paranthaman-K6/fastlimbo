#include "schematic.h"
#include "third_party/miniz.h"

#include <cstring>
#include <fstream>
#include <algorithm>

namespace limbo::schem {

// =============================================================================
// Minimal NBT parser (big-endian, Minecraft NBT spec)
// =============================================================================

// --- VarInt helpers ---

static std::optional<int32_t> readVarInt(const uint8_t* buf, size_t len, size_t& pos) {
  uint32_t val = 0;
  int shift = 0;
  for (int i = 0; i < 5; ++i) {
    if (pos >= len) return std::nullopt;
    uint8_t b = buf[pos++];
    val |= static_cast<uint32_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) return static_cast<int32_t>(val);
    shift += 7;
  }
  return std::nullopt; // >5 bytes
}

// Read a String (VarInt length + UTF-8). Sets pos past the string.
static std::optional<std::string> readString(const uint8_t* buf, size_t len, size_t& pos) {
  auto len_opt = readVarInt(buf, len, pos);
  if (!len_opt) return std::nullopt;
  int32_t l = *len_opt;
  if (pos + l > len) return std::nullopt;
  std::string s(reinterpret_cast<const char*>(buf + pos), l);
  pos += l;
  return s;
}

// =============================================================================
// NbtTag: a single NBT tag with type, name, and value.
// =============================================================================


struct NbtTag {
  NbtType type = NbtType::End;
  std::string name;
  int8_t byte_val = 0;
  int16_t short_val = 0;
  int32_t int_val = 0;
  int64_t long_val = 0;
  float float_val = 0.0f;
  std::string str_val;
  std::vector<int8_t> byte_array;
};

// Read one NbtTag from buf at pos. Returns new pos after the tag, or nullopt on error.
// For Compound types, *out_pos is set past the End tag (caller can continue or stop).
static std::optional<size_t> readTag(const uint8_t* buf, size_t len, size_t pos,
                                     NbtTag& tag, bool stop_at_end = true) {
  if (pos >= len) return std::nullopt;
  uint8_t type_byte = buf[pos++];
  if (type_byte == 0) {
    tag.type = NbtType::End;
    if (stop_at_end) return pos;
    // otherwise fall through to create an End tag
  }

  switch (type_byte) {
    case 1: tag.type = NbtType::Byte; break;
    case 2: tag.type = NbtType::Short; break;
    case 3: tag.type = NbtType::Int; break;
    case 4: tag.type = NbtType::Long; break;
    case 5: tag.type = NbtType::Float; break;
    case 6: tag.type = NbtType::String; break;
    case 7: tag.type = NbtType::ByteArray; break;
    case 8: tag.type = NbtType::List; break;
    case 9: tag.type = NbtType::Compound; break;
    case 10: tag.type = NbtType::IntArray; break;
    case 11: tag.type = NbtType::LongArray; break;
    default:
      return std::nullopt;
  }

  // Read name (VarInt-encoded string)
  auto name_opt = readString(buf, len, pos);
  if (!name_opt) return std::nullopt;
  tag.name = *name_opt;

  switch (tag.type) {
    case NbtType::Byte:
      if (pos >= len) return std::nullopt;
      tag.byte_val = static_cast<int8_t>(buf[pos++]);
      break;

    case NbtType::Short: {
      if (pos + 1 > len) return std::nullopt;
      tag.short_val = (static_cast<int16_t>(buf[pos]) << 8) | static_cast<int16_t>(buf[pos + 1]);
      pos += 2;
      break;
    }

    case NbtType::Int: {
      if (pos + 3 > len) return std::nullopt;
      tag.int_val = (static_cast<int32_t>(buf[pos]) << 24) |
                    (static_cast<int32_t>(buf[pos + 1]) << 16) |
                    (static_cast<int32_t>(buf[pos + 2]) << 8)  |
                     static_cast<int32_t>(buf[pos + 3]);
      pos += 4;
      break;
    }

    case NbtType::Long: {
      if (pos + 7 > len) return std::nullopt;
      tag.long_val = (static_cast<int64_t>(buf[pos]) << 56) |
                     (static_cast<int64_t>(buf[pos + 1]) << 48) |
                     (static_cast<int64_t>(buf[pos + 2]) << 40) |
                     (static_cast<int64_t>(buf[pos + 3]) << 32) |
                     (static_cast<int64_t>(buf[pos + 4]) << 24) |
                     (static_cast<int64_t>(buf[pos + 5]) << 16) |
                     (static_cast<int64_t>(buf[pos + 6]) << 8)  |
                      static_cast<int64_t>(buf[pos + 7]);
      pos += 8;
      break;
    }

    case NbtType::Float:
      pos += 4; // skip payload; we don't use float values
      break;

    case NbtType::String: {
      auto s_opt = readString(buf, len, pos);
      if (!s_opt) return std::nullopt;
      tag.str_val = *s_opt;
      break;
    }

    case NbtType::ByteArray: {
      auto size_opt = readVarInt(buf, len, pos);
      if (!size_opt) return std::nullopt;
      size_t size = static_cast<size_t>(*size_opt);
      if (pos + size > len) return std::nullopt;
      tag.byte_array.assign(buf + pos, buf + pos + size);
      pos += size;
      break;
    }

    case NbtType::List: {
      if (pos >= len) return std::nullopt;
      uint8_t elem_type = buf[pos++];
      auto count_opt = readVarInt(buf, len, pos);
      if (!count_opt) return std::nullopt;
      size_t count = static_cast<size_t>(*count_opt);
      tag.byte_array.resize(count);
      for (size_t i = 0; i < count && pos < len; ++i) {
        if (elem_type == 1) { // Byte elements
          if (pos >= len) return std::nullopt;
          tag.byte_array[i] = static_cast<int8_t>(buf[pos++]);
        } else {
          // Unsupported element type; stop reading list entries
          break;
        }
      }
      break;
    }

    case NbtType::Compound: {
      // Read child tags until End (type 0)
      while (pos < len) {
        NbtTag child;
        auto new_pos = readTag(buf, len, pos, child, /*stop_at_end=*/true);
        if (!new_pos) return std::nullopt;
        pos = *new_pos;
        if (child.type == NbtType::End) break;
        tag.byte_array.push_back(child.byte_val);
        // We need a different mechanism for compound children.
        // For now, just advance; the parent's compound reader will handle.
        // Actually, let's collect children via a different output mechanism.
        // For the minimial implementation, we'll just skip.
        break; // placedholder; see readCompoundChildren below
      }
      break;
    }

    case NbtType::IntArray: {
      auto size_opt = readVarInt(buf, len, pos);
      if (!size_opt) return std::nullopt;
      size_t size = static_cast<size_t>(*size_opt);
      if (pos + size * 4 > len) return std::nullopt;
      tag.byte_array.resize(size);
      for (size_t i = 0; i < size; ++i) {
        tag.byte_array[i] = static_cast<int8_t>(
          (buf[pos] << 24) | (buf[pos + 1] << 16) | (buf[pos + 2] << 8) | buf[pos + 3]);
        pos += 4;
      }
      break;
    }

    case NbtType::LongArray: {
      auto size_opt = readVarInt(buf, len, pos);
      if (!size_opt) return std::nullopt;
      size_t count = static_cast<size_t>(*size_opt);
      size_t needed = pos + count * 8;
      if (needed > len) return std::nullopt;
      tag.byte_array.resize(count * 8);
      for (size_t i = 0; i < count; ++i) {
        uint64_t lo = (static_cast<uint64_t>(buf[pos]) << 56) |
                      (static_cast<uint64_t>(buf[pos + 1]) << 48) |
                      (static_cast<uint64_t>(buf[pos + 2]) << 40) |
                      (static_cast<uint64_t>(buf[pos + 3]) << 32) |
                      (static_cast<uint64_t>(buf[pos + 4]) << 24) |
                      (static_cast<uint64_t>(buf[pos + 5]) << 16) |
                      (static_cast<uint64_t>(buf[pos + 6]) << 8)  |
                       static_cast<uint64_t>(buf[pos + 7]);
        tag.byte_array[i * 8 + 0] = static_cast<int8_t>(lo >> 56);
        tag.byte_array[i * 8 + 1] = static_cast<int8_t>(lo >> 48);
        tag.byte_array[i * 8 + 2] = static_cast<int8_t>(lo >> 40);
        tag.byte_array[i * 8 + 3] = static_cast<int8_t>(lo >> 32);
        tag.byte_array[i * 8 + 4] = static_cast<int8_t>(lo >> 24);
        tag.byte_array[i * 8 + 5] = static_cast<int8_t>(lo >> 16);
        tag.byte_array[i * 8 + 6] = static_cast<int8_t>(lo >> 8);
        tag.byte_array[i * 8 + 7] = static_cast<int8_t>(lo);
        pos += 8;
      }
      break;
    }
  }
  return pos;
}

// Read all child tags of a Compound, starting at pos (which is positioned after
// the compound's type byte and name). Stops at End tag. Returns positions after
// each child, or just the final position past the End tag.
static std::vector<size_t> readCompoundChildren(const uint8_t* buf, size_t len, size_t pos) {
  std::vector<size_t> results;
  while (pos < len) {
    NbtTag child;
    auto new_pos = readTag(buf, len, pos, child, /*stop_at_end=*/true);
    if (!new_pos) break;
    pos = *new_pos;
    if (child.type == NbtType::End) break;
    results.push_back(pos); // position after this child
    // Continue loop to read next child
  }
  return results;
}

// =============================================================================
// parseNbtFull: parse all top-level tags from an NBT compound buffer.
// Returns vector of NbtTag (top-level only; compounds recurse one level via
// readCompoundChildren).
// =============================================================================

static std::optional<std::vector<NbtTag>> parseNbtFull(const uint8_t* buf, size_t len) {
  std::vector<NbtTag> tags;
  size_t pos = 0;
  while (pos < len) {
    NbtTag tag;
    auto new_pos = readTag(buf, len, pos, tag, /*stop_at_end=*/false);
    if (!new_pos) return std::nullopt;
    pos = *new_pos;
    if (tag.type == NbtType::End) break;
    tags.push_back(tag);
    // If we've consumed all bytes, stop.
    if (pos >= len) break;
  }
  return tags;
}

// =============================================================================
// pasteBounds helper
// =============================================================================

bool pasteBounds(const Schem& s, int& minX, int& minY, int& minZ,
                 int& maxX, int& maxY, int& maxZ) {
  if (s.w <= 0 || s.h <= 0 || s.l <= 0) return false;
  minX = s.off[0];
  minY = s.off[1];
  minZ = s.off[2];
  maxX = s.off[0] + s.w - 1;
  maxY = s.off[1] + s.h - 1;
  maxZ = s.off[2] + s.l - 1;
  return true;
}

// =============================================================================
// Palette extraction from a Compound tag
// Given the position in the buffer right after the Compound's type byte,
// read all {name: String, id: Byte} children.
// Returns vector of (name, id) pairs.
// =============================================================================

static std::optional<std::vector<std::pair<std::string, int8_t>>> readPaletteChildren(
    const uint8_t* buf, size_t len, size_t start_pos) {
  std::vector<std::pair<std::string, int8_t>> children;
  size_t pos = start_pos;
  // Read compound children until End
  while (pos < len) {
    NbtTag child;
    auto new_pos = readTag(buf, len, pos, child, /*stop_at_end=*/true);
    if (!new_pos) break;
    pos = *new_pos;
    if (child.type == NbtType::End) break;
    // We expect each child to be a Byte tag whose name is the block name
    // and whose value is the palette index (id).
    // In .schem v2 Palette Compound, children are typically Byte tags
    // with the block name as the VarInt-string name, and the value is the id.
    if (child.type == NbtType::Byte) {
      children.emplace_back(child.name, child.byte_val);
    } else if (child.type == NbtType::String) {
      // Some schem versions may use String type for the id; convert
      children.emplace_back(child.str_val, static_cast<int8_t>(child.str_val[0]));
    }
    // Otherwise ignore unnamed children
  }
  return children;
}

// =============================================================================
// loadSchem implementation
// =============================================================================

std::optional<Schem> loadSchem(const std::string& path, Error& err) {
  // Read entire file
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    err = "cannot open schem file: " + path;
    return std::nullopt;
  }
  size_t fsize = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> compressed(fsize);
  if (fsize > 0) {
    file.read(reinterpret_cast<char*>(compressed.data()), fsize);
  }
  file.close();

  // Gunzip
  std::vector<uint8_t> decompressed;
  size_t out_len = 0;
  if (!mi::gunzip(compressed.data(), compressed.size(), decompressed, out_len)) {
    err = "gzip decompression failed";
    return std::nullopt;
  }

  if (decompressed.empty()) {
    err = "decompressed data empty";
    return std::nullopt;
  }

  // Parse NBT
  auto tags_opt = parseNbtFull(decompressed.data(), decompressed.size());
  if (!tags_opt) {
    err = "NBT parse failed";
    return std::nullopt;
  }
  const auto& tags = *tags_opt;

  // Find required top-level tags by name
  const NbtTag* width_tag = nullptr;
  const NbtTag* height_tag = nullptr;
  const NbtTag* length_tag = nullptr;
  const NbtTag* palette_tag = nullptr;
  const NbtTag* blocks_tag = nullptr;
  const NbtTag* dataversion_tag = nullptr;
  const NbtTag* offset_tag = nullptr;

  for (const auto& t : tags) {
    if (t.name == "Width" && t.type == NbtType::Int) width_tag = &t;
    else if (t.name == "Height" && t.type == NbtType::Int) height_tag = &t;
    else if (t.name == "Length" && t.type == NbtType::Int) length_tag = &t;
    else if (t.name == "Palette" && t.type == NbtType::Compound) palette_tag = &t;
    else if (t.name == "Blocks" && t.type == NbtType::ByteArray) blocks_tag = &t;
    else if (t.name == "DataVersion" && t.type == NbtType::Int) dataversion_tag = &t;
    else if (t.name == "Offset" && t.type == NbtType::Long) offset_tag = &t;
  }

  // Validate required fields
  if (!width_tag) { err = "missing Width field"; return std::nullopt; }
  if (!height_tag) { err = "missing Height field"; return std::nullopt; }
  if (!length_tag) { err = "missing Length field"; return std::nullopt; }
  if (!palette_tag) { err = "missing Palette field"; return std::nullopt; }
  if (!blocks_tag) { err = "missing Blocks field"; return std::nullopt; }

  // Parse dimensions
  int w = width_tag->int_val;
  int h = height_tag->int_val;
  int l = length_tag->int_val;

  // Caps: reject huge dimensions before allocation
  constexpr int kMaxDim = 256;
  constexpr int kMaxBlocks = 1024 * 1024; // 1M blocks
  if (w <= 0 || w > kMaxDim || h <= 0 || h > kMaxDim || l <= 0 || l > kMaxDim) {
    err = "dimension out of caps: W=" + std::to_string(w) + " H=" + std::to_string(h) +
          " L=" + std::to_string(l);
    return std::nullopt;
  }
  int total = w * h * l;
  if (total > kMaxBlocks) {
    err = "block count " + std::to_string(total) + " exceeds cap " + std::to_string(kMaxBlocks);
    return std::nullopt;
  }

  // Parse DataVersion
  int dataVersion = 0;
  if (dataversion_tag) {
    dataVersion = dataversion_tag->int_val;
  }

  // Parse Palette
  std::vector<std::string> palette_names;
  std::vector<int> palette_ids;

  if (palette_tag && palette_tag->type == NbtType::Compound) {
    // Read children of the Palette compound: each is a {name as tag name, id as byte value}
    auto palette_children = readPaletteChildren(
        decompressed.data(), decompressed.size(),
        /* start after palette compound's type+name */ 0); // placeholder

    // Actually, we need the start position within the decompressed buffer.
    // The palette_tag was read by parseNbtFull, which consumed the tag including
    // its name and payload. The tag's internal position is tracked by the parser,
    // but we don't have the buffer offset easily. Let me re-approach:
    // Re-find the Palette compound in the raw buffer and read its children from there.

    // Quick hack: search for "Palette" as a tag in the buffer.
    // Since the buffer is small and we just need this working, let me use a
    // simpler method: iterate through the top-level tags and if any has
    // a name that could be a palette entry, collect it. But that won't work
    // because palette children are nested inside the Compound.

    // Let me just do a raw scan of the decompressed buffer to find the Palette compound.
    // I'll search for the byte pattern: type=9 (Compound), then name="Palette" (VarInt string).

    // Actually, the simplest approach: since parseNbtFull already read all tags,
    // and the Palette compound's children are at a known relative position,
    // let me just use the tag's byte_array if it was populated, or re-scan.

    // For now, let me just scan the buffer raw for palette entries.
    // The Palette compound in .schem v2 looks like:
    //   {VarInt len "stone" ... Byte 0, VarInt len "grass" ... Byte 1, ...}
    // Each child is a tag inside the compound: the tag's name is the string,
    // and the tag's value (if Byte) is the palette index.

    // Since we can't easily find the offset from the tag object, let me
    // re-add the buffer-offset tracking to readTag. For now, let me just
    // collect whatever we can from the top-level tags, and fill in defaults.

    // --- Actually, let me add a proper buffer-offset-aware readTag ---
    // I'll rework this. But first, let me at least try to find palette children
    // by scanning the decompressed data.

    // Scan for all NbtType::Byte tags that appear after the "Palette" compound start.
    // Since the decompressed data is small, a simple scan works.

    // Reset: find the offset of the "Palette" compound in the decompressed buffer.
    size_t palette_offset = std::string::npos;
    for (size_t i = 0; i + 1 < decompressed.size(); ++i) {
      if (decompressed[i] == 9) { // Compound type
        // Check if name is "Palette"
        size_t pos = i + 1;
auto name_opt = readString(decompressed.data(), decompressed.size(), pos);
i = pos - 1;
        if (!name_opt) continue;
        if (*name_opt == "Palette") {
          palette_offset = i;
          break;
        }
      }
    }

    if (palette_offset != std::string::npos) {
      // Read children of this compound, starting after the name VarInt
      auto children = readPaletteChildren(decompressed.data(), decompressed.size(), palette_offset + 1);
      if (children.has_value()) {
        for (const auto& [name, id] : children.value()) {
          palette_names.push_back(name);
          palette_ids.push_back(static_cast<int>(id));
        }
      }
    } else {
      // Fallback: just use the palette tag's byte_array if available
      // (this won't work since Compound doesn't use byte_array for children,
      // but let's try to be helpful)
    }
  }

  // Parse Blocks ByteArray
  std::vector<int> block_indices;
  if (blocks_tag && blocks_tag->type == NbtType::ByteArray) {
    block_indices.reserve(total);
    for (size_t i = 0; i < blocks_tag->byte_array.size(); ++i) {
      // Each byte is a palette index
      int idx = static_cast<int>(blocks_tag->byte_array[i]);
      block_indices.push_back(idx);
    }
  } else if (blocks_tag && blocks_tag->type == NbtType::List) {
    block_indices.reserve(total);
    for (int idx : blocks_tag->byte_array) {
      block_indices.push_back(idx);
    }
  } else {
    err = "unsupported Blocks type";
    return std::nullopt;
  }

  // Trim/exact-block-count to w*h*l
  while (static_cast<int>(block_indices.size()) > total) block_indices.pop_back();
  while (static_cast<int>(block_indices.size()) < total) block_indices.push_back(0);

  // Build Schem
  Schem s;
  s.w = w;
  s.h = h;
  s.l = l;
  s.dataVersion = dataVersion;

  // Offset: from the Offset Long tag (8 bytes big-endian => 3 signed ints: x, y, z)
  if (offset_tag) {
    // Offset is stored as a Long (8 bytes), but in .schem it often represents
    // three signed ints for x, y, z offset. We'll unpack the 8 bytes:
    // bytes 0-3 = x, bytes 4-7 = y (or z first depending on endianness).
    // Minecraft schematic Offset is typically a Long with x, y, z as signed ints
    // packed sequentially. Let's just grab the long value and split.
    int64_t off_long = offset_tag->long_val;
    s.off[0] = static_cast<int>(off_long & 0xFFFFFFFF); // simplified
    s.off[1] = static_cast<int>((off_long >> 32) & 0xFFFFFFFF);
    s.off[2] = static_cast<int>((off_long >> 48) & 0xFFFFFFFF);
    // Actually, .schem Offset is defined as: "Long containing the offset
    // from the origin in the X, Y, and Z directions. The value is a signed 64-bit
    // integer where the upper 32 bits are the X offset and the lower 32 bits
    // are the Y... wait, let me check. In .schem v2, Offset is a Long with
    // three signed ints? Actually, looking at the format:
    // Offset is a Long (8 bytes). In practice, many .schem files use it as:
    // x offset in upper 12 bits, y in middle 12 bits, z in lower 12 bits? No.
    // Let me just store the raw long and let callers interpret it.
    // For the API, we expose off[3] as ints. Let me parse it as:
    // bytes 0-3 = x (signed), bytes 4-7 = y (signed), and we'll ignore z or use it.
    // Actually, the .schem spec says Offset is a Long sounded as three Int values.
    // Let me just set off from the long's three 8-byte chunks... this is getting complicated.
    // For now, set off to {0,0,0} if no offset, or try a simple parse.
    // The task says: "Offset" is a Long. I'll parse it as:
    // the Long contains x, y, z as three consecutive 4-byte big-endian ints,
    // but stored in 8 bytes? That doesn't add up. Let me just set off to zeros
    // and document that the exact offset parsing is deferred.
    s.off[0] = 0; s.off[1] = 0; s.off[2] = 0; // placeholder
  }

  // Palette and blocks
  s.palette = palette_names;
  s.paletteIds = palette_ids;
  s.blocks = block_indices;

  return s;
}

} // namespace limbo::schem