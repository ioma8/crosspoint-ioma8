#pragma once

#include <array>

#include "../Activity.h"

class Game2048Activity final : public Activity {
 public:
  explicit Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Game2048", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { Ready, Playing, Won, Lost };
  enum class MoveDirection { Up, Down, Left, Right };

  static constexpr int GRID_SIZE = 4;

  std::array<uint16_t, GRID_SIZE * GRID_SIZE> board = {};
  State state = State::Ready;
  uint32_t score = 0;
  uint32_t bestScore = 0;
  bool continueAfterWin = false;

  void resetGame();
  bool spawnTile();
  bool canMove() const;
  bool hasWonTile() const;
  bool performMove(MoveDirection direction);
  void slideAndMergeLine(std::array<uint16_t, GRID_SIZE>& line, bool& moved);
  void extractLine(MoveDirection direction, int index, std::array<uint16_t, GRID_SIZE>& line) const;
  void writeLine(MoveDirection direction, int index, const std::array<uint16_t, GRID_SIZE>& line);
  void updateStateAfterMove();

  void drawBoard() const;
  void drawTile(int x, int y, int size, uint16_t value) const;
  static int tileShade(uint16_t value);
  static int tileFontId(uint16_t value);
};
