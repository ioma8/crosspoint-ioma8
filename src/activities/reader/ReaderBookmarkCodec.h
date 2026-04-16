#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ReaderBookmark {
  uint32_t primary = 0;
  uint32_t secondary = 0;
  uint8_t percent = 0;
  std::string snippet;
};

namespace ReaderBookmarkCodec {

std::string sanitizeSnippet(const std::string& text, size_t maxChars = 90);
std::string firstWords(const std::string& text, size_t maxWords = 12, size_t maxChars = 90);
bool parse(const std::string& data, std::vector<ReaderBookmark>& out);
std::string serialize(const std::vector<ReaderBookmark>& bookmarks);
const ReaderBookmark* find(const std::vector<ReaderBookmark>& bookmarks, uint32_t primary, uint32_t secondary);
bool toggle(std::vector<ReaderBookmark>& bookmarks, const ReaderBookmark& bookmark);

}  // namespace ReaderBookmarkCodec
