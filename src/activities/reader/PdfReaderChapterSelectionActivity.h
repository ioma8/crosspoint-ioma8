#pragma once

#include <Pdf.h>

#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class PdfReaderChapterSelectionActivity final : public Activity {
  std::vector<PdfOutlineEntry> outline;
  uint32_t currentPage = 0;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int getPageItems() const;
  int getTotalItems() const;

 public:
  PdfReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::vector<PdfOutlineEntry>& outlineIn, uint32_t currentPageZeroBased);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
