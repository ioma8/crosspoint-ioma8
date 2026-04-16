#include <cassert>
#include <iostream>
#include <vector>

#include "ReaderBookmarkCodec.h"

int main() {
  std::vector<ReaderBookmark> bookmarks;

  assert(ReaderBookmarkCodec::toggle(bookmarks, ReaderBookmark{5, 2, 42, "  First\tfew\nwords  "}));
  assert(bookmarks.size() == 1);
  assert(bookmarks[0].snippet == "  First\tfew\nwords  ");
  assert(!ReaderBookmarkCodec::toggle(bookmarks, ReaderBookmark{5, 2, 42, "ignored"}));
  assert(bookmarks.empty());

  ReaderBookmarkCodec::toggle(bookmarks, ReaderBookmark{9, 0, 90, "later"});
  ReaderBookmarkCodec::toggle(bookmarks, ReaderBookmark{1, 3, 10, "earlier"});
  assert(bookmarks[0].primary == 1);
  assert(bookmarks[0].secondary == 3);

  const std::string serialized = ReaderBookmarkCodec::serialize(bookmarks);
  std::vector<ReaderBookmark> parsed;
  assert(ReaderBookmarkCodec::parse(serialized, parsed));
  assert(parsed.size() == 2);
  assert(parsed[0].snippet == "earlier");
  assert(ReaderBookmarkCodec::find(parsed, 9, 0) != nullptr);
  assert(ReaderBookmarkCodec::find(parsed, 7, 0) == nullptr);

  const std::string words =
      ReaderBookmarkCodec::firstWords("  One two\tthree\nfour five six seven eight nine ten eleven twelve thirteen");
  assert(words == "One two three four five six seven eight nine ten eleven twelve");

  std::cout << "reader bookmark codec tests passed\n";
  return 0;
}
