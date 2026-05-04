#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr uint32_t MAX_CACHE_WORD_LEN = 256;
}

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Validate iterator bounds before rendering
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u)\n", (uint32_t)words.size(),
            (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size());
    return;
  }

  const bool haveWidths = !wordWidths.empty() && wordWidths.size() == words.size();

  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    renderer.drawText(fontId, wordX, y, words[i].c_str(), true, currentStyle);

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      // Use cached width when available to avoid re-measuring all glyph advances.
      // Cached widths include soft-hyphen stripping, so they match rendered output.
      const int fullWordWidth =
          haveWidths ? static_cast<int>(wordWidths[i]) : renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // y is the top of the text line; add ascender to reach baseline, then offset 2px below
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + 2;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83", currentStyle);
        const int visibleWidth = haveWidths ? static_cast<int>(wordWidths[i]) - prefixWidth
                                            : renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, true);
    }
  }
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u)\n", words.size(),
            wordXpos.size(), wordStyles.size());
    return false;
  }

  const bool hasWidths = !wordWidths.empty() && wordWidths.size() == words.size();

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // Cached word widths (new in section version 19+).
  // Tag byte: 1 = widths present, 0 = absent (old format, render() falls back).
  serialization::writePod(file, static_cast<uint8_t>(hasWidths ? 1 : 0));
  if (hasWidths) {
    for (auto w : wordWidths) serialization::writePod(file, w);
  }

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<uint16_t> wordWidths;
  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) {
    if (!serialization::readString(file, w, MAX_CACHE_WORD_LEN)) {
      LOG_ERR("TXB", "Deserialization failed: invalid cached word length");
      return nullptr;
    }
  }
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // Cached word widths tag (section version 19+).
  // If present, read widths; otherwise leave wordWidths empty (fallback in render()).
  uint8_t hasWidths = 0;
  serialization::readPod(file, hasWidths);
  if (hasWidths) {
    wordWidths.resize(wc);
    for (auto& w : wordWidths) serialization::readPod(file, w);
  }

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);

  if (hasWidths) {
    return std::unique_ptr<TextBlock>(
        new TextBlock(std::move(words), std::move(wordXpos), std::move(wordWidths), std::move(wordStyles), blockStyle));
  }
  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), blockStyle));
}
