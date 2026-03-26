#pragma once

#include <Pdf.h>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class PdfReaderChapterSelectionActivity final : public Activity {
  const PdfFixedVector<PdfOutlineEntry, PDF_MAX_OUTLINE_ENTRIES> outline;
  uint32_t currentPage = 0;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int getPageItems() const;
  int getTotalItems() const;

 public:
  PdfReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const PdfFixedVector<PdfOutlineEntry, PDF_MAX_OUTLINE_ENTRIES>& outlineIn,
                                    uint32_t currentPageZeroBased);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
