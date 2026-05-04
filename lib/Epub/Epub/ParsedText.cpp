#include "ParsedText.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Compute first codepoint (skipping soft hyphens). Separated so we don't scan twice.
uint32_t firstCodepointRaw(const std::string& word) {
  if (word.empty()) return 0;
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.data());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;
  }
}

// Compute last codepoint by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepointRaw(const std::string& word) {
  if (word.empty()) return 0;
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.data() + i);
  return utf8NextCodepoint(&ptr);
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious) {
  if (word.empty()) return;

  if (words.empty()) {
    words.reserve(64);
    wordStyles.reserve(64);
    wordContinues.reserve(64);
    wordFirstCp.reserve(64);
    wordLastCp.reserve(64);
  }
  wordFirstCp.push_back(firstCodepointRaw(word));
  wordLastCp.push_back(lastCodepointRaw(word));
  words.push_back(std::move(word));
  EpdFontFamily::Style combinedStyle = fontStyle;
  if (underline) {
    combinedStyle = static_cast<EpdFontFamily::Style>(combinedStyle | EpdFontFamily::UNDERLINE);
  }
  wordStyles.push_back(combinedStyle);
  wordContinues.push_back(attachToPrevious);
}

void ParsedText::refreshCodepointCache(const size_t index) {
  if (index >= words.size()) return;
  wordFirstCp[index] = firstCodepointRaw(words[index]);
  wordLastCp[index] = lastCodepointRaw(words[index]);
}

void ParsedText::refreshAllCodepoints() {
  wordFirstCp.resize(words.size());
  wordLastCp.resize(words.size());
  for (size_t i = 0; i < words.size(); ++i) {
    wordFirstCp[i] = firstCodepointRaw(words[i]);
    wordLastCp[i] = lastCodepointRaw(words[i]);
  }
}

int ParsedText::computeGapWidthAt(const GfxRenderer& renderer, const int fontId,
                                  const std::vector<bool>& continuesVec, const size_t index) const {
  if (index == 0 || index >= words.size()) return 0;
  if (!continuesVec[index]) {
    return renderer.getSpaceAdvance(fontId, wordLastCp[index - 1], wordFirstCp[index], wordStyles[index - 1]);
  }
  return renderer.getKerning(fontId, wordLastCp[index - 1], wordFirstCp[index], wordStyles[index - 1]);
}

std::vector<int> ParsedText::computeGapWidths(const GfxRenderer& renderer, const int fontId,
                                              const std::vector<uint16_t>& wordWidths,
                                              const std::vector<bool>& continuesVec) const {
  (void)wordWidths;
  const size_t n = words.size();
  std::vector<int> gaps(n, 0);  // gap[0] unused
  for (size_t i = 1; i < n; ++i) {
    gaps[i] = computeGapWidthAt(renderer, fontId, continuesVec, i);
  }
  return gaps;
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent();

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);
  auto gapWidths = computeGapWidths(renderer, fontId, wordWidths, wordContinues);

  std::vector<size_t> lineBreakIndices;
  if (hyphenationEnabled) {
    lineBreakIndices = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, gapWidths);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, gapWidths);
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, wordWidths, wordContinues, lineBreakIndices, processLine, renderer, fontId, gapWidths);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    wordFirstCp.erase(wordFirstCp.begin(), wordFirstCp.begin() + consumed);
    wordLastCp.erase(wordLastCp.begin(), wordLastCp.begin() + consumed);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, words[i], wordStyles[i]));
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  std::vector<int>& gapWidths) {
  (void)renderer;
  (void)fontId;
  if (words.empty()) {
    return {};
  }

  // Calculate first line indent (only for left/justified text).
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, gapWidths,
                                /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // DP table
  std::vector<int> dp(totalWordCount);
  std::vector<size_t> ans(totalWordCount);

  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      int gap = (j > static_cast<size_t>(i)) ? gapWidths[j] : 0;
      currlen += wordWidths[j] + gap;

      if (currlen > effectivePageWidth) {
        break;
      }

      if (j + 1 < totalWordCount && continuesVec[j + 1]) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];
        cost = (cost_ll > MAX_COST) ? MAX_COST : static_cast<int>(cost_ll);
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;
      }
    }

    if (dp[i] == MAX_COST) {
      ans[i] = i;
      dp[i] = (i + 1 < static_cast<int>(totalWordCount)) ? dp[i + 1] : 0;
    }
  }

  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;
    if (nextBreakIndex <= currentWordIndex) {
      nextBreakIndex = currentWordIndex + 1;
    }
    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent() {
  if (extraParagraphSpacing || words.empty()) {
    return;
  }

  if (blockStyle.textIndentDefined) {
    // CSS text-indent is explicitly set — no EmSpace fallback.
    // indent positioning is handled in extractLine()
  } else if (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left) {
    words.front().insert(0, "\xe2\x80\x83");
    // Refresh cached first codepoint since we prepended to the word.
    refreshCodepointCache(0);
  }
}

std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            std::vector<int>& gapWidths) {
  const int firstLineIndent =
      blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      const int spacing = (!isFirstWord) ? gapWidths[currentIndex] : 0;
      const int candidateWidth = spacing + wordWidths[currentIndex];

      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, gapWidths,
                               allowFallbackBreaks)) {
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      std::vector<int>& gapWidths, const bool allowFallbackBreaks) {
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];

  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) continue;

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) continue;

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) return false;

  std::string remainder = word.substr(chosenOffset);
  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }

  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);

  // Refresh cached codepoints for both the prefix (was modified) and the inserted remainder.
  wordFirstCp.insert(wordFirstCp.begin() + wordIndex + 1, 0);
  wordLastCp.insert(wordLastCp.begin() + wordIndex + 1, 0);
  refreshCodepointCache(wordIndex);
  refreshCodepointCache(wordIndex + 1);

  gapWidths.insert(gapWidths.begin() + static_cast<int64_t>(wordIndex + 1), 0);
  gapWidths[wordIndex + 1] = computeGapWidthAt(renderer, fontId, wordContinues, wordIndex + 1);
  if (wordIndex + 2 < gapWidths.size()) {
    gapWidths[wordIndex + 2] = computeGapWidthAt(renderer, fontId, wordContinues, wordIndex + 2);
  }

  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const std::vector<uint16_t>& wordWidths,
                             const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             const GfxRenderer& renderer, const int fontId, const std::vector<int>& gapWidths) {
  (void)renderer;
  (void)fontId;
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  const bool isFirstLine = breakIndex == 0;
  const int firstLineIndent =
      isFirstLine && blockStyle.textIndentDefined && (blockStyle.textIndent < 0 || !extraParagraphSpacing) &&
              (blockStyle.alignment == CssTextAlign::Justify || blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    const size_t globalIdx = lastBreakAt + wordIdx;
    lineWordWidthSum += wordWidths[globalIdx];
    if (wordIdx > 0 && !continuesVec[globalIdx]) {
      actualGapCount++;
      totalNaturalGaps += gapWidths[globalIdx];
    } else if (wordIdx > 0 && continuesVec[globalIdx]) {
      totalNaturalGaps += gapWidths[globalIdx];
    }
  }

  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  const int spareSpace = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
  const int justifyExtra = (blockStyle.alignment == CssTextAlign::Justify && !isLastLine && actualGapCount >= 1)
                               ? spareSpace / static_cast<int>(actualGapCount)
                               : 0;

  auto xpos = static_cast<int16_t>(firstLineIndent);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
  }

  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    const size_t globalIdx = lastBreakAt + wordIdx;
    lineXPos.push_back(xpos);

    if (wordIdx + 1 < lineWordCount) {
      const size_t nextIdx = globalIdx + 1;
      const bool nextIsContinuation = continuesVec[nextIdx];
      int advance = wordWidths[globalIdx];
      if (nextIsContinuation) {
        advance += gapWidths[nextIdx];
      } else {
        int gap = gapWidths[nextIdx];
        if (blockStyle.alignment == CssTextAlign::Justify && !isLastLine) {
          gap += justifyExtra;
        }
        advance += gap;
      }
      xpos += advance;
    }
  }

  // Build line data by moving from the original vectors using index range
  std::vector<std::string> lineWords(std::make_move_iterator(words.begin() + lastBreakAt),
                                     std::make_move_iterator(words.begin() + lineBreak));
  std::vector<EpdFontFamily::Style> lineWordStyles(wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);
  std::vector<uint16_t> lineWordWidths(wordWidths.begin() + static_cast<int64_t>(lastBreakAt),
                                       wordWidths.begin() + static_cast<int64_t>(lineBreak));

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordWidths),
                                          std::move(lineWordStyles), blockStyle));
}
