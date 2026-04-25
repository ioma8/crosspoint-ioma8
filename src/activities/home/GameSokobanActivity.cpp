#include "GameSokobanActivity.h"
#include "GameSokobanLevels.h"

#include <Arduino.h>

#include <I18n.h>

#include "GfxRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int TOP_MARGIN = 24;
constexpr int HUD_HEIGHT = 92;
constexpr int BOARD_MARGIN = 22;
constexpr int CELL_GAP = 4;
}  // namespace

void GameSokobanActivity::onEnter() {
  Activity::onEnter();
  loadLevel(0);
}

void GameSokobanActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::LevelComplete) {
      loadLevel(levelIndex + 1);
    } else if (state == State::CompletedAll) {
      loadLevel(0);
    } else {
      loadLevel(levelIndex);
    }
    return;
  }

  if (state != State::Playing) {
    return;
  }

  bool moved = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moved = tryMove(MoveDirection::Left);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moved = tryMove(MoveDirection::Right);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    moved = tryMove(MoveDirection::Up);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    moved = tryMove(MoveDirection::Down);
  }

  if (moved) {
    if (isSolved()) {
      if (levelIndex + 1 >= GameSokobanLevels::LEVEL_COUNT) {
        state = State::CompletedAll;
      } else {
        state = State::LevelComplete;
      }
    }
    requestUpdate();
  }
}

void GameSokobanActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawBoard();
  renderer.displayBuffer(state == State::Playing ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
}

void GameSokobanActivity::loadLevel(const int index) {
  levelIndex = index % GameSokobanLevels::LEVEL_COUNT;
  levelWidth = GameSokobanLevels::kLevels[levelIndex].width;
  levelHeight = GameSokobanLevels::kLevels[levelIndex].height;
  moveCount = 0;
  pushCount = 0;
  state = State::Playing;
  board.fill(Tile::Empty);

  for (int y = 0; y < levelHeight; y++) {
    const char* row = GameSokobanLevels::kLevels[levelIndex].rows[y];
    for (int x = 0; x < levelWidth; x++) {
      switch (row[x]) {
        case '#':
          setTile(x, y, Tile::Wall);
          break;
        case '.':
          setTile(x, y, Tile::Goal);
          break;
        case '$':
          setTile(x, y, Tile::Box);
          break;
        case '*':
          setTile(x, y, Tile::BoxOnGoal);
          break;
        case '@':
          setTile(x, y, Tile::Player);
          break;
        case '+':
          setTile(x, y, Tile::PlayerOnGoal);
          break;
        default:
          setTile(x, y, Tile::Empty);
          break;
      }
    }
  }

  requestUpdate();
}

bool GameSokobanActivity::tryMove(const MoveDirection direction) {
  const int playerIndex = findPlayer();
  if (playerIndex < 0) {
    return false;
  }

  const int px = playerIndex % MAX_W;
  const int py = playerIndex / MAX_W;
  int dx = 0;
  int dy = 0;
  switch (direction) {
    case MoveDirection::Left:
      dx = -1;
      break;
    case MoveDirection::Right:
      dx = 1;
      break;
    case MoveDirection::Up:
      dy = -1;
      break;
    case MoveDirection::Down:
      dy = 1;
      break;
  }

  const int tx = px + dx;
  const int ty = py + dy;
  const Tile target = getTile(tx, ty);
  if (target == Tile::Wall) {
    return false;
  }

  const auto leaveTile = board[playerIndex] == Tile::PlayerOnGoal ? Tile::Goal : Tile::Empty;

  if (target == Tile::Box || target == Tile::BoxOnGoal) {
    const int bx = tx + dx;
    const int by = ty + dy;
    const Tile beyond = getTile(bx, by);
    if (beyond == Tile::Wall || beyond == Tile::Box || beyond == Tile::BoxOnGoal) {
      return false;
    }
    setTile(bx, by, beyond == Tile::Goal ? Tile::BoxOnGoal : Tile::Box);
    setTile(tx, ty, target == Tile::BoxOnGoal ? Tile::PlayerOnGoal : Tile::Player);
    setTile(px, py, leaveTile);
    moveCount++;
    pushCount++;
    return true;
  }

  if (target == Tile::Empty || target == Tile::Goal) {
    setTile(tx, ty, target == Tile::Goal ? Tile::PlayerOnGoal : Tile::Player);
    setTile(px, py, leaveTile);
    moveCount++;
    return true;
  }

  return false;
}

bool GameSokobanActivity::isSolved() const {
  for (Tile tile : board) {
    if (tile == Tile::Box) {
      return false;
    }
  }
  return true;
}

int GameSokobanActivity::findPlayer() const {
  for (int i = 0; i < MAX_W * MAX_H; i++) {
    if (board[i] == Tile::Player || board[i] == Tile::PlayerOnGoal) {
      return i;
    }
  }
  return -1;
}

GameSokobanActivity::Tile GameSokobanActivity::getTile(const int x, const int y) const {
  if (x < 0 || x >= levelWidth || y < 0 || y >= levelHeight) {
    return Tile::Wall;
  }
  return board[y * MAX_W + x];
}

void GameSokobanActivity::setTile(const int x, const int y, const Tile tile) { board[y * MAX_W + x] = tile; }

void GameSokobanActivity::drawBoard() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  char levelBuffer[20];
  char statsBuffer[32];
  snprintf(levelBuffer, sizeof(levelBuffer), "Level %d/%d", levelIndex + 1, GameSokobanLevels::LEVEL_COUNT);
  snprintf(statsBuffer, sizeof(statsBuffer), "Moves %d  Pushes %d", moveCount, pushCount);

  renderer.drawCenteredText(UI_12_FONT_ID, TOP_MARGIN, "Sokoban", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, BOARD_MARGIN, TOP_MARGIN + 34, levelBuffer, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, TOP_MARGIN + 58, statsBuffer);
  renderer.drawCenteredText(UI_10_FONT_ID, TOP_MARGIN + 74, "Push all crates onto targets");

  const int availableWidth = screenWidth - BOARD_MARGIN * 2;
  const int availableHeight = screenHeight - HUD_HEIGHT - BOARD_MARGIN * 2;
  const int cellSize =
      std::min((availableWidth - CELL_GAP * (levelWidth + 1)) / levelWidth,
               (availableHeight - CELL_GAP * (levelHeight + 1)) / levelHeight);
  const int boardWidthPx = levelWidth * cellSize + (levelWidth + 1) * CELL_GAP;
  const int boardHeightPx = levelHeight * cellSize + (levelHeight + 1) * CELL_GAP;
  const int boardX = (screenWidth - boardWidthPx) / 2;
  const int boardY = HUD_HEIGHT + (availableHeight - boardHeightPx) / 2;

  renderer.fillRoundedRect(boardX, boardY, boardWidthPx, boardHeightPx, 8, Color::LightGray);
  renderer.drawRoundedRect(boardX, boardY, boardWidthPx, boardHeightPx, 2, 8, true);

  for (int y = 0; y < levelHeight; y++) {
    for (int x = 0; x < levelWidth; x++) {
      const int tileX = boardX + CELL_GAP + x * (cellSize + CELL_GAP);
      const int tileY = boardY + CELL_GAP + y * (cellSize + CELL_GAP);
      drawTile(tileX, tileY, cellSize, getTile(x, y));
    }
  }

  if (state == State::LevelComplete || state == State::CompletedAll) {
    const int overlayWidth = boardWidthPx - 36;
    const int overlayHeight = 120;
    const int overlayX = boardX + (boardWidthPx - overlayWidth) / 2;
    const int overlayY = boardY + (boardHeightPx - overlayHeight) / 2;
    renderer.fillRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 8, Color::White);
    renderer.drawRoundedRect(overlayX, overlayY, overlayWidth, overlayHeight, 2, 8, true);
    renderer.drawCenteredText(UI_12_FONT_ID, overlayY + 18, state == State::CompletedAll ? "All Levels Clear" : "Level Clear",
                              true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, overlayY + 52,
                              state == State::CompletedAll ? "Confirm restarts at level 1" : "Confirm loads next level");
    renderer.drawCenteredText(UI_10_FONT_ID, overlayY + 74, "Back returns to Games");
  }

  const char* confirmLabel = state == State::Playing ? "Restart" : "Next";
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, "Left", "Right");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void GameSokobanActivity::drawTile(const int x, const int y, const int size, const Tile tile) const {
  switch (tile) {
    case Tile::Wall:
      renderer.fillRoundedRect(x, y, size, size, 4, Color::Black);
      break;
    case Tile::Goal:
      renderer.fillRoundedRect(x, y, size, size, 4, Color::White);
      renderer.drawRoundedRect(x, y, size, size, 1, 4, true);
      renderer.fillRectDither(x + size / 4, y + size / 4, size / 2, size / 2, Color::LightGray);
      break;
    case Tile::Box:
      renderer.fillRoundedRect(x, y, size, size, 4, Color::DarkGray);
      renderer.drawRoundedRect(x, y, size, size, 1, 4, true);
      renderer.drawLine(x + 4, y + 4, x + size - 5, y + size - 5, true);
      renderer.drawLine(x + size - 5, y + 4, x + 4, y + size - 5, true);
      break;
    case Tile::BoxOnGoal:
      renderer.fillRoundedRect(x, y, size, size, 4, Color::DarkGray);
      renderer.drawRoundedRect(x, y, size, size, 1, 4, true);
      renderer.fillRectDither(x + size / 4, y + size / 4, size / 2, size / 2, Color::White);
      break;
    case Tile::Player:
    case Tile::PlayerOnGoal:
      if (tile == Tile::PlayerOnGoal) {
        renderer.fillRoundedRect(x, y, size, size, 4, Color::White);
        renderer.drawRoundedRect(x, y, size, size, 1, 4, true);
        renderer.fillRectDither(x + size / 4, y + size / 4, size / 2, size / 2, Color::LightGray);
      }
      {
        const int headSize = std::max(6, size / 4);
        const int headX = x + (size - headSize) / 2;
        const int headY = y + std::max(2, size / 10);
        const int torsoWidth = std::max(8, size / 2);
        const int torsoHeight = std::max(10, size / 3);
        const int torsoX = x + (size - torsoWidth) / 2;
        const int torsoY = headY + headSize - 1;
        const int shoulderY = torsoY + std::max(2, torsoHeight / 5);
        const int footY = y + size - std::max(5, size / 6);
        const int legGap = std::max(2, size / 10);
        const int leftLegX = x + size / 2 - legGap - 2;
        const int rightLegX = x + size / 2 + legGap - 1;

        renderer.fillRoundedRect(headX, headY, headSize, headSize, headSize / 2, Color::Black);
        renderer.fillRoundedRect(torsoX, torsoY, torsoWidth, torsoHeight, 4, Color::Black);
        renderer.fillRect(torsoX + torsoWidth / 4, torsoY + 3, torsoWidth / 2, std::max(3, torsoHeight / 3), false);
        renderer.drawLine(torsoX - 1, shoulderY, torsoX + torsoWidth / 3, shoulderY + std::max(2, size / 10), true);
        renderer.drawLine(torsoX + torsoWidth, shoulderY, torsoX + torsoWidth * 2 / 3, shoulderY + std::max(2, size / 10),
                          true);
        renderer.drawLine(x + size / 2 - 1, torsoY + torsoHeight - 1, leftLegX, footY, true);
        renderer.drawLine(x + size / 2 + 1, torsoY + torsoHeight - 1, rightLegX, footY, true);
      }
      break;
    case Tile::Empty:
    default:
      renderer.fillRoundedRect(x, y, size, size, 4, Color::White);
      renderer.drawRoundedRect(x, y, size, size, 1, 4, true);
      break;
  }
}
