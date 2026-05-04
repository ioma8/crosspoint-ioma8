#pragma once

#include <array>

#include "../Activity.h"

class GameSokobanActivity final : public Activity {
 public:
  explicit GameSokobanActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GameSokoban", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State { Playing, CompletedAll, LevelComplete };
  enum class Tile : uint8_t { Empty, Wall, Goal, Box, BoxOnGoal, Player, PlayerOnGoal };
  enum class MoveDirection { Up, Down, Left, Right };

  static constexpr int MAX_W = 14;
  static constexpr int MAX_H = 16;

  int levelIndex = 0;
  int levelWidth = 0;
  int levelHeight = 0;
  int moveCount = 0;
  int pushCount = 0;
  State state = State::Playing;
  std::array<Tile, MAX_W * MAX_H> board = {};

  void loadLevel(int index);
  bool tryMove(MoveDirection direction);
  bool isSolved() const;
  int findPlayer() const;
  Tile getTile(int x, int y) const;
  void setTile(int x, int y, Tile tile);
  void drawBoard() const;
  void drawTile(int x, int y, int size, Tile tile) const;
};
