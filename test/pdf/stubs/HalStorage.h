#pragma once

// Host-only in-memory file stand-in for lib/Pdf tests (shadows lib/hal/HalStorage.h).
#define HAL_STORAGE_STUB 1

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "Print.h"

class HalFile : public Print {
  std::vector<uint8_t> data_;
  std::string writePath_;
  size_t pos_ = 0;
  bool writeMode_ = false;

 public:
  HalFile() = default;

  bool loadPath(const char* path) {
    data_.clear();
    pos_ = 0;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) {
      std::fclose(f);
      return false;
    }
    data_.resize(static_cast<size_t>(sz));
    const size_t rd = std::fread(data_.data(), 1, data_.size(), f);
    std::fclose(f);
    if (rd != data_.size()) {
      data_.clear();
      return false;
    }
    return true;
  }

  bool openForWrite(const char* path) {
    data_.clear();
    writePath_ = path ? path : "";
    pos_ = 0;
    writeMode_ = true;
    return !writePath_.empty();
  }

  void flush() {}
  size_t getName(char*, size_t) { return 0; }
  size_t size() { return data_.size(); }
  size_t fileSize() { return data_.size(); }

  bool seek(size_t p) {
    pos_ = std::min(p, data_.size());
    return true;
  }

  bool seekCur(int64_t offset) {
    if (offset < 0) {
      const uint64_t absOff = static_cast<uint64_t>(-offset);
      if (absOff > pos_) return false;
      pos_ -= static_cast<size_t>(absOff);
      return true;
    }
    pos_ += static_cast<size_t>(offset);
    if (pos_ > data_.size()) pos_ = data_.size();
    return true;
  }

  bool seekSet(size_t offset) { return seek(offset); }

  int available() const { return static_cast<int>(data_.size() - pos_); }

  size_t position() const { return pos_; }

  int read(void* buf, size_t count) {
    const size_t n = std::min(count, data_.size() - pos_);
    if (n > 0) {
      std::memcpy(buf, data_.data() + pos_, n);
    }
    pos_ += n;
    return static_cast<int>(n);
  }

  int read() {
    if (pos_ >= data_.size()) return -1;
    return data_[pos_++];
  }

  size_t write(const void* buf, size_t count) {
    if (!writeMode_ || !buf) {
      return 0;
    }
    if (pos_ + count > data_.size()) {
      data_.resize(pos_ + count);
    }
    std::memcpy(data_.data() + pos_, buf, count);
    pos_ += count;
    return count;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  bool rename(const char*) { return false; }
  bool isDirectory() const { return false; }
  void rewindDirectory() {}
  HalFile openNextFile() { return HalFile(); }
  bool close() {
    if (!writeMode_) {
      return true;
    }
    std::filesystem::create_directories(std::filesystem::path(writePath_).parent_path());
    FILE* f = std::fopen(writePath_.c_str(), "wb");
    if (!f) {
      writeMode_ = false;
      return false;
    }
    const size_t written = data_.empty() ? 0 : std::fwrite(data_.data(), 1, data_.size(), f);
    const bool ok = written == data_.size() && std::fclose(f) == 0;
    writeMode_ = false;
    return ok;
  }
  bool isOpen() const { return writeMode_ || !data_.empty(); }
  explicit operator bool() const { return isOpen(); }

  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
};

using FsFile = HalFile;

class HalStorage {
  static std::string hostPath(const char* path) {
    if (!path) return {};
    constexpr const char* kDevicePrefix = "/.crosspoint";
    constexpr size_t kDevicePrefixLen = 12;
    if (std::strncmp(path, kDevicePrefix, kDevicePrefixLen) == 0) {
      return std::string("test/pdf/build/.crosspoint") + (path + kDevicePrefixLen);
    }
    return path;
  }

 public:
  bool openFileForRead(const char*, const char* path, FsFile& out) { return out.loadPath(hostPath(path).c_str()); }
  bool openFileForWrite(const char*, const char* path, FsFile& out) { return out.openForWrite(hostPath(path).c_str()); }
  bool ensureDirectoryExists(const char* path) {
    if (!path || !path[0]) return false;
    std::filesystem::create_directories(hostPath(path));
    return true;
  }
  bool exists(const char* path) { return path && std::filesystem::exists(hostPath(path)); }
  bool remove(const char* path) { return path && std::filesystem::remove(hostPath(path)); }
  bool rename(const char* from, const char* to) {
    if (!from || !to) return false;
    std::error_code ec;
    std::filesystem::rename(hostPath(from), hostPath(to), ec);
    return !ec;
  }
  bool removeDir(const char* path) {
    if (!path) return false;
    std::filesystem::remove_all(hostPath(path));
    return true;
  }
};

inline HalStorage Storage;
