#include "PdfObject.h"

#include "XrefTable.h"

#include <Logging.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

size_t findStreamKeyword(const std::string& s) {
  static const char pat[] = "\nstream";
  constexpr size_t plen = 7;
  size_t pos = 0;
  while (pos + plen <= s.size()) {
    if (std::memcmp(s.data() + pos, pat, plen) == 0) {
      return pos + 1;
    }
    if (pos + 8 <= s.size() && s[pos] == '\r' && s[pos + 1] == '\n' &&
        std::memcmp(s.data() + pos + 2, "stream", 6) == 0) {
      return pos + 2;
    }
    ++pos;
  }
  return std::string::npos;
}

size_t findEndobjKeyword(const std::string& s) {
  size_t p = 0;
  while (p < s.size()) {
    p = s.find("endobj", p);
    if (p == std::string::npos) return std::string::npos;
    if (p > 0 && (std::isalnum(static_cast<unsigned char>(s[p - 1])) || s[p - 1] == '/')) {
      p += 6;
      continue;
    }
    size_t after = p + 6;
    if (after < s.size()) {
      const char c = s[after];
      if (c != '\r' && c != '\n' && c != ' ' && c != '\t' && c != '<' && c != '\0') {
        p += 6;
        continue;
      }
    }
    return p;
  }
  return std::string::npos;
}

void trimTrailingWs(std::string& s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.pop_back();
  }
}

bool locateObjKeyword(std::string& acc, size_t& outEraseTo) {
  for (size_t i = 0; i + 3 <= acc.size(); ++i) {
    if (std::memcmp(acc.data() + i, "obj", 3) != 0) continue;
    if (i >= 3 && acc[i - 1] == 'b' && acc[i - 2] == 'd' && acc[i - 3] == 'n') continue;
    if (i > 0) {
      const char prev = acc[i - 1];
      if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '/') continue;
    }
    const size_t after = i + 3;
    if (after >= acc.size()) continue;
    {
      const char c = acc[after];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '<') continue;
    }
    outEraseTo = after;
    return true;
  }
  return false;
}

uint32_t resolveStreamLength(FsFile& file, const XrefTable* xref, const std::string& dictPart) {
  std::string v = PdfObject::getDictValue("/Length", dictPart);
  while (!v.empty() && (v[0] == ' ' || v[0] == '\t' || v[0] == '\r' || v[0] == '\n')) {
    v.erase(0, 1);
  }
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) {
    v.pop_back();
  }
  if (v.empty()) return 0;
  const char* p = v.c_str();
  char* e1 = nullptr;
  const unsigned long num = std::strtoul(p, &e1, 10);
  if (e1 == p) return 0;
  while (*e1 == ' ' || *e1 == '\t' || *e1 == '\r' || *e1 == '\n') ++e1;
  if (*e1 == '\0') {
    return static_cast<uint32_t>(num);
  }
  char* e2 = nullptr;
  const unsigned long gen = std::strtoul(e1, &e2, 10);
  (void)gen;
  if (e2 == e1) {
    return static_cast<uint32_t>(num);
  }
  while (*e2 == ' ' || *e2 == '\t') ++e2;
  if (*e2 != 'R' && *e2 != 'r') {
    return static_cast<uint32_t>(num);
  }
  if (!xref) {
    return 0;
  }
  const uint32_t lenObjId = static_cast<uint32_t>(num);
  const uint32_t loff = xref->getOffset(lenObjId);
  if (loff == 0) {
    return 0;
  }
  std::string lenBody;
  if (!PdfObject::readAt(file, loff, lenBody, nullptr, nullptr, nullptr)) {
    return 0;
  }
  p = lenBody.c_str();
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  return static_cast<uint32_t>(std::strtoul(p, nullptr, 10));
}

}  // namespace

bool PdfObject::readAt(FsFile& file, uint32_t offset, std::string& bodyStr, uint32_t* streamOffset,
                       uint32_t* streamLength, const XrefTable* xrefForIndirectLength) {
  bodyStr.clear();
  if (streamOffset) *streamOffset = 0;
  if (streamLength) *streamLength = 0;

  if (!file.seek(offset)) {
    return false;
  }

  auto* buf = static_cast<uint8_t*>(malloc(4096));
  if (!buf) {
    LOG_ERR("PDF", "PdfObject::readAt malloc failed");
    return false;
  }

  std::string acc;
  acc.reserve(16384);
  constexpr size_t kMaxAcc = 196608;

  bool strippedHeader = false;
  while (acc.size() < kMaxAcc) {
    const int n = file.read(buf, 4096);
    if (n <= 0) break;
    acc.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));

    if (!strippedHeader) {
      size_t eraseTo = 0;
      if (locateObjKeyword(acc, eraseTo)) {
        acc.erase(0, eraseTo);
        while (!acc.empty() && (acc[0] == ' ' || acc[0] == '\t' || acc[0] == '\r' || acc[0] == '\n')) {
          acc.erase(0, 1);
        }
        strippedHeader = true;
      }
    }

    if (!strippedHeader) {
      continue;
    }

    if (streamOffset != nullptr) {
      const size_t sp = findStreamKeyword(acc);
      if (sp != std::string::npos) {
        std::string dictPart = acc.substr(0, sp);
        trimTrailingWs(dictPart);
        bodyStr = std::move(dictPart);
        size_t dataIdx = sp + 6;
        while (dataIdx < acc.size() && (acc[dataIdx] == '\r' || acc[dataIdx] == '\n')) {
          ++dataIdx;
        }
        const size_t curPos = file.position();
        if (curPos < acc.size()) {
          free(buf);
          return false;
        }
        const uint32_t acc0File = static_cast<uint32_t>(curPos - acc.size());
        *streamOffset = acc0File + static_cast<uint32_t>(dataIdx);
        *streamLength = resolveStreamLength(file, xrefForIndirectLength, bodyStr);
        free(buf);
        return !bodyStr.empty() && *streamLength > 0;
      }
    }

    const size_t ep = findEndobjKeyword(acc);
    if (ep != std::string::npos && streamOffset == nullptr) {
      bodyStr = acc.substr(0, ep);
      trimTrailingWs(bodyStr);
      free(buf);
      return !bodyStr.empty();
    }
  }

  if (strippedHeader && streamOffset == nullptr) {
    const size_t ep = findEndobjKeyword(acc);
    if (ep != std::string::npos) {
      bodyStr = acc.substr(0, ep);
      trimTrailingWs(bodyStr);
      free(buf);
      return !bodyStr.empty();
    }
  }

  free(buf);
  return false;
}

std::string PdfObject::getDictValue(const char* key, const std::string& dict) {
  const size_t keyLen = std::strlen(key);
  if (keyLen == 0) return {};

  size_t pos = 0;
  while (pos < dict.size()) {
    pos = dict.find(key, pos);
    if (pos == std::string::npos) break;
    if (pos > 0 && dict[pos - 1] == '/') {
      pos += keyLen;
      continue;
    }
    size_t v = pos + keyLen;
    while (v < dict.size() && (dict[v] == ' ' || dict[v] == '\t' || dict[v] == '\r' || dict[v] == '\n')) {
      ++v;
    }
    if (v >= dict.size()) return {};

    const size_t start = v;
    int depth = 0;
    while (v < dict.size()) {
      if (v + 1 < dict.size() && dict[v] == '<' && dict[v + 1] == '<') {
        depth += 2;
        v += 2;
        continue;
      }
      if (v + 1 < dict.size() && dict[v] == '>' && dict[v + 1] == '>') {
        depth -= 2;
        v += 2;
        if (depth <= 0) {
          break;
        }
        continue;
      }
      if (depth == 0) {
        if (dict[v] == '/') {
          // Value can be a PDF name (e.g. /Type /Pages). Only treat '/' as the
          // next key when we have already consumed part of the value.
          if (v > start) {
            break;
          }
          ++v;
          while (v < dict.size()) {
            const unsigned char c = static_cast<unsigned char>(dict[v]);
            if (std::isspace(c) || c == '/' || c == '(' || c == '[' || c == '<' || c == '>' || c == ']' ||
                c == '{' || c == '}') {
              break;
            }
            ++v;
          }
          return dict.substr(start, v - start);
        }
        if (dict[v] == '>' && v + 1 < dict.size() && dict[v + 1] == '>') {
          break;
        }
      }
      ++v;
    }
    return dict.substr(start, v - start);
  }
  return {};
}

int PdfObject::getDictInt(const char* key, const std::string& dict, int defaultVal) {
  const std::string v = getDictValue(key, dict);
  if (v.empty()) return defaultVal;
  const char* p = v.c_str();
  while (*p == ' ' || *p == '\t') ++p;
  if (!*p) return defaultVal;
  char* end = nullptr;
  const long n = std::strtol(p, &end, 10);
  if (end == p) return defaultVal;
  return static_cast<int>(n);
}

uint32_t PdfObject::getDictRef(const char* key, const std::string& dict) {
  const std::string v = getDictValue(key, dict);
  if (v.empty()) return 0;
  const char* p = v.c_str();
  while (*p == ' ' || *p == '\t') ++p;
  char* end = nullptr;
  const unsigned long id = std::strtoul(p, &end, 10);
  if (end == p) return 0;
  return static_cast<uint32_t>(id);
}
