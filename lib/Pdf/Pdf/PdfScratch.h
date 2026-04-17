#pragma once

#include <cstddef>

#include "PdfFixed.h"
#include "PdfLimits.h"

namespace PdfScratch {

struct ToUnicodeWorkspace {
  PdfFixedString<PDF_OBJECT_BODY_MAX> body;
  PdfFixedString<PDF_OBJECT_BODY_MAX> cmapDict;
  PdfByteBuffer payload;
  PdfByteBuffer decoded;
};

ToUnicodeWorkspace* acquireToUnicodeWorkspace();
void releaseToUnicodeWorkspaceUse();
void releaseRetainedBuffers();
size_t retainedBufferBytes();

}  // namespace PdfScratch
