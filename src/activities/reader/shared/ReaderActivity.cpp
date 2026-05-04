#include "ReaderActivity.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "Epub.h"
#include "Pdf.h"
#include "Txt.h"
#include "Xtc.h"
#include "activities/RenderLock.h"
#include "activities/reader/epub/EpubReaderActivity.h"
#include "activities/reader/pdf/PdfReaderActivity.h"
#include "activities/reader/txt/TxtReaderActivity.h"
#include "activities/reader/xtc/XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "app/CrossPointSettings.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {

class CenterLoadingIndicator {
 public:
  explicit CenterLoadingIndicator(GfxRenderer& renderer) : renderer_(renderer) {}

  void show() { drawFrame(); }

  void advance() {
    const uint8_t next = progress_ + kProgressStep;
    progress_ = next > kMaxProgress ? kMaxProgress : next;
    drawFrame();
  }

 private:
  void drawFrame() {
    RenderLock lock;

    const int screenW = renderer_.getScreenWidth();
    const int screenH = renderer_.getScreenHeight();
    const int boxW = std::min(260, screenW - 40);
    const int boxH = 112;
    const int boxX = (screenW - boxW) / 2;
    const int boxY = (screenH - boxH) / 2;

    renderer_.clearScreen();
    renderer_.fillRoundedRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, Color::Black);
    renderer_.fillRoundedRect(boxX, boxY, boxW, boxH, 6, Color::White);

    const int dotSize = 8;
    const int dotGap = 10;
    const int dotsW = kDotCount * dotSize + (kDotCount - 1) * dotGap;
    const int dotsX = boxX + (boxW - dotsW) / 2;
    const int dotsY = boxY + 22;
    for (uint8_t i = 0; i < kDotCount; ++i) {
      const int x = dotsX + i * (dotSize + dotGap);
      if (i == frame_) {
        renderer_.fillRoundedRect(x - 1, dotsY - 1, dotSize + 2, dotSize + 2, 3, Color::Black);
      } else {
        renderer_.drawRoundedRect(x, dotsY, dotSize, dotSize, 1, 3, true);
      }
    }

    renderer_.drawCenteredText(UI_12_FONT_ID, boxY + 50, tr(STR_LOADING), true, EpdFontFamily::BOLD);

    const int barW = boxW - 46;
    const int barH = 5;
    const int barX = boxX + (boxW - barW) / 2;
    const int barY = boxY + boxH - 26;
    renderer_.drawRect(barX, barY, barW, barH, true);
    const int fillW = std::max(2, (barW - 2) * progress_ / 100);
    renderer_.fillRect(barX + 1, barY + 1, fillW, barH - 2, true);

    renderer_.displayBuffer(HalDisplay::FAST_REFRESH);
    frame_ = static_cast<uint8_t>((frame_ + 1) % kDotCount);
    progress_ = std::min<uint8_t>(progress_ + kProgressStep, kMaxProgress);
  }

  static constexpr uint8_t kDotCount = 5;
  static constexpr uint8_t kProgressStep = 12;
  static constexpr uint8_t kMaxProgress = 92;

  GfxRenderer& renderer_;
  uint8_t frame_ = 0;
  uint8_t progress_ = 8;
};

void advanceLoadingIndicator(void* context) {
  if (context) {
    static_cast<CenterLoadingIndicator*>(context)->advance();
  }
}

}  // namespace

std::string ReaderActivity::extractFolderPath(const std::string& filePath) { return StringUtils::dirName(filePath); }

bool ReaderActivity::isXtcFile(const std::string& path) {
  return FsHelpers::detectFileType(path) == FsHelpers::FileType::Xtc;
}

bool ReaderActivity::isTxtFile(const std::string& path) { return FsHelpers::isTextDocument(path); }

bool ReaderActivity::isBmpFile(const std::string& path) {
  return FsHelpers::detectFileType(path) == FsHelpers::FileType::Image;
}

bool ReaderActivity::isPdfFile(const std::string& path) {
  return FsHelpers::detectFileType(path) == FsHelpers::FileType::Pdf;
}

std::unique_ptr<Pdf> ReaderActivity::loadPdf(const std::string& path, LoadProgressCallback progressCallback,
                                             void* progressContext) {
  auto pdf = std::make_unique<Pdf>();
  if (!pdf->open(path.c_str(), progressCallback, progressContext)) {
    LOG_ERR("READER", "Failed to load PDF");
    return nullptr;
  }
  return pdf;
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path, LoadProgressCallback progressCallback,
                                               void* progressContext) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load(true, SETTINGS.embeddedStyle == 0, progressCallback, progressContext)) {
    return epub;
  }

  LOG_ERR("READER", "Failed to load epub");
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.crosspoint"));
  if (xtc->load()) {
    return xtc;
  }

  LOG_ERR("READER", "Failed to load XTC");
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.crosspoint"));
  if (txt->load()) {
    return txt;
  }

  LOG_ERR("READER", "Failed to load TXT");
  return nullptr;
}

void ReaderActivity::goToLibrary(const std::string& fromBookPath) {
  // If coming from a book, start in that book's folder; otherwise start from root
  auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(fromBookPath);
  activityManager.goToFileBrowser(std::move(initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  activityManager.replaceActivity(std::make_unique<EpubReaderActivity>(renderer, mappedInput, std::move(epub)));
}

void ReaderActivity::onGoToBmpViewer(const std::string& path) {
  activityManager.replaceActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  activityManager.replaceActivity(std::make_unique<XtcReaderActivity>(renderer, mappedInput, std::move(xtc)));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  activityManager.replaceActivity(std::make_unique<TxtReaderActivity>(renderer, mappedInput, std::move(txt)));
}

void ReaderActivity::onGoToPdfReader(std::unique_ptr<Pdf> pdf) {
  currentBookPath = pdf->filePath().c_str();
  activityManager.replaceActivity(std::make_unique<PdfReaderActivity>(renderer, mappedInput, std::move(pdf)));
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  if (initialBookPath.empty()) {
    goToLibrary();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;
  if (isBmpFile(initialBookPath)) {
    onGoToBmpViewer(initialBookPath);
  } else if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else if (isPdfFile(initialBookPath)) {
    CenterLoadingIndicator loading(renderer);
    loading.show();
    auto pdf = loadPdf(initialBookPath, advanceLoadingIndicator, &loading);
    if (!pdf) {
      onGoBack();
      return;
    }
    onGoToPdfReader(std::move(pdf));
  } else {
    CenterLoadingIndicator loading(renderer);
    loading.show();
    auto epub = loadEpub(initialBookPath, advanceLoadingIndicator, &loading);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
