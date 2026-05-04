#include "ReaderBookmarkStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <memory>
#include <new>

namespace ReaderBookmarkStore {
namespace {
constexpr size_t MAX_BOOKMARK_FILE_BYTES = 16 * 1024;
constexpr size_t BOOKMARK_READ_CHUNK_BYTES = 256;
}

std::string bookmarkPath(const std::string& cachePath) { return cachePath + "/bookmarks.txt"; }

bool load(const std::string& cachePath, std::vector<ReaderBookmark>& bookmarks) {
  bookmarks.clear();
  FsFile file;
  if (!Storage.openFileForRead("BKM", bookmarkPath(cachePath), file)) {
    return true;
  }

  const size_t fileSize = file.size();
  if (fileSize > MAX_BOOKMARK_FILE_BYTES) {
    LOG_ERR("BKM", "Bookmark file too large: %u", static_cast<unsigned>(fileSize));
    file.close();
    return false;
  }

  std::string data;
  data.reserve(fileSize);
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[BOOKMARK_READ_CHUNK_BYTES]);
  if (!buffer) {
    LOG_ERR("BKM", "Failed to allocate bookmark read buffer");
    file.close();
    return false;
  }
  while (file.available()) {
    const size_t bytesRead = file.read(buffer.get(), BOOKMARK_READ_CHUNK_BYTES);
    if (bytesRead == 0) {
      break;
    }
    data.append(reinterpret_cast<const char*>(buffer.get()), bytesRead);
  }
  file.close();

  if (!ReaderBookmarkCodec::parse(data, bookmarks)) {
    LOG_ERR("BKM", "Invalid bookmark file: %s", bookmarkPath(cachePath).c_str());
    bookmarks.clear();
    return false;
  }
  return true;
}

bool save(const std::string& cachePath, const std::vector<ReaderBookmark>& bookmarks) {
  FsFile file;
  if (!Storage.openFileForWrite("BKM", bookmarkPath(cachePath), file)) {
    LOG_ERR("BKM", "Could not save bookmarks: %s", bookmarkPath(cachePath).c_str());
    return false;
  }
  const std::string data = ReaderBookmarkCodec::serialize(bookmarks);
  file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  file.close();
  return true;
}

bool toggle(const std::string& cachePath, const ReaderBookmark& bookmark) {
  std::vector<ReaderBookmark> bookmarks;
  load(cachePath, bookmarks);
  ReaderBookmarkCodec::toggle(bookmarks, bookmark);
  return save(cachePath, bookmarks);
}

bool toggleAndReload(const std::string& cachePath, const ReaderBookmark& bookmark,
                     std::vector<ReaderBookmark>& bookmarks) {
  if (!toggle(cachePath, bookmark)) {
    return false;
  }
  return load(cachePath, bookmarks);
}

}  // namespace ReaderBookmarkStore
