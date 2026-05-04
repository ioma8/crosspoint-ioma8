#include "GfxRenderer.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "FontCacheManager.h"

namespace {

struct BitmapRowBuffers {
  uint8_t* output = nullptr;
  uint8_t* bytes = nullptr;

  BitmapRowBuffers(int outputSize, int rowBytes) {
    output = static_cast<uint8_t*>(malloc(outputSize));
    bytes = static_cast<uint8_t*>(malloc(rowBytes));
  }

  ~BitmapRowBuffers() {
    free(output);
    free(bytes);
  }

  bool ok() const { return output && bytes; }
};

}  // namespace

const uint8_t* GfxRenderer::getGlyphBitmap(const EpdFontData* fontData, const EpdGlyph* glyph) const {
  if (fontData->groups != nullptr) {
    auto* fd = fontCacheManager_ ? fontCacheManager_->getDecompressor() : nullptr;
    if (!fd) {
      LOG_ERR("GFX", "Compressed font but no FontDecompressor set");
      return nullptr;
    }
    uint32_t glyphIndex = static_cast<uint32_t>(glyph - fontData->glyph);
    // For page-buffer hits the pointer is stable for the page lifetime.
    // For hot-group hits it is valid only until the next getBitmap() call — callers
    // must consume it (draw the glyph) before requesting another bitmap.
    return fd->getBitmap(fontData, glyph, glyphIndex);
  }
  return &fontData->bitmap[glyph->dataOffset];
}

void GfxRenderer::begin() {
  frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    LOG_ERR("GFX", "!! No framebuffer");
    assert(false);
  }
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) {
  fontMap.insert({fontId, font});
  // Invalidate the last-used cache so it doesn't hold a stale pointer after
  // the map potentially rehashes/reallocates on insert.
  cachedFontId = -1;
  cachedFont = nullptr;
}

const EpdFontFamily* GfxRenderer::findFont(const int fontId) const {
  if (fontId == cachedFontId && cachedFont != nullptr) {
    return cachedFont;
  }
  const auto it = fontMap.find(fontId);
  if (it != fontMap.end()) {
    cachedFontId = fontId;
    cachedFont = &it->second;
    return cachedFont;
  }
  // Font not registered (e.g. slim build): try the configured fallback.
  if (defaultFontId_ >= 0 && fontId != defaultFontId_) {
    const auto fb = fontMap.find(defaultFontId_);
    if (fb != fontMap.end()) {
      cachedFontId = defaultFontId_;
      cachedFont = &fb->second;
      return cachedFont;
    }
  }
  return nullptr;
}

// Translate logical (x,y) coordinates to physical panel coordinates based on current orientation
// MUST be inlined — called for every pixel drawn. Under -Os the compiler may
// ignore a bare `inline` hint; always_inline guarantees the switch collapses
// against the known compile-time-visible member `orientation`.
static inline __attribute__((always_inline)) void rotateCoordinates(const GfxRenderer::Orientation orientation,
                                                                    const int x, const int y, int* phyX, int* phyY) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees clockwise
      *phyX = y;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      // Logical landscape (800x480) rotated 180 degrees (swap top/bottom and left/right)
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees counter-clockwise
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      // Logical landscape (800x480) aligned with panel orientation
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

enum class TextRotation { None, Rotated90CW };

// Shared glyph rendering logic for normal and rotated text.
// Coordinate mapping and cursor advance direction are selected at compile time via the template parameter.
template <TextRotation rotation>
static void renderCharImpl(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                           const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                           const bool pixelState, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  const bool is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;
  const int top = glyph->top;

  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);

  if (bitmap != nullptr) {
    // For Normal:  outer loop advances screenY, inner loop advances screenX
    // For Rotated: outer loop advances screenX, inner loop advances screenY (in reverse)
    int outerBase, innerBase;
    if constexpr (rotation == TextRotation::Rotated90CW) {
      outerBase = cursorX + fontData->ascender - top;  // screenX = outerBase + glyphY
      innerBase = cursorY - left;                      // screenY = innerBase - glyphX
    } else {
      outerBase = cursorY - top;   // screenY = outerBase + glyphY
      innerBase = cursorX + left;  // screenX = innerBase + glyphX
    }

    if (is2Bit) {
      // Hoist renderMode dispatch above the pixel loop to avoid 3 conditional branches
      // per pixel on RISC-V (no branch predictor). The mode is constant for the entire
      // page rendering pass.
      if (renderMode == GfxRenderer::BW) {
        int pixelPosition = 0;
        for (int glyphY = 0; glyphY < height; glyphY++) {
          const int outerCoord = outerBase + glyphY;
          for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
            int screenX, screenY;
            if constexpr (rotation == TextRotation::Rotated90CW) {
              screenX = outerCoord;
              screenY = innerBase - glyphX;
            } else {
              screenX = innerBase + glyphX;
              screenY = outerCoord;
            }

            const uint8_t byte = bitmap[pixelPosition >> 2];
            const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
            // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
            const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

            if (bmpVal < 3) {
              // Black (also paints over the grays in BW mode)
              renderer.drawPixel(screenX, screenY, pixelState);
            }
          }
        }
      } else if (renderMode == GfxRenderer::GRAYSCALE_MSB) {
        int pixelPosition = 0;
        for (int glyphY = 0; glyphY < height; glyphY++) {
          const int outerCoord = outerBase + glyphY;
          for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
            int screenX, screenY;
            if constexpr (rotation == TextRotation::Rotated90CW) {
              screenX = outerCoord;
              screenY = innerBase - glyphX;
            } else {
              screenX = innerBase + glyphX;
              screenY = outerCoord;
            }

            const uint8_t byte = bitmap[pixelPosition >> 2];
            const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

            if (bmpVal == 1 || bmpVal == 2) {
              // Light gray (mark MSB)
              renderer.drawPixel(screenX, screenY, false);
            }
          }
        }
      } else if (renderMode == GfxRenderer::GRAYSCALE_LSB) {
        int pixelPosition = 0;
        for (int glyphY = 0; glyphY < height; glyphY++) {
          const int outerCoord = outerBase + glyphY;
          for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
            int screenX, screenY;
            if constexpr (rotation == TextRotation::Rotated90CW) {
              screenX = outerCoord;
              screenY = innerBase - glyphX;
            } else {
              screenX = innerBase + glyphX;
              screenY = outerCoord;
            }

            const uint8_t byte = bitmap[pixelPosition >> 2];
            const uint8_t bit_index = (3 - (pixelPosition & 3)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);

            if (bmpVal == 1) {
              // Dark gray
              renderer.drawPixel(screenX, screenY, false);
            }
          }
        }
      }
    } else {
      int pixelPosition = 0;
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const int outerCoord = outerBase + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++, pixelPosition++) {
          int screenX, screenY;
          if constexpr (rotation == TextRotation::Rotated90CW) {
            screenX = outerCoord;
            screenY = innerBase - glyphX;
          } else {
            screenX = innerBase + glyphX;
            screenY = outerCoord;
          }

          const uint8_t byte = bitmap[pixelPosition >> 3];
          const uint8_t bit_index = 7 - (pixelPosition & 7);

          if ((byte >> bit_index) & 1) {
            renderer.drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }
}

// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  int phyX = 0;
  int phyY = 0;

  // Note: this call should be inlined for better performance
  rotateCoordinates(orientation, x, y, &phyX, &phyY);

  // Bounds checking against physical panel dimensions
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) {
    LOG_ERR("GFX", "!! Outside range (%d, %d) -> (%d, %d)", x, y, phyX, phyY);
    return;
  }

  // Calculate byte position and bit position
  const uint32_t byteIndex = static_cast<uint32_t>(phyY) * HalDisplay::DISPLAY_WIDTH_BYTES + (phyX / 8);
  const uint8_t bitPosition = 7 - (phyX % 8);  // MSB first

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  int w = 0, h = 0;
  font->getTextDimensions(text, &w, &h, style);
  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  const int yPos = y + getFontAscenderSize(fontId);
  int32_t xPosFP = fp4::fromPixel(x);  // 12.4 fixed-point accumulator
  int lastBaseX = x;
  int lastBaseAdvanceFP = 0;  // 12.4 fixed-point
  int lastBaseTop = 0;

  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontCacheManager_ && fontCacheManager_->isScanning()) {
    fontCacheManager_->recordText(text, fontId, style);
    return;
  }

  const EpdFontFamily* fontPtr = findFont(fontId);
  if (!fontPtr) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }
  const auto& font = *fontPtr;
  constexpr int MIN_COMBINING_GAP_PX = 1;

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      int raiseBy = 0;
      if (combiningGlyph) {
        const int currentGap = combiningGlyph->top - combiningGlyph->height - lastBaseTop;
        if (currentGap < MIN_COMBINING_GAP_PX) {
          raiseBy = MIN_COMBINING_GAP_PX - currentGap;
        }
      }

      const int combiningX = lastBaseX + fp4::toPixel(lastBaseAdvanceFP / 2);
      const int combiningY = yPos - raiseBy;
      renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, combiningX, combiningY, black, style);
      continue;
    }

    cp = font.applyLigatures(cp, text, style);
    const int kernFP = (prevCp != 0) ? font.getKerning(prevCp, cp, style) : 0;  // 4.4 fixed-point kern
    xPosFP += kernFP;

    lastBaseX = fp4::toPixel(xPosFP);  // snap 12.4 fixed-point to nearest pixel
    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseAdvanceFP = glyph ? glyph->advanceX : 0;
    lastBaseTop = glyph ? glyph->top : 0;

    renderCharImpl<TextRotation::None>(*this, renderMode, font, cp, lastBaseX, yPos, black, style);
    if (glyph) {
      xPosFP += glyph->advanceX;  // 12.4 fixed-point advance
    }
    prevCp = cp;
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // Bresenham's line algorithm — integer arithmetic only
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    dx = sx * dx;  // abs
    dy = sy * dy;  // abs

    int err = dx - dy;
    while (true) {
      drawPixel(x1, y1, state);
      if (x1 == x2 && y1 == y2) break;
      int e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x1 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y1 += sy;
      }
    }
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth, const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x1, y1 + i, x2, y2 + i, state);
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

// Border is inside the rectangle
void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,
                           const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x + i, y + i, x + width - i, y + i, state);
    drawLine(x + width - i, y + i, x + width - i, y + height - i, state);
    drawLine(x + width - i, y + height - i, x + i, y + height - i, state);
    drawLine(x + i, y + height - i, x + i, y + i, state);
  }
}

void GfxRenderer::drawArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir,
                          const int lineWidth, const bool state) const {
  const int stroke = std::min(lineWidth, maxRadius);
  const int innerRadius = std::max(maxRadius - stroke, 0);
  const int outerRadiusSq = maxRadius * maxRadius;
  const int innerRadiusSq = innerRadius * innerRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      if (distSq > outerRadiusSq || distSq < innerRadiusSq) {
        continue;
      }
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      drawPixel(px, py, state);
    }
  }
};

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool state) const {
  drawRoundedRect(x, y, width, height, lineWidth, cornerRadius, true, true, true, true, state);
}

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool roundTopLeft, bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight, bool state) const {
  if (lineWidth <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    drawRect(x, y, width, height, lineWidth, state);
    return;
  }

  const int stroke = std::min(lineWidth, maxRadius);
  const int right = x + width - 1;
  const int bottom = y + height - 1;

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    if (roundTopLeft || roundTopRight) {
      fillRect(x + maxRadius, y, horizontalWidth, stroke, state);
    }
    if (roundBottomLeft || roundBottomRight) {
      fillRect(x + maxRadius, bottom - stroke + 1, horizontalWidth, stroke, state);
    }
  }

  const int verticalHeight = height - 2 * maxRadius;
  if (verticalHeight > 0) {
    if (roundTopLeft || roundBottomLeft) {
      fillRect(x, y + maxRadius, stroke, verticalHeight, state);
    }
    if (roundTopRight || roundBottomRight) {
      fillRect(right - stroke + 1, y + maxRadius, stroke, verticalHeight, state);
    }
  }

  if (roundTopLeft) {
    drawArc(maxRadius, x + maxRadius, y + maxRadius, -1, -1, lineWidth, state);
  }
  if (roundTopRight) {
    drawArc(maxRadius, right - maxRadius, y + maxRadius, 1, -1, lineWidth, state);
  }
  if (roundBottomRight) {
    drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, state);
  }
  if (roundBottomLeft) {
    drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  for (int fillY = y; fillY < y + height; fillY++) {
    drawLine(x, fillY, x + width - 1, fillY, state);
  }
}

// NOTE: Those are in critical path, and need to be templated to avoid runtime checks for every pixel.
// Any branching must be done outside the loops to avoid performance degradation.
template <>
void GfxRenderer::drawPixelDither<Color::Clear>(const int x, const int y) const {
  // Do nothing
}

template <>
void GfxRenderer::drawPixelDither<Color::Black>(const int x, const int y) const {
  drawPixel(x, y, true);
}

template <>
void GfxRenderer::drawPixelDither<Color::White>(const int x, const int y) const {
  drawPixel(x, y, false);
}

template <>
void GfxRenderer::drawPixelDither<Color::LightGray>(const int x, const int y) const {
  drawPixel(x, y, x % 2 == 0 && y % 2 == 0);
}

template <>
void GfxRenderer::drawPixelDither<Color::DarkGray>(const int x, const int y) const {
  drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
}

void GfxRenderer::fillRectDither(const int x, const int y, const int width, const int height, Color color) const {
  if (color == Color::Clear) {
  } else if (color == Color::Black) {
    fillRect(x, y, width, height, true);
  } else if (color == Color::White) {
    fillRect(x, y, width, height, false);
  } else if (color == Color::LightGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::LightGray>(fillX, fillY);
      }
    }
  } else if (color == Color::DarkGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::DarkGray>(fillX, fillY);
      }
    }
  }
}

template <Color color>
void GfxRenderer::fillArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir) const {
  const int radiusSq = maxRadius * maxRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      if (distSq <= radiusSq) {
        drawPixelDither<color>(px, py);
      }
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const Color color) const {
  fillRoundedRect(x, y, width, height, cornerRadius, true, true, true, true, color);
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  bool roundTopLeft, bool roundTopRight, bool roundBottomLeft, bool roundBottomRight,
                                  const Color color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  // Assume if we're not rounding all corners then we are only rounding one side
  const int roundedSides = (!roundTopLeft || !roundTopRight || !roundBottomLeft || !roundBottomRight) ? 1 : 2;
  const int maxRadius = std::min({cornerRadius, width / roundedSides, height / roundedSides});
  if (maxRadius <= 0) {
    fillRectDither(x, y, width, height, color);
    return;
  }

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    fillRectDither(x + maxRadius + 1, y, horizontalWidth - 2, height, color);
  }

  const int leftFillTop = y + (roundTopLeft ? (maxRadius + 1) : 0);
  const int leftFillBottom = y + height - 1 - (roundBottomLeft ? (maxRadius + 1) : 0);
  if (leftFillBottom >= leftFillTop) {
    fillRectDither(x, leftFillTop, maxRadius + 1, leftFillBottom - leftFillTop + 1, color);
  }

  const int rightFillTop = y + (roundTopRight ? (maxRadius + 1) : 0);
  const int rightFillBottom = y + height - 1 - (roundBottomRight ? (maxRadius + 1) : 0);
  if (rightFillBottom >= rightFillTop) {
    fillRectDither(x + width - maxRadius - 1, rightFillTop, maxRadius + 1, rightFillBottom - rightFillTop + 1, color);
  }

  auto fillArcTemplated = [this](int maxRadius, int cx, int cy, int xDir, int yDir, Color color) {
    switch (color) {
      case Color::Clear:
        break;
      case Color::Black:
        fillArc<Color::Black>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::White:
        fillArc<Color::White>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(maxRadius, cx, cy, xDir, yDir);
        break;
    }
  };

  if (roundTopLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + maxRadius, -1, -1, color);
  }

  if (roundTopRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + maxRadius, 1, -1, color);
  }

  if (roundBottomRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + height - maxRadius - 1, 1, 1, color);
  }

  if (roundBottomLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + height - maxRadius - 1, -1, 1, color);
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  int phyX = 0;
  int phyY = 0;
  rotateCoordinates(orientation, x, y, &phyX, &phyY);
  // EInkDisplay copies bitmap rows as provided; it does not rotate bitmap bits.
  // Only adjust the transformed origin corner here.
  switch (orientation) {
    case Portrait:
      phyY = phyY - height;
      break;
    case PortraitInverted:
      phyX = phyX - width;
      break;
    case LandscapeClockwise:
      phyY = phyY - height;
      phyX = phyX - width;
      break;
    case LandscapeCounterClockwise:
      break;
  }
  display.drawImage(bitmap, phyX, phyY, width, height);
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  int phyX = 0;
  int phyY = 0;
  rotateCoordinates(orientation, x, y, &phyX, &phyY);
  switch (orientation) {
    case Portrait:
      phyY = phyY - height;
      break;
    case PortraitInverted:
      phyX = phyX - width;
      break;
    case LandscapeClockwise:
      phyY = phyY - height;
      phyX = phyX - width;
      break;
    case LandscapeCounterClockwise:
      break;
  }
  display.drawImageTransparent(bitmap, phyX, phyY, width, height);
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  if (fontCacheManager_ && fontCacheManager_->isScanning()) return;
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);
  LOG_DBG("GFX", "Cropping %dx%d by %dx%d pix, is %s", bitmap.getWidth(), bitmap.getHeight(), cropPixX, cropPixY,
          bitmap.isTopDown() ? "top-down" : "bottom-up");

  if (maxWidth > 0 && (1.0f - cropX) * bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>((1.0f - cropX) * bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && (1.0f - cropY) * bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>((1.0f - cropY) * bitmap.getHeight()));
    isScaled = true;
  }
  LOG_DBG("GFX", "Scaling by %f - %s", scale, isScaled ? "scaled" : "not scaled");

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  BitmapRowBuffers rowBuffers(outputRowSize, bitmap.getRowBytes());

  if (!rowBuffers.ok()) {
    LOG_ERR("GFX", "!! Failed to allocate BMP row buffers");
    return;
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    screenY += y;  // the offset should not be scaled
    if (screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(rowBuffers.output, rowBuffers.bytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from bitmap", bmpY);
      return;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) {
        screenX = std::floor(screenX * scale);
      }
      screenX += x;  // the offset should not be scaled
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = rowBuffers.output[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // For 1-bit BMP, output is still 2-bit packed (for consistency with readNextRow)
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  BitmapRowBuffers rowBuffers(outputRowSize, bitmap.getRowBytes());

  if (!rowBuffers.ok()) {
    LOG_ERR("GFX", "!! Failed to allocate 1-bit BMP row buffers");
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    // Read rows sequentially using readNextRow
    if (bitmap.readNextRow(rowBuffers.output, rowBuffers.bytes) != BmpReaderError::Ok) {
      LOG_ERR("GFX", "Failed to read row %d from 1-bit bitmap", bmpY);
      return;
    }

    // Calculate screen Y based on whether BMP is top-down or bottom-up
    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue;  // Continue reading to keep row counter in sync
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      // Get 2-bit value (result of readNextRow quantization)
      const uint8_t val = rowBuffers.output[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      // For 1-bit source: 0 or 1 -> map to black (0,1,2) or white (3)
      // val < 3 means black pixel (draw it)
      if (val < 3) {
        drawPixel(screenX, screenY, true);
      }
      // White pixels (val == 3) are not drawn (leave background)
    }
  }
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    LOG_ERR("GFX", "!! Failed to allocate polygon node buffer");
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X (simple bubble sort, numPoints is small)
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

// For performance measurement (using static to allow "const" methods)
static unsigned long start_ms = 0;

void GfxRenderer::clearScreen(const uint8_t color) const {
  start_ms = millis();
  display.clearScreen(color);
}

void GfxRenderer::invertScreen() const {
  for (int i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    frameBuffer[i] = ~frameBuffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  auto elapsed = millis() - start_ms;
  LOG_DBG("GFX", "Time = %lu ms from clearScreen to displayBuffer", elapsed);
  display.displayBuffer(refreshMode, fadingFix);
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  // U+2026 HORIZONTAL ELLIPSIS (UTF-8: 0xE2 0x80 0xA6)
  static constexpr const char* ellipsis = "\xe2\x80\xa6";
  static constexpr int ELLIPSIS_BYTES = 3;  // 3 UTF-8 bytes for U+2026

  // Fast path: if the text already fits, return it verbatim (one measurement).
  const int textWidth = getTextWidth(fontId, text, style);
  if (textWidth <= maxWidth) {
    return text;
  }

  const EpdFontFamily* fontPtr = findFont(fontId);
  if (!fontPtr) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return "";
  }
  const EpdFontFamily& font = *fontPtr;

  // Measure the ellipsis width once up front.
  int ellipsisW = 0, ellipsisH = 0;
  font.getTextDimensions(ellipsis, &ellipsisW, &ellipsisH, style);

  if (ellipsisW > maxWidth) {
    // Even the ellipsis alone doesn't fit — return empty string.
    return "";
  }

  const int budget = maxWidth - ellipsisW;

  // Single forward pass: accumulate glyph advances (same fixed-point logic as
  // getTextAdvanceX) and track the last byte offset where accumulated <= budget.
  // Combining marks do not advance the cursor (same as getTextAdvanceX).
  int32_t widthFP = 0;  // 12.4 fixed-point accumulator
  uint32_t cp = 0;
  uint32_t prevCp = 0;
  const char* cursor = text;
  const char* lastFitEnd = text;  // byte position of the last fitting cut point

  while (*(cursor)) {
    cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor));
    if (cp == 0) break;

    if (utf8IsCombiningMark(cp)) {
      // Combining marks are drawn over the previous base glyph and don't advance.
      continue;
    }

    cp = font.applyLigatures(cp, cursor, style);

    int32_t kernFP = 0;
    if (prevCp != 0) {
      kernFP = font.getKerning(prevCp, cp, style);  // 4.4 fixed-point
    }

    const EpdGlyph* glyph = font.getGlyph(cp, style);
    const int32_t advanceFP = glyph ? static_cast<int32_t>(glyph->advanceX) : 0;

    // Check BEFORE adding this glyph: if the addition would exceed budget, stop.
    if (fp4::toPixel(widthFP + kernFP + advanceFP) > budget) {
      break;
    }

    widthFP += kernFP + advanceFP;
    prevCp = cp;
    lastFitEnd = cursor;  // cursor has already advanced past this codepoint
  }

  // Build result: prefix up to lastFitEnd, then the ellipsis.
  const int prefixLen = static_cast<int>(lastFitEnd - text);

  std::string result;
  result.reserve(static_cast<size_t>(prefixLen) + ELLIPSIS_BYTES);
  result.append(text, static_cast<size_t>(prefixLen));
  result.append(ellipsis, ELLIPSIS_BYTES);
  return result;
}

std::vector<std::string> GfxRenderer::wrappedText(const int fontId, const char* text, const int maxWidth,
                                                  const int maxLines, const EpdFontFamily::Style style) const {
  constexpr size_t kWrappedTextSegmentLimit = 512;
  std::vector<std::string> lines;

  if (!text || maxWidth <= 0 || maxLines <= 0) return lines;

  std::string currentLine;
  const char* cursor = text;

  auto boundedWord = [kWrappedTextSegmentLimit](const char* start, size_t len) {
    return std::string(start, std::min(len, kWrappedTextSegmentLimit));
  };
  auto boundedCStringLen = [](const char* start, size_t limit) {
    size_t len = 0;
    while (len < limit && start[len] != '\0') {
      ++len;
    }
    return len;
  };

  while (*cursor != '\0') {
    while (*cursor == ' ') {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    if (static_cast<int>(lines.size()) == maxLines - 1) {
      // Last available line: keep input bounded so malformed or huge PDF text
      // runs cannot force a large allocation before truncation.
      if (currentLine.empty()) {
        lines.push_back(truncatedText(
            fontId, boundedWord(cursor, boundedCStringLen(cursor, kWrappedTextSegmentLimit)).c_str(), maxWidth, style));
        return lines;
      }
      std::string lastContent = currentLine;
      if (lastContent.size() < kWrappedTextSegmentLimit) {
        lastContent.push_back(' ');
        const size_t room = kWrappedTextSegmentLimit - lastContent.size();
        lastContent.append(cursor, boundedCStringLen(cursor, room));
      }
      lines.push_back(truncatedText(fontId, lastContent.c_str(), maxWidth, style));
      return lines;
    }

    const char* wordStart = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
      ++cursor;
    }
    const size_t wordLen = static_cast<size_t>(cursor - wordStart);

    const size_t oldLen = currentLine.size();
    bool hitSegmentLimit = false;
    if (!currentLine.empty() && currentLine.size() < kWrappedTextSegmentLimit) {
      currentLine.push_back(' ');
    }
    if (currentLine.size() < kWrappedTextSegmentLimit) {
      const size_t room = kWrappedTextSegmentLimit - currentLine.size();
      const size_t appended = std::min(wordLen, room);
      currentLine.append(wordStart, appended);
      hitSegmentLimit = appended < wordLen;
    } else {
      hitSegmentLimit = true;
    }

    if (!hitSegmentLimit && getTextWidth(fontId, currentLine.c_str(), style) <= maxWidth) {
      continue;
    } else {
      currentLine.resize(oldLen);
      if (!currentLine.empty()) {
        lines.push_back(currentLine);
        // If the carried-over word itself exceeds maxWidth, truncate it and
        // push it as a complete line immediately — storing it in currentLine
        // would allow a subsequent short word to be appended after the ellipsis.
        currentLine = boundedWord(wordStart, wordLen);
        if (getTextWidth(fontId, currentLine.c_str(), style) > maxWidth) {
          lines.push_back(truncatedText(fontId, currentLine.c_str(), maxWidth, style));
          currentLine.clear();
          if (static_cast<int>(lines.size()) >= maxLines) return lines;
        }
      } else {
        // Single word wider than maxWidth: truncate and stop to avoid complicated
        // splitting rules (different between languages). Results in an aesthetically
        // pleasing end.
        currentLine = boundedWord(wordStart, wordLen);
        lines.push_back(truncatedText(fontId, currentLine.c_str(), maxWidth, style));
        return lines;
      }
    }
  }

  if (!currentLine.empty() && static_cast<int>(lines.size()) < maxLines) {
    lines.push_back(currentLine);
  }

  return lines;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId, const EpdFontFamily::Style style) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  const EpdGlyph* spaceGlyph = font->getGlyph(' ', style);
  return spaceGlyph ? fp4::toPixel(spaceGlyph->advanceX) : 0;  // snap 12.4 fixed-point to nearest pixel
}

int GfxRenderer::getSpaceAdvance(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                                 const EpdFontFamily::Style style) const {
  const EpdFontFamily* fontPtr = findFont(fontId);
  if (!fontPtr) return 0;
  const auto& font = *fontPtr;
  const EpdGlyph* spaceGlyph = font.getGlyph(' ', style);
  const int32_t spaceAdvanceFP = spaceGlyph ? static_cast<int32_t>(spaceGlyph->advanceX) : 0;
  // Combine space advance + flanking kern into one fixed-point sum before snapping.
  // Snapping the combined value avoids the +/-1 px error from snapping each component separately.
  const int32_t kernFP = static_cast<int32_t>(font.getKerning(leftCp, ' ', style)) +
                         static_cast<int32_t>(font.getKerning(' ', rightCp, style));
  return fp4::toPixel(spaceAdvanceFP + kernFP);
}

int GfxRenderer::getKerning(const int fontId, const uint32_t leftCp, const uint32_t rightCp,
                            const EpdFontFamily::Style style) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) return 0;
  const int kernFP = font->getKerning(leftCp, rightCp, style);  // 4.4 fixed-point
  return fp4::toPixel(kernFP);                                  // snap 4.4 fixed-point to nearest pixel
}

int GfxRenderer::getTextAdvanceX(const int fontId, const char* text, EpdFontFamily::Style style) const {
  const EpdFontFamily* fontPtr = findFont(fontId);
  if (!fontPtr) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  uint32_t cp;
  uint32_t prevCp = 0;
  int32_t widthFP = 0;  // 12.4 fixed-point accumulator
  const auto& font = *fontPtr;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }
    cp = font.applyLigatures(cp, text, style);
    if (prevCp != 0) {
      widthFP += font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern
    }
    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (glyph) widthFP += glyph->advanceX;  // 12.4 fixed-point advance
    prevCp = cp;
  }
  return fp4::toPixel(widthFP);  // snap 12.4 fixed-point to nearest pixel
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return font->getData(EpdFontFamily::REGULAR)->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }

  return font->getData(EpdFontFamily::REGULAR)->advanceY;
}

int GfxRenderer::getTextHeight(const int fontId) const {
  const EpdFontFamily* font = findFont(fontId);
  if (!font) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return 0;
  }
  return font->getData(EpdFontFamily::REGULAR)->ascender;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  const EpdFontFamily* fontPtr = findFont(fontId);
  if (!fontPtr) {
    LOG_ERR("GFX", "Font %d not found", fontId);
    return;
  }

  const auto& font = *fontPtr;

  int32_t yPosFP = fp4::fromPixel(y);  // 12.4 fixed-point accumulator
  int lastBaseY = y;
  int lastBaseAdvanceFP = 0;  // 12.4 fixed-point
  int lastBaseTop = 0;
  constexpr int MIN_COMBINING_GAP_PX = 1;

  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    if (utf8IsCombiningMark(cp)) {
      const EpdGlyph* combiningGlyph = font.getGlyph(cp, style);
      int raiseBy = 0;
      if (combiningGlyph) {
        const int currentGap = combiningGlyph->top - combiningGlyph->height - lastBaseTop;
        if (currentGap < MIN_COMBINING_GAP_PX) {
          raiseBy = MIN_COMBINING_GAP_PX - currentGap;
        }
      }

      const int combiningX = x - raiseBy;
      const int combiningY = lastBaseY - fp4::toPixel(lastBaseAdvanceFP / 2);
      renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, combiningX, combiningY, black, style);
      continue;
    }

    cp = font.applyLigatures(cp, text, style);
    if (prevCp != 0) {
      yPosFP -= font.getKerning(prevCp, cp, style);  // 4.4 fixed-point kern (subtract for rotated)
    }

    lastBaseY = fp4::toPixel(yPosFP);  // snap 12.4 fixed-point to nearest pixel
    const EpdGlyph* glyph = font.getGlyph(cp, style);

    lastBaseAdvanceFP = glyph ? glyph->advanceX : 0;  // 12.4 fixed-point
    lastBaseTop = glyph ? glyph->top : 0;

    renderCharImpl<TextRotation::Rotated90CW>(*this, renderMode, font, cp, x, lastBaseY, black, style);
    if (glyph) {
      yPosFP -= glyph->advanceX;  // 12.4 fixed-point advance (subtract for rotated)
    }
    prevCp = cp;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(fadingFix); }

void GfxRenderer::freeBwBufferChunks() {
  if (bwBufferSingleAlloc) {
    // Single allocation: only slot 0 is used
    bwBufferSingleAlloc = false;
    if (bwBufferChunks[0]) {
      free(bwBufferChunks[0]);
      bwBufferChunks[0] = nullptr;
    }
    return;
  }
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Tries a single 48KB allocation first (lower fragmentation), falls back to 8KB chunks
 * if heap cannot supply a single contiguous block.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  // Sanity: no buffer should be stored when this is called
  for (auto& chunk : bwBufferChunks) {
    if (chunk) {
      LOG_ERR("GFX", "!! BW buffer already stored - freeing stale data");
      break;
    }
  }
  freeBwBufferChunks();

  // Strategy 1: single large allocation (creates 1 heap region instead of 6)
  uint8_t* buf = static_cast<uint8_t*>(malloc(HalDisplay::BUFFER_SIZE));
  if (buf) {
    memcpy(buf, frameBuffer, HalDisplay::BUFFER_SIZE);
    bwBufferChunks[0] = buf;
    bwBufferSingleAlloc = true;
    LOG_DBG("GFX", "Stored BW buffer as single alloc (%zu bytes)", static_cast<size_t>(HalDisplay::BUFFER_SIZE));
    return true;
  }

  // Strategy 2: chunked allocation (fallback when heap is fragmented)
  bwBufferSingleAlloc = false;
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(BW_BUFFER_CHUNK_SIZE));

    if (!bwBufferChunks[i]) {
      LOG_ERR("GFX", "!! Failed to allocate BW buffer chunk %zu (%zu bytes)", i, BW_BUFFER_CHUNK_SIZE);
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, BW_BUFFER_CHUNK_SIZE);
  }

  LOG_DBG("GFX", "Stored BW buffer in %zu chunks (%zu bytes each) [single alloc failed]", BW_BUFFER_NUM_CHUNKS,
          BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Supports both single-allocation and chunked storage modes.
 */
void GfxRenderer::restoreBwBuffer() {
  if (bwBufferSingleAlloc) {
    // Single allocation: one big memcpy from slot 0
    if (!bwBufferChunks[0]) {
      LOG_ERR("GFX", "!! Single-alloc BW buffer: slot 0 is null");
      bwBufferSingleAlloc = false;
      return;
    }
    memcpy(frameBuffer, bwBufferChunks[0], HalDisplay::BUFFER_SIZE);
  } else {
    // Chunked: verify all slots are present
    for (const auto& chunk : bwBufferChunks) {
      if (!chunk) {
        LOG_ERR("GFX", "!! Chunked BW buffer: missing chunk, skipping restore");
        freeBwBufferChunks();
        return;
      }
    }

    for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
      const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
      memcpy(frameBuffer + offset, bwBufferChunks[i], BW_BUFFER_CHUNK_SIZE);
    }
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  LOG_DBG("GFX", "Restored and freed BW buffer");
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
