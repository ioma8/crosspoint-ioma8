#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
constexpr uint32_t MAX_CACHE_STRING_LEN = 65536;

template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len = 0;
  readPod(is, len);
  if (len > MAX_CACHE_STRING_LEN) {
    s.clear();
    is.setstate(std::ios::failbit);
    return;
  }
  s.resize(len);
  if (len > 0) {
    is.read(&s[0], len);
    if (is.gcount() != static_cast<std::streamsize>(len)) {
      s.clear();
      is.setstate(std::ios::failbit);
    }
  }
}

inline void readString(FsFile& file, std::string& s) {
  uint32_t len = 0;
  readPod(file, len);
  if (len > MAX_CACHE_STRING_LEN) {
    s.clear();
    return;
  }
  s.resize(len);
  if (len > 0) {
    const size_t read = file.read(reinterpret_cast<uint8_t*>(&s[0]), len);
    if (read != len) {
      s.clear();
    }
  }
}
}  // namespace serialization
