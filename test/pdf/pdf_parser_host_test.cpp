/**
 * Host-side integration test for lib/Pdf (real sources, in-memory HalFile stub).
 * Build: test/pdf/run_pdf_parser_tests.sh
 */

#include "ContentStream.h"
#include "PageTree.h"
#include "PdfObject.h"
#include "PdfOutline.h"
#include "PdfPage.h"
#include "XrefTable.h"

#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s:%d  (%s)\n", file, line, expr);
    ++g_failures;
  }
}

#define REQUIRE(cond) check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

// Stop this file's checks on first failure chain (avoids cascading bogus failures).
#define REQF(cond)                       \
  do {                                   \
    if (!(cond)) {                       \
      REQUIRE(cond);                     \
      return false;                      \
    }                                    \
  } while (0)

uint32_t contentsObjectId(const std::string& pageBody) {
  std::string cv = PdfObject::getDictValue("/Contents", pageBody);
  while (!cv.empty() && (cv[0] == ' ' || cv[0] == '\t' || cv[0] == '\r' || cv[0] == '\n')) {
    cv.erase(0, 1);
  }
  if (cv.empty()) return 0;
  if (!cv.empty() && cv[0] == '[') {
    const char* p = cv.c_str();
    while (*p && *p != '[') ++p;
    if (*p == '[') ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    char* end = nullptr;
    const unsigned long id = std::strtoul(p, &end, 10);
    if (end == p) return 0;
    return static_cast<uint32_t>(id);
  }
  return PdfObject::getDictRef("/Contents", pageBody);
}

bool parseSinglePage(HalFile& file, const XrefTable& xref, const std::string& pageBody, PdfPage& outPage) {
  const uint32_t contentId = contentsObjectId(pageBody);
  if (contentId == 0) return false;
  std::string contentDict;
  std::vector<uint8_t> payload;
  bool compressed = false;
  if (!xref.readStreamForObject(file, contentId, contentDict, payload, compressed)) return false;
  if (payload.empty()) return false;
  return ContentStream::parseBuffer(payload.data(), payload.size(), compressed, file, xref, pageBody, outPage);
}

struct Expectation {
  const char* filename;
  unsigned minPages;
  unsigned minOutlineEntries;  // 0 = do not check outline count
  unsigned minTextCharsPage0;  // UTF-8 bytes ~chars
  bool requireTextOnPage0;
  unsigned minTextCharsPage1;  // 0 = skip; page 1 is index 1 (second page)
};

// Thresholds are conservative: real PDFs vary; tighten if a file regresses.
const Expectation kExpect[] = {
    {"sample.pdf", 1, 0, 20, true, 0},
    // Cover + body often use CMap/CID fonts; pipeline check only until encoding support improves.
    {"EE-366.pdf", 1, 0, 0, false, 0},
    {"esp32-c6_datasheet_en.pdf", 20, 0, 10, true, 0},
};

const Expectation* findExpect(const char* base) {
  for (const auto& e : kExpect) {
    if (std::strcmp(base, e.filename) == 0) return &e;
  }
  return nullptr;
}

size_t totalTextChars(const PdfPage& p) {
  size_t n = 0;
  for (const auto& b : p.textBlocks) n += b.text.size();
  return n;
}

bool runOnePdf(const char* path) {
  HalFile file;
  REQF(file.loadPath(path));
  const char* slash = std::strrchr(path, '/');
  const char* base = slash ? slash + 1 : path;

  XrefTable xref;
  REQF(xref.parse(file));

  std::string catalogBody;
  const uint32_t rootId = xref.rootObjId();
  REQF(rootId != 0);
  REQF(xref.readDictForObject(file, rootId, catalogBody));

  const uint32_t pagesObjId = PdfObject::getDictRef("/Pages", catalogBody);
  REQF(pagesObjId != 0);

  PageTree pageTree;
  REQF(pageTree.parse(file, xref, pagesObjId));
  const uint32_t pageCount = pageTree.pageCount();
  REQF(pageCount > 0);

  const Expectation* exp = findExpect(base);
  if (exp) {
    REQF(pageCount >= exp->minPages);
  }

  std::vector<PdfOutlineEntry> outline;
  const uint32_t outlinesId = PdfObject::getDictRef("/Outlines", catalogBody);
  if (outlinesId != 0) {
    REQF(PdfOutlineParser::parse(file, xref, pageTree, outlinesId, outline));
    if (exp && exp->minOutlineEntries > 0) {
      REQF(outline.size() >= exp->minOutlineEntries);
    }
  }

  PdfPage page0;
  {
    const uint32_t pageObjId = pageTree.getPageObjectId(0);
    REQF(pageObjId != 0);
    std::string pageBody;
    REQF(xref.readDictForObject(file, pageObjId, pageBody));
    REQF(parseSinglePage(file, xref, pageBody, page0));
  }

  if (exp && exp->requireTextOnPage0) {
    REQF(totalTextChars(page0) >= exp->minTextCharsPage0);
  }

  if (pageCount > 1) {
    PdfPage page1;
    const uint32_t pageObjId = pageTree.getPageObjectId(1);
    REQF(pageObjId != 0);
    std::string pageBody;
    REQF(xref.readDictForObject(file, pageObjId, pageBody));
    REQF(parseSinglePage(file, xref, pageBody, page1));
    if (exp && exp->minTextCharsPage1 > 0) {
      REQF(totalTextChars(page1) >= exp->minTextCharsPage1);
    }
  } else if (exp && exp->minTextCharsPage1 > 0) {
    REQF(false);
  }

  std::printf("OK  %s  pages=%u  outline=%zu  page0_blocks=%zu  page0_chars~%zu\n", base,
              static_cast<unsigned>(pageCount), outline.size(), page0.textBlocks.size(), totalTextChars(page0));
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<const char*> paths;
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) paths.push_back(argv[i]);
  } else {
    paths = {"test/pdf/sample.pdf", "test/pdf/EE-366.pdf", "test/pdf/esp32-c6_datasheet_en.pdf"};
  }

  for (const char* path : paths) {
    runOnePdf(path);
  }

  if (g_failures != 0) {
    std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("\nAll PDF parser checks passed.\n");
  return 0;
}
