#include "ReaderBookmarkCodec.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {
constexpr char kHeader[] = "BKM1";
constexpr size_t kMaxBookmarks = 128;

uint8_t clampPercent(uint32_t percent) { return static_cast<uint8_t>(std::min<uint32_t>(percent, 100)); }

bool parseUint(const std::string& text, uint32_t& out) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

}  // namespace

namespace ReaderBookmarkCodec {

std::string sanitizeSnippet(const std::string& text, const size_t maxChars) {
  std::string out;
  out.reserve(std::min(text.size(), maxChars));
  bool lastWasSpace = true;
  for (const char ch : text) {
    const bool isSpace = ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    if (isSpace) {
      if (!lastWasSpace && out.size() < maxChars) {
        out.push_back(' ');
      }
      lastWasSpace = true;
      continue;
    }
    if (out.size() >= maxChars) {
      break;
    }
    out.push_back(ch);
    lastWasSpace = false;
  }
  if (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

std::string firstWords(const std::string& text, const size_t maxWords, const size_t maxChars) {
  std::string out;
  size_t words = 0;
  bool inWord = false;
  for (const char ch : text) {
    const bool isSpace = ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
    if (isSpace) {
      inWord = false;
      if (!out.empty() && out.back() != ' ' && out.size() < maxChars) {
        out.push_back(' ');
      }
      continue;
    }
    if (!inWord) {
      if (words >= maxWords) {
        break;
      }
      ++words;
      inWord = true;
    }
    if (out.size() >= maxChars) {
      break;
    }
    out.push_back(ch);
  }
  return sanitizeSnippet(out, maxChars);
}

bool parse(const std::string& data, std::vector<ReaderBookmark>& out) {
  out.clear();
  std::istringstream stream(data);
  std::string line;
  if (!std::getline(stream, line) || line != kHeader) {
    return false;
  }

  while (out.size() < kMaxBookmarks && std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }
    const auto firstTab = line.find('\t');
    const auto secondTab = firstTab == std::string::npos ? std::string::npos : line.find('\t', firstTab + 1);
    const auto thirdTab = secondTab == std::string::npos ? std::string::npos : line.find('\t', secondTab + 1);
    if (firstTab == std::string::npos || secondTab == std::string::npos || thirdTab == std::string::npos) {
      continue;
    }

    uint32_t primary = 0;
    uint32_t secondary = 0;
    uint32_t percent = 0;
    if (!parseUint(line.substr(0, firstTab), primary) ||
        !parseUint(line.substr(firstTab + 1, secondTab - firstTab - 1), secondary) ||
        !parseUint(line.substr(secondTab + 1, thirdTab - secondTab - 1), percent)) {
      continue;
    }

    out.push_back(
        ReaderBookmark{primary, secondary, clampPercent(percent), sanitizeSnippet(line.substr(thirdTab + 1))});
  }
  return true;
}

std::string serialize(const std::vector<ReaderBookmark>& bookmarks) {
  std::string out = std::string(kHeader) + "\n";
  for (const auto& bookmark : bookmarks) {
    out += std::to_string(bookmark.primary);
    out += '\t';
    out += std::to_string(bookmark.secondary);
    out += '\t';
    out += std::to_string(bookmark.percent);
    out += '\t';
    out += sanitizeSnippet(bookmark.snippet);
    out += '\n';
  }
  return out;
}

const ReaderBookmark* find(const std::vector<ReaderBookmark>& bookmarks, const uint32_t primary,
                           const uint32_t secondary) {
  const auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [primary, secondary](const ReaderBookmark& b) {
    return b.primary == primary && b.secondary == secondary;
  });
  return it == bookmarks.end() ? nullptr : &*it;
}

bool toggle(std::vector<ReaderBookmark>& bookmarks, const ReaderBookmark& bookmark) {
  const auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&bookmark](const ReaderBookmark& b) {
    return b.primary == bookmark.primary && b.secondary == bookmark.secondary;
  });
  if (it != bookmarks.end()) {
    bookmarks.erase(it);
    return false;
  }

  bookmarks.push_back(bookmark);
  std::sort(bookmarks.begin(), bookmarks.end(), [](const ReaderBookmark& a, const ReaderBookmark& b) {
    if (a.primary != b.primary) {
      return a.primary < b.primary;
    }
    return a.secondary < b.secondary;
  });
  if (bookmarks.size() > kMaxBookmarks) {
    bookmarks.resize(kMaxBookmarks);
  }
  return true;
}

}  // namespace ReaderBookmarkCodec
