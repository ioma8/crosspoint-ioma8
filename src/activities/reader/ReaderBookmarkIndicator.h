#pragma once

class GfxRenderer;

namespace ReaderBookmarkIndicator {
void draw(const GfxRenderer& renderer);
void drawIf(const GfxRenderer& renderer, bool visible);
}  // namespace ReaderBookmarkIndicator
