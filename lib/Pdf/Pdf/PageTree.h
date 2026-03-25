#pragma once
#include <HalStorage.h>

#include <vector>

#include "XrefTable.h"

class PageTree {
  std::vector<uint32_t> pageOffsets;    // byte offset of each page object
  std::vector<uint32_t> pageObjectIds;  // parallel PDF object ids (for outline dest resolution)
 public:
  bool parse(FsFile& file, const XrefTable& xref, uint32_t rootObjId);
  uint32_t pageCount() const;
  uint32_t getPageOffset(uint32_t pageIndex) const;
  uint32_t getPageObjectId(uint32_t pageIndex) const;
  uint32_t pageIndexForObjectId(uint32_t objId) const;
};
