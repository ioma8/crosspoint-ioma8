#include "PageProgressStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

std::string progressPath(const std::string& cachePath) { return cachePath + "/progress.bin"; }

}  // namespace

namespace PageProgressStore {

bool save(const char* logTag, const std::string& cachePath, const uint32_t page) {
  FsFile file;
  if (!Storage.openFileForWrite(logTag, progressPath(cachePath), file)) {
    return false;
  }

  const uint8_t data[4] = {
      static_cast<uint8_t>(page & 0xFF),
      static_cast<uint8_t>((page >> 8) & 0xFF),
      static_cast<uint8_t>((page >> 16) & 0xFF),
      static_cast<uint8_t>((page >> 24) & 0xFF),
  };
  file.write(data, sizeof(data));
  file.close();
  return true;
}

bool load(const char* logTag, const std::string& cachePath, uint32_t& page) {
  FsFile file;
  if (!Storage.openFileForRead(logTag, progressPath(cachePath), file)) {
    return false;
  }

  uint8_t data[4];
  const bool ok = file.read(data, sizeof(data)) == sizeof(data);
  file.close();
  if (!ok) {
    LOG_ERR(logTag, "Invalid progress file: %s", progressPath(cachePath).c_str());
    return false;
  }

  page = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  return true;
}

}  // namespace PageProgressStore
