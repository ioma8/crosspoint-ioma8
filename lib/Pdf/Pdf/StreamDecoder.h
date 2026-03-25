#pragma once
#include <HalStorage.h>

// Wraps InflateReader/uzlib to FlateDecode a PDF stream.
// Reads up to `maxOutBytes` of decompressed data into `outBuf`.
// Returns number of bytes written, or 0 on error.
class StreamDecoder {
 public:
  static size_t flateDecode(FsFile& file, uint32_t streamOffset, uint32_t compressedLen, uint8_t* outBuf,
                            size_t maxOutBytes);
  static size_t flateDecodeBytes(const uint8_t* compressed, size_t compressedLen, uint8_t* outBuf, size_t maxOutBytes);
};
