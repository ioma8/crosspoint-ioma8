#pragma once

#include <vector>

#include "ReaderBookmarkCodec.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ReaderBookmarkSelectionActivity final : public Activity {
  std::vector<ReaderBookmark> bookmarks;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  int getPageItems() const;

 public:
  ReaderBookmarkSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::vector<ReaderBookmark> bookmarks);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
