#include "PdfCache.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr uint8_t kMetaVersion = 1;
constexpr uint8_t kPageVersionV1 = 1;
constexpr uint8_t kPageVersionV2 = 2;

}  // namespace

PdfCache::PdfCache(const std::string& pdfFilePath) {
  const size_t hash = std::hash<std::string>{}(pdfFilePath);
  char buf[64];
  snprintf(buf, sizeof(buf), "/.crosspoint/pdf_%zu", hash);
  cacheDir = buf;
}

bool PdfCache::loadMeta(uint32_t& pageCount, std::vector<PdfOutlineEntry>& outline) {
  outline.clear();
  const std::string path = cacheDir + "/meta.bin";
  FsFile f;
  if (!Storage.openFileForRead("PDF", path, f)) {
    return false;
  }
  uint8_t ver = 0;
  serialization::readPod(f, ver);
  if (ver != kMetaVersion) {
    f.close();
    return false;
  }
  serialization::readPod(f, pageCount);
  uint32_t outlineCount = 0;
  serialization::readPod(f, outlineCount);
  if (outlineCount > 512) {
    f.close();
    return false;
  }
  outline.reserve(outlineCount);
  for (uint32_t i = 0; i < outlineCount; ++i) {
    PdfOutlineEntry e;
    serialization::readPod(f, e.pageNum);
    serialization::readString(f, e.title);
    outline.push_back(std::move(e));
  }
  f.close();
  return true;
}

bool PdfCache::saveMeta(uint32_t pageCount, const std::vector<PdfOutlineEntry>& outline) {
  Storage.ensureDirectoryExists(cacheDir.c_str());
  const std::string path = cacheDir + "/meta.bin";
  FsFile f;
  if (!Storage.openFileForWrite("PDF", path, f)) {
    return false;
  }
  serialization::writePod(f, kMetaVersion);
  serialization::writePod(f, pageCount);
  const uint32_t outlineCount = static_cast<uint32_t>(std::min(outline.size(), size_t{512}));
  serialization::writePod(f, outlineCount);
  for (uint32_t i = 0; i < outlineCount; ++i) {
    serialization::writePod(f, outline[i].pageNum);
    serialization::writeString(f, outline[i].title);
  }
  f.close();
  return true;
}

bool PdfCache::loadPage(uint32_t pageNum, PdfPage& outPage) {
  outPage.textBlocks.clear();
  outPage.images.clear();
  outPage.drawOrder.clear();

  char name[32];
  snprintf(name, sizeof(name), "%u", static_cast<unsigned>(pageNum));
  const std::string path = cacheDir + "/pages/" + name + ".bin";
  FsFile f;
  if (!Storage.openFileForRead("PDF", path, f)) {
    return false;
  }

  uint8_t ver = 0;
  serialization::readPod(f, ver);
  if (ver != kPageVersionV1 && ver != kPageVersionV2) {
    f.close();
    return false;
  }

  uint32_t textCount = 0;
  serialization::readPod(f, textCount);
  if (textCount > 10000) {
    f.close();
    return false;
  }
  outPage.textBlocks.resize(textCount);
  for (uint32_t i = 0; i < textCount; ++i) {
    serialization::readString(f, outPage.textBlocks[i].text);
    serialization::readPod(f, outPage.textBlocks[i].orderHint);
  }

  uint32_t imageCount = 0;
  serialization::readPod(f, imageCount);
  if (imageCount > 1024) {
    f.close();
    return false;
  }
  outPage.images.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    serialization::readPod(f, outPage.images[i].pdfStreamOffset);
    serialization::readPod(f, outPage.images[i].pdfStreamLength);
    serialization::readPod(f, outPage.images[i].width);
    serialization::readPod(f, outPage.images[i].height);
    serialization::readPod(f, outPage.images[i].format);
  }

  if (ver >= kPageVersionV2) {
    uint32_t drawCount = 0;
    serialization::readPod(f, drawCount);
    if (drawCount > 20000) {
      f.close();
      return false;
    }
    outPage.drawOrder.resize(drawCount);
    for (uint32_t i = 0; i < drawCount; ++i) {
      uint8_t im = 0;
      serialization::readPod(f, im);
      serialization::readPod(f, outPage.drawOrder[i].index);
      outPage.drawOrder[i].isImage = im != 0;
    }
  } else {
    for (uint32_t i = 0; i < textCount; ++i) {
      outPage.drawOrder.push_back({false, i});
    }
    for (uint32_t j = 0; j < imageCount; ++j) {
      outPage.drawOrder.push_back({true, j});
    }
  }

  f.close();
  return true;
}

bool PdfCache::savePage(uint32_t pageNum, const PdfPage& page) {
  const std::string pagesDir = cacheDir + "/pages";
  Storage.ensureDirectoryExists(cacheDir.c_str());
  Storage.ensureDirectoryExists(pagesDir.c_str());

  char name[32];
  snprintf(name, sizeof(name), "%u", static_cast<unsigned>(pageNum));
  const std::string path = pagesDir + "/" + name + ".bin";
  FsFile f;
  if (!Storage.openFileForWrite("PDF", path, f)) {
    return false;
  }

  serialization::writePod(f, kPageVersionV2);
  const uint32_t textCount = static_cast<uint32_t>(page.textBlocks.size());
  serialization::writePod(f, textCount);
  for (uint32_t i = 0; i < textCount; ++i) {
    serialization::writeString(f, page.textBlocks[i].text);
    serialization::writePod(f, page.textBlocks[i].orderHint);
  }
  const uint32_t imageCount = static_cast<uint32_t>(page.images.size());
  serialization::writePod(f, imageCount);
  for (uint32_t i = 0; i < imageCount; ++i) {
    serialization::writePod(f, page.images[i].pdfStreamOffset);
    serialization::writePod(f, page.images[i].pdfStreamLength);
    serialization::writePod(f, page.images[i].width);
    serialization::writePod(f, page.images[i].height);
    serialization::writePod(f, page.images[i].format);
  }
  const uint32_t drawCount = static_cast<uint32_t>(page.drawOrder.size());
  serialization::writePod(f, drawCount);
  for (uint32_t i = 0; i < drawCount; ++i) {
    const uint8_t im = page.drawOrder[i].isImage ? uint8_t{1} : uint8_t{0};
    serialization::writePod(f, im);
    serialization::writePod(f, page.drawOrder[i].index);
  }
  f.close();
  return true;
}

bool PdfCache::loadProgress(uint32_t& currentPage) {
  const std::string path = cacheDir + "/progress.bin";
  FsFile f;
  if (!Storage.openFileForRead("PDF", path, f)) {
    currentPage = 0;
    return false;
  }
  serialization::readPod(f, currentPage);
  f.close();
  return true;
}

bool PdfCache::saveProgress(uint32_t currentPage) {
  Storage.ensureDirectoryExists(cacheDir.c_str());
  const std::string path = cacheDir + "/progress.bin";
  FsFile f;
  if (!Storage.openFileForWrite("PDF", path, f)) {
    return false;
  }
  serialization::writePod(f, currentPage);
  f.close();
  return true;
}

void PdfCache::invalidate() { Storage.removeDir(cacheDir.c_str()); }

const std::string& PdfCache::getCacheDir() const { return cacheDir; }
