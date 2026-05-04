#pragma once
#include <HalStorage.h>

#include <cstdint>
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

inline bool readString(FsFile& file, std::string& s, const uint32_t maxLen) {
  uint32_t len = 0;
  readPod(file, len);
  if (len > maxLen || len > MAX_CACHE_STRING_LEN) {
    s.clear();
    return false;
  }
  s.resize(len);
  if (len == 0) {
    return true;
  }

  const size_t read = file.read(reinterpret_cast<uint8_t*>(&s[0]), len);
  if (read != len) {
    s.clear();
    return false;
  }
  return true;
}
// Deterministic string hash function (FNV-1a 64-bit).
// Returns the same value for identical input across all program invocations.
// Unlike std::hash<std::string>, this is NOT randomized per process — critical
// for cache path generation where stability across reboots is required.
inline uint64_t fnvHash64(const std::string& s) {
  uint64_t hash = 14695981039346656037ull;
  for (const char c : s) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}
}  // namespace serialization
