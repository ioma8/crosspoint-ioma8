#include "PageTree.h"

#include <Logging.h>

#include <vector>

#include "PdfObject.h"

namespace {

void trimInPlace(std::string& s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.erase(0, 1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.pop_back();
  }
}

bool typeIs(const std::string& body, const char* name) {
  std::string t = PdfObject::getDictValue("/Type", body);
  trimInPlace(t);
  return t == name;
}

void parseKidsRefs(const std::string& arr, std::vector<uint32_t>& out) {
  out.clear();
  const char* p = arr.c_str();
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
  if (*p == '[') ++p;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p == ']' || *p == '\0') break;
    char* end = nullptr;
    const unsigned long id = std::strtoul(p, &end, 10);
    if (end == p) {
      ++p;
      continue;
    }
    p = end;
    while (*p == ' ' || *p == '\t') ++p;
    std::strtoul(p, const_cast<char**>(&end), 10);
    p = end;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 'R' || (*p == 'r')) {
      ++p;
      out.push_back(static_cast<uint32_t>(id));
    }
  }
}

}  // namespace

bool PageTree::parse(FsFile& file, const XrefTable& xref, uint32_t pagesObjId) {
  pageOffsets.clear();
  pageObjectIds.clear();

  std::vector<uint32_t> stack;
  stack.push_back(pagesObjId);

  while (!stack.empty() && pageOffsets.size() < 9999) {
    const uint32_t objId = stack.back();
    stack.pop_back();

    std::string body;
    if (!xref.readDictForObject(file, objId, body)) continue;

    if (typeIs(body, "/Pages")) {
      const std::string kidsStr = PdfObject::getDictValue("/Kids", body);
      std::vector<uint32_t> kids;
      parseKidsRefs(kidsStr, kids);
      for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        stack.push_back(*it);
      }
    } else if (typeIs(body, "/Page")) {
      pageOffsets.push_back(xref.getOffset(objId));
      pageObjectIds.push_back(objId);
    }
  }

  if (pageOffsets.empty()) {
    LOG_ERR("PDF", "PageTree: no pages");
    return false;
  }
  return true;
}

uint32_t PageTree::pageCount() const { return static_cast<uint32_t>(pageOffsets.size()); }

uint32_t PageTree::getPageOffset(uint32_t pageIndex) const {
  if (pageIndex >= pageOffsets.size()) return 0;
  return pageOffsets[pageIndex];
}

uint32_t PageTree::getPageObjectId(uint32_t pageIndex) const {
  if (pageIndex >= pageObjectIds.size()) return 0;
  return pageObjectIds[pageIndex];
}

uint32_t PageTree::pageIndexForObjectId(uint32_t objId) const {
  for (size_t i = 0; i < pageObjectIds.size(); ++i) {
    if (pageObjectIds[i] == objId) return static_cast<uint32_t>(i);
  }
  return 0;
}
