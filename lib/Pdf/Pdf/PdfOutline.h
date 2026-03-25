#pragma once
#include <HalStorage.h>

#include <string>
#include <vector>

#include "XrefTable.h"

class PageTree;

struct PdfOutlineEntry {
  std::string title;
  uint32_t pageNum = 0;
};

class PdfOutlineParser {
 public:
  static bool parse(FsFile& file, const XrefTable& xref, const PageTree& pageTree, uint32_t outlinesObjId,
                    std::vector<PdfOutlineEntry>& outEntries);
};
