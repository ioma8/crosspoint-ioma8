#include "PdfReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

PdfReaderMenuActivity::PdfReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool hasOutline)
    : Activity("PdfReaderMenu", renderer, mappedInput) {
  if (hasOutline) {
    labels.push_back(std::string(tr(STR_PDF_OUTLINE)));
    actions.push_back(ACTION_OUTLINE);
  }
  labels.push_back(std::string(tr(STR_BOOKMARKS)));
  actions.push_back(ACTION_BOOKMARKS);
  labels.push_back(std::string(tr(STR_GO_HOME_BUTTON)));
  actions.push_back(ACTION_HOME);
}

void PdfReaderMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void PdfReaderMenuActivity::onExit() { Activity::onExit(); }

void PdfReaderMenuActivity::loop() {
  const int n = static_cast<int>(labels.size());
  if (n == 0) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    setResult(MenuResult{actions[static_cast<size_t>(selectedIndex)], 0, 0});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult r;
    r.isCancelled = true;
    r.data = MenuResult{-1, 0, 0};
    setResult(std::move(r));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, n] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, n);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, n] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, n);
    requestUpdate();
  });
}

void PdfReaderMenuActivity::render(RenderLock&&) {
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

  const int titleW = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_PDF_MENU_TITLE), EpdFontFamily::BOLD);
  const int titleX = contentX + (contentWidth - titleW) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_PDF_MENU_TITLE), true, EpdFontFamily::BOLD);

  const int n = static_cast<int>(labels.size());
  renderer.fillRect(contentX, 60 + contentY + (selectedIndex % n) * 30 - 2, contentWidth - 1, 30);

  for (int i = 0; i < n; ++i) {
    const int displayY = 60 + contentY + i * 30;
    const bool isSelected = (i == selectedIndex);
    const std::string line =
        renderer.truncatedText(UI_10_FONT_ID, labels[static_cast<size_t>(i)].c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, line.c_str(), !isSelected);
  }

  const auto labelsHints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labelsHints.btn1, labelsHints.btn2, labelsHints.btn3, labelsHints.btn4);

  renderer.displayBuffer();
}
