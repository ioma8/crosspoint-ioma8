#include "ContentStream.h"

#include <Logging.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <sstream>

#include "PdfFixed.h"
#include "PdfLimits.h"
#include "PdfObject.h"
#include "PdfScratch.h"
#include "StreamDecoder.h"

namespace {

// WinAnsiEncoding bytes 0x80–0xFF → Unicode (PDF 1.7, Adobe table)
static constexpr uint16_t kWin1252ToU[128] = {
    0x20AC, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x017D, 0x2018,
    0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x017E, 0x0178, 0x00A0,
    0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE,
    0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC,
    0x00BD, 0x00BE, 0x00BF, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA,
    0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF, 0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8,
    0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF, 0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6,
    0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF, 0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4,
    0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF};

// MacRomanEncoding bytes 0x80–0xFF → Unicode.
static constexpr uint16_t kMacRomanToU[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5,
    0x00E7, 0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4,
    0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6,
    0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265,
    0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF,
    0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5,
    0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044,
    0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4, 0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9,
    0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};

enum class SimpleFontEncoding {
  WinAnsi,
  MacRoman,
};

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string pdfBytesToUtf8(const uint8_t* data, size_t len, SimpleFontEncoding encoding = SimpleFontEncoding::WinAnsi) {
  std::string out;
  if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
    for (size_t i = 2; i + 1 < len; i += 2) {
      const uint16_t cu = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
      appendUtf8(out, cu);
    }
    return out;
  }
  // UTF-16BE in hex strings: pairs big-endian (common for Identity-H / CJK datasheets).
  if (len >= 2 && (len % 2) == 0) {
    bool allHiZero = true;
    for (size_t i = 0; i < len; i += 2) {
      if (data[i] != 0) {
        allHiZero = false;
        break;
      }
    }
    if (allHiZero) {
      for (size_t i = 0; i + 1 < len; i += 2) {
        const uint16_t cu = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
        if (cu != 0) {
          appendUtf8(out, cu);
        }
      }
      return out;
    }
    // Do not guess UTF-16BE when high bytes vary: hex is often glyph/CID data, not Unicode.
  }
  for (size_t i = 0; i < len; ++i) {
    const unsigned char b = data[i];
    if (b == 0) {
      continue;
    }
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
    } else {
      const uint16_t cp = encoding == SimpleFontEncoding::MacRoman ? kMacRomanToU[b - 0x80] : kWin1252ToU[b - 0x80];
      appendUtf8(out, cp);
    }
  }
  return out;
}

struct ToUnicodeMap {
  struct Entry {
    uint16_t cid = 0;
    PdfFixedString<16> utf8;
  };
  struct Range {
    uint16_t srcFirst = 0;
    uint16_t srcLast = 0;
    uint16_t dstFirst = 0;
  };

  PdfFixedVector<Entry, PDF_MAX_CID_MAP_ENTRIES> glyphs;
  PdfFixedVector<Range, PDF_MAX_CID_MAP_ENTRIES> ranges;

  void clear() {
    glyphs.clear();
    ranges.clear();
  }
  bool empty() const { return glyphs.empty() && ranges.empty(); }

  bool has(uint16_t cid) const {
    for (const auto& entry : glyphs) {
      if (entry.cid == cid && !entry.utf8.empty()) {
        return true;
      }
    }
    for (const auto& range : ranges) {
      if (cid >= range.srcFirst && cid <= range.srcLast) {
        return true;
      }
    }
    return false;
  }

  bool appendMapped(uint16_t cid, std::string& out) const {
    for (const auto& entry : glyphs) {
      if (entry.cid == cid && !entry.utf8.empty()) {
        out += entry.utf8.c_str();
        return true;
      }
    }
    for (const auto& range : ranges) {
      if (cid >= range.srcFirst && cid <= range.srcLast) {
        appendUtf8(out, static_cast<uint16_t>(range.dstFirst + (cid - range.srcFirst)));
        return true;
      }
    }
    return false;
  }

  bool put(uint16_t cid, std::string_view utf8) {
    for (auto& entry : glyphs) {
      if (entry.cid == cid) {
        return entry.utf8.assign(utf8);
      }
    }
    Entry entry;
    entry.cid = cid;
    if (!entry.utf8.assign(utf8)) {
      return false;
    }
    return glyphs.push_back(entry);
  }

  bool putRange(uint16_t srcFirst, uint16_t srcLast, uint16_t dstFirst) {
    Range range;
    range.srcFirst = srcFirst;
    range.srcLast = srcLast;
    range.dstFirst = dstFirst;
    return ranges.push_back(range);
  }
};

static int hexDigitVal(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

static std::string utf16BeHexToUtf8(std::string_view hex) {
  std::string out;
  if (hex.size() < 2 || (hex.size() % 2) != 0) {
    return out;
  }
  const size_t step = (hex.size() % 4) == 0 ? 4 : 2;
  for (size_t i = 0; i + step - 1 < hex.size(); i += step) {
    uint16_t cu = 0;
    for (size_t j = 0; j < step; ++j) {
      const int digit = hexDigitVal(hex[i + j]);
      if (digit < 0) {
        cu = 0;
        break;
      }
      cu = static_cast<uint16_t>((cu << 4) | static_cast<uint16_t>(digit));
    }
    if (cu == 0) {
      continue;
    }
    if (cu >= 0xD800 && cu <= 0xDFFF) {
      continue;
    }
    appendUtf8(out, cu);
  }
  return out;
}

// Parse PDF ToUnicode CMap (beginbfchar / beginbfrange); enough for common Identity-H fonts.
static bool parseToUnicodeCMap(std::string_view cmap, ToUnicodeMap& out) {
  out.clear();
  enum class Mode { None, BfChar, BfRange };
  Mode mode = Mode::None;
  size_t lineStart = 0;
  while (lineStart < cmap.size()) {
    size_t lineEnd = cmap.find('\n', lineStart);
    if (lineEnd == std::string_view::npos) {
      lineEnd = cmap.size();
    }
    std::string_view line = cmap.substr(lineStart, lineEnd - lineStart);
    lineStart = lineEnd + 1;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    size_t p = 0;
    while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) {
      ++p;
    }
    const std::string_view t = line.substr(p);
    if (t.find("beginbfchar") != std::string::npos) {
      mode = Mode::BfChar;
      continue;
    }
    if (t.find("endbfchar") != std::string::npos) {
      mode = Mode::None;
      continue;
    }
    if (t.find("beginbfrange") != std::string::npos) {
      mode = Mode::BfRange;
      continue;
    }
    if (t.find("endbfrange") != std::string::npos) {
      mode = Mode::None;
      continue;
    }
    if (t.find("begincodespacerange") != std::string::npos) {
      continue;
    }
    if (t.find("endcodespacerange") != std::string::npos) {
      continue;
    }
    if (mode == Mode::BfChar) {
      const size_t a = t.find('<');
      if (a == std::string::npos) {
        continue;
      }
      const size_t b = t.find('>', a + 1);
      if (b == std::string::npos) {
        continue;
      }
      const size_t c = t.find('<', b + 1);
      if (c == std::string::npos) {
        continue;
      }
      const size_t d = t.find('>', c + 1);
      if (d == std::string::npos) {
        continue;
      }
      const std::string_view srcHex = t.substr(a + 1, b - a - 1);
      const std::string_view dstHex = t.substr(c + 1, d - c - 1);
      if ((srcHex.size() % 2) != 0 || srcHex.empty() || srcHex.size() > 4) {
        continue;
      }
      uint32_t cid = 0;
      for (const char ch : srcHex) {
        const int digit = hexDigitVal(ch);
        if (digit < 0) {
          cid = 0x10000UL;
          break;
        }
        cid = (cid << 4) | static_cast<uint32_t>(digit);
      }
      if (cid > 0xFFFFUL) {
        continue;
      }
      out.put(static_cast<uint16_t>(cid), utf16BeHexToUtf8(dstHex));
    } else if (mode == Mode::BfRange) {
      const size_t a = t.find('<');
      if (a == std::string::npos) {
        continue;
      }
      const size_t b = t.find('>', a + 1);
      const size_t c = t.find('<', b + 1);
      const size_t d = t.find('>', c + 1);
      const size_t e = t.find('<', d + 1);
      const size_t f = t.find('>', e + 1);
      if (a == std::string::npos || b == std::string::npos || c == std::string::npos || d == std::string::npos ||
          e == std::string::npos || f == std::string::npos) {
        continue;
      }
      const std::string_view h1 = t.substr(a + 1, b - a - 1);
      const std::string_view h2 = t.substr(c + 1, d - c - 1);
      const std::string_view h3 = t.substr(e + 1, f - e - 1);
      if ((h1.size() % 2) != 0 || h1.empty() || h1.size() > 4 || h1.size() != h2.size() || h3.size() != h1.size()) {
        continue;
      }
      auto parseHex = [](std::string_view hex, uint32_t& outValue) {
        outValue = 0;
        for (const char ch : hex) {
          const int digit = hexDigitVal(ch);
          if (digit < 0) {
            return false;
          }
          outValue = (outValue << 4) | static_cast<uint32_t>(digit);
        }
        return true;
      };
      uint32_t v1 = 0;
      uint32_t v2 = 0;
      uint32_t v3 = 0;
      if (!parseHex(h1, v1) || !parseHex(h2, v2) || !parseHex(h3, v3)) {
        continue;
      }
      if (v1 > 0xFFFFUL || v2 > 0xFFFFUL || v3 > 0xFFFFUL || v2 < v1) {
        continue;
      }
      const uint16_t srcFirst = static_cast<uint16_t>(v1);
      const uint16_t srcLast = static_cast<uint16_t>(v2);
      const uint16_t dstFirst = static_cast<uint16_t>(v3);
      if (static_cast<uint32_t>(dstFirst) + static_cast<uint32_t>(srcLast - srcFirst) <= 0xFFFFU) {
        out.putRange(srcFirst, srcLast, dstFirst);
      }
    }
  }
  return !out.empty();
}

class ToUnicodeWorkspaceUse {
 public:
  ToUnicodeWorkspaceUse() : workspace_(PdfScratch::acquireToUnicodeWorkspace()) {}
  ~ToUnicodeWorkspaceUse() {
    if (workspace_) {
      PdfScratch::releaseToUnicodeWorkspaceUse();
    }
  }
  ToUnicodeWorkspaceUse(const ToUnicodeWorkspaceUse&) = delete;
  ToUnicodeWorkspaceUse& operator=(const ToUnicodeWorkspaceUse&) = delete;

  PdfScratch::ToUnicodeWorkspace* get() const { return workspace_; }

 private:
  PdfScratch::ToUnicodeWorkspace* workspace_ = nullptr;
};

template <typename T>
struct MallocScratchDeleter {
  void operator()(T* ptr) const {
    if (!ptr) {
      return;
    }
    ptr->~T();
    std::free(ptr);
  }
};

template <typename T>
std::unique_ptr<T, MallocScratchDeleter<T>> makeContentScratch() {
  void* mem = std::malloc(sizeof(T));
  if (!mem) {
    return nullptr;
  }
  return std::unique_ptr<T, MallocScratchDeleter<T>>(new (mem) T());
}

[[gnu::noinline]] static bool loadToUnicodeMapForFont(FsFile& file, const XrefTable& xref, uint32_t fontObjId,
                                                      ToUnicodeMap& out) {
  out.clear();
  ToUnicodeWorkspaceUse workspaceUse;
  PdfScratch::ToUnicodeWorkspace* workspace = workspaceUse.get();
  if (!workspace) {
    return false;
  }

  if (!xref.readDictForObject(file, fontObjId, workspace->body)) {
    return false;
  }
  const uint32_t tuId = PdfObject::getDictRef("/ToUnicode", workspace->body.view());
  if (tuId == 0) {
    return false;
  }
  workspace->payload.clear();
  workspace->decoded.clear();
  bool flate = false;
  if (!xref.readStreamForObject(file, tuId, workspace->cmapDict, workspace->payload, flate)) {
    return false;
  }
  size_t got = 0;
  if (flate) {
    got = StreamDecoder::flateDecodeBytes(workspace->payload.ptr(), workspace->payload.len, workspace->decoded.ptr(),
                                          workspace->decoded.data.size());
  } else {
    got = std::min(workspace->payload.len, workspace->decoded.data.size());
    std::memcpy(workspace->decoded.ptr(), workspace->payload.ptr(), got);
  }
  if (got == 0) {
    return false;
  }
  const bool parsed =
      parseToUnicodeCMap(std::string_view(reinterpret_cast<const char*>(workspace->decoded.ptr()), got), out);
  return parsed;
}

static std::string bytesToUtf8WithCidMap(const uint8_t* data, size_t len, const ToUnicodeMap* map,
                                         SimpleFontEncoding encoding) {
  if (!map || map->empty()) {
    return pdfBytesToUtf8(data, len, encoding);
  }

  auto countHits = [&](const size_t step) -> size_t {
    if (step == 0 || (step == 2 && (len % 2) != 0)) {
      return 0;
    }
    size_t hits = 0;
    for (size_t i = 0; i + step - 1 < len; i += step) {
      const uint16_t cid =
          step == 1 ? static_cast<uint16_t>(data[i])
                    : static_cast<uint16_t>((static_cast<uint16_t>(data[i]) << 8) | static_cast<uint8_t>(data[i + 1]));
      if (map->has(cid)) {
        ++hits;
      }
    }
    return hits;
  };

  const size_t hits1 = countHits(1);
  const size_t hits2 = countHits(2);
  const size_t step = hits1 > hits2 ? 1u : 2u;
  if (step == 2 && ((len % 2) != 0 || hits2 == 0) && hits1 == 0) {
    return pdfBytesToUtf8(data, len, encoding);
  }

  std::string out;
  out.reserve(len);
  for (size_t i = 0; i + step - 1 < len; i += step) {
    const uint16_t cid =
        step == 1 ? static_cast<uint16_t>(data[i])
                  : static_cast<uint16_t>((static_cast<uint16_t>(data[i]) << 8) | static_cast<uint8_t>(data[i + 1]));
    if (map->appendMapped(cid, out)) {
    } else {
      out += pdfBytesToUtf8(data + i, step, encoding);
    }
  }
  return out;
}

void skipWsComment(char*& p, char* end) {
  while (p < end) {
    if (*p == '%') {
      while (p < end && *p != '\r' && *p != '\n') ++p;
      continue;
    }
    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      ++p;
      continue;
    }
    break;
  }
}

template <size_t N>
bool readPdfStringLiteralFs(char*& p, char* end, PdfFixedString<N>& rawBytes) {
  rawBytes.clear();
  if (p >= end || *p != '(') return false;
  ++p;
  int depth = 1;
  while (p < end && depth > 0) {
    if (*p == '\\') {
      ++p;
      if (p >= end) break;
      char c = *p++;
      if (c >= '0' && c <= '7') {
        int oct = c - '0';
        int count = 1;
        while (count < 3 && p < end && *p >= '0' && *p <= '7') {
          oct = (oct << 3) | (*p++ - '0');
          ++count;
        }
        rawBytes.append(static_cast<char>(oct & 0xFF));
        continue;
      }
      char out = c;
      if (c == 'n')
        out = '\n';
      else if (c == 'r')
        out = '\r';
      else if (c == 't')
        out = '\t';
      else if (c == 'b')
        out = '\b';
      else if (c == 'f')
        out = '\f';
      rawBytes.append(out);
      continue;
    }
    if (*p == '(') {
      ++depth;
      rawBytes.append(*p++);
      continue;
    }
    if (*p == ')') {
      --depth;
      if (depth == 0) {
        ++p;
        break;
      }
      rawBytes.append(*p++);
      continue;
    }
    rawBytes.append(*p++);
  }
  return true;
}

template <size_t N>
bool readHexStringFs(char*& p, char* end, PdfFixedString<N>& rawBytes) {
  rawBytes.clear();
  if (p >= end || *p != '<') return false;
  ++p;
  uint8_t acc = 0;
  bool haveNibble = false;
  while (p < end) {
    if (*p == '>') {
      ++p;
      if (haveNibble) rawBytes.append(static_cast<char>(acc));
      return true;
    }
    if (std::isspace(static_cast<unsigned char>(*p))) {
      ++p;
      continue;
    }
    int v = -1;
    if (*p >= '0' && *p <= '9')
      v = *p - '0';
    else if (*p >= 'A' && *p <= 'F')
      v = *p - 'A' + 10;
    else if (*p >= 'a' && *p <= 'f')
      v = *p - 'a' + 10;
    if (v < 0) return false;
    if (!haveNibble) {
      acc = static_cast<uint8_t>(v << 4);
      haveNibble = true;
    } else {
      acc |= static_cast<uint8_t>(v);
      rawBytes.append(static_cast<char>(acc));
      haveNibble = false;
    }
    ++p;
  }
  return false;
}

template <size_t N>
bool readNumberFs(char*& p, char* end, PdfFixedString<N>& out) {
  out.clear();
  if (p >= end) return false;
  if (*p == '+' || *p == '-') out.append(*p++);
  bool sawDigit = false;
  while (p < end && *p >= '0' && *p <= '9') {
    sawDigit = true;
    out.append(*p++);
  }
  if (p < end && *p == '.') {
    out.append(*p++);
    while (p < end && *p >= '0' && *p <= '9') {
      sawDigit = true;
      out.append(*p++);
    }
  }
  return sawDigit || !out.empty();
}

template <size_t N>
bool readNameFs(char*& p, char* end, PdfFixedString<N>& out) {
  out.clear();
  if (p >= end || *p != '/') return false;
  ++p;
  while (p < end) {
    char c = *p;
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '[' || c == ']' || c == '<' || c == '(' ||
        c == '>' || c == '%') {
      break;
    }
    if (c == '#') {
      if (p + 2 >= end) break;
      char h1 = p[1];
      char h2 = p[2];
      auto hexv = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        return -1;
      };
      const int a = hexv(h1);
      const int b = hexv(h2);
      if (a >= 0 && b >= 0) {
        out.append(static_cast<char>((a << 4) | b));
        p += 3;
        continue;
      }
    }
    out.append(c);
    ++p;
  }
  return !out.empty();
}

template <size_t N>
bool readOperatorFs(char*& p, char* end, PdfFixedString<N>& out) {
  out.clear();
  while (p < end) {
    char c = *p;
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '*' || c == '"' || c == '\'') {
      out.append(c);
      ++p;
    } else {
      break;
    }
  }
  return !out.empty();
}

bool skipPdfStringLiteral(char*& p, char* end) {
  if (p >= end || *p != '(') return false;
  ++p;
  int depth = 1;
  while (p < end && depth > 0) {
    if (*p == '\\' && p + 1 < end) {
      p += 2;
      continue;
    }
    if (*p == '(') {
      ++depth;
      ++p;
      continue;
    }
    if (*p == ')') {
      --depth;
      ++p;
      continue;
    }
    ++p;
  }
  return depth == 0;
}

bool skipHexString(char*& p, char* end) {
  if (p >= end || *p != '<') return false;
  ++p;
  while (p < end) {
    if (*p == '>') {
      ++p;
      return true;
    }
    ++p;
  }
  return false;
}

// Capture a PDF array span including nested `[` `]`; skips string literals and comments inside.
bool readArraySpan(char*& p, char* end, char*& outStart, char*& outEnd) {
  if (p >= end || *p != '[') return false;
  int depth = 1;
  char* start = p;
  ++p;
  while (p < end && depth > 0) {
    if (*p == '%') {
      while (p < end && *p != '\r' && *p != '\n') ++p;
      continue;
    }
    if (*p == '\\' && p + 1 < end) {
      p += 2;
      continue;
    }
    if (*p == '(') {
      if (!skipPdfStringLiteral(p, end)) return false;
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] != '<') {
      if (!skipHexString(p, end)) return false;
      continue;
    }
    if (*p == '[') {
      ++depth;
      ++p;
      continue;
    }
    if (*p == ']') {
      --depth;
      ++p;
      continue;
    }
    ++p;
  }
  outStart = start;
  outEnd = p;
  return depth == 0;
}

// Skip `<<` ... `>>` inline dictionary (marked-content properties, etc.).
bool skipInlineDictionary(char*& p, char* end) {
  if (p + 1 >= end || p[0] != '<' || p[1] != '<') return false;
  p += 2;
  int depth = 1;
  while (p < end && depth > 0) {
    if (*p == '%') {
      while (p < end && *p != '\r' && *p != '\n') ++p;
      continue;
    }
    if (p + 1 < end && p[0] == '<' && p[1] == '<') {
      depth++;
      p += 2;
      continue;
    }
    if (p + 1 < end && p[0] == '>' && p[1] == '>') {
      depth--;
      p += 2;
      continue;
    }
    if (*p == '(') {
      if (!skipPdfStringLiteral(p, end)) return false;
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] != '<') {
      if (!skipHexString(p, end)) return false;
      continue;
    }
    if (*p == '[') {
      char* arrayStart = nullptr;
      char* arrayEnd = nullptr;
      if (!readArraySpan(p, end, arrayStart, arrayEnd)) return false;
      continue;
    }
    ++p;
  }
  return depth == 0;
}

// Operand count for graphics/text state operators (PDF content stream).
int popOperandsForOperator(std::string_view op) {
  if (op == "cm" || op == "c") return 6;
  if (op == "re") return 4;
  if (op == "v" || op == "y") return 4;
  if (op == "m" || op == "l") return 2;
  if (op == "w" || op == "J" || op == "j" || op == "M" || op == "ri" || op == "i" || op == "gs") return 1;
  if (op == "Tc" || op == "Tw" || op == "Tz" || op == "TL" || op == "Tr" || op == "Ts") return 1;
  if (op == "d") return 2;
  if (op == "rg" || op == "RG") return 3;
  if (op == "g" || op == "G") return 1;
  return 0;
}

float toFloat(const char* s) { return static_cast<float>(std::strtod(s, nullptr)); }

[[gnu::noinline]] bool resolveResourcesDict(FsFile& file, const XrefTable& xref, std::string_view pageBody,
                                            PdfFixedString<PDF_OBJECT_BODY_MAX>& out) {
  out.clear();
  PdfFixedString<PDF_DICT_VALUE_MAX> r;
  auto body = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  if (!body) {
    return false;
  }
  std::string_view currentBody = pageBody;
  for (int depth = 0; depth < 16; ++depth) {
    if (PdfObject::getDictValue("/Resources", currentBody, r)) {
      while (!r.empty() && (r[0] == ' ' || r[0] == '\t' || r[0] == '\r' || r[0] == '\n')) r.erase_prefix(1);
      if (r.empty()) return false;
      if (r.size() >= 2 && r[0] == '<' && r[1] == '<') return out.assign(r.view());
      const uint32_t rid = PdfObject::getDictRef("/Resources", currentBody);
      if (rid == 0) return out.assign(r.view());
      if (!xref.readDictForObject(file, rid, *body)) return out.assign(r.view());
      return out.assign(body->view());
    }

    const uint32_t parentId = PdfObject::getDictRef("/Parent", currentBody);
    if (parentId == 0 || !xref.readDictForObject(file, parentId, *body)) {
      break;
    }
    currentBody = body->view();
  }
  return false;
}

[[gnu::noinline]] bool resolveFontDict(FsFile& file, const XrefTable& xref, std::string_view pageBody,
                                       PdfFixedString<PDF_OBJECT_BODY_MAX>& out) {
  out.clear();
  auto resBody = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  if (!resBody || !resolveResourcesDict(file, xref, pageBody, *resBody)) {
    return false;
  }
  PdfFixedString<PDF_DICT_VALUE_MAX> r;
  if (!PdfObject::getDictValue("/Font", resBody->view(), r)) {
    return false;
  }
  while (!r.empty() && (r[0] == ' ' || r[0] == '\t' || r[0] == '\r' || r[0] == '\n')) r.erase_prefix(1);
  if (r.empty()) {
    return false;
  }
  if (r.size() >= 2 && r[0] == '<' && r[1] == '<') {
    return out.assign(r.view());
  }
  const uint32_t rid = PdfObject::getDictRef("/Font", resBody->view());
  if (rid == 0) {
    return out.assign(r.view());
  }
  auto body = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  if (!body || !xref.readDictForObject(file, rid, *body)) {
    return out.assign(r.view());
  }
  return out.assign(body->view());
}

uint32_t resourceObjectIdForName(std::string_view dict, std::string_view name) {
  if (name.empty()) {
    return 0;
  }
  size_t pos = 0;
  while (pos < dict.size()) {
    pos = dict.find('/', pos);
    if (pos == std::string_view::npos) {
      break;
    }
    const size_t keyStart = pos + 1;
    if (keyStart + name.size() > dict.size() || dict.substr(keyStart, name.size()) != name) {
      ++pos;
      continue;
    }
    const size_t afterKey = keyStart + name.size();
    if (afterKey < dict.size()) {
      const unsigned char c = static_cast<unsigned char>(dict[afterKey]);
      if (!std::isspace(c) && c != '>' && c != '[' && c != '<' && c != '/') {
        pos = afterKey;
        continue;
      }
    }
    size_t v = afterKey;
    while (v < dict.size() && (dict[v] == ' ' || dict[v] == '\t')) {
      ++v;
    }
    char* end = nullptr;
    const unsigned long id = std::strtoul(dict.data() + v, &end, 10);
    if (end == dict.data() + v) {
      return 0;
    }
    return static_cast<uint32_t>(id);
  }
  return 0;
}

uint32_t fontObjectIdForName(std::string_view fontDict, std::string_view name) {
  return resourceObjectIdForName(fontDict, name);
}

[[gnu::noinline]] std::string getXObjectDict(std::string_view resourcesBody) {
  PdfFixedString<PDF_DICT_VALUE_MAX> out;
  if (!PdfObject::getDictValue("/XObject", resourcesBody, out)) {
    return {};
  }
  return std::string(out.view());
}

uint8_t fontStyleFromDict(std::string_view fontDictBody);

struct FontInfo {
  uint8_t style = PdfTextStyleRegular;
  SimpleFontEncoding encoding = SimpleFontEncoding::WinAnsi;
  ToUnicodeMap map;
  bool hasMap = false;
};

template <size_t N>
class NameIdCache {
  struct Entry {
    bool used = false;
    PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> name;
    uint32_t id = 0;
  };

  std::array<Entry, N> entries_{};

 public:
  bool find(std::string_view name, uint32_t& outId) const {
    for (const auto& entry : entries_) {
      if (entry.used && entry.name.view() == name) {
        outId = entry.id;
        return true;
      }
    }
    return false;
  }

  void put(std::string_view name, uint32_t id) {
    for (auto& entry : entries_) {
      if (entry.used && entry.name.view() == name) {
        entry.id = id;
        return;
      }
    }
    for (auto& entry : entries_) {
      if (!entry.used) {
        if (entry.name.assign(name)) {
          entry.used = true;
          entry.id = id;
        }
        return;
      }
    }
  }
};

template <size_t N>
class FontInfoCache {
  struct Entry {
    bool used = false;
    uint32_t fontId = 0;
    FontInfo info;
  };

  std::array<Entry, N> entries_{};

 public:
  FontInfo* find(uint32_t fontId) {
    for (auto& entry : entries_) {
      if (entry.used && entry.fontId == fontId) {
        return &entry.info;
      }
    }
    return nullptr;
  }

  FontInfo* put(uint32_t fontId, FontInfo&& info) {
    for (auto& entry : entries_) {
      if (entry.used && entry.fontId == fontId) {
        entry.info = std::move(info);
        return &entry.info;
      }
    }
    for (auto& entry : entries_) {
      if (!entry.used) {
        entry.used = true;
        entry.fontId = fontId;
        entry.info = std::move(info);
        return &entry.info;
      }
    }
    return nullptr;
  }
};

[[gnu::noinline]] static void updateCurrentCidMapForFont(
    FsFile& file, const XrefTable& xref, std::string_view fontDictBody, std::string_view fontName,
    uint8_t& currentFontStyle, FontInfoCache<PDF_MAX_FONT_CID_MAPS>& fontInfoCache, FontInfo& uncachedFontInfo,
    PdfFixedString<PDF_OBJECT_BODY_MAX>& fontBodyScratch, const ToUnicodeMap*& currentCidMap,
    SimpleFontEncoding& currentSimpleEncoding) {
  const uint32_t fid = fontObjectIdForName(fontDictBody, fontName);
  currentCidMap = nullptr;
  currentFontStyle = PdfTextStyleRegular;
  currentSimpleEncoding = SimpleFontEncoding::WinAnsi;
  if (fid == 0) {
    return;
  }

  fontBodyScratch.clear();
  if (!xref.readDictForObject(file, fid, fontBodyScratch)) {
    return;
  }

  const std::string_view fbodyView = fontBodyScratch.view();
  const bool usesMacRoman = (fbodyView.find("/MacRomanEncoding") != std::string_view::npos);
  const uint8_t dictStyle = fontStyleFromDict(fbodyView);
  currentFontStyle = dictStyle;
  if (usesMacRoman) {
    currentSimpleEncoding = SimpleFontEncoding::MacRoman;
  }
  FontInfo* cachedInfo = fontInfoCache.find(fid);
  if (!cachedInfo) {
    FontInfo info;
    if (loadToUnicodeMapForFont(file, xref, fid, info.map) && !info.map.empty()) {
      info.hasMap = true;
    }
    info.style = dictStyle;
    if (usesMacRoman) {
      info.encoding = SimpleFontEncoding::MacRoman;
    }
    cachedInfo = fontInfoCache.put(fid, std::move(info));
    if (!cachedInfo) {
      uncachedFontInfo = std::move(info);
      cachedInfo = &uncachedFontInfo;
    }
  }
  if (cachedInfo) {
    currentFontStyle = cachedInfo->style;
    currentSimpleEncoding = cachedInfo->encoding;
    if (cachedInfo->hasMap) {
      currentCidMap = &cachedInfo->map;
    }
  }
}

uint32_t xobjectIdForName(std::string_view xobjDict, std::string_view name) {
  return resourceObjectIdForName(xobjDict, name);
}

uint8_t fontStyleFromDict(std::string_view fontDictBody) {
  PdfFixedString<PDF_DICT_VALUE_MAX> fontName;
  if (!PdfObject::getDictValue("/BaseFont", fontDictBody, fontName) &&
      !PdfObject::getDictValue("/FontName", fontDictBody, fontName)) {
    return PdfTextStyleRegular;
  }
  const std::string_view name = fontName.view();
  uint8_t style = PdfTextStyleRegular;
  if (name.find("Bold") != std::string_view::npos || name.find("bold") != std::string_view::npos) {
    style = static_cast<uint8_t>(style | PdfTextStyleBold);
  }
  if (name.find("Italic") != std::string_view::npos || name.find("italic") != std::string_view::npos ||
      name.find("Oblique") != std::string_view::npos || name.find("oblique") != std::string_view::npos) {
    style = static_cast<uint8_t>(style | PdfTextStyleItalic);
  }
  return style;
}

bool fillImageDescriptor(FsFile& file, const XrefTable& xref, uint32_t objId, PdfImageDescriptor& out) {
  const uint32_t off = xref.getOffset(objId);
  if (off == 0) {
    return false;
  }
  auto body = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  uint32_t so = 0;
  uint32_t sl = 0;
  if (!body || !PdfObject::readAt(file, off, *body, &so, &sl, &xref)) {
    return false;
  }
  PdfFixedString<PDF_DICT_VALUE_MAX> st;
  if (!PdfObject::getDictValue("/Subtype", body->view(), st)) return false;
  while (!st.empty() && (st[0] == ' ' || st[0] == '\t')) st.erase_prefix(1);
  while (!st.empty() && (st[st.size() - 1] == ' ' || st[st.size() - 1] == '\t')) st.resize(st.size() - 1);
  if (st.view() != "/Image") return false;

  out.pdfStreamOffset = so;
  out.pdfStreamLength = sl;
  out.width = static_cast<uint16_t>(PdfObject::getDictInt("/Width", body->view(), 0));
  out.height = static_cast<uint16_t>(PdfObject::getDictInt("/Height", body->view(), 0));
  if (body->view().find("/DCTDecode") != std::string_view::npos ||
      body->view().find("/DCT") != std::string_view::npos) {
    out.format = 0;
  } else {
    out.format = 1;
  }
  return out.pdfStreamLength > 0 && out.width > 0 && out.height > 0;
}

struct TmpRun {
  float x = 0;
  float y = 0;
  float endX = 0;
  std::string utf8;
  uint8_t style = PdfTextStyleRegular;
  uint16_t fontSize = 0;
  uint32_t seq = 0;
};

constexpr size_t kMaxExtractedTextBytes = PDF_MAX_TEXT_BLOCK_BYTES - 1;

static void trimUtf8ToByteLimit(std::string& s, size_t maxBytes = kMaxExtractedTextBytes) {
  if (s.size() <= maxBytes) {
    return;
  }
  size_t n = maxBytes;
  while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0U) == 0x80U) {
    --n;
  }
  if (n == 0) {
    n = maxBytes;
  }
  s.resize(n);
}

static void appendBounded(std::string& dst, std::string_view src, size_t maxBytes = kMaxExtractedTextBytes) {
  if (dst.size() >= maxBytes || src.empty()) {
    return;
  }
  const size_t take = std::min(src.size(), maxBytes - dst.size());
  dst.append(src.data(), take);
  trimUtf8ToByteLimit(dst, maxBytes);
}

static void appendBounded(std::string& dst, char c, size_t maxBytes = kMaxExtractedTextBytes) {
  if (dst.size() < maxBytes) {
    dst.push_back(c);
  }
}

void logTmpRunPushDiagnostics(const TmpRun& run, size_t runCount) {
#if defined(ENABLE_SERIAL_LOG)
  logSerial.print("[ERR] [PDF] TmpRun push size/text/font/seq=");
  logSerial.print(static_cast<uint32_t>(runCount));
  logSerial.print(' ');
  logSerial.print(static_cast<uint32_t>(run.utf8.size()));
  logSerial.print(' ');
  logSerial.print(static_cast<uint32_t>(run.fontSize));
  logSerial.print(' ');
  logSerial.println(run.seq);

  logSerial.print("[ERR] [PDF] TmpRun push xy/endX=");
  logSerial.print(run.x);
  logSerial.print(' ');
  logSerial.print(run.y);
  logSerial.print(' ');
  logSerial.println(run.endX);
#else
  (void)run;
  (void)runCount;
#endif
}

struct PdfMatrix {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float e = 0.0f;
  float f = 0.0f;
};

static PdfMatrix multiplyMatrix(const PdfMatrix& lhs, const PdfMatrix& rhs) {
  PdfMatrix out;
  out.a = lhs.a * rhs.a + lhs.c * rhs.b;
  out.b = lhs.b * rhs.a + lhs.d * rhs.b;
  out.c = lhs.a * rhs.c + lhs.c * rhs.d;
  out.d = lhs.b * rhs.c + lhs.d * rhs.d;
  out.e = lhs.a * rhs.e + lhs.c * rhs.f + lhs.e;
  out.f = lhs.b * rhs.e + lhs.d * rhs.f + lhs.f;
  return out;
}

static void transformPoint(const PdfMatrix& m, float x, float y, float& outX, float& outY) {
  outX = m.a * x + m.c * y + m.e;
  outY = m.b * x + m.d * y + m.f;
}

// UTF-8 code unit length starting at s[i] (1–4), or 1 if invalid/truncated.
static size_t utf8CodepointByteLen(const std::string& s, size_t i) {
  if (i >= s.size()) {
    return 0;
  }
  const unsigned char b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    return 1;
  }
  if ((b0 & 0xE0) == 0xC0) {
    return i + 1 < s.size() ? 2 : 1;
  }
  if ((b0 & 0xF0) == 0xE0) {
    return i + 2 < s.size() ? 3 : 1;
  }
  if ((b0 & 0xF8) == 0xF0) {
    return i + 3 < s.size() ? 4 : 1;
  }
  return 1;
}

// Decode one UTF-8 codepoint at i; returns bytes consumed (0 on error).
static size_t utf8DecodeAt(const std::string& s, size_t i, uint32_t* cpOut) {
  if (i >= s.size()) {
    return 0;
  }
  const unsigned char b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    *cpOut = b0;
    return 1;
  }
  if ((b0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
    const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
    if ((b1 & 0xC0) != 0x80) {
      return 0;
    }
    *cpOut = ((b0 & 0x1FU) << 6) | (b1 & 0x3FU);
    return 2;
  }
  if ((b0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
    const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
    const unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
      return 0;
    }
    *cpOut = ((b0 & 0x0FU) << 12) | ((b1 & 0x3FU) << 6) | (b2 & 0x3FU);
    return 3;
  }
  if ((b0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
    const unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
    const unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
    const unsigned char b3 = static_cast<unsigned char>(s[i + 3]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
      return 0;
    }
    *cpOut = ((b0 & 0x07U) << 18) | ((b1 & 0x3FU) << 12) | ((b2 & 0x3FU) << 6) | (b3 & 0x3FU);
    return 4;
  }
  return 0;
}

// Drop C0 controls (except tab/LF/CR), combining marks, and variation selectors — common PDF noise.
static bool keepExtractedCodepoint(uint32_t cp) {
  if (cp < 0x20U) {
    return cp == 0x09U || cp == 0x0AU || cp == 0x0DU;
  }
  if (cp >= 0x300U && cp <= 0x36FU) {
    return false;
  }
  if (cp >= 0xFE00U && cp <= 0xFE0FU) {
    return false;
  }
  return true;
}

static void stripPdfExtractedNoiseUtf8(std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = 0;
    const size_t n = utf8DecodeAt(s, i, &cp);
    if (n == 0) {
      out.push_back(s[i]);
      ++i;
      continue;
    }
    if (keepExtractedCodepoint(cp)) {
      for (size_t j = 0; j < n; ++j) {
        out.push_back(s[i + j]);
      }
    }
    i += n;
  }
  s = std::move(out);
}

static bool startsWithAt(const std::string& s, size_t pos, const char* needle) {
  const size_t len = std::strlen(needle);
  return pos + len <= s.size() && std::memcmp(s.data() + pos, needle, len) == 0;
}

static void appendDecomposedLigature(std::string& out, uint32_t cp) {
  switch (cp) {
    case 0xFB00:
      out += "ff";
      return;
    case 0xFB01:
      out += "fi";
      return;
    case 0xFB02:
      out += "fl";
      return;
    case 0xFB03:
      out += "ffi";
      return;
    case 0xFB04:
      out += "ffl";
      return;
    default:
      appendUtf8(out, cp);
      return;
  }
}

// Collapse ASCII + common PDF Unicode spaces to a single ASCII space; copy other UTF-8 as-is.
static void normalizePdfText(std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pendingSpace = false;
  for (size_t i = 0; i < s.size();) {
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    // U+00A0 NO-BREAK SPACE (WinAnsi 0xA0 → UTF-8) — looks empty in plain terminals
    if (b0 == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      pendingSpace = true;
      i += 2;
      continue;
    }
    if (b0 == ' ' || b0 == '\t' || b0 == '\r' || b0 == '\n') {
      pendingSpace = true;
      i += 1;
      continue;
    }
    if (pendingSpace && !out.empty()) {
      out.push_back(' ');
    }
    pendingSpace = false;
    uint32_t cp = 0;
    const size_t n = utf8DecodeAt(s, i, &cp);
    if (n > 0) {
      appendDecomposedLigature(out, cp);
      i += n;
      continue;
    }
    const size_t fallback = utf8CodepointByteLen(s, i);
    for (size_t j = 0; j < fallback && i + j < s.size(); ++j) {
      out.push_back(s[i + j]);
    }
    i += fallback;
  }
  s = std::move(out);
  stripPdfExtractedNoiseUtf8(s);
}

static bool isAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static void repairCommonPdfTextSpacing(std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (!out.empty() && out.back() == ' ' &&
        (startsWithAt(s, i, "\302\256") || startsWithAt(s, i, "\342\204\242") || startsWithAt(s, i, "\302\251"))) {
      out.pop_back();
    }
    if (i + 3 < s.size() && s[i] == ' ' && s[i + 1] == 's' && s[i + 2] == ' ' && !out.empty() &&
        isAsciiLetter(out.back()) && isAsciiLetter(s[i + 3])) {
      out += "\342\200\231s";
      i += 2;
      continue;
    }
    out.push_back(s[i]);
    ++i;
  }
  s = std::move(out);
}

static float estimateTextAdvance(const std::string& utf8, uint16_t fontSize) {
  size_t glyphs = 0;
  for (size_t i = 0; i < utf8.size();) {
    const size_t n = utf8CodepointByteLen(utf8, i);
    i += n == 0 ? 1 : n;
    ++glyphs;
  }
  return static_cast<float>(glyphs) * static_cast<float>(fontSize == 0 ? 10 : fontSize) * 0.60f;
}

static float textAdjustmentToUserSpace(float adjustment, uint16_t fontSize) {
  return (-adjustment * static_cast<float>(fontSize == 0 ? 10 : fontSize)) / 1000.0f;
}

static float sameLineThreshold(uint16_t a, uint16_t b) {
  const uint16_t maxFont = std::max<uint16_t>(a == 0 ? 10 : a, b == 0 ? 10 : b);
  return std::max(4.0f, static_cast<float>(maxFont) * 0.45f);
}

bool tryMergeTmpRun(PdfFixedVector<TmpRun, PDF_MAX_TMP_RUNS>& runs, const TmpRun& run) {
  if (runs.empty()) {
    return false;
  }

  TmpRun& last = runs.back();
  if (std::fabs(run.y - last.y) >= sameLineThreshold(last.fontSize, run.fontSize)) {
    return false;
  }

  const float gap = run.x - last.endX;
  const float spaceGap = std::max(1.5f, static_cast<float>(std::max<uint16_t>(last.fontSize, run.fontSize)) * 0.18f);
  if (gap > spaceGap && !last.utf8.empty() && last.utf8.back() != ' ') {
    appendBounded(last.utf8, ' ');
  }
  appendBounded(last.utf8, run.utf8);
  last.style = static_cast<uint8_t>(last.style | run.style);
  last.fontSize = std::max(last.fontSize, run.fontSize);
  last.endX = std::max(last.endX, run.endX);
  return true;
}

struct BlockPlacementState {
  bool havePrev = false;
  float prevY = 0.0f;
  float prevEndX = 0.0f;
  uint32_t prevHint = 0;
  uint16_t prevFontSize = 0;
};

void flushTextGroup(PdfFixedVector<TmpRun, PDF_MAX_TMP_RUNS>& runs, PdfPage& page, uint32_t& blockCounter,
                    BlockPlacementState& placement) {
  if (runs.empty()) return;
  auto pushBlock = [&](std::string& block, float y, float endX, uint16_t maxFontSize, uint8_t styleBits) {
    trimUtf8ToByteLimit(block);
    normalizePdfText(block);
    trimUtf8ToByteLimit(block);
    repairCommonPdfTextSpacing(block);
    trimUtf8ToByteLimit(block);
    if (block.empty()) {
      return;
    }
    if (block.size() <= 120) {
      const bool strongStyle = (styleBits & (PdfTextStyleBold | PdfTextStyleItalic)) != 0;
      const bool bigText = maxFontSize >= 14;
      if (bigText || (strongStyle && blockCounter < 3) || (blockCounter == 0 && block.size() <= 80)) {
        styleBits = static_cast<uint8_t>(styleBits | PdfTextStyleHeader);
      }
    }
    PdfTextBlock tb;
    if (!tb.text.assign(block)) {
      return;
    }
    tb.style = styleBits;
    uint32_t hint = static_cast<uint32_t>(std::lround(y * 100.0f));
    if (placement.havePrev && std::fabs(y - placement.prevY) < sameLineThreshold(placement.prevFontSize, maxFontSize)) {
      hint = placement.prevHint;
    }
    tb.orderHint = hint;
    page.textBlocks.push_back(std::move(tb));
    page.drawOrder.push_back({false, static_cast<uint32_t>(page.textBlocks.size() - 1)});
    ++blockCounter;
    placement.havePrev = true;
    placement.prevY = y;
    placement.prevEndX = endX;
    placement.prevHint = hint;
    placement.prevFontSize = maxFontSize;
  };

  std::string block = runs[0].utf8;
  trimUtf8ToByteLimit(block);
  float lineY = runs[0].y;
  uint16_t maxFontSize = runs[0].fontSize;
  uint8_t styleBits = runs[0].style;
  float blockEndX = runs[0].endX;
  for (size_t i = 1; i < runs.size(); ++i) {
    if (std::fabs(runs[i].y - runs[i - 1].y) < sameLineThreshold(runs[i - 1].fontSize, runs[i].fontSize)) {
      const float gap = runs[i].x - blockEndX;
      const float spaceGap =
          std::max(1.5f, static_cast<float>(std::max<uint16_t>(runs[i - 1].fontSize, runs[i].fontSize)) * 0.18f);
      if (gap > spaceGap && !block.empty() && block.back() != ' ') {
        appendBounded(block, ' ');
      }
      appendBounded(block, runs[i].utf8);
      styleBits = static_cast<uint8_t>(styleBits | runs[i].style);
      maxFontSize = std::max(maxFontSize, runs[i].fontSize);
      blockEndX = std::max(blockEndX, runs[i].endX);
    } else {
      pushBlock(block, lineY, blockEndX, maxFontSize, styleBits);
      block = runs[i].utf8;
      trimUtf8ToByteLimit(block);
      lineY = runs[i].y;
      maxFontSize = runs[i].fontSize;
      styleBits = runs[i].style;
      blockEndX = runs[i].endX;
    }
  }
  pushBlock(block, lineY, blockEndX, maxFontSize, styleBits);
  runs.clear();
}

enum class OpTokenKind : uint8_t {
  Empty,
  Bytes,
  Name,
  Number,
  ArraySpan,
};

struct OpToken {
  OpTokenKind kind = OpTokenKind::Empty;
  PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> bytes;
  float number = 0;
  char* spanStart = nullptr;
  char* spanEnd = nullptr;
};

class OpStack {
  std::array<OpToken, PDF_MAX_OP_STACK> items_{};
  size_t count_ = 0;

  OpToken& appendSlot() {
    if (count_ < items_.size()) {
      return items_[count_++];
    }
    for (size_t i = 1; i < items_.size(); ++i) {
      items_[i - 1] = items_[i];
    }
    return items_[items_.size() - 1];
  }

 public:
  void clear() { count_ = 0; }
  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  OpToken* back() { return count_ == 0 ? nullptr : &items_[count_ - 1]; }

  void pop() {
    if (count_ > 0) {
      --count_;
      items_[count_] = OpToken{};
    }
  }

  bool pushBytes(const PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES>& bytes) {
    OpToken& token = appendSlot();
    token = OpToken{};
    token.kind = OpTokenKind::Bytes;
    return token.bytes.assign(bytes.view());
  }

  bool pushName(const PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES>& name) {
    OpToken& token = appendSlot();
    token = OpToken{};
    token.kind = OpTokenKind::Name;
    return token.bytes.assign(name.view());
  }

  bool pushNumber(float number) {
    OpToken& token = appendSlot();
    token = OpToken{};
    token.kind = OpTokenKind::Number;
    token.number = number;
    return true;
  }

  bool pushArray(char* start, char* end) {
    OpToken& token = appendSlot();
    token = OpToken{};
    token.kind = OpTokenKind::ArraySpan;
    token.spanStart = start;
    token.spanEnd = end;
    return true;
  }

  bool pushEmpty() {
    appendSlot() = OpToken{};
    return true;
  }

  bool popNumber(float& out) {
    OpToken* token = back();
    if (!token) {
      return false;
    }
    if (token->kind == OpTokenKind::Number) {
      out = token->number;
    } else {
      out = toFloat(token->bytes.c_str());
    }
    pop();
    return true;
  }
};

bool runContentOperators(char* p, char* end, FsFile& file, const XrefTable& xref, std::string_view pageObjectBody,
                         PdfPage& outPage) {
  auto resBodyStorage = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  const std::string_view resBody = (resBodyStorage && resolveResourcesDict(file, xref, pageObjectBody, *resBodyStorage))
                                       ? resBodyStorage->view()
                                       : std::string_view{};
  const std::string xobjDict = getXObjectDict(resBody);
  auto fontDictBodyStorage = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  const std::string_view fontDictBody =
      (fontDictBodyStorage && resolveFontDict(file, xref, pageObjectBody, *fontDictBodyStorage))
          ? fontDictBodyStorage->view()
          : std::string_view{};
  auto fontInfoCacheStorage = makeContentScratch<FontInfoCache<PDF_MAX_FONT_CID_MAPS>>();
  auto uncachedFontInfoStorage = makeContentScratch<FontInfo>();
  auto fontNameCacheStorage = makeContentScratch<NameIdCache<PDF_MAX_OP_STACK>>();
  auto xobjNameCacheStorage = makeContentScratch<NameIdCache<PDF_MAX_OP_STACK>>();
  auto ctmStackStorage = makeContentScratch<std::array<PdfMatrix, PDF_MAX_OP_STACK>>();
  auto runsStorage = makeContentScratch<PdfFixedVector<TmpRun, PDF_MAX_TMP_RUNS>>();
  auto opStackStorage = makeContentScratch<OpStack>();
  auto fontBodyScratchStorage = makeContentScratch<PdfFixedString<PDF_OBJECT_BODY_MAX>>();
  if (!resBodyStorage || !fontDictBodyStorage || !fontInfoCacheStorage || !uncachedFontInfoStorage ||
      !fontNameCacheStorage || !xobjNameCacheStorage || !ctmStackStorage || !runsStorage || !opStackStorage ||
      !fontBodyScratchStorage) {
    return false;
  }
  auto& fontInfoCache = *fontInfoCacheStorage;
  auto& uncachedFontInfo = *uncachedFontInfoStorage;
  auto& fontNameCache = *fontNameCacheStorage;
  auto& xobjNameCache = *xobjNameCacheStorage;
  auto& ctmStack = *ctmStackStorage;
  auto& runs = *runsStorage;
  auto& stack = *opStackStorage;
  auto& fontBodyScratch = *fontBodyScratchStorage;
  const ToUnicodeMap* currentCidMap = nullptr;
  SimpleFontEncoding currentSimpleFontEncoding = SimpleFontEncoding::WinAnsi;
  PdfMatrix currentCtm;
  size_t ctmStackSize = 0;

  float textX = 0;
  float textY = 0;
  float lineSpacing = 0;
  uint8_t currentFontStyle = PdfTextStyleRegular;
  uint16_t currentFontSize = 0;
  uint32_t textBlockCounter = 0;
  uint32_t seqCounter = 0;
  BlockPlacementState placement;
  auto emitRun = [&](TmpRun&& run) {
    if (run.utf8.empty()) {
      return;
    }
    if (tryMergeTmpRun(runs, run)) {
      return;
    }
    if (runs.size() >= PDF_MAX_TMP_RUNS) {
      flushTextGroup(runs, outPage, textBlockCounter, placement);
    }
    if (!runs.push_back(std::move(run))) {
      logTmpRunPushDiagnostics(run, runs.size());
    }
  };

  while (p < end) {
    skipWsComment(p, end);
    if (p >= end) break;

    if (*p == '(') {
      PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> raw;
      if (!readPdfStringLiteralFs(p, end, raw)) break;
      stack.pushBytes(raw);
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] == '<') {
      if (!skipInlineDictionary(p, end)) break;
      stack.pushEmpty();
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] != '<') {
      PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> raw;
      if (!readHexStringFs(p, end, raw)) break;
      stack.pushBytes(raw);
      continue;
    }
    if (*p == '[') {
      char* arrStart = nullptr;
      char* arrEnd = nullptr;
      if (!readArraySpan(p, end, arrStart, arrEnd)) break;
      stack.pushArray(arrStart, arrEnd);
      continue;
    }
    if (*p == '/' && (p + 1 >= end || p[1] != '/')) {
      PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> name;
      if (!readNameFs(p, end, name)) break;
      stack.pushName(name);
      continue;
    }
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
        (*p == '.' && p + 1 < end && p[1] >= '0' && p[1] <= '9')) {
      PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> num;
      if (!readNumberFs(p, end, num)) break;
      stack.pushNumber(toFloat(num.c_str()));
      continue;
    }

    PdfFixedString<8> op;
    if (!readOperatorFs(p, end, op)) {
      ++p;
      continue;
    }
    const std::string_view opView = op.view();

    if (opView == "BT") {
      stack.clear();
      runs.clear();
      textX = textY = 0;
      lineSpacing = 0;
    } else if (opView == "ET") {
      flushTextGroup(runs, outPage, textBlockCounter, placement);
    } else if (opView == "Tm" && stack.size() >= 6) {
      float f = 0;
      float e = 0;
      stack.popNumber(f);
      stack.popNumber(e);
      stack.pop();
      stack.pop();
      stack.pop();
      stack.pop();
      textX = e;
      textY = f;
    } else if (opView == "cm" && stack.size() >= 6) {
      PdfMatrix m;
      stack.popNumber(m.f);
      stack.popNumber(m.e);
      stack.popNumber(m.d);
      stack.popNumber(m.c);
      stack.popNumber(m.b);
      stack.popNumber(m.a);
      currentCtm = multiplyMatrix(currentCtm, m);
    } else if (opView == "q") {
      if (ctmStackSize < ctmStack.size()) {
        ctmStack[ctmStackSize++] = currentCtm;
      }
    } else if (opView == "Q") {
      if (ctmStackSize > 0) {
        currentCtm = ctmStack[--ctmStackSize];
      } else {
        currentCtm = PdfMatrix{};
      }
    } else if (opView == "Td" && stack.size() >= 2) {
      float dy = 0;
      float dx = 0;
      stack.popNumber(dy);
      stack.popNumber(dx);
      textX += dx;
      textY += dy;
    } else if (opView == "TD" && stack.size() >= 2) {
      float dy = 0;
      float dx = 0;
      stack.popNumber(dy);
      stack.popNumber(dx);
      lineSpacing = -dy;
      textX += dx;
      textY += dy;
    } else if (opView == "T*") {
      textY -= lineSpacing;
    } else if (opView == "Tj" && !stack.empty()) {
      OpToken* raw = stack.back();
      if (!raw || raw->kind != OpTokenKind::Bytes) {
        stack.pop();
        continue;
      }
      TmpRun r;
      transformPoint(currentCtm, textX, textY, r.x, r.y);
      r.style = currentFontStyle;
      r.fontSize = currentFontSize;
      r.seq = seqCounter++;
      r.utf8 = bytesToUtf8WithCidMap(reinterpret_cast<const uint8_t*>(raw->bytes.data()), raw->bytes.size(),
                                     currentCidMap, currentSimpleFontEncoding);
      r.endX = r.x + estimateTextAdvance(r.utf8, r.fontSize);
      textX += estimateTextAdvance(r.utf8, r.fontSize);
      stack.pop();
      emitRun(std::move(r));
    } else if (opView == "TJ" && !stack.empty()) {
      OpToken* arrToken = stack.back();
      if (!arrToken || arrToken->kind != OpTokenKind::ArraySpan) {
        stack.pop();
        continue;
      }
      char* q = arrToken->spanStart;
      char* qend = arrToken->spanEnd;
      stack.pop();
      skipWsComment(q, qend);
      if (q < qend && *q == '[') ++q;
      while (q < qend) {
        skipWsComment(q, qend);
        if (q >= qend || *q == ']') break;
        if (*q == '(') {
          PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> raw;
          if (!readPdfStringLiteralFs(q, qend, raw)) break;
          TmpRun r;
          transformPoint(currentCtm, textX, textY, r.x, r.y);
          r.style = currentFontStyle;
          r.fontSize = currentFontSize;
          r.seq = seqCounter++;
          r.utf8 = bytesToUtf8WithCidMap(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), currentCidMap,
                                         currentSimpleFontEncoding);
          r.endX = r.x + estimateTextAdvance(r.utf8, r.fontSize);
          textX += estimateTextAdvance(r.utf8, r.fontSize);
          emitRun(std::move(r));
        } else if (*q == '<' && q + 1 < qend && q[1] != '<') {
          PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> raw;
          if (!readHexStringFs(q, qend, raw)) break;
          TmpRun r;
          transformPoint(currentCtm, textX, textY, r.x, r.y);
          r.style = currentFontStyle;
          r.fontSize = currentFontSize;
          r.seq = seqCounter++;
          r.utf8 = bytesToUtf8WithCidMap(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), currentCidMap,
                                         currentSimpleFontEncoding);
          r.endX = r.x + estimateTextAdvance(r.utf8, r.fontSize);
          textX += estimateTextAdvance(r.utf8, r.fontSize);
          emitRun(std::move(r));
        } else if ((*q >= '0' && *q <= '9') || *q == '-' || *q == '+' || *q == '.') {
          PdfFixedString<PDF_MAX_STACK_TOKEN_BYTES> num;
          readNumberFs(q, qend, num);
          textX += textAdjustmentToUserSpace(toFloat(num.c_str()), currentFontSize);
        } else {
          ++q;
        }
      }
    } else if ((opView == "'" || opView == "\"") && !stack.empty()) {
      textY -= lineSpacing;
      OpToken* raw = stack.back();
      if (!raw || raw->kind != OpTokenKind::Bytes) {
        stack.pop();
        continue;
      }
      TmpRun r;
      transformPoint(currentCtm, textX, textY, r.x, r.y);
      r.style = currentFontStyle;
      r.fontSize = currentFontSize;
      r.seq = seqCounter++;
      r.utf8 = bytesToUtf8WithCidMap(reinterpret_cast<const uint8_t*>(raw->bytes.data()), raw->bytes.size(),
                                     currentCidMap, currentSimpleFontEncoding);
      r.endX = r.x + estimateTextAdvance(r.utf8, r.fontSize);
      textX += estimateTextAdvance(r.utf8, r.fontSize);
      stack.pop();
      emitRun(std::move(r));
    } else if (opView == "Tf" && stack.size() >= 2) {
      float fontSize = 0;
      stack.popNumber(fontSize);
      currentFontSize = static_cast<uint16_t>(std::max(0.0f, fontSize));
      OpToken* fontNameToken = stack.back();
      if (!fontNameToken || fontNameToken->kind != OpTokenKind::Name) {
        stack.pop();
        continue;
      }
      const std::string_view fontName = fontNameToken->bytes.view();
      uint32_t fontId = 0;
      if (!fontNameCache.find(fontName, fontId)) {
        fontId = fontObjectIdForName(fontDictBody, fontName);
        fontNameCache.put(fontName, fontId);
      }
      if (fontId == 0) {
        stack.pop();
        continue;
      }
      updateCurrentCidMapForFont(file, xref, fontDictBody, fontName, currentFontStyle, fontInfoCache, uncachedFontInfo,
                                 fontBodyScratch, currentCidMap, currentSimpleFontEncoding);
      stack.pop();
    } else if (opView == "Do" && !stack.empty()) {
      OpToken* nameToken = stack.back();
      if (!nameToken || nameToken->kind != OpTokenKind::Name) {
        stack.pop();
        continue;
      }
      const std::string_view name = nameToken->bytes.view();
      uint32_t xid = 0;
      if (!xobjNameCache.find(name, xid)) {
        xid = xobjectIdForName(xobjDict, name);
        xobjNameCache.put(name, xid);
      }
      stack.pop();
      if (xid != 0) {
        PdfImageDescriptor img{};
        if (fillImageDescriptor(file, xref, xid, img)) {
          outPage.images.push_back(img);
          outPage.drawOrder.push_back({true, static_cast<uint32_t>(outPage.images.size() - 1)});
        }
      }
    } else if (opView == "BDC" && stack.size() >= 2) {
      stack.pop();
      stack.pop();
    } else if (opView == "EMC" || opView == "BMC") {
    } else if ((opView == "MP" || opView == "DP") && !stack.empty()) {
      stack.pop();
    } else {
      const int n = popOperandsForOperator(opView);
      if (n > 0 && stack.size() >= static_cast<size_t>(n)) {
        for (int i = 0; i < n; ++i) stack.pop();
      }
    }
  }
  return true;
}

bool decodeFileContentStream(FsFile& file, uint32_t streamOffset, uint32_t streamLen, bool isCompressed, uint8_t* buf,
                             size_t maxStream, size_t& got) {
  if (isCompressed) {
    got = StreamDecoder::flateDecode(file, streamOffset, streamLen, buf, maxStream);
    return true;
  }

  if (!file.seek(streamOffset)) {
    return false;
  }
  const int rd = file.read(buf, std::min<size_t>(static_cast<size_t>(streamLen), maxStream));
  if (rd < 0) {
    return false;
  }
  got = static_cast<size_t>(rd);
  return true;
}

bool runDecodedContentOperators(uint8_t* buf, size_t got, FsFile& file, const XrefTable& xref,
                                std::string_view pageObjectBody, PdfPage& outPage) {
  buf[got] = '\0';
  char* p = reinterpret_cast<char*>(buf);
  char* endp = p + got;
  return runContentOperators(p, endp, file, xref, pageObjectBody, outPage);
}

}  // namespace

bool ContentStream::parse(FsFile& file, uint32_t streamOffset, uint32_t streamLen, bool isCompressed,
                          const XrefTable& xref, std::string_view pageObjectBody, PdfPage& outPage) {
  outPage.textBlocks.clear();
  outPage.images.clear();
  outPage.drawOrder.clear();

  const size_t kMaxStream = pdfContentStreamMaxBytes();
  auto* buf = static_cast<uint8_t*>(malloc(kMaxStream + 1));
  if (!buf) {
    LOG_ERR("PDF", "ContentStream: malloc failed (need %zu bytes for page stream)", kMaxStream);
    return false;
  }

  size_t got = 0;
  if (!decodeFileContentStream(file, streamOffset, streamLen, isCompressed, buf, kMaxStream, got)) {
    free(buf);
    return false;
  }
  const bool ok = runDecodedContentOperators(buf, got, file, xref, pageObjectBody, outPage);
  free(buf);
  return ok;
}

bool ContentStream::parseBuffer(const uint8_t* streamBytes, size_t streamLen, bool isCompressed, FsFile& file,
                                const XrefTable& xref, std::string_view pageObjectBody, PdfPage& outPage) {
  outPage.textBlocks.clear();
  outPage.images.clear();
  outPage.drawOrder.clear();

  const size_t kMaxStream = pdfContentStreamMaxBytes();
  auto* buf = static_cast<uint8_t*>(malloc(kMaxStream + 1));
  if (!buf) {
    LOG_ERR("PDF", "ContentStream: malloc failed (need %zu bytes for page stream)", kMaxStream);
    return false;
  }

  size_t got = 0;
  if (isCompressed) {
    got = StreamDecoder::flateDecodeBytes(streamBytes, streamLen, buf, kMaxStream);
  } else {
    got = std::min(streamLen, kMaxStream);
    std::memcpy(buf, streamBytes, got);
  }
  const bool ok = runDecodedContentOperators(buf, got, file, xref, pageObjectBody, outPage);
  free(buf);
  return ok;
}
