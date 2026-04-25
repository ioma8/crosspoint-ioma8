#include "Game2048Activity.h"

#include <Arduino.h>
#include <esp_system.h>

#include "GfxRenderer.h"
#include <I18n.h>
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TOP_MARGIN = 24;
constexpr int HUD_HEIGHT = 88;
constexpr int BOARD_MARGIN = 24;
constexpr int CELL_GAP = 10;
constexpr int BOARD_CORNER_RADIUS = 8;
constexpr int TILE_CORNER_RADIUS = 6;
uint32_t sessionBestScore = 0;

int tileShadeForValue(const uint16_t value) {
  if (value == 0) return 0;
  if (value <= 4) return 1;
  if (value <= 64) return 2;
  return 3;
}

Color tileColorForValue(const uint16_t value) {
  switch (tileShadeForValue(value)) {
    case 1:
      return Color::LightGray;
    case 2:
      return Color::DarkGray;
    case 3:
      return Color::Black;
    default:
      return Color::White;
  }
}
}  // namespace

void Game2048Activity::onEnter() {
  Activity::onEnter();
  static bool seeded = false;
  if (!seeded) {
    randomSeed(esp_random());
    seeded = true;
  }
  bestScore = sessionBestScore;
  resetGame();
}

void Game2048Activity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    resetGame();
    return;
  }

  MoveDirection direction;
  bool hasMove = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    direction = MoveDirection::Left;
    hasMove = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    direction = MoveDirection::Right;
    hasMove = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    direction = MoveDirection::Up;
    hasMove = true;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    direction = MoveDirection::Down;
    hasMove = true;
  }

  if (!hasMove || state == State::Lost || (state == State::Won && !continueAfterWin)) {
    return;
  }

  if (performMove(direction)) {
    spawnTile();
    updateStateAfterMove();
    requestUpdate();
  }
}

void Game2048Activity::render(RenderLock&&) {
  renderer.clearScreen();
  drawBoard();
  renderer.displayBuffer(state == State::Playing ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
}

void Game2048Activity::resetGame() {
  board.fill(0);
  score = 0;
  state = State::Playing;
  continueAfterWin = false;
  spawnTile();
  spawnTile();
  requestUpdate();
}

bool Game2048Activity::spawnTile() {
  int emptyCount = 0;
  for (uint16_t value : board) {
    if (value == 0) {
      emptyCount++;
    }
  }
  if (emptyCount == 0) {
    return false;
  }

  int selected = random(emptyCount);
  for (uint16_t& value : board) {
    if (value != 0) {
      continue;
    }
    if (selected == 0) {
      value = random(10) == 0 ? 4 : 2;
      return true;
    }
    selected--;
  }
  return false;
}

bool Game2048Activity::canMove() const {
  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      const uint16_t value = board[row * GRID_SIZE + col];
      if (value == 0) {
        return true;
      }
      if (col + 1 < GRID_SIZE && value == board[row * GRID_SIZE + col + 1]) {
        return true;
      }
      if (row + 1 < GRID_SIZE && value == board[(row + 1) * GRID_SIZE + col]) {
        return true;
      }
    }
  }
  return false;
}

bool Game2048Activity::hasWonTile() const {
  for (uint16_t value : board) {
    if (value >= 2048) {
      return true;
    }
  }
  return false;
}

bool Game2048Activity::performMove(const MoveDirection direction) {
  bool moved = false;
  for (int index = 0; index < GRID_SIZE; index++) {
    std::array<uint16_t, GRID_SIZE> line{};
    extractLine(direction, index, line);
    const auto original = line;
    slideAndMergeLine(line, moved);
    if (line != original) {
      writeLine(direction, index, line);
      moved = true;
    }
  }
  if (score > bestScore) {
    bestScore = score;
    sessionBestScore = bestScore;
  }
  return moved;
}

void Game2048Activity::slideAndMergeLine(std::array<uint16_t, GRID_SIZE>& line, bool& moved) {
  std::array<uint16_t, GRID_SIZE> compact{};
  int writeIndex = 0;
  for (uint16_t value : line) {
    if (value != 0) {
      compact[writeIndex++] = value;
    }
  }

  for (int i = 0; i < GRID_SIZE - 1; i++) {
    if (compact[i] != 0 && compact[i] == compact[i + 1]) {
      compact[i] *= 2;
      score += compact[i];
      compact[i + 1] = 0;
      moved = true;
    }
  }

  std::array<uint16_t, GRID_SIZE> merged{};
  writeIndex = 0;
  for (uint16_t value : compact) {
    if (value != 0) {
      merged[writeIndex++] = value;
    }
  }

  if (merged != line) {
    moved = true;
    line = merged;
  }
}

void Game2048Activity::extractLine(const MoveDirection direction, const int index,
                                   std::array<uint16_t, GRID_SIZE>& line) const {
  for (int offset = 0; offset < GRID_SIZE; offset++) {
    switch (direction) {
      case MoveDirection::Left:
        line[offset] = board[index * GRID_SIZE + offset];
        break;
      case MoveDirection::Right:
        line[offset] = board[index * GRID_SIZE + (GRID_SIZE - 1 - offset)];
        break;
      case MoveDirection::Up:
        line[offset] = board[offset * GRID_SIZE + index];
        break;
      case MoveDirection::Down:
        line[offset] = board[(GRID_SIZE - 1 - offset) * GRID_SIZE + index];
        break;
    }
  }
}

void Game2048Activity::writeLine(const MoveDirection direction, const int index,
                                 const std::array<uint16_t, GRID_SIZE>& line) {
  for (int offset = 0; offset < GRID_SIZE; offset++) {
    switch (direction) {
      case MoveDirection::Left:
        board[index * GRID_SIZE + offset] = line[offset];
        break;
      case MoveDirection::Right:
        board[index * GRID_SIZE + (GRID_SIZE - 1 - offset)] = line[offset];
        break;
      case MoveDirection::Up:
        board[offset * GRID_SIZE + index] = line[offset];
        break;
      case MoveDirection::Down:
        board[(GRID_SIZE - 1 - offset) * GRID_SIZE + index] = line[offset];
        break;
    }
  }
}

void Game2048Activity::updateStateAfterMove() {
  if (hasWonTile() && !continueAfterWin) {
    state = State::Won;
    continueAfterWin = true;
  } else if (!canMove()) {
    state = State::Lost;
  } else {
    state = State::Playing;
  }
}

void Game2048Activity::drawBoard() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  char scoreBuffer[24];
  char bestBuffer[24];
  snprintf(scoreBuffer, sizeof(scoreBuffer), "Score %lu", static_cast<unsigned long>(score));
  snprintf(bestBuffer, sizeof(bestBuffer), "Best %lu", static_cast<unsigned long>(bestScore));

  renderer.drawCenteredText(UI_12_FONT_ID, TOP_MARGIN, "2048", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, BOARD_MARGIN, TOP_MARGIN + 34, scoreBuffer, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, screenWidth - BOARD_MARGIN - renderer.getTextWidth(UI_10_FONT_ID, bestBuffer,
                                                                                      EpdFontFamily::BOLD),
                    TOP_MARGIN + 34, bestBuffer, true, EpdFontFamily::BOLD);

  const char* statusLine = "Swipe with arrows";
  if (state == State::Won) {
    statusLine = "2048 reached - keep going";
  } else if (state == State::Lost) {
    statusLine = "No more moves";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, TOP_MARGIN + 58, statusLine);
  renderer.drawCenteredText(UI_10_FONT_ID, TOP_MARGIN + 74, "Front: Left/Right  Side: Up/Down");

  const int boardSize = std::min(screenWidth - BOARD_MARGIN * 2, screenHeight - HUD_HEIGHT - BOARD_MARGIN * 2);
  const int boardX = (screenWidth - boardSize) / 2;
  const int boardY = HUD_HEIGHT;
  const int cellSize = (boardSize - CELL_GAP * (GRID_SIZE + 1)) / GRID_SIZE;

  renderer.fillRoundedRect(boardX, boardY, boardSize, boardSize, BOARD_CORNER_RADIUS, Color::LightGray);
  renderer.drawRoundedRect(boardX, boardY, boardSize, boardSize, 2, BOARD_CORNER_RADIUS, true);

  for (int row = 0; row < GRID_SIZE; row++) {
    for (int col = 0; col < GRID_SIZE; col++) {
      const int tileX = boardX + CELL_GAP + col * (cellSize + CELL_GAP);
      const int tileY = boardY + CELL_GAP + row * (cellSize + CELL_GAP);
      drawTile(tileX, tileY, cellSize, board[row * GRID_SIZE + col]);
    }
  }

  if (state == State::Won || state == State::Lost) {
    const int overlayWidth = boardSize - 48;
    const int overlayHeight = 132;
    const int overlayX = boardX + (boardSize - overlayWidth) / 2;
    const int overlayY = boardY + (boardSize - overlayHeight) / 2;
    renderer.fillRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 8, Color::White);
    renderer.drawRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 2, 8, true);
    renderer.drawCenteredText(UI_12_FONT_ID, overlayY + 18, state == State::Won ? "You Win" : "Game Over", true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, overlayY + 52, "Confirm starts a new run");
    renderer.drawCenteredText(UI_10_FONT_ID, overlayY + 74, "Back returns to Games");
    if (state == State::Won) {
      renderer.drawCenteredText(UI_10_FONT_ID, overlayY + 96, "Arrow keys keep playing");
    }
  }

  const char* confirmLabel = (state == State::Lost || state == State::Won) ? "Restart" : "New";
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, "Left", "Right");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void Game2048Activity::drawTile(const int x, const int y, const int size, const uint16_t value) const {
  const Color fill = tileColorForValue(value);
  renderer.fillRoundedRect(x, y, size, size, TILE_CORNER_RADIUS, fill);
  renderer.drawRoundedRect(x, y, size, size, 1, TILE_CORNER_RADIUS, true);

  if (value == 0) {
    return;
  }

  char tileText[8];
  snprintf(tileText, sizeof(tileText), "%u", value);
  const int fontId = tileFontId(value);
  const auto style = fill == Color::Black ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int textWidth = renderer.getTextWidth(fontId, tileText, style);
  const int textX = x + (size - textWidth) / 2;
  const int textY = y + (size - renderer.getLineHeight(fontId)) / 2 + 4;
  const bool blackText = fill != Color::Black;
  renderer.drawText(fontId, textX, textY, tileText, blackText, style);
}

int Game2048Activity::tileShade(const uint16_t value) {
  return tileShadeForValue(value);
}

int Game2048Activity::tileFontId(const uint16_t value) {
  if (value >= 1024) return UI_10_FONT_ID;
  return UI_12_FONT_ID;
}
