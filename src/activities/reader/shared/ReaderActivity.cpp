#include "ReaderActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include "Epub.h"
#include "Pdf.h"
#include "Txt.h"
#include "Xtc.h"
#include "activities/reader/epub/EpubReaderActivity.h"
#include "activities/reader/pdf/PdfReaderActivity.h"
#include "activities/reader/txt/TxtReaderActivity.h"
#include "activities/reader/xtc/XtcReaderActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "app/CrossPointSettings.h"
#include "util/StringUtils.h"

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

std::unique_ptr<Pdf> ReaderActivity::loadPdf(const std::string& path) {
  auto pdf = std::make_unique<Pdf>();
  if (!pdf->open(path.c_str())) {
    LOG_ERR("READER", "Failed to load PDF");
    return nullptr;
  }
  return pdf;
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!Storage.exists(path.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.crosspoint"));
  if (epub->load(true, SETTINGS.embeddedStyle == 0)) {
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
    auto pdf = loadPdf(initialBookPath);
    if (!pdf) {
      onGoBack();
      return;
    }
    onGoToPdfReader(std::move(pdf));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}

void ReaderActivity::onGoBack() { finish(); }
