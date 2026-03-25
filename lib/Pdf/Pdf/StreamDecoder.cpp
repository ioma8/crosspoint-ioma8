#include "StreamDecoder.h"

#include <InflateReader.h>
#include <Logging.h>

#include <cstdlib>

size_t StreamDecoder::flateDecode(FsFile& file, uint32_t streamOffset, uint32_t compressedLen, uint8_t* outBuf,
                                  size_t maxOutBytes) {
  if (!outBuf || maxOutBytes == 0 || compressedLen == 0) return 0;
  if (!file.seek(streamOffset)) return 0;

  auto* inbuf = static_cast<uint8_t*>(malloc(compressedLen));
  if (!inbuf) {
    LOG_ERR("PDF", "StreamDecoder: malloc inbuf failed");
    return 0;
  }
  if (file.read(inbuf, compressedLen) != static_cast<int>(compressedLen)) {
    free(inbuf);
    return 0;
  }

  InflateReader ir;
  if (!ir.init(false)) {
    free(inbuf);
    return 0;
  }
  ir.setSource(inbuf, compressedLen);
  if (compressedLen >= 2 && inbuf[0] == 0x78 &&
      (inbuf[1] == 0x01 || inbuf[1] == 0x5E || inbuf[1] == 0x9C || inbuf[1] == 0xDA)) {
    ir.skipZlibHeader();
  }

  size_t total = 0;
  while (total < maxOutBytes) {
    size_t produced = 0;
    const InflateStatus st = ir.readAtMost(outBuf + total, maxOutBytes - total, &produced);
    total += produced;
    if (st == InflateStatus::Error) {
      break;
    }
    if (st == InflateStatus::Done) {
      break;
    }
  }

  free(inbuf);
  return total;
}

size_t StreamDecoder::flateDecodeBytes(const uint8_t* compressed, size_t compressedLen, uint8_t* outBuf,
                                       size_t maxOutBytes) {
  if (!compressed || compressedLen == 0 || !outBuf || maxOutBytes == 0) return 0;

  InflateReader ir;
  if (!ir.init(false)) {
    return 0;
  }
  ir.setSource(compressed, compressedLen);
  if (compressedLen >= 2 && compressed[0] == 0x78 &&
      (compressed[1] == 0x01 || compressed[1] == 0x5E || compressed[1] == 0x9C || compressed[1] == 0xDA)) {
    ir.skipZlibHeader();
  }

  size_t total = 0;
  while (total < maxOutBytes) {
    size_t produced = 0;
    const InflateStatus st = ir.readAtMost(outBuf + total, maxOutBytes - total, &produced);
    total += produced;
    if (st == InflateStatus::Error) {
      break;
    }
    if (st == InflateStatus::Done) {
      break;
    }
  }
  return total;
}
