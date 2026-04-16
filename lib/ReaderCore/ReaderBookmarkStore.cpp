#include "ReaderBookmarkStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace ReaderBookmarkStore {

std::string bookmarkPath(const std::string& cachePath) { return cachePath + "/bookmarks.txt"; }

bool load(const std::string& cachePath, std::vector<ReaderBookmark>& bookmarks) {
  bookmarks.clear();
  FsFile file;
  if (!Storage.openFileForRead("BKM", bookmarkPath(cachePath), file)) {
    return true;
  }

  std::string data;
  data.reserve(file.size());
  while (file.available()) {
    const int ch = file.read();
    if (ch < 0) {
      break;
    }
    data.push_back(static_cast<char>(ch));
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
