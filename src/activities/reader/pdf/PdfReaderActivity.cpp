#include "activities/reader/pdf/PdfReaderActivity.h"

#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "PdfReaderChapterSelectionActivity.h"
#include "PdfReaderMenuActivity.h"
#include "ReaderBookmarkStore.h"
#include "activities/reader/shared/ReaderBookmarkIndicator.h"
#include "activities/reader/shared/ReaderBookmarkSelectionActivity.h"
#include "activities/reader/shared/ReaderUtils.h"
#include "app/CrossPointSettings.h"
#include "app/CrossPointState.h"
#include "app/MappedInputManager.h"
#include "app/RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
// Cap stream buffer for embedded heap; corrupt PDFs can advertise huge lengths.
constexpr size_t kMaxPdfImageStreamBytes = 2 * 1024 * 1024;
}  // namespace

void PdfReaderActivity::jumpToPage(uint32_t page) {
  if (page < totalPages) {
    currentPage = page;
    requestUpdate();
  }
}

PdfReaderActivity::PdfReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Pdf> pdfIn)
    : Activity("PdfReader", renderer, mappedInput), pdf(std::move(pdfIn)) {}

void PdfReaderActivity::ensureLayout() {
  if (layoutReady) {
    return;
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  const int nextFontId = SETTINGS.getReaderFontId();
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const uint8_t sm = SETTINGS.screenMargin;
  marginTop += sm;
  marginLeft += sm;
  marginRight += sm;
  marginBottom += std::max(sm, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));
  const int nextViewportWidth = renderer.getScreenWidth() - marginLeft - marginRight;
  if (cachedFontId != nextFontId || viewportWidth != nextViewportWidth) {
    wrappedTextCache_.clear();
  }
  cachedFontId = nextFontId;
  viewportWidth = nextViewportWidth;
  layoutReady = true;
}

bool PdfReaderActivity::loadPage(uint32_t page) {
  if (!pdf) {
    return false;
  }
  pageReader.close();
  if (!pageReader.open(pdf->cacheDirectory().c_str(), page)) {
    scratchPage_.clear();
    if (!pdf->getPage(page, scratchPage_)) {
      return false;
    }
    scratchPage_.clear();
    if (!pageReader.open(pdf->cacheDirectory().c_str(), page)) {
      return false;
    }
  }
  loadedPage = page;
  navigationState.page = page;
  navigationState.slice = 0;
  wrappedTextCache_.clear();
  rebuildPageSlices();
  return !pageSliceStarts.empty();
}

void PdfReaderActivity::drawPdfImagePlaceholder(int y) const {
  renderer.drawText(UI_10_FONT_ID, marginLeft, y, tr(STR_PDF_IMAGE_PLACEHOLDER));
}

bool PdfReaderActivity::renderPdfImage(const PdfImageDescriptor& img, int y, int bottomLimit) {
  if (!pdf || img.pdfStreamLength == 0 || img.pdfStreamLength > kMaxPdfImageStreamBytes) {
    return false;
  }

  const auto dir = pdf->cacheDirectory().view();
  if (dir.empty()) {
    return false;
  }

  char name[96];
  std::snprintf(name, sizeof(name), "/img_%lu_%lu.%s", static_cast<unsigned long>(img.pdfStreamOffset),
                static_cast<unsigned long>(img.pdfStreamLength), img.format == 0 ? "jpg" : "png");
  const std::string imagePath = std::string(dir) + name;
  if (!Storage.exists(imagePath.c_str())) {
    FsFile wf;
    if (!Storage.openFileForWrite("PDF", imagePath, wf)) {
      return false;
    }
    const size_t got = pdf->extractImageStreamToFile(img, wf, kMaxPdfImageStreamBytes);
    wf.close();
    if (got < 4) {
      Storage.remove(imagePath.c_str());
      return false;
    }
  }

  bool drawn = false;
  RenderConfig cfg;
  cfg.x = marginLeft;
  cfg.y = y;
  cfg.maxWidth = viewportWidth;
  cfg.maxHeight = bottomLimit - y;
  cfg.useGrayscale = true;
  cfg.useDithering = true;
  if (cfg.maxHeight >= 16) {
    if (img.format == 0) {
      JpegToFramebufferConverter jpg;
      drawn = jpg.decodeToFramebuffer(imagePath, renderer, cfg);
    } else {
      PngToFramebufferConverter png;
      drawn = png.decodeToFramebuffer(imagePath, renderer, cfg);
    }
  }

  return drawn;
}

const std::vector<std::string>& PdfReaderActivity::getWrappedTextLines(uint32_t textIndex, const PdfTextBlock& block,
                                                                       EpdFontFamily::Style style) {
  const auto it = std::find_if(wrappedTextCache_.begin(), wrappedTextCache_.end(), [&](const auto& entry) {
    return entry.textIndex == textIndex && entry.fontId == cachedFontId && entry.width == viewportWidth &&
           entry.style == style;
  });
  if (it != wrappedTextCache_.end()) {
    return it->lines;
  }

  constexpr int kMaxLines = 400;
  WrappedTextCacheEntry entry;
  entry.textIndex = textIndex;
  entry.fontId = cachedFontId;
  entry.width = viewportWidth;
  entry.style = style;
  entry.lines = renderer.wrappedText(cachedFontId, block.text.c_str(), viewportWidth, kMaxLines, style);
  wrappedTextCache_.push_back(std::move(entry));
  return wrappedTextCache_.back().lines;
}

bool PdfReaderActivity::renderPageSlice(PdfCachedPageReader& page, const PdfRenderCursor& start, PdfRenderCursor& next,
                                        bool draw) {
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  int y = marginTop;
  const int bottomLimit = renderer.getScreenHeight() - marginBottom - lineHeight;

  auto toRendererStyle = [](uint8_t pdfStyle) {
    uint8_t style = EpdFontFamily::REGULAR;
    if ((pdfStyle & PdfTextStyleBold) != 0) {
      style |= EpdFontFamily::BOLD;
    }
    if ((pdfStyle & PdfTextStyleItalic) != 0) {
      style |= EpdFontFamily::ITALIC;
    }
    return static_cast<EpdFontFamily::Style>(style);
  };

  auto getStep = [&](size_t stepIndex, bool& isImage, uint32_t& index) -> bool {
    if (stepIndex >= page.drawCount()) {
      return false;
    }
    PdfDrawStep step{};
    if (!page.loadDrawStep(static_cast<uint32_t>(stepIndex), step)) {
      return false;
    }
    isImage = step.isImage;
    index = step.index;
    return true;
  };

  auto drawTextBlock = [&](uint32_t textIndex, size_t stepIndex, size_t startLine) -> bool {
    PdfTextBlock block;
    if (!page.loadTextBlock(textIndex, block)) {
      next.stepIndex = stepIndex;
      next.lineIndex = 0;
      return false;
    }

    if (block.text.empty()) {
      next.stepIndex = stepIndex + 1;
      next.lineIndex = 0;
      return true;
    }

    const auto textStyle = toRendererStyle(block.style);
    const bool isHeader = (block.style & PdfTextStyleHeader) != 0;
    const auto& lines = getWrappedTextLines(textIndex, block, textStyle);
    if (startLine > lines.size()) {
      startLine = lines.size();
    }
    const bool blockStartsHere = startLine == 0;

    if (isHeader && blockStartsHere && y > marginTop) {
      y += std::max(2, lineHeight / 3);
    }

    for (size_t lineIndex = startLine; lineIndex < lines.size(); ++lineIndex) {
      if (y > bottomLimit) {
        next.stepIndex = stepIndex;
        next.lineIndex = lineIndex;
        return false;
      }
      if (draw) {
        int x = marginLeft;
        if (isHeader) {
          x = marginLeft +
              (viewportWidth - renderer.getTextWidth(cachedFontId, lines[lineIndex].c_str(), textStyle)) / 2;
        } else {
          switch (SETTINGS.paragraphAlignment) {
            case CrossPointSettings::CENTER_ALIGN:
              x = marginLeft +
                  (viewportWidth - renderer.getTextWidth(cachedFontId, lines[lineIndex].c_str(), textStyle)) / 2;
              break;
            case CrossPointSettings::RIGHT_ALIGN:
              x = marginLeft + viewportWidth - renderer.getTextWidth(cachedFontId, lines[lineIndex].c_str(), textStyle);
              break;
            default:
              break;
          }
        }
        renderer.drawText(cachedFontId, x, y, lines[lineIndex].c_str(), true, textStyle);
      }
      y += lineHeight;
    }

    if (isHeader && blockStartsHere) {
      y += std::max(2, lineHeight / 4);
    }
    next.stepIndex = stepIndex + 1;
    next.lineIndex = 0;
    return true;
  };

  for (size_t stepIndex = start.stepIndex;; ++stepIndex) {
    bool isImage = false;
    uint32_t index = 0;
    if (!getStep(stepIndex, isImage, index)) {
      next.stepIndex = stepIndex;
      next.lineIndex = 0;
      return true;
    }

    if (isImage) {
      PdfImageDescriptor img{};
      if (!page.loadImage(index, img)) {
        next.stepIndex = stepIndex;
        next.lineIndex = 0;
        return false;
      }
      if (y > bottomLimit) {
        next.stepIndex = stepIndex;
        next.lineIndex = 0;
        return false;
      }

      int advanceY = lineHeight;
      if (img.width > 0 && img.height > 0) {
        advanceY = static_cast<int>(img.height) * viewportWidth / static_cast<int>(img.width);
        if (advanceY > bottomLimit - y + lineHeight) {
          advanceY = bottomLimit - y + lineHeight;
        }
        if (advanceY < lineHeight) {
          advanceY = lineHeight;
        }
      }
      if (draw) {
        if (!renderPdfImage(img, y, bottomLimit)) {
          drawPdfImagePlaceholder(y);
        }
      }
      y += advanceY;
      next.stepIndex = stepIndex + 1;
      next.lineIndex = 0;
      continue;
    }

    if (index < page.textCount()) {
      const size_t startLine = (stepIndex == start.stepIndex) ? start.lineIndex : 0;
      if (!drawTextBlock(index, stepIndex, startLine)) {
        return false;
      }
      continue;
    }

    next.stepIndex = stepIndex + 1;
    next.lineIndex = 0;
  }
}

void PdfReaderActivity::rebuildPageSlices() {
  pageSliceStarts.clear();
  PdfRenderCursor cursor{};
  if (!pageSliceStarts.push_back(cursor)) {
    return;
  }

  while (pageSliceStarts.size() < PDF_MAX_PAGE_SLICES) {
    PdfRenderCursor next{};
    const bool finished = renderPageSlice(pageReader, cursor, next, false);
    if (finished) {
      return;
    }
    if (!pageSliceStarts.push_back(next)) {
      return;
    }
    cursor = next;
  }
}

void PdfReaderActivity::onEnter() {
  Activity::onEnter();
  if (!pdf) {
    return;
  }
  layoutReady = false;
  loadedPage = UINT32_MAX;
  navigationState = {};
  pageSliceStarts.clear();
  ensureLayout();
  pageReader.close();

  totalPages = pdf->pageCount();
  ReaderBookmarkStore::load(std::string(pdf->cacheDirectory().c_str()), bookmarks);
  currentPage = 0;
  if (pdf->loadProgress(currentPage) && currentPage >= totalPages && totalPages > 0) {
    currentPage = totalPages - 1;
  }

  const auto path = std::string(pdf->filePath().c_str());
  const auto fileName = StringUtils::baseName(path);
  APP_STATE.openEpubPath = path;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(path, fileName, "", "");

  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  lastSavedPage = UINT32_MAX;
  requestUpdate();
}

void PdfReaderActivity::onExit() {
  Activity::onExit();
  saveProgressNow();
  ReaderUtils::resetReaderSession(&renderer);
  lastSavedPage = UINT32_MAX;
  pageReader.close();
  pdf.reset();
  bookmarks.clear();
  wrappedTextCache_.clear();
  layoutReady = false;
}

void PdfReaderActivity::saveProgressNow() {
  if (!pdf || currentPage == lastSavedPage) {
    return;
  }
  if (pdf->saveProgress(currentPage)) {
    lastSavedPage = currentPage;
  }
}

void PdfReaderActivity::loop() {
  if (!pdf) {
    finish();
    return;
  }

  if (ReaderUtils::handleBookmarkChord(mappedInput, bookmarkChordActive, [this] { toggleCurrentBookmark(); })) {
    return;
  }

  if (ReaderUtils::handleBackNavigation(
          mappedInput,
          [this] {
            saveProgressNow();
            activityManager.goToFileBrowser(std::string(pdf->filePath().c_str()));
          },
          [this] {
            saveProgressNow();
            onGoHome();
          })) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool hasOutline = !pdf->outline().empty();
    startActivityForResult(std::make_unique<PdfReaderMenuActivity>(renderer, mappedInput, hasOutline),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) {
                               return;
                             }
                             const auto& menu = std::get<MenuResult>(result.data);
                             if (menu.action == PdfReaderMenuActivity::ACTION_HOME) {
                               saveProgressNow();
                               onGoHome();
                             } else if (menu.action == PdfReaderMenuActivity::ACTION_BOOKMARKS) {
                               openBookmarkSelection();
                             } else if (menu.action == PdfReaderMenuActivity::ACTION_OUTLINE) {
                               startActivityForResult(std::make_unique<PdfReaderChapterSelectionActivity>(
                                                          renderer, mappedInput, pdf->outline(), currentPage),
                                                      [this](const ActivityResult& res) {
                                                        if (!res.isCancelled) {
                                                          const uint32_t p = std::get<PageResult>(res.data).page;
                                                          jumpToPage(p);
                                                        }
                                                      });
                             }
                           });
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  if (prevTriggered) {
    if (navigationState.slice > 0) {
      --navigationState.slice;
      requestUpdate();
      return;
    }
    if (currentPage > 0) {
      const uint32_t previousPage = currentPage - 1;
      if (loadPage(previousPage)) {
        currentPage = previousPage;
        navigationState.page = previousPage;
        navigationState.slice = static_cast<uint16_t>(pageSliceStarts.size() - 1);
        requestUpdate();
      }
      return;
    }
  } else if (nextTriggered) {
    if (navigationState.slice + 1 < pageSliceStarts.size()) {
      ++navigationState.slice;
      requestUpdate();
      return;
    }
    if (currentPage + 1 < totalPages) {
      currentPage++;
      loadedPage = UINT32_MAX;
      navigationState.page = currentPage;
      navigationState.slice = 0;
      requestUpdate();
      return;
    }
  }
}

uint8_t PdfReaderActivity::getCurrentBookProgressPercent() const {
  if (totalPages == 0) {
    return 0;
  }
  const uint32_t percent = ((currentPage + 1) * 100U) / totalPages;
  return static_cast<uint8_t>(std::min<uint32_t>(percent, 100));
}

std::string PdfReaderActivity::getCurrentPageSnippet() {
  if (loadedPage != currentPage || pageSliceStarts.empty()) {
    if (!loadPage(currentPage)) {
      return "";
    }
  }

  std::string text;
  for (uint32_t i = 0; i < pageReader.textCount(); ++i) {
    PdfTextBlock block;
    if (!pageReader.loadTextBlock(i, block) || block.text.empty()) {
      continue;
    }
    if (!text.empty()) {
      text.push_back(' ');
    }
    text += block.text.c_str();
    if (text.size() > 160) {
      break;
    }
  }
  return ReaderBookmarkCodec::firstWords(text);
}

void PdfReaderActivity::toggleCurrentBookmark() {
  if (!pdf || totalPages == 0 || currentPage >= totalPages) {
    return;
  }

  const ReaderBookmark bookmark{currentPage, 0, getCurrentBookProgressPercent(), getCurrentPageSnippet()};
  if (ReaderBookmarkStore::toggleAndReload(std::string(pdf->cacheDirectory().c_str()), bookmark, bookmarks)) {
    requestUpdate();
  }
}

bool PdfReaderActivity::isCurrentPageBookmarked() const {
  return ReaderBookmarkCodec::find(bookmarks, currentPage, 0) != nullptr;
}

void PdfReaderActivity::openBookmarkSelection() {
  std::vector<ReaderBookmark> latestBookmarks;
  ReaderBookmarkStore::load(std::string(pdf->cacheDirectory().c_str()), latestBookmarks);
  bookmarks = latestBookmarks;
  startActivityForResult(std::make_unique<ReaderBookmarkSelectionActivity>(renderer, mappedInput, latestBookmarks),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             jumpToPage(std::get<BookmarkResult>(result.data).primary);
                           }
                         });
}

void PdfReaderActivity::drawBookmarkIndicatorIfNeeded() {
  ReaderBookmarkIndicator::drawIf(renderer, isCurrentPageBookmarked());
}

void PdfReaderActivity::renderStatusBar() const {
  if (!pdf || totalPages == 0) {
    return;
  }
  const float progress = static_cast<float>(currentPage + 1) * 100.0f / static_cast<float>(totalPages);
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) {
    title = StringUtils::baseName(std::string(pdf->filePath().c_str()));
  }
  GUI.drawStatusBar(renderer, progress, static_cast<int>(currentPage + 1), static_cast<int>(totalPages), title);
}

void PdfReaderActivity::renderContents(PdfCachedPageReader& page) {
  if (pageSliceStarts.empty()) {
    return;
  }
  const uint16_t slice = std::min<uint16_t>(navigationState.slice, static_cast<uint16_t>(pageSliceStarts.size() - 1));
  PdfRenderCursor next{};
  renderPageSlice(page, pageSliceStarts[slice], next, true);
}

void PdfReaderActivity::render(RenderLock&&) {
  if (!pdf) {
    return;
  }
  ensureLayout();

  renderer.clearScreen();

  if (loadedPage != currentPage || pageSliceStarts.empty()) {
    if (!loadPage(currentPage)) {
      renderer.drawCenteredText(UI_12_FONT_ID, 200, tr(STR_PDF_LOAD_ERROR), true, EpdFontFamily::BOLD);
      renderer.displayBuffer();
      return;
    }
  }

  renderContents(pageReader);
  drawBookmarkIndicatorIfNeeded();
  renderStatusBar();
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&, this]() {
      renderContents(pageReader);
      drawBookmarkIndicatorIfNeeded();
      renderStatusBar();
    });
  }
  saveProgressNow();
}
