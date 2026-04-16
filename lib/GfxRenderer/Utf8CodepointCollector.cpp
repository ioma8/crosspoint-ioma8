#include "Utf8CodepointCollector.h"

namespace {

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

void Utf8CodepointCollector::clear() { count_ = 0; }

bool Utf8CodepointCollector::add(uint32_t codepoint) {
  for (size_t i = 0; i < count_; ++i) {
    if (codepoints_[i] == codepoint) {
      return true;
    }
  }
  if (full()) {
    return false;
  }
  codepoints_[count_++] = codepoint;
  return true;
}

std::string Utf8CodepointCollector::toUtf8String() const {
  std::string out;
  out.reserve(count_ * 2);
  for (size_t i = 0; i < count_; ++i) {
    appendUtf8(out, codepoints_[i]);
  }
  return out;
}
