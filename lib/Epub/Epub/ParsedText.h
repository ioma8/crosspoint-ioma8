#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<bool> wordContinues;  // true = word attaches to previous (no space before it)
  // Cached first/last codepoints per word — avoids repeated UTF-8 scans during layout.
  // Populated in addWord() or calculateWordWidths().
  std::vector<uint32_t> wordFirstCp;
  std::vector<uint32_t> wordLastCp;
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;

  void applyParagraphIndent();
  int computeGapWidthAt(const GfxRenderer& renderer, int fontId, const std::vector<bool>& continuesVec,
                        size_t index) const;
  /// Pre-compute inter-word gap widths (kerning + space advance) for all adjacent pairs.
  /// gapWidths[i] = width between word[i-1] and word[i] (index 0 unused, stays 0).
  std::vector<int> computeGapWidths(const GfxRenderer& renderer, int fontId, const std::vector<uint16_t>& wordWidths,
                                    const std::vector<bool>& continuesVec) const;
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                        std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                        std::vector<int>& gapWidths);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  std::vector<int>& gapWidths);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, std::vector<int>& gapWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine, const GfxRenderer& renderer,
                   int fontId, const std::vector<int>& gapWidths);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);
  /// Refresh cached first/last codepoints for a single word (e.g. after hyphenation split).
  void refreshCodepointCache(size_t index);
  /// Refresh cached codepoints for all words.
  void refreshAllCodepoints();

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool hyphenationEnabled = false,
                      const BlockStyle& blockStyle = BlockStyle())
      : blockStyle(blockStyle), extraParagraphSpacing(extraParagraphSpacing), hyphenationEnabled(hyphenationEnabled) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false);
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
