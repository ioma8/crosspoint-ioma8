#include "ReaderBookmarkIndicator.h"

#include <GfxRenderer.h>

namespace ReaderBookmarkIndicator {

void draw(const GfxRenderer& renderer) {
  constexpr int width = 10;
  constexpr int height = 18;
  constexpr int top = 8;
  const int left = renderer.getScreenWidth() - width - 8;

  renderer.fillRect(left, top, width, height, true);
  const int notchX[] = {left, left + width / 2, left + width - 1};
  const int notchY[] = {top + height - 1, top + height - 6, top + height - 1};
  renderer.fillPolygon(notchX, notchY, 3, false);
}

void drawIf(const GfxRenderer& renderer, const bool visible) {
  if (visible) {
    draw(renderer);
  }
}

}  // namespace ReaderBookmarkIndicator
