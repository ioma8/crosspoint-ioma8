#pragma once
#include <string>
#include <vector>

#include "PdfOutline.h"
#include "PdfPage.h"

class PdfCache {
  std::string cacheDir;

 public:
  explicit PdfCache(const std::string& pdfFilePath);
  bool loadMeta(uint32_t& pageCount, std::vector<PdfOutlineEntry>& outline);
  bool saveMeta(uint32_t pageCount, const std::vector<PdfOutlineEntry>& outline);
  bool loadPage(uint32_t pageNum, PdfPage& outPage);
  bool savePage(uint32_t pageNum, const PdfPage& page);
  bool loadProgress(uint32_t& currentPage);
  bool saveProgress(uint32_t currentPage);
  void invalidate();  // delete all cache files
  const std::string& getCacheDir() const;
};
