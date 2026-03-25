#pragma once

#include <cstddef>

// Max uncompressed bytes we hold for one page content stream (inflate output + parse buffer).
// Default 96 KiB fits typical text pages on ESP32-class devices (~320 KiB RAM); 200 KiB was too large.
#ifndef PDF_CONTENT_STREAM_MAX
#define PDF_CONTENT_STREAM_MAX (96 * 1024)
#endif

inline constexpr size_t pdfContentStreamMaxBytes() { return static_cast<size_t>(PDF_CONTENT_STREAM_MAX); }
