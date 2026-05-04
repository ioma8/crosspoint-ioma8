#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class GamesActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

 public:
  explicit GamesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Games", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
