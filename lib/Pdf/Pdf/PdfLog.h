#pragma once

#include <Logging.h>

#if defined(ENABLE_SERIAL_LOG)
inline void pdfLogLine(const char* level, const char* message) {
  logSerial.print(level);
  logSerial.print(" [PDF] ");
  logSerial.println(message);
}

inline void pdfLogErr(const char* message) { pdfLogLine("[ERR]", message); }
inline void pdfLogDbg(const char* message) { pdfLogLine("[DBG]", message); }

inline void pdfLogErrU32(const char* message, uint32_t value) {
  logSerial.print("[ERR] [PDF] ");
  logSerial.print(message);
  logSerial.println(value);
}

inline void pdfLogErrPath(const char* message, const char* path) {
  logSerial.print("[ERR] [PDF] ");
  logSerial.print(message);
  logSerial.println(path);
}

inline void pdfLogErrU32U32(const char* message, uint32_t a, uint32_t b) {
  logSerial.print("[ERR] [PDF] ");
  logSerial.print(message);
  logSerial.print(a);
  logSerial.print(' ');
  logSerial.println(b);
}

inline void pdfLogDbgU32U32(const char* message, uint32_t a, uint32_t b) {
  logSerial.print("[DBG] [PDF] ");
  logSerial.print(message);
  logSerial.print(a);
  logSerial.print(' ');
  logSerial.println(b);
}
#else
inline void pdfLogErr(const char*) {}
inline void pdfLogDbg(const char*) {}
inline void pdfLogErrU32(const char*, uint32_t) {}
inline void pdfLogErrPath(const char*, const char*) {}
inline void pdfLogErrU32U32(const char*, uint32_t, uint32_t) {}
inline void pdfLogDbgU32U32(const char*, uint32_t, uint32_t) {}
#endif
