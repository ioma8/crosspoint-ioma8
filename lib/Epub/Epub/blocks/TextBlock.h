#pragma once
#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"

// Represents a line of text on a page
class TextBlock final : public Block {
 private:
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  // Cached word widths in pixels — populated during layout so render() doesn't
  // re-measure widths for underline positioning (avoids redundant getTextWidth calls).
  // Empty means widths are not cached (legacy cache file); render() falls back to
  // measuring on demand.
  std::vector<uint16_t> wordWidths;
  std::vector<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

 public:
  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, const BlockStyle& blockStyle = BlockStyle())
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle) {}
  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos, std::vector<uint16_t> word_widths,
                     std::vector<EpdFontFamily::Style> word_styles, const BlockStyle& blockStyle = BlockStyle())
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordWidths(std::move(word_widths)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle) {}
  ~TextBlock() override = default;
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  const std::vector<std::string>& getWords() const { return words; }
  bool isEmpty() override { return words.empty(); }
  size_t wordCount() const { return words.size(); }
  // given a renderer works out where to break the words into lines
  void render(const GfxRenderer& renderer, int fontId, int x, int y) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
