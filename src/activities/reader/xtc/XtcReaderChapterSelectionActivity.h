#pragma once
#include <Xtc.h>

#include <memory>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class XtcReaderChapterSelectionActivity final : public Activity {
  Xtc* xtc;
  ButtonNavigator buttonNavigator;
  uint32_t currentPage = 0;
  int selectorIndex = 0;

  int getPageItems() const;
  int findChapterIndexForPage(uint32_t page) const;

 public:
  explicit XtcReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             Xtc* xtc, uint32_t currentPage)
      : Activity("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
