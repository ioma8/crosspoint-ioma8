#pragma once

#include <Pdf.h>

#include "../Activity.h"

class PdfReaderActivity final : public Activity {
  std::unique_ptr<Pdf> pdf;
  uint32_t currentPage = 0;
  uint32_t totalPages = 0;
  int pagesUntilFullRefresh = 0;
  uint32_t lastSavedPage = UINT32_MAX;

  int cachedFontId = 0;
  int viewportWidth = 0;
  int marginTop = 0;
  int marginLeft = 0;
  int marginRight = 0;
  int marginBottom = 0;
  bool layoutReady = false;

  void ensureLayout();
  void renderContents(const PdfPage& page);
  void renderStatusBar() const;
  void saveProgressNow();

 public:
  explicit PdfReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pdf> pdf);
  void jumpToPage(uint32_t page);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
