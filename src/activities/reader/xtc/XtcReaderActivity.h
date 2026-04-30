/**
 * XtcReaderActivity.h
 *
 * XTC ebook reader activity for CrossPoint Reader
 * Displays pre-rendered XTC pages on e-ink display
 */

#pragma once

#include <Xtc.h>
#include <freertos/task.h>

#include "activities/Activity.h"

struct XtcThumbTaskContext {
  Xtc* xtc;
  int coverHeight;
  TaskHandle_t* taskHandle;
  XtcThumbTaskContext** contextSlot;
};

class XtcReaderActivity final : public Activity {
  std::unique_ptr<Xtc> xtc;

  uint32_t currentPage = 0;
  int pagesUntilFullRefresh = 0;
  bool thumbGenerationPending = false;
  bool thumbGenerationArmed = false;
  TaskHandle_t thumbTaskHandle = nullptr;
  XtcThumbTaskContext* thumbTaskContext = nullptr;

  void renderPage();
  void saveProgress() const;
  void loadProgress();
  void maybeScheduleHomeThumbGeneration();
  void cancelHomeThumbGeneration();

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Xtc> xtc)
      : Activity("XtcReader", renderer, mappedInput), xtc(std::move(xtc)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
};
