#pragma once
#include <HalStorage.h>

#include <string_view>

#include "PdfPage.h"
#include "XrefTable.h"

class ContentStream {
 public:
  // `pageObjectBody` is the page dict (for /Resources → /XObject lookup on Do).
  static bool parse(FsFile& file, uint32_t streamOffset, uint32_t streamLen, bool isCompressed, const XrefTable& xref,
                    std::string_view pageObjectBody, PdfPage& outPage);
  // Same as parse but stream bytes are already in memory (e.g. from an object stream).
  static bool parseBuffer(const uint8_t* streamBytes, size_t streamLen, bool isCompressed, FsFile& file,
                          const XrefTable& xref, std::string_view pageObjectBody, PdfPage& outPage);
};
