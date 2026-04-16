#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class Utf8CodepointCollector {
 public:
  static constexpr size_t kMaxCodepoints = 512;

  void clear();
  bool add(uint32_t codepoint);
  [[nodiscard]] size_t size() const { return count_; }
  [[nodiscard]] bool full() const { return count_ >= kMaxCodepoints; }
  [[nodiscard]] std::string toUtf8String() const;

 private:
  uint32_t codepoints_[kMaxCodepoints] = {};
  size_t count_ = 0;
};
