#include "BootTiming.h"

#include <Arduino.h>
#include <Logging.h>

#ifdef ENABLE_BOOT_TIMING
namespace {
unsigned long bootTimingLast = 0;
}

void bootTimingReset(unsigned long now) { bootTimingLast = now; }

void bootTimingMark(const char* phase) {
  const unsigned long now = millis();
  LOG_INF("BOOT", "%s: +%lu ms, total=%lu ms", phase, now - bootTimingLast, now);
  bootTimingLast = now;
}
#else
void bootTimingReset(unsigned long) {}

void bootTimingMark(const char*) {}
#endif
