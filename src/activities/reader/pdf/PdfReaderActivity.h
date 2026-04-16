#pragma once

#include <Pdf.h>

#include <string>
#include <vector>

#include "Pdf/PdfCachedPageReader.h"
#include "PdfPageNavigation.h"
#include "ReaderBookmarkCodec.h"
#include "activities/Activity.h"

class PdfReaderActivity final : public Activity {
  struct PdfRenderCursor {
    size_t stepIndex = 0;
    size_t lineIndex = 0;
  };

  struct WrappedTextCacheEntry {
    uint32_t textIndex = UINT32_MAX;
    int fontId = 0;
    int width = 0;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    std::vector<std::string> lines;
  };

  std::unique_ptr<Pdf> pdf;
  uint32_t currentPage = 0;
  uint32_t totalPages = 0;
  int pagesUntilFullRefresh = 0;
  uint32_t lastSavedPage = UINT32_MAX;
  uint32_t loadedPage = UINT32_MAX;
  PdfCachedPageReader pageReader;
  PdfFixedVector<PdfRenderCursor, PDF_MAX_PAGE_SLICES> pageSliceStarts;
  PdfPageNavigationState navigationState;
  PdfPage scratchPage_;
  std::vector<WrappedTextCacheEntry> wrappedTextCache_;

  int cachedFontId = 0;
  int viewportWidth = 0;
  int marginTop = 0;
  int marginLeft = 0;
  int marginRight = 0;
  int marginBottom = 0;
  bool layoutReady = false;
  bool bookmarkChordActive = false;
  std::vector<ReaderBookmark> bookmarks;

  void ensureLayout();
  bool renderPageSlice(PdfCachedPageReader& page, const PdfRenderCursor& start, PdfRenderCursor& next, bool draw);
  void rebuildPageSlices();
  bool loadPage(uint32_t page);
  const std::vector<std::string>& getWrappedTextLines(uint32_t textIndex, const PdfTextBlock& block,
                                                      EpdFontFamily::Style style);
  void renderContents(PdfCachedPageReader& page);
  void renderStatusBar() const;
  void saveProgressNow();
  void drawPdfImagePlaceholder(int y) const;
  bool renderPdfImage(const PdfImageDescriptor& img, int y, int bottomLimit);
  void toggleCurrentBookmark();
  void openBookmarkSelection();
  bool isCurrentPageBookmarked() const;
  uint8_t getCurrentBookProgressPercent() const;
  std::string getCurrentPageSnippet();
  void drawBookmarkIndicatorIfNeeded();

 public:
  explicit PdfReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pdf> pdf);
  void jumpToPage(uint32_t page);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
