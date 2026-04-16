#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class PdfReaderMenuActivity final : public Activity {
 public:
  static constexpr int ACTION_OUTLINE = 200;
  static constexpr int ACTION_HOME = 201;
  static constexpr int ACTION_BOOKMARKS = 202;

  PdfReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool hasOutline);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<std::string> labels;
  std::vector<int> actions;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
};
