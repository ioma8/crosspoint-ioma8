#include "XrefTable.h"

#include "PdfObject.h"
#include "StreamDecoder.h"

#include <Logging.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

bool matchBytes(const uint8_t* p, size_t n, const char* lit, size_t litLen) {
  if (n < litLen) return false;
  return std::memcmp(p, lit, litLen) == 0;
}

// Find "startxref" in buf[0..len), return index or SIZE_MAX
size_t findStartXref(const uint8_t* buf, size_t len) {
  static const char k[] = "startxref";
  constexpr size_t kLen = 9;
  if (len < kLen) return SIZE_MAX;
  for (size_t i = 0; i + kLen <= len; ++i) {
    if (matchBytes(buf + i, len - i, k, kLen)) {
      return i;
    }
  }
  return SIZE_MAX;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0'; }

void skipWs(const char*& p) {
  while (*p && isSpace(*p)) ++p;
}

void skipWs(char*& p) {
  while (*p && isSpace(*p)) ++p;
}

bool readLineFromFile(FsFile& file, char* out, size_t outCap, size_t& outLen) {
  outLen = 0;
  while (outLen + 1 < outCap) {
    int c = file.read();
    if (c < 0) break;
    if (c == '\r') {
      int c2 = file.read();
      if (c2 >= 0 && c2 != '\n') {
        file.seekCur(-1);
      }
      break;
    }
    if (c == '\n') break;
    out[outLen++] = static_cast<char>(c);
  }
  out[outLen] = '\0';
  return true;
}

struct XrefSectionResult {
  std::vector<std::pair<uint32_t, uint32_t>> updates;
  uint32_t rootObjId = 0;
  uint32_t prevXref = 0;
  uint32_t declaredSize = 0;
};

// Parse one classic xref table at file offset xrefOff; collect per-object updates and trailer links.
bool parseClassicXrefSection(FsFile& file, size_t fileSize, uint32_t xrefOff, XrefSectionResult& out) {
  out.updates.clear();
  out.rootObjId = 0;
  out.prevXref = 0;
  out.declaredSize = 0;

  if (xrefOff >= fileSize || !file.seek(xrefOff)) {
    return false;
  }

  char lineBuf[128];
  size_t lineLen = 0;
  readLineFromFile(file, lineBuf, sizeof(lineBuf), lineLen);
  if (std::strncmp(lineBuf, "xref", 4) != 0) {
    return false;
  }

  std::string trailer;

  for (;;) {
    readLineFromFile(file, lineBuf, sizeof(lineBuf), lineLen);
    if (lineLen == 0) {
      if (file.position() >= fileSize) {
        LOG_ERR("PDF", "xref: unexpected EOF before trailer");
        return false;
      }
      continue;
    }
    if (lineLen >= 7 && std::strncmp(lineBuf, "trailer", 7) == 0 &&
        (lineBuf[7] == '\0' || lineBuf[7] == ' ' || lineBuf[7] == '\t')) {
      const char* rest = lineBuf + 7;
      skipWs(rest);
      trailer.assign(rest);
      break;
    }
    char* endp = nullptr;
    const unsigned long firstObj = strtoul(lineBuf, &endp, 10);
    skipWs(endp);
    const unsigned long count = strtoul(endp, &endp, 10);
    // count==0 is valid (incremental placeholder before trailer-only update).
    if (count > 100000 || firstObj + count > 2000000) {
      LOG_ERR("PDF", "xref: bad subsection");
      return false;
    }
    if (count == 0) {
      continue;
    }

    auto* entryBuf = static_cast<char*>(malloc(21 * count));
    if (!entryBuf) {
      LOG_ERR("PDF", "xref: entry malloc failed");
      return false;
    }
    const size_t toRead = 20 * count;
    const int rd = file.read(reinterpret_cast<uint8_t*>(entryBuf), toRead);
    if (rd != static_cast<int>(toRead)) {
      free(entryBuf);
      LOG_ERR("PDF", "xref: short read entries");
      return false;
    }
    entryBuf[toRead] = '\0';

    for (unsigned long i = 0; i < count; ++i) {
      const char* row = entryBuf + i * 20;
      if (row[10] != ' ' || row[16] != ' ') {
        free(entryBuf);
        LOG_ERR("PDF", "xref: bad entry row");
        return false;
      }
      char offStr[11];
      std::memcpy(offStr, row, 10);
      offStr[10] = '\0';
      const char flag = row[17];
      const uint32_t idx = static_cast<uint32_t>(firstObj + i);
      const uint32_t offVal = (flag == 'n') ? static_cast<uint32_t>(strtoul(offStr, nullptr, 10)) : 0;
      out.updates.push_back({idx, offVal});
    }
    free(entryBuf);
  }

  {
    constexpr size_t kChunk = 256;
    char chBuf[kChunk];
    for (int safety = 0; safety < 200 && trailer.size() < 16384; ++safety) {
      if (trailer.find("startxref") != std::string::npos) {
        break;
      }
      const int rd = file.read(reinterpret_cast<uint8_t*>(chBuf), kChunk);
      if (rd <= 0) break;
      trailer.append(chBuf, static_cast<size_t>(rd));
      const size_t sx = trailer.find("startxref");
      if (sx != std::string::npos) {
        trailer.resize(sx);
        break;
      }
    }
  }

  {
    const char* s = trailer.c_str();
    const char* rootKey = strstr(s, "/Root");
    if (rootKey) {
      rootKey += 5;
      skipWs(rootKey);
      char rb[32];
      size_t ri = 0;
      while (*rootKey >= '0' && *rootKey <= '9' && ri + 1 < sizeof(rb)) {
        rb[ri++] = *rootKey++;
      }
      rb[ri] = '\0';
      if (ri > 0) {
        out.rootObjId = static_cast<uint32_t>(strtoul(rb, nullptr, 10));
      }
    }
    const char* prevKey = strstr(s, "/Prev");
    if (prevKey) {
      prevKey += 5;
      skipWs(prevKey);
      char pb[32];
      size_t pi = 0;
      while (*prevKey >= '0' && *prevKey <= '9' && pi + 1 < sizeof(pb)) {
        pb[pi++] = *prevKey++;
      }
      pb[pi] = '\0';
      if (pi > 0) {
        out.prevXref = static_cast<uint32_t>(strtoul(pb, nullptr, 10));
      }
    }
    const char* sizeKey = strstr(s, "/Size");
    if (sizeKey) {
      sizeKey += 5;
      skipWs(sizeKey);
      char sb[32];
      size_t si = 0;
      while (*sizeKey >= '0' && *sizeKey <= '9' && si + 1 < sizeof(sb)) {
        sb[si++] = *sizeKey++;
      }
      sb[si] = '\0';
      if (si > 0) {
        const uint32_t sz = static_cast<uint32_t>(strtoul(sb, nullptr, 10));
        if (sz > 0 && sz < 2000000) {
          out.declaredSize = sz;
        }
      }
    }
  }

  return true;
}

bool mergeXrefChainRecursive(FsFile& file, size_t fileSize, uint32_t xrefOff, std::vector<uint32_t>& offsets,
                             uint32_t& rootOut, unsigned chainDepth = 0) {
  if (chainDepth > 128) {
    LOG_ERR("PDF", "xref: /Prev chain too deep");
    return false;
  }
  XrefSectionResult sec;
  if (!parseClassicXrefSection(file, fileSize, xrefOff, sec)) {
    return false;
  }

  if (sec.prevXref != 0 && sec.prevXref < fileSize) {
    if (!mergeXrefChainRecursive(file, fileSize, sec.prevXref, offsets, rootOut, chainDepth + 1)) {
      return false;
    }
  }

  for (const auto& u : sec.updates) {
    const uint32_t id = u.first;
    const uint32_t offVal = u.second;
    if (offsets.size() <= id) {
      offsets.resize(static_cast<size_t>(id) + 1, 0);
    }
    offsets[id] = offVal;
  }
  if (sec.declaredSize > 0 && sec.declaredSize < 2000000) {
    if (offsets.size() < sec.declaredSize) {
      offsets.resize(sec.declaredSize, 0);
    }
  }
  if (sec.rootObjId != 0) {
    rootOut = sec.rootObjId;
  }
  return true;
}

uint32_t readBe24(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[2]);
}

uint32_t readBe16(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 8) | static_cast<uint32_t>(p[1]);
}

bool parseBracketInts(const std::string& src, std::vector<int>& out) {
  out.clear();
  const size_t lb = src.find('[');
  if (lb == std::string::npos) return false;
  const size_t rb = src.find(']', lb + 1);
  if (rb == std::string::npos) return false;
  const char* p = src.c_str() + lb + 1;
  const char* end = src.c_str() + rb;
  while (p < end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    if (p >= end) break;
    char* e = nullptr;
    const long n = std::strtol(p, &e, 10);
    if (e == p) return false;
    out.push_back(static_cast<int>(n));
    p = e;
  }
  return !out.empty();
}

size_t findStreamKeywordSlice(const std::string& s) {
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

void trimWsBoth(std::string& s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.erase(0, 1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.pop_back();
  }
}

bool splitObjStmObjectSlice(const std::string& slice, std::string& dictOut, std::vector<uint8_t>& streamOut) {
  streamOut.clear();
  const size_t sp = findStreamKeywordSlice(slice);
  if (sp == std::string::npos) {
    dictOut = slice;
    trimWsBoth(dictOut);
    return !dictOut.empty();
  }
  dictOut = slice.substr(0, sp);
  while (!dictOut.empty() && (dictOut.back() == ' ' || dictOut.back() == '\t' || dictOut.back() == '\r' ||
                              dictOut.back() == '\n')) {
    dictOut.pop_back();
  }
  size_t dataIdx = sp + 6;
  while (dataIdx < slice.size() && (slice[dataIdx] == '\r' || slice[dataIdx] == '\n')) {
    ++dataIdx;
  }
  const size_t ep = slice.rfind("endstream");
  if (ep == std::string::npos || ep < dataIdx) {
    return false;
  }
  streamOut.assign(slice.begin() + static_cast<std::ptrdiff_t>(dataIdx),
                   slice.begin() + static_cast<std::ptrdiff_t>(ep));
  return !dictOut.empty();
}

}  // namespace

bool XrefTable::parseXrefStream(FsFile& file, size_t /*fileSize*/, uint32_t xrefObjOffset) {
  std::string dict;
  uint32_t so = 0;
  uint32_t sl = 0;
  if (!PdfObject::readAt(file, xrefObjOffset, dict, &so, &sl, nullptr)) {
    LOG_ERR("PDF", "xref: cannot read xref stream object");
    return false;
  }
  std::string t = PdfObject::getDictValue("/Type", dict);
  trimWsBoth(t);
  if (t != "/XRef") {
    LOG_ERR("PDF", "xref: expected /Type /XRef");
    return false;
  }

  rootObjId_ = PdfObject::getDictRef("/Root", dict);
  const int size = PdfObject::getDictInt("/Size", dict, 0);
  if (size <= 0 || size > 2000000) {
    LOG_ERR("PDF", "xref: bad /Size in xref stream");
    return false;
  }

  std::vector<int> wv;
  const std::string wStr = PdfObject::getDictValue("/W", dict);
  if (!parseBracketInts(wStr, wv) || wv.size() < 3 || wv[0] < 0 || wv[1] < 0 || wv[2] < 0) {
    LOG_ERR("PDF", "xref: bad /W in xref stream");
    return false;
  }
  const size_t w0 = static_cast<size_t>(wv[0]);
  const size_t w1 = static_cast<size_t>(wv[1]);
  const size_t w2 = static_cast<size_t>(wv[2]);
  const size_t recBytes = w0 + w1 + w2;
  if (recBytes == 0 || recBytes > 32) {
    LOG_ERR("PDF", "xref: invalid /W entry sizes");
    return false;
  }

  int indexStart = 0;
  int indexCount = size;
  const std::string idxStr = PdfObject::getDictValue("/Index", dict);
  std::vector<int> idxv;
  if (!idxStr.empty() && parseBracketInts(idxStr, idxv) && idxv.size() >= 2) {
    indexStart = idxv[0];
    indexCount = idxv[1];
  }
  if (indexCount <= 0 || indexStart < 0 || static_cast<size_t>(indexStart) + static_cast<size_t>(indexCount) > 2000000) {
    LOG_ERR("PDF", "xref: bad /Index");
    return false;
  }

  constexpr size_t kMaxDecoded = 8 * 1024 * 1024;
  auto* decoded = static_cast<uint8_t*>(malloc(kMaxDecoded + 1));
  if (!decoded) {
    LOG_ERR("PDF", "xref: malloc decoded xref failed");
    return false;
  }
  const size_t got =
      StreamDecoder::flateDecode(file, so, sl, decoded, kMaxDecoded);
  if (got == 0) {
    free(decoded);
    LOG_ERR("PDF", "xref: xref stream decode failed");
    return false;
  }

  offsets.assign(static_cast<size_t>(size), 0);
  std::unordered_set<uint32_t> objStmContainers;

  const size_t nRows = static_cast<size_t>(indexCount);
  if (got < nRows * recBytes) {
    free(decoded);
    LOG_ERR("PDF", "xref: xref stream too short");
    return false;
  }

  for (size_t i = 0; i < nRows; ++i) {
    const uint8_t* row = decoded + i * recBytes;
    const uint32_t objId = static_cast<uint32_t>(indexStart + static_cast<int>(i));
    if (objId >= offsets.size()) {
      offsets.resize(static_cast<size_t>(objId) + 1, 0);
    }
    uint32_t ft = 0;
    if (w0 >= 1) {
      ft = row[0];
    }
    const uint8_t* f1b = row + w0;
    if (ft == 1 && w1 > 0) {
      uint32_t fileOff = 0;
      if (w1 == 1) {
        fileOff = f1b[0];
      } else if (w1 == 2) {
        fileOff = readBe16(f1b);
      } else {
        fileOff = readBe24(f1b);
      }
      offsets[objId] = fileOff;
    } else if (ft == 2 && w1 > 0) {
      uint32_t stmObj = 0;
      if (w1 == 1) {
        stmObj = f1b[0];
      } else if (w1 == 2) {
        stmObj = readBe16(f1b);
      } else {
        stmObj = readBe24(f1b);
      }
      objStmContainers.insert(stmObj);
    } else if (ft == 0) {
      offsets[objId] = 0;
    }
  }
  free(decoded);

  for (uint32_t sid : objStmContainers) {
    loadObjStream(file, sid);
  }

  bool any = false;
  for (uint32_t v : offsets) {
    if (v != 0) {
      any = true;
      break;
    }
  }
  if (!any && inlineDict_.empty()) {
    LOG_ERR("PDF", "xref: no objects after xref stream");
    return false;
  }
  if (rootObjId_ == 0) {
    LOG_ERR("PDF", "xref: missing /Root");
    return false;
  }
  return true;
}

void XrefTable::loadObjStream(FsFile& file, uint32_t stmObjId) {
  if (loadedObjStreams_.count(stmObjId) != 0) {
    return;
  }
  const uint32_t off = stmObjId < offsets.size() ? offsets[stmObjId] : 0;
  if (off == 0) {
    return;
  }
  std::string dict;
  uint32_t so = 0;
  uint32_t sl = 0;
  if (!PdfObject::readAt(file, off, dict, &so, &sl, this)) {
    return;
  }
  std::string st = PdfObject::getDictValue("/Type", dict);
  trimWsBoth(st);
  if (st != "/ObjStm") {
    return;
  }
  const int nObj = PdfObject::getDictInt("/N", dict, 0);
  const int first = PdfObject::getDictInt("/First", dict, 0);
  if (nObj <= 0 || nObj > 100000 || first < 0) {
    return;
  }

  constexpr size_t kMaxObjStm = 16 * 1024 * 1024;
  auto* rawBuf = static_cast<uint8_t*>(malloc(kMaxObjStm + 1));
  if (!rawBuf) {
    return;
  }
  size_t rawLen = StreamDecoder::flateDecode(file, so, sl, rawBuf, kMaxObjStm);
  if (rawLen == 0) {
    free(rawBuf);
    return;
  }
  rawBuf[rawLen] = '\0';
  const std::string raw(reinterpret_cast<char*>(rawBuf), rawLen);
  free(rawBuf);

  if (static_cast<size_t>(first) > raw.size()) {
    return;
  }
  const std::string header = raw.substr(0, static_cast<size_t>(first));
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  {
    const char* p = header.c_str();
    while (*p) {
      while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
      if (!*p) break;
      char* e = nullptr;
      const unsigned long oid = std::strtoul(p, &e, 10);
      if (e == p) break;
      p = e;
      while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
      const unsigned long rel = std::strtoul(p, &e, 10);
      if (e == p) break;
      p = e;
      pairs.push_back({static_cast<uint32_t>(oid), static_cast<uint32_t>(rel)});
    }
  }
  if (pairs.size() < static_cast<size_t>(nObj)) {
    return;
  }

  loadedObjStreams_.insert(stmObjId);

  const size_t base = static_cast<size_t>(first);
  for (int i = 0; i < nObj && i < static_cast<int>(pairs.size()); ++i) {
    const uint32_t objNum = pairs[static_cast<size_t>(i)].first;
    const uint32_t rel = pairs[static_cast<size_t>(i)].second;
    const size_t start = base + static_cast<size_t>(rel);
    if (start > raw.size()) {
      continue;
    }
    size_t end = raw.size();
    if (i + 1 < static_cast<int>(pairs.size())) {
      const uint32_t relNext = pairs[static_cast<size_t>(i + 1)].second;
      const size_t nextStart = base + static_cast<size_t>(relNext);
      if (nextStart > start && nextStart <= raw.size()) {
        end = nextStart;
      }
    }
    std::string slice = raw.substr(start, end - start);
    trimWsBoth(slice);
    if (slice.empty()) {
      continue;
    }
    std::string d;
    std::vector<uint8_t> stm;
    if (!splitObjStmObjectSlice(slice, d, stm)) {
      continue;
    }
    inlineDict_[objNum] = std::move(d);
    if (!stm.empty()) {
      inlineStream_[objNum] = std::move(stm);
    }
  }
}

bool XrefTable::parse(FsFile& file) {
  offsets.clear();
  rootObjId_ = 0;
  inlineDict_.clear();
  inlineStream_.clear();
  loadedObjStreams_.clear();

  const size_t fileSize = file.fileSize();
  if (fileSize < 32) {
    LOG_ERR("PDF", "xref: file too small");
    return false;
  }

  const size_t tailSize = std::min<size_t>(1024, fileSize);
  const size_t tailStart = fileSize - tailSize;
  auto* tailBuf = static_cast<uint8_t*>(malloc(tailSize));
  if (!tailBuf) {
    LOG_ERR("PDF", "xref: tail malloc failed");
    return false;
  }

  if (!file.seek(tailStart) || file.read(tailBuf, tailSize) != static_cast<int>(tailSize)) {
    free(tailBuf);
    LOG_ERR("PDF", "xref: read tail failed");
    return false;
  }

  const size_t sx = findStartXref(tailBuf, tailSize);
  if (sx == SIZE_MAX) {
    free(tailBuf);
    LOG_ERR("PDF", "xref: startxref not found");
    return false;
  }

  const char* p = reinterpret_cast<const char*>(tailBuf + sx + 9);
  skipWs(p);
  char numBuf[32];
  size_t nb = 0;
  while (*p >= '0' && *p <= '9' && nb + 1 < sizeof(numBuf)) {
    numBuf[nb++] = *p++;
  }
  numBuf[nb] = '\0';
  if (nb == 0) {
    free(tailBuf);
    LOG_ERR("PDF", "xref: bad startxref offset");
    return false;
  }
  const uint32_t xrefOffset = static_cast<uint32_t>(strtoul(numBuf, nullptr, 10));
  free(tailBuf);

  if (xrefOffset >= fileSize) {
    LOG_ERR("PDF", "xref: startxref out of range");
    return false;
  }

  if (!file.seek(xrefOffset)) {
    LOG_ERR("PDF", "xref: seek failed");
    return false;
  }

  char lineBuf[128];
  size_t lineLen = 0;
  readLineFromFile(file, lineBuf, sizeof(lineBuf), lineLen);
  if (std::strncmp(lineBuf, "xref", 4) != 0) {
    return parseXrefStream(file, fileSize, xrefOffset);
  }

  rootObjId_ = 0;
  if (!mergeXrefChainRecursive(file, fileSize, xrefOffset, offsets, rootObjId_)) {
    LOG_ERR("PDF", "xref: merge failed");
    return false;
  }

  bool any = false;
  for (uint32_t v : offsets) {
    if (v != 0) {
      any = true;
      break;
    }
  }
  if (!any) {
    LOG_ERR("PDF", "xref: no objects");
    return false;
  }
  if (rootObjId_ == 0) {
    LOG_ERR("PDF", "xref: missing /Root");
    return false;
  }
  return true;
}

bool XrefTable::readDictForObject(FsFile& file, uint32_t objId, std::string& dictBody) const {
  dictBody.clear();
  const auto it = inlineDict_.find(objId);
  if (it != inlineDict_.end()) {
    dictBody = it->second;
    return !dictBody.empty();
  }
  const uint32_t off = getOffset(objId);
  if (off == 0) {
    return false;
  }
  return PdfObject::readAt(file, off, dictBody, nullptr, nullptr, this);
}

bool XrefTable::readStreamForObject(FsFile& file, uint32_t objId, std::string& dictOut, std::vector<uint8_t>& streamPayload,
                                    bool& flateDecode) const {
  dictOut.clear();
  streamPayload.clear();
  flateDecode = false;

  const auto dit = inlineDict_.find(objId);
  if (dit != inlineDict_.end()) {
    dictOut = dit->second;
    const auto sit = inlineStream_.find(objId);
    if (sit == inlineStream_.end() || sit->second.empty()) {
      return false;
    }
    streamPayload = sit->second;
    flateDecode = dictOut.find("/FlateDecode") != std::string::npos || dictOut.find("/Fl ") != std::string::npos;
    return !dictOut.empty();
  }

  const uint32_t off = getOffset(objId);
  if (off == 0) {
    return false;
  }
  uint32_t so = 0;
  uint32_t sl = 0;
  if (!PdfObject::readAt(file, off, dictOut, &so, &sl, this)) {
    return false;
  }
  flateDecode = dictOut.find("/FlateDecode") != std::string::npos || dictOut.find("/Fl ") != std::string::npos;
  if (sl == 0) {
    return false;
  }
  if (!file.seek(so)) {
    return false;
  }
  streamPayload.resize(sl);
  if (file.read(streamPayload.data(), sl) != static_cast<int>(sl)) {
    streamPayload.clear();
    return false;
  }
  return true;
}

uint32_t XrefTable::getOffset(uint32_t objId) const {
  if (objId >= offsets.size()) return 0;
  return offsets[objId];
}

uint32_t XrefTable::objectCount() const { return static_cast<uint32_t>(offsets.size()); }

uint32_t XrefTable::rootObjId() const { return rootObjId_; }
