#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Pdf/PdfOutline.h"
#include "Pdf/PdfPage.h"

class Pdf {
 public:
  struct Impl;

 private:
  std::unique_ptr<Impl> impl;

  explicit Pdf(std::unique_ptr<Impl> impl);

 public:
  ~Pdf();

  static std::unique_ptr<Pdf> open(const std::string& path);

  uint32_t pageCount() const;
  const std::vector<PdfOutlineEntry>& outline() const;
  const std::string& filePath() const;

  std::unique_ptr<PdfPage> getPage(uint32_t pageNum);

  bool saveProgress(uint32_t page);
  bool loadProgress(uint32_t& page);

  size_t extractImageStream(const PdfImageDescriptor& img, uint8_t* outBuf, size_t maxBytes);

  const std::string& cacheDirectory() const;
};
