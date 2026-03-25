#pragma once
#include <HalStorage.h>

#include <string>

class XrefTable;

// Minimal PDF object reader.
// Seeks to offset, reads past "id gen obj", returns the raw body as a string.
// For stream objects: bodyStr contains the dictionary, streamOffset is set.
// When `xrefForIndirectLength` is set, /Length given as "n g R" is resolved via the xref table.
class PdfObject {
 public:
  static bool readAt(FsFile& file, uint32_t offset, std::string& bodyStr, uint32_t* streamOffset = nullptr,
                     uint32_t* streamLength = nullptr, const XrefTable* xrefForIndirectLength = nullptr);

  static std::string getDictValue(const char* key, const std::string& dict);
  static int getDictInt(const char* key, const std::string& dict, int defaultVal = 0);
  static uint32_t getDictRef(const char* key, const std::string& dict);
};
