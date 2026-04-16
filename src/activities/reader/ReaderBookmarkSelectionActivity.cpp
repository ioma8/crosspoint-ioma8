#include "ReaderBookmarkSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

ReaderBookmarkSelectionActivity::ReaderBookmarkSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                                 std::vector<ReaderBookmark> bookmarks)
    : Activity("ReaderBookmarkSelection", renderer, mappedInput), bookmarks(std::move(bookmarks)) {}

int ReaderBookmarkSelectionActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

void ReaderBookmarkSelectionActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void ReaderBookmarkSelectionActivity::onExit() { Activity::onExit(); }

void ReaderBookmarkSelectionActivity::loop() {
  const int totalItems = static_cast<int>(bookmarks.size());
  const int pageItems = getPageItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < totalItems) {
      const auto& bookmark = bookmarks[static_cast<size_t>(selectedIndex)];
      setResult(BookmarkResult{bookmark.primary, bookmark.secondary});
    } else {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
    }
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void ReaderBookmarkSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int totalItems = static_cast<int>(bookmarks.size());
  const int pageItems = getPageItems();

  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_BOOKMARKS), EpdFontFamily::BOLD);
  const int titleX = contentX + (contentWidth - titleW) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_BOOKMARKS), true, EpdFontFamily::BOLD);

  if (totalItems == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120 + contentY, tr(STR_NO_BOOKMARKS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectedIndex - pageStartIndex) * 30 - 2, contentWidth - 1, 30);

  for (int i = 0; i < pageItems; ++i) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }
    const int displayY = 60 + contentY + i * 30;
    const bool isSelected = itemIndex == selectedIndex;
    const auto& bookmark = bookmarks[static_cast<size_t>(itemIndex)];

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%u%% ", static_cast<unsigned>(bookmark.percent));
    std::string line = std::string(prefix) + bookmark.snippet;
    if (line.size() == std::char_traits<char>::length(prefix)) {
      line += tr(STR_BOOKMARK);
    }
    line = renderer.truncatedText(UI_10_FONT_ID, line.c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, line.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
