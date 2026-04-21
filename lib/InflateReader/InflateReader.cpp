#include "InflateReader.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {
// Keep the deflate history window intentionally small to avoid large transient RAM use
// in the PDF/image paths while still supporting full DEFLATE distance range.
// ZIP/deflate streams can require up to 32KiB back-references.
constexpr size_t INFLATE_DICT_SIZE = 32768;
uint8_t* g_inflateDict = nullptr;
bool g_inflateDictInUse = false;
}  // namespace

InflateReader::~InflateReader() { deinit(); }

bool InflateReader::init(const bool streaming) {
  deinit();  // free any previously allocated ring buffer and reset state
  decomp.reset(new (std::nothrow) uzlib_uncomp());
  if (!decomp) {
    return false;
  }

  if (streaming) {
    if (g_inflateDictInUse) {
      decomp.reset();
      return false;
    }
    if (!g_inflateDict) {
      g_inflateDict = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
      if (!g_inflateDict) {
        decomp.reset();
        return false;
      }
    }
    g_inflateDictInUse = true;
    usingSharedRingBuffer = true;
    ringBuffer = g_inflateDict;
    memset(ringBuffer, 0, INFLATE_DICT_SIZE);
  }

  uzlib_uncompress_init(decomp.get(), ringBuffer, ringBuffer ? INFLATE_DICT_SIZE : 0);
  return true;
}

void InflateReader::deinit() {
  if (ringBuffer) {
    if (usingSharedRingBuffer) {
      g_inflateDictInUse = false;
      free(g_inflateDict);
      g_inflateDict = nullptr;
    }
    ringBuffer = nullptr;
  }
  usingSharedRingBuffer = false;
  decomp.reset();
}

void InflateReader::releaseSharedBuffer() {
  if (g_inflateDictInUse) {
    return;
  }
  free(g_inflateDict);
  g_inflateDict = nullptr;
}

size_t InflateReader::retainedSharedBufferBytes() { return g_inflateDict ? INFLATE_DICT_SIZE : 0; }

void InflateReader::setSource(const uint8_t* src, size_t len) {
  if (!decomp) return;
  decomp->source = src;
  decomp->source_limit = src + len;
}

void InflateReader::setReadCallback(int (*cb)(struct uzlib_uncomp*)) {
  if (!decomp) return;
  decomp->source_read_cb = cb;
}

void InflateReader::setUserContext(void* ctx) {
  if (!decomp) return;
  decomp->user_context = ctx;
}

void InflateReader::skipZlibHeader() {
  if (!decomp) return;
  uzlib_get_byte(decomp.get());
  uzlib_get_byte(decomp.get());
}

bool InflateReader::read(uint8_t* dest, size_t len) {
  if (!decomp) return false;
  // Keep dest_start aligned with the current output chunk. This keeps the
  // streaming path valid even when callers reuse a small scratch buffer.
  decomp->dest_start = dest;
  decomp->dest = dest;
  decomp->dest_limit = dest + len;

  const int res = uzlib_uncompress(decomp.get());
  if (res < 0) return false;
  return decomp->dest == decomp->dest_limit;
}

InflateStatus InflateReader::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  if (!decomp) {
    *produced = 0;
    return InflateStatus::Error;
  }
  // Keep dest_start aligned with the current output chunk. This keeps the
  // streaming path valid even when callers reuse a small scratch buffer.
  decomp->dest_start = dest;
  decomp->dest = dest;
  decomp->dest_limit = dest + maxLen;

  const int res = uzlib_uncompress(decomp.get());
  *produced = static_cast<size_t>(decomp->dest - dest);

  if (res == TINF_DONE) return InflateStatus::Done;
  if (res < 0) return InflateStatus::Error;
  return InflateStatus::Ok;
}
