/*
 * miniz.h - Minimal gzip/inflate wrapper for .schem decompression.
 * Public domain. Based on miniz by Rich Geldreich (richgel999.github.io/miniz/).
 * Stripped to the minimal set needed for .schem v2 gzip decompression.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mi {

// Decompress a raw deflate stream (no zlib/gzip header).
// Returns true on success. out_len = bytes written to out.
bool deflate_raw(const uint8_t* src, size_t src_len, std::vector<uint8_t>& dst, size_t& out_len);

// Gunzip: parse gzip header, decompress deflate payload, validate Adler-32.
// Returns true on success. out gets the decompressed bytes.
bool gunzip(const uint8_t* buf, size_t buf_len, std::vector<uint8_t>& out, size_t& out_len);

// Convenience: gunzip from a byte span, returns decompressed vector.
// The 4-arg version is the canonical API.

} // namespace mi