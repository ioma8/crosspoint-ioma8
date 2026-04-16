#pragma once

#include <I18n.h>

#include <functional>

#include "activities/Activity.h"

class ClearCacheActivity final : public Activity {
 public:
  enum class CacheKind : uint8_t { Epub, Pdf };

  explicit ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, CacheKind cacheKind)
      : Activity("ClearCache", renderer, mappedInput), cacheKind(cacheKind) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, CLEARING, SUCCESS, FAILED };

  State state = WARNING;
  CacheKind cacheKind;

  void goBack() { finish(); }
  StrId titleId() const;

  int clearedCount = 0;
  int failedCount = 0;
  void clearCache();
};
