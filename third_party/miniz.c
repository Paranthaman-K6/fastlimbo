/*
 * miniz.c - Small complete inflate (deflate) + zlib/gzip wrapper.
 * Public domain. No system zlib required. Builds with -I third_party.
 *
 * Supports: stored (BTYPE 00), fixed Huffman (01), dynamic Huffman (10)
 * code lengths incl. repeat codes 16/17/18, LZ77 sliding window 32K,
 * zlib streams (Adler-32 verified), gzip members (CRC-32 + ISIZE verified).
 */
#include "miniz.h"

#include <cstring>
#include <vector>

namespace mi {

namespace {

struct BitReader {
  const uint8_t* p;
  size_t n;
  size_t pos = 0;      // byte position
  uint32_t bitbuf = 0;  // pending bits (LSB-first)
  int bitcount = 0;
  bool eof = false;

  BitReader(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}

  uint32_t bits(int count) {
    uint32_t out = 0;
    int shift = 0;
    while (count > 0) {
      if (bitcount == 0) {
        if (pos >= n) {
          eof = true;
          return out;
        }
        bitbuf = p[pos++];
        bitcount = 8;
      }
      int take = count < bitcount ? count : bitcount;
      out |= ((bitbuf & ((1u << take) - 1u)) << shift);
      bitbuf >>= take;
      bitcount -= take;
      count -= take;
      shift += take;
    }
    return out;
  }

  bool alignByte() {
    bitbuf = 0;
    bitcount = 0;
    return true;
  }
};

// Canonical Huffman decoder from code lengths.
struct Huffman {
  // fast path: 9-bit table
  int16_t fast[512];
  // slow path: sorted symbols
  uint16_t syms[288];
  uint8_t lens[288];
  int count = 0;
  int maxlen = 0;

  bool build(const uint8_t* lengths, int n) {
    for (int i = 0; i < 512; ++i) fast[i] = -1;
    count = n;
    maxlen = 0;
    int bl_count[16] = {};
    for (int i = 0; i < n; ++i) {
      lens[i] = lengths[i];
      if (lengths[i] > 15) return false;
      if (lengths[i]) {
        bl_count[lengths[i]]++;
        if (lengths[i] > maxlen) maxlen = lengths[i];
      }
    }
    if (maxlen > 15) return false;
    unsigned next_code[16] = {};
    unsigned code = 0;
    for (int b = 1; b <= 15; ++b) {
      code = (code + (unsigned)bl_count[b - 1]) << 1;
      next_code[b] = code;
    }
    int order[288];
    for (int i = 0; i < n; ++i) order[i] = i;
    // sort by (len, symbol)
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j)
        if (lens[order[j]] && (!lens[order[i]] || lens[order[j]] < lens[order[i]] ||
                              (lens[order[j]] == lens[order[i]] && order[j] < order[i]))) {
          int t = order[i];
          order[i] = order[j];
          order[j] = t;
        }
    int k = 0;
    for (int i = 0; i < n; ++i) {
      int s = order[i];
      if (!lens[s]) continue;
      syms[k++] = (uint16_t)s;
    }
    // build codes in sorted order
    uint16_t codes[288];
    for (int b = 1; b <= 15; ++b) {
      code = (code + bl_count[b - 1]) << 1;
      next_code[b] = code;
    }
    for (int i = 0; i < k; ++i) {
      int s = syms[i];
      int len = lens[s];
      codes[i] = (uint16_t)next_code[len]++;
    }
    // fill fast table for len <= 9 (codes are MSB-first within `len` bits as read LSB-first reversed)
    for (int i = 0; i < k; ++i) {
      int len = lens[syms[i]];
      if (len > 9) continue;
      uint16_t c = codes[i];
      // reverse `len` bits (deflate codes packed MSB-first, read LSB-first)
      uint16_t rev = 0;
      for (int b = 0; b < len; ++b) {
        rev = (rev << 1) | (c & 1);
        c >>= 1;
      }
      int step = 1 << len;
      for (int j = rev; j < 512; j += step) {
        if (fast[j] == -1) fast[j] = (int16_t)((len << 9) | syms[i]);
      }
    }
    // slow path tables: canonical codes aligned with syms order.
    {
      unsigned nc[16] = {};
      unsigned cc = 0;
      int blc[16] = {};
      for (int i = 0; i < n; ++i)
        if (lens[i]) blc[lens[i]]++;
      for (int b = 1; b <= 15; ++b) {
        cc = (cc + blc[b - 1]) << 1;
        nc[b] = cc;
      }
      for (int i = 0; i < k; ++i) {
        int s = syms[i];
        unsigned c = nc[lens[s]]++;
        uint16_t r = 0;
        for (int b = 0; b < lens[s]; ++b) {
          r = (r << 1) | (c & 1);
          c >>= 1;
        }
        slowCodes[i] = r;
      }
      slowCount = k;
    }
    return true;
  }

  uint16_t slowCodes[288];
  int slowCount = 0;

  // NOTE: build() fills slowCodes; decode() below uses fast table then slow scan.
  int decode(BitReader& br) {
    // peek up to 9 bits without consuming: refill manually
    while (br.bitcount < 9 && br.pos < br.n) {
      br.bitbuf |= (uint32_t)br.p[br.pos++] << br.bitcount;
      br.bitcount += 8;
    }
    int idx = br.bitbuf & 0x1FF;
    int16_t f = fast[idx];
    if (f >= 0) {
      int len = f >> 9;
      int sym = f & 0x1FF;
      if (len > br.bitcount && br.pos >= br.n && br.bitcount < len) {
        // not enough bits and no more input
        if (br.bitcount < len) {
          br.eof = true;
          return -1;
        }
      }
      br.bitbuf >>= len;
      br.bitcount -= len;
      return sym;
    }
    // slow path: accumulate bit by bit up to maxlen
    uint16_t code = 0;
    // we already have bitcount bits buffered; consume one at a time
    for (int len = 1; len <= maxlen && len <= 15; ++len) {
      if (br.bitcount == 0) {
        if (br.pos >= br.n) {
          br.eof = true;
          return -1;
        }
        br.bitbuf = br.p[br.pos++];
        br.bitcount = 8;
      }
      int bit = br.bitbuf & 1;
      br.bitbuf >>= 1;
      br.bitcount--;
      code |= (uint16_t)(bit << (len - 1));
      // compare reversed? codes stored LSB-first as read: match (code,len)
      for (int i = 0; i < slowCount; ++i) {
        if (lens[syms[i]] == len && slowCodes[i] == code) return syms[i];
      }
    }
    return -1;
  }
};

uint32_t adler32(const uint8_t* data, size_t len) {
  const uint32_t MOD = 65521;
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % MOD;
    b = (b + a) % MOD;
  }
  return (b << 16) | a;
}

uint32_t crcTable[256];
bool crcInit = false;
void initCrc() {
  if (crcInit) return;
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crcTable[i] = c;
  }
  crcInit = true;
}
uint32_t crc32(const uint8_t* data, size_t len) {
  initCrc();
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

bool inflateRaw(BitReader& br, std::vector<uint8_t>& out, size_t cap) {
  static const int kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 13, 15, 17, 19, 23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5,  5,  5,  5,  0};
  static const int kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
                                   33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  out.reserve(1 << 16);
  while (true) {
    if (br.eof) return false;
    int finalBlock = (int)br.bits(1);
    int btype = (int)br.bits(2);
    if (br.eof) return false;
    if (btype == 0) {
      br.alignByte();
      if (br.pos + 4 > br.n) return false;
      uint16_t len = br.p[br.pos] | (br.p[br.pos + 1] << 8);
      uint16_t nlen = br.p[br.pos + 2] | (br.p[br.pos + 3] << 8);
      br.pos += 4;
      if ((uint16_t)(len + nlen) != 0xFFFF) return false;
      if (br.pos + len > br.n) return false;
      if (out.size() + len > cap) return false;
      out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
      br.pos += len;
    } else if (btype == 1 || btype == 2) {
      Huffman lit, dist;
      if (btype == 1) {
        uint8_t ll[288];
        for (int i = 0; i < 288; ++i) ll[i] = (i < 144) ? 8 : (i < 256 ? 9 : (i < 280 ? 7 : 8));
        uint8_t dd[32];
        for (int i = 0; i < 32; ++i) dd[i] = 5;
        if (!lit.build(ll, 288) || !dist.build(dd, 32)) return false;
      } else {
        int hlit = (int)br.bits(5) + 257;
        int hdist = (int)br.bits(5) + 1;
        int hclen = (int)br.bits(4) + 4;
        if (br.eof) return false;
        static const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint8_t cl[19] = {};
        for (int i = 0; i < hclen; ++i) cl[kOrder[i]] = (uint8_t)br.bits(3);
        if (br.eof) return false;
        Huffman pre;
        if (!pre.build(cl, 19)) return false;
        uint8_t ll[288] = {};
        uint8_t dd[32] = {};
        int total = hlit + hdist;
        int i = 0;
        int prev = 0;
        while (i < total) {
          int s = pre.decode(br);
          if (s < 0) return false;
          if (s <= 15) {
            if (i < hlit) ll[i] = (uint8_t)s;
            else dd[i - hlit] = (uint8_t)s;
            prev = s;
            ++i;
          } else if (s == 16) {
            int rep = (int)br.bits(2) + 3;
            if (br.eof || i + rep > total) return false;
            for (int k = 0; k < rep; ++k) {
              if (i < hlit) ll[i] = (uint8_t)prev;
              else dd[i - hlit] = (uint8_t)prev;
              ++i;
            }
          } else if (s == 17) {
            int rep = (int)br.bits(3) + 3;
            if (br.eof || i + rep > total) return false;
            for (int k = 0; k < rep; ++k) {
              if (i < hlit) ll[i] = 0;
              else dd[i - hlit] = 0;
              ++i;
            }
            prev = 0;
          } else if (s == 18) {
            int rep = (int)br.bits(7) + 11;
            if (br.eof || i + rep > total) return false;
            for (int k = 0; k < rep; ++k) {
              if (i < hlit) ll[i] = 0;
              else dd[i - hlit] = 0;
              ++i;
            }
            prev = 0;
          } else {
            return false;
          }
        }
        if (!lit.build(ll, hlit) || !dist.build(dd, hdist)) return false;
      }
      while (true) {
        int sym = lit.decode(br);
        if (sym < 0) return false;
        if (sym < 256) {
          if (out.size() + 1 > cap) return false;
          out.push_back((uint8_t)sym);
        } else if (sym == 256) {
          break;
        } else if (sym <= 285) {
          int li = sym - 257;
          int length = kLenBase[li] + (kLenExtra[li] ? (int)br.bits(kLenExtra[li]) : 0);
          if (br.eof) return false;
          int dsym = dist.decode(br);
          if (dsym < 0 || dsym > 29) return false;
          int distance =
              kDistBase[dsym] + (kDistExtra[dsym] ? (int)br.bits(kDistExtra[dsym]) : 0);
          if (br.eof) return false;
          if (distance <= 0 || (size_t)distance > out.size()) return false;
          if (out.size() + (size_t)length > cap) return false;
          size_t from = out.size() - (size_t)distance;
          for (int k = 0; k < length; ++k) out.push_back(out[from + k]);
        } else {
          return false;
        }
      }
    } else {
      return false;  // BTYPE 11 reserved
    }
    if (finalBlock) break;
  }
  return true;
}

}  // namespace

bool gunzip(const uint8_t* buf, size_t buf_len, std::vector<uint8_t>& out,
            size_t& out_len) {
  out.clear();
  out_len = 0;
  if (buf_len < 2) return false;
  const size_t kOutCap = 8u * 1024u * 1024u;
  if (buf[0] == 0x1F && buf[1] == 0x8B) {
    // gzip member
    if (buf_len < 10) return false;
    if (buf[2] != 8) return false;
    uint8_t flg = buf[3];
    size_t pos = 10;
    if (flg & 0x04) {  // FEXTRA
      if (pos + 2 > buf_len) return false;
      uint16_t xlen = buf[pos] | (buf[pos + 1] << 8);
      pos += 2 + xlen;
      if (pos > buf_len) return false;
    }
    if (flg & 0x08) {  // FNAME
      while (pos < buf_len && buf[pos]) ++pos;
      ++pos;
      if (pos > buf_len) return false;
    }
    if (flg & 0x10) {  // FCOMMENT
      while (pos < buf_len && buf[pos]) ++pos;
      ++pos;
      if (pos > buf_len) return false;
    }
    if (flg & 0x02) {  // FHCRC
      pos += 2;
      if (pos > buf_len) return false;
    }
    if (buf_len < pos + 8) return false;
    size_t comp_len = buf_len - pos - 8;
    BitReader br(buf + pos, comp_len);
    std::vector<uint8_t> raw;
    if (!inflateRaw(br, raw, kOutCap)) return false;
    uint32_t wantCrc = buf[buf_len - 8] | (buf[buf_len - 7] << 8) |
                       (buf[buf_len - 6] << 16) | (buf[buf_len - 5] << 24);
    uint32_t wantSize = buf[buf_len - 4] | (buf[buf_len - 3] << 8) |
                        (buf[buf_len - 2] << 16) | (buf[buf_len - 1] << 24);
    if ((raw.size() & 0xFFFFFFFFu) != wantSize) return false;
    if (crc32(raw.data(), raw.size()) != wantCrc) return false;
    out = std::move(raw);
    out_len = out.size();
    return true;
  }
  // assume zlib stream: CMF FLG + deflate + Adler32
  if (buf_len < 6) return false;
  if ((buf[0] & 0x0F) != 8) return false;
  if ((((uint16_t)buf[0] << 8) | buf[1]) % 31 != 0) return false;
  if (buf[1] & 0x20) return false;  // preset dictionary unsupported (FDICT is FLG bit 5)
  BitReader br(buf + 2, buf_len - 6);
  std::vector<uint8_t> raw;
  if (!inflateRaw(br, raw, kOutCap)) return false;
  uint32_t wantAdler = ((uint32_t)buf[buf_len - 4] << 24) | ((uint32_t)buf[buf_len - 3] << 16) |
                       ((uint32_t)buf[buf_len - 2] << 8) | buf[buf_len - 1];
  if (adler32(raw.data(), raw.size()) != wantAdler) return false;
  out = std::move(raw);
  out_len = out.size();
  return true;
}

bool inflate(const uint8_t* buf, size_t len, std::vector<uint8_t>& out, size_t& out_len) {
  return gunzip(buf, len, out, out_len);
}

}  // namespace mi
