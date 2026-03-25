#include "Pdf.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Pdf/ContentStream.h"
#include "Pdf/PageTree.h"
#include "Pdf/PdfCache.h"
#include "Pdf/PdfObject.h"
#include "Pdf/PdfOutline.h"
#include "Pdf/XrefTable.h"

namespace {

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

}  // namespace

struct Pdf::Impl {
  std::string path;
  FsFile file;
  XrefTable xref;
  PageTree pageTree;
  std::vector<PdfOutlineEntry> outlineEntries;
  std::unique_ptr<PdfCache> cache;
  uint32_t pages = 0;
};

Pdf::Pdf(std::unique_ptr<Impl> implIn) : impl(std::move(implIn)) {}
Pdf::~Pdf() = default;

std::unique_ptr<Pdf> Pdf::open(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("PDF", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  impl->path = path;

  if (!Storage.openFileForRead("PDF", path, impl->file)) {
    LOG_ERR("PDF", "Cannot open: %s", path.c_str());
    return nullptr;
  }

  if (!impl->xref.parse(impl->file)) {
    LOG_ERR("PDF", "Failed to parse xref: %s", path.c_str());
    return nullptr;
  }

  std::string catalogBody;
  const uint32_t rootId = impl->xref.rootObjId();
  if (rootId == 0 || !impl->xref.readDictForObject(impl->file, rootId, catalogBody)) {
    LOG_ERR("PDF", "Bad catalog");
    return nullptr;
  }

  const uint32_t pagesObjId = PdfObject::getDictRef("/Pages", catalogBody);
  if (pagesObjId == 0 || !impl->pageTree.parse(impl->file, impl->xref, pagesObjId)) {
    LOG_ERR("PDF", "Failed to parse page tree");
    return nullptr;
  }

  impl->pages = impl->pageTree.pageCount();
  impl->cache = std::make_unique<PdfCache>(path);

  uint32_t cachedPageCount = 0;
  if (!impl->cache->loadMeta(cachedPageCount, impl->outlineEntries) || cachedPageCount != impl->pages) {
    impl->outlineEntries.clear();
    const uint32_t outlinesId = PdfObject::getDictRef("/Outlines", catalogBody);
    if (outlinesId != 0) {
      PdfOutlineParser::parse(impl->file, impl->xref, impl->pageTree, outlinesId, impl->outlineEntries);
    }
    impl->cache->saveMeta(impl->pages, impl->outlineEntries);
  }

  return std::unique_ptr<Pdf>(new Pdf(std::move(impl)));
}

uint32_t Pdf::pageCount() const { return impl ? impl->pages : 0; }

const std::vector<PdfOutlineEntry>& Pdf::outline() const {
  static const std::vector<PdfOutlineEntry> empty;
  return impl ? impl->outlineEntries : empty;
}

const std::string& Pdf::filePath() const {
  static const std::string empty;
  return impl ? impl->path : empty;
}

bool Pdf::saveProgress(uint32_t page) { return impl && impl->cache && impl->cache->saveProgress(page); }

bool Pdf::loadProgress(uint32_t& page) { return impl && impl->cache && impl->cache->loadProgress(page); }

std::unique_ptr<PdfPage> Pdf::getPage(uint32_t pageNum) {
  if (!impl || pageNum >= impl->pages) {
    return nullptr;
  }
  auto page = std::make_unique<PdfPage>();
  if (impl->cache->loadPage(pageNum, *page)) {
    return page;
  }

  const uint32_t pageObjId = impl->pageTree.getPageObjectId(pageNum);
  if (pageObjId == 0) {
    return nullptr;
  }

  std::string pageBody;
  if (!impl->xref.readDictForObject(impl->file, pageObjId, pageBody)) {
    return nullptr;
  }

  const uint32_t contentId = contentsObjectId(pageBody);
  if (contentId == 0) {
    LOG_ERR("PDF", "No /Contents for page %u", static_cast<unsigned>(pageNum));
    return nullptr;
  }

  std::string contentDict;
  std::vector<uint8_t> streamPayload;
  bool compressed = false;
  if (!impl->xref.readStreamForObject(impl->file, contentId, contentDict, streamPayload, compressed)) {
    return nullptr;
  }
  if (streamPayload.empty()) {
    return nullptr;
  }

  if (!ContentStream::parseBuffer(streamPayload.data(), streamPayload.size(), compressed, impl->file, impl->xref,
                                   pageBody, *page)) {
    LOG_ERR("PDF", "Failed to parse page %u", static_cast<unsigned>(pageNum));
    return nullptr;
  }

  impl->cache->savePage(pageNum, *page);
  return page;
}

const std::string& Pdf::cacheDirectory() const {
  static const std::string empty;
  if (!impl || !impl->cache) {
    return empty;
  }
  return impl->cache->getCacheDir();
}

size_t Pdf::extractImageStream(const PdfImageDescriptor& img, uint8_t* outBuf, size_t maxBytes) {
  if (!impl || !outBuf || maxBytes == 0 || img.pdfStreamLength == 0) {
    return 0;
  }
  if (!impl->file.seek(img.pdfStreamOffset)) {
    return 0;
  }
  const size_t toRead = std::min(maxBytes, static_cast<size_t>(img.pdfStreamLength));
  const int r = impl->file.read(outBuf, toRead);
  if (r <= 0) {
    return 0;
  }
  return static_cast<size_t>(r);
}
