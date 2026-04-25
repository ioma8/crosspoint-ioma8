#include "GamesActivity.h"

#include <I18n.h>

#include "Game2048Activity.h"
#include "GameSokobanActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int GAME_COUNT = 2;

const char* gameTitle(const int index) {
  if (index == 0) return "2048";
  return "Sokoban";
}

const char* gameSummary(const int index) {
  if (index == 0) return "Merge tiles to 2048 using all four directions.";
  return "Push every crate onto a target without getting stuck.";
}
}  // namespace

void GamesActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate();
}

void GamesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex == 0) {
      startActivityForResult(std::make_unique<Game2048Activity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
    } else {
      startActivityForResult(std::make_unique<GameSokobanActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, GAME_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, GAME_COUNT);
    requestUpdate();
  });
}

void GamesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Games");
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, GAME_COUNT, selectorIndex,
               [](int index) { return std::string(gameTitle(index)); }, nullptr,
               [](int) { return UIIcon::Book; });

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - metrics.buttonHintsHeight - 34, gameSummary(selectorIndex));

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
