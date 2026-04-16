#pragma once

#include <string>
#include <vector>

#include "ReaderBookmarkCodec.h"

namespace ReaderBookmarkStore {

std::string bookmarkPath(const std::string& cachePath);
bool load(const std::string& cachePath, std::vector<ReaderBookmark>& bookmarks);
bool save(const std::string& cachePath, const std::vector<ReaderBookmark>& bookmarks);
bool toggle(const std::string& cachePath, const ReaderBookmark& bookmark);

}  // namespace ReaderBookmarkStore
