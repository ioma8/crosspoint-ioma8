#include "ContentStream.h"

#include <Logging.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "PdfObject.h"
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

std::string pdfBytesToUtf8(const uint8_t* data, size_t len) {
  std::string out;
  if (len >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
    for (size_t i = 2; i + 1 < len; i += 2) {
      const uint16_t cu = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
      appendUtf8(out, cu);
    }
    return out;
  }
  for (size_t i = 0; i < len; ++i) {
    const unsigned char b = data[i];
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
    } else {
      appendUtf8(out, kWin1252ToU[b - 0x80]);
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

bool readPdfStringLiteral(char*& p, char* end, std::string& rawBytes) {
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
        rawBytes.push_back(static_cast<char>(oct & 0xFF));
        continue;
      }
      if (c == 'n')
        rawBytes.push_back('\n');
      else if (c == 'r')
        rawBytes.push_back('\r');
      else if (c == 't')
        rawBytes.push_back('\t');
      else if (c == 'b')
        rawBytes.push_back('\b');
      else if (c == 'f')
        rawBytes.push_back('\f');
      else
        rawBytes.push_back(c);
      continue;
    }
    if (*p == '(') {
      ++depth;
      rawBytes.push_back(*p++);
      continue;
    }
    if (*p == ')') {
      --depth;
      if (depth == 0) {
        ++p;
        break;
      }
      rawBytes.push_back(*p++);
      continue;
    }
    rawBytes.push_back(*p++);
  }
  return true;
}

bool readHexString(char*& p, char* end, std::string& rawBytes) {
  rawBytes.clear();
  if (p >= end || *p != '<') return false;
  ++p;
  uint8_t acc = 0;
  bool haveNibble = false;
  while (p < end) {
    if (*p == '>') {
      ++p;
      if (haveNibble) rawBytes.push_back(static_cast<char>(acc));
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
      rawBytes.push_back(static_cast<char>(acc));
      haveNibble = false;
    }
    ++p;
  }
  return false;
}

bool readNumber(char*& p, char* end, std::string& out) {
  out.clear();
  if (p >= end) return false;
  if (*p == '+' || *p == '-') out.push_back(*p++);
  bool sawDigit = false;
  while (p < end && *p >= '0' && *p <= '9') {
    sawDigit = true;
    out.push_back(*p++);
  }
  if (p < end && *p == '.') {
    out.push_back(*p++);
    while (p < end && *p >= '0' && *p <= '9') {
      sawDigit = true;
      out.push_back(*p++);
    }
  }
  return sawDigit || !out.empty();
}

bool readName(char*& p, char* end, std::string& out) {
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
        out.push_back(static_cast<char>((a << 4) | b));
        p += 3;
        continue;
      }
    }
    out.push_back(c);
    ++p;
  }
  return !out.empty();
}

bool readOperator(char*& p, char* end, std::string& out) {
  out.clear();
  while (p < end) {
    char c = *p;
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '*' || c == '"' || c == '\'') {
      out.push_back(c);
      ++p;
    } else {
      break;
    }
  }
  return !out.empty();
}

// Capture a PDF array including nested `[` `]`; skips string literals and comments inside.
bool readArrayToken(char*& p, char* end, std::string& out) {
  out.clear();
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
      std::string dummy;
      if (!readPdfStringLiteral(p, end, dummy)) return false;
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] != '<') {
      std::string dummy;
      if (!readHexString(p, end, dummy)) return false;
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
  out.assign(start, p);
  return true;
}

float toFloat(const std::string& s) { return static_cast<float>(std::strtod(s.c_str(), nullptr)); }

std::string resolveResourcesDict(FsFile& file, const XrefTable& xref, const std::string& pageBody) {
  std::string r = PdfObject::getDictValue("/Resources", pageBody);
  while (!r.empty() && (r[0] == ' ' || r[0] == '\t' || r[0] == '\r' || r[0] == '\n')) r.erase(0, 1);
  if (r.empty()) return {};
  if (r.size() >= 2 && r[0] == '<' && r[1] == '<') return r;
  const uint32_t rid = PdfObject::getDictRef("/Resources", pageBody);
  if (rid == 0) return r;
  std::string body;
  if (!xref.readDictForObject(file, rid, body)) return r;
  return body;
}

std::string getXObjectDict(const std::string& resourcesBody) {
  return PdfObject::getDictValue("/XObject", resourcesBody);
}

uint32_t xobjectIdForName(const std::string& xobjDict, const std::string& name) {
  const std::string key = "/" + name;
  size_t pos = 0;
  while (pos < xobjDict.size()) {
    pos = xobjDict.find(key, pos);
    if (pos == std::string::npos) break;
    if (pos > 0 && xobjDict[pos - 1] == '/') {
      pos += key.size();
      continue;
    }
    size_t v = pos + key.size();
    while (v < xobjDict.size() && (xobjDict[v] == ' ' || xobjDict[v] == '\t')) ++v;
    char* end = nullptr;
    const unsigned long id = std::strtoul(xobjDict.c_str() + v, &end, 10);
    if (end == xobjDict.c_str() + v) return 0;
    return static_cast<uint32_t>(id);
  }
  return 0;
}

bool fillImageDescriptor(FsFile& file, const XrefTable& xref, uint32_t objId, PdfImageDescriptor& out) {
  const uint32_t off = xref.getOffset(objId);
  if (off == 0) {
    return false;
  }
  std::string body;
  uint32_t so = 0;
  uint32_t sl = 0;
  if (!PdfObject::readAt(file, off, body, &so, &sl, &xref)) {
    return false;
  }
  std::string st = PdfObject::getDictValue("/Subtype", body);
  while (!st.empty() && (st.front() == ' ' || st.front() == '\t')) st.erase(0, 1);
  while (!st.empty() && (st.back() == ' ' || st.back() == '\t')) st.pop_back();
  if (st != "/Image") return false;

  out.pdfStreamOffset = so;
  out.pdfStreamLength = sl;
  out.width = static_cast<uint16_t>(PdfObject::getDictInt("/Width", body, 0));
  out.height = static_cast<uint16_t>(PdfObject::getDictInt("/Height", body, 0));
  if (body.find("/DCTDecode") != std::string::npos || body.find("/DCT") != std::string::npos) {
    out.format = 0;
  } else {
    out.format = 1;
  }
  return out.pdfStreamLength > 0 && out.width > 0 && out.height > 0;
}

struct TmpRun {
  float y = 0;
  std::string utf8;
  uint32_t seq = 0;
};

void flushTextGroup(std::vector<TmpRun>& runs, PdfPage& page) {
  if (runs.empty()) return;
  std::sort(runs.begin(), runs.end(), [](const TmpRun& a, const TmpRun& b) {
    if (a.y != b.y) return a.y > b.y;
    return a.seq < b.seq;
  });
  constexpr float kLineThresh = 20.0f;
  std::string block = runs[0].utf8;
  uint32_t hint = static_cast<uint32_t>(runs[0].y);
  for (size_t i = 1; i < runs.size(); ++i) {
    if (std::fabs(runs[i].y - runs[i - 1].y) < kLineThresh) {
      if (!block.empty() && !runs[i].utf8.empty()) block.push_back(' ');
      block += runs[i].utf8;
    } else {
      if (!block.empty()) {
        page.textBlocks.push_back({block, hint});
        page.drawOrder.push_back({false, static_cast<uint32_t>(page.textBlocks.size() - 1)});
        block.clear();
      }
      block = runs[i].utf8;
      hint = static_cast<uint32_t>(runs[i].y);
    }
  }
  if (!block.empty()) {
    page.textBlocks.push_back({std::move(block), hint});
    page.drawOrder.push_back({false, static_cast<uint32_t>(page.textBlocks.size() - 1)});
  }
  runs.clear();
}

bool runContentOperators(char* p, char* end, FsFile& file, const XrefTable& xref,
                           const std::string& pageObjectBody, PdfPage& outPage) {
  const std::string resBody = resolveResourcesDict(file, xref, pageObjectBody);
  const std::string xobjDict = getXObjectDict(resBody);

  bool inText = false;
  float textX = 0;
  float textY = 0;
  float lineSpacing = 0;
  uint32_t seqCounter = 0;
  std::vector<TmpRun> runs;
  std::vector<std::string> stack;

  while (p < end) {
    skipWsComment(p, end);
    if (p >= end) break;

    if (*p == '(') {
      std::string raw;
      if (!readPdfStringLiteral(p, end, raw)) break;
      stack.push_back(std::move(raw));
      continue;
    }
    if (*p == '<' && p + 1 < end && p[1] != '<') {
      std::string raw;
      if (!readHexString(p, end, raw)) break;
      stack.push_back(std::move(raw));
      continue;
    }
    if (*p == '[') {
      std::string arr;
      if (!readArrayToken(p, end, arr)) break;
      stack.push_back(std::move(arr));
      continue;
    }
    if (*p == '/' && (p + 1 >= end || p[1] != '/')) {
      std::string name;
      if (!readName(p, end, name)) break;
      stack.push_back(name);
      continue;
    }
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' ||
        (*p == '.' && p + 1 < end && p[1] >= '0' && p[1] <= '9')) {
      std::string num;
      if (!readNumber(p, end, num)) break;
      stack.push_back(std::move(num));
      continue;
    }

    std::string op;
    if (!readOperator(p, end, op)) {
      ++p;
      continue;
    }

    if (op == "BT") {
      inText = true;
      runs.clear();
      textX = textY = 0;
      lineSpacing = 0;
    } else if (op == "ET") {
      if (inText) {
        flushTextGroup(runs, outPage);
      }
      inText = false;
    } else if (op == "Tm" && stack.size() >= 6) {
      const float f = toFloat(stack.back());
      stack.pop_back();
      const float e = toFloat(stack.back());
      stack.pop_back();
      stack.pop_back();
      stack.pop_back();
      stack.pop_back();
      stack.pop_back();
      textX = e;
      textY = f;
    } else if (op == "Td" && stack.size() >= 2) {
      const float dy = toFloat(stack.back());
      stack.pop_back();
      const float dx = toFloat(stack.back());
      stack.pop_back();
      textX += dx;
      textY += dy;
    } else if (op == "TD" && stack.size() >= 2) {
      const float dy = toFloat(stack.back());
      stack.pop_back();
      const float dx = toFloat(stack.back());
      stack.pop_back();
      lineSpacing = -dy;
      textX += dx;
      textY += dy;
    } else if (op == "T*") {
      textY -= lineSpacing;
    } else if (op == "Tj" && !stack.empty()) {
      const std::string raw = stack.back();
      stack.pop_back();
      TmpRun r;
      r.y = textY;
      r.seq = seqCounter++;
      r.utf8 = pdfBytesToUtf8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
      if (!r.utf8.empty()) runs.push_back(std::move(r));
    } else if (op == "TJ" && !stack.empty()) {
      std::string arr = stack.back();
      stack.pop_back();
      char* q = arr.data();
      char* qend = q + arr.size();
      skipWsComment(q, qend);
      if (q < qend && *q == '[') ++q;
      while (q < qend) {
        skipWsComment(q, qend);
        if (q >= qend || *q == ']') break;
        if (*q == '(') {
          std::string raw;
          if (!readPdfStringLiteral(q, qend, raw)) break;
          TmpRun r;
          r.y = textY;
          r.seq = seqCounter++;
          r.utf8 = pdfBytesToUtf8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
          if (!r.utf8.empty()) runs.push_back(std::move(r));
        } else if (*q == '<' && q + 1 < qend && q[1] != '<') {
          std::string raw;
          if (!readHexString(q, qend, raw)) break;
          TmpRun r;
          r.y = textY;
          r.seq = seqCounter++;
          r.utf8 = pdfBytesToUtf8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
          if (!r.utf8.empty()) runs.push_back(std::move(r));
        } else if ((*q >= '0' && *q <= '9') || *q == '-' || *q == '+' || *q == '.') {
          std::string num;
          readNumber(q, qend, num);
        } else {
          ++q;
        }
      }
    } else if ((op == "'" || op == "\"") && !stack.empty()) {
      textY -= lineSpacing;
      const std::string raw = stack.back();
      stack.pop_back();
      TmpRun r;
      r.y = textY;
      r.seq = seqCounter++;
      r.utf8 = pdfBytesToUtf8(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
      if (!r.utf8.empty()) runs.push_back(std::move(r));
    } else if (op == "Tf" && stack.size() >= 2) {
      stack.pop_back();
      stack.pop_back();
    } else if (op == "Do" && !stack.empty()) {
      const std::string name = stack.back();
      stack.pop_back();
      const uint32_t xid = xobjectIdForName(xobjDict, name);
      if (xid != 0) {
        PdfImageDescriptor img{};
        if (fillImageDescriptor(file, xref, xid, img)) {
          outPage.images.push_back(img);
          outPage.drawOrder.push_back({true, static_cast<uint32_t>(outPage.images.size() - 1)});
        }
      }
    }
  }

  return true;
}

}  // namespace

bool ContentStream::parse(FsFile& file, uint32_t streamOffset, uint32_t streamLen, bool isCompressed,
                          const XrefTable& xref, const std::string& pageObjectBody, PdfPage& outPage) {
  outPage.textBlocks.clear();
  outPage.images.clear();
  outPage.drawOrder.clear();

  constexpr size_t kMaxStream = 200 * 1024;
  auto* buf = static_cast<uint8_t*>(malloc(kMaxStream + 1));
  if (!buf) {
    LOG_ERR("PDF", "ContentStream: malloc failed");
    return false;
  }

  size_t got = 0;
  if (isCompressed) {
    got = StreamDecoder::flateDecode(file, streamOffset, streamLen, buf, kMaxStream);
  } else {
    if (!file.seek(streamOffset)) {
      free(buf);
      return false;
    }
    const int rd = file.read(buf, std::min<size_t>(streamLen, kMaxStream));
    if (rd < 0) {
      free(buf);
      return false;
    }
    got = static_cast<size_t>(rd);
  }
  buf[got] = '\0';

  char* p = reinterpret_cast<char*>(buf);
  char* endp = p + got;
  const bool ok = runContentOperators(p, endp, file, xref, pageObjectBody, outPage);
  free(buf);
  return ok;
}

bool ContentStream::parseBuffer(const uint8_t* streamBytes, size_t streamLen, bool isCompressed, FsFile& file,
                                const XrefTable& xref, const std::string& pageObjectBody, PdfPage& outPage) {
  outPage.textBlocks.clear();
  outPage.images.clear();
  outPage.drawOrder.clear();

  constexpr size_t kMaxStream = 200 * 1024;
  auto* buf = static_cast<uint8_t*>(malloc(kMaxStream + 1));
  if (!buf) {
    LOG_ERR("PDF", "ContentStream: malloc failed");
    return false;
  }

  size_t got = 0;
  if (isCompressed) {
    got = StreamDecoder::flateDecodeBytes(streamBytes, streamLen, buf, kMaxStream);
  } else {
    got = std::min(streamLen, kMaxStream);
    std::memcpy(buf, streamBytes, got);
  }
  buf[got] = '\0';

  char* p = reinterpret_cast<char*>(buf);
  char* endp = p + got;
  const bool ok = runContentOperators(p, endp, file, xref, pageObjectBody, outPage);
  free(buf);
  return ok;
}
