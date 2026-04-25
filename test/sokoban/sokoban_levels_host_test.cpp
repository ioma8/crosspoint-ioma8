#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/activities/home/GameSokobanLevels.h"

namespace {
struct LevelState {
  int width = 0;
  int height = 0;
  std::string walls;
  std::string goals;
  std::string boxes;
  int player = -1;
};

struct Reachability {
  std::vector<uint8_t> seen;
  int canonicalPlayer = -1;
};

LevelState parseLevel(const GameSokobanLevels::LevelDef& def) {
  LevelState st;
  st.width = def.width;
  st.height = def.height;
  const int size = st.width * st.height;
  st.walls.assign(size, '0');
  st.goals.assign(size, '0');
  st.boxes.assign(size, '0');

  int goalCount = 0;
  int boxCount = 0;
  for (int y = 0; y < st.height; y++) {
    const char* row = def.rows[y];
    for (int x = 0; x < st.width; x++) {
      const int idx = y * st.width + x;
      switch (row[x]) {
        case '#':
          st.walls[idx] = '1';
          break;
        case '.':
          st.goals[idx] = '1';
          goalCount++;
          break;
        case '$':
          st.boxes[idx] = '1';
          boxCount++;
          break;
        case '*':
          st.goals[idx] = '1';
          st.boxes[idx] = '1';
          goalCount++;
          boxCount++;
          break;
        case '@':
          st.player = idx;
          break;
        case '+':
          st.player = idx;
          st.goals[idx] = '1';
          goalCount++;
          break;
        default:
          break;
      }
    }
  }

  assert(st.player >= 0);
  assert(goalCount == boxCount);
  return st;
}

bool isSolved(const LevelState& st) { return st.boxes == st.goals; }

bool isBlocked(const LevelState& st, const int idx) {
  return idx < 0 || idx >= st.width * st.height || st.walls[idx] == '1' || st.boxes[idx] == '1';
}

Reachability computeReachability(const LevelState& st) {
  Reachability out;
  out.seen.assign(st.width * st.height, 0);
  std::queue<int> q;
  q.push(st.player);
  out.seen[st.player] = 1;
  out.canonicalPlayer = st.player;

  while (!q.empty()) {
    const int cur = q.front();
    q.pop();
    out.canonicalPlayer = std::min(out.canonicalPlayer, cur);
    const int x = cur % st.width;
    const int y = cur / st.width;
    const int neighbors[4] = {
        (x > 0) ? cur - 1 : -1,
        (x + 1 < st.width) ? cur + 1 : -1,
        (y > 0) ? cur - st.width : -1,
        (y + 1 < st.height) ? cur + st.width : -1,
    };
    for (int next : neighbors) {
      if (next < 0 || out.seen[next] || isBlocked(st, next)) continue;
      out.seen[next] = 1;
      q.push(next);
    }
  }
  return out;
}

std::string encode(const LevelState& st, const Reachability& reach) {
  return std::to_string(reach.canonicalPlayer) + "|" + st.boxes;
}

bool isDeadSquare(const LevelState& st, const int idx) {
  if (st.goals[idx] == '1') return false;
  const int x = idx % st.width;
  const int y = idx / st.width;
  const bool leftBlocked = x == 0 || st.walls[idx - 1] == '1';
  const bool rightBlocked = x + 1 >= st.width || st.walls[idx + 1] == '1';
  const bool upBlocked = y == 0 || st.walls[idx - st.width] == '1';
  const bool downBlocked = y + 1 >= st.height || st.walls[idx + st.width] == '1';
  return (leftBlocked || rightBlocked) && (upBlocked || downBlocked);
}

std::vector<LevelState> successors(const LevelState& st, const Reachability& reach) {
  std::vector<LevelState> out;
  for (int box = 0; box < st.width * st.height; box++) {
    if (st.boxes[box] != '1') continue;
    const int x = box % st.width;
    const int y = box / st.width;

    struct Dir {
      int dx;
      int dy;
    };
    constexpr Dir dirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& dir : dirs) {
      const int pushFromX = x - dir.dx;
      const int pushFromY = y - dir.dy;
      const int pushToX = x + dir.dx;
      const int pushToY = y + dir.dy;
      if (pushFromX < 0 || pushFromX >= st.width || pushFromY < 0 || pushFromY >= st.height) continue;
      if (pushToX < 0 || pushToX >= st.width || pushToY < 0 || pushToY >= st.height) continue;
      const int pushFrom = pushFromY * st.width + pushFromX;
      const int pushTo = pushToY * st.width + pushToX;
      if (isBlocked(st, pushTo) || !reach.seen[pushFrom] || isDeadSquare(st, pushTo)) continue;

      LevelState next = st;
      next.boxes[box] = '0';
      next.boxes[pushTo] = '1';
      next.player = box;
      out.push_back(std::move(next));
    }
  }
  return out;
}

bool isSolvable(const LevelState& initial) {
  std::queue<LevelState> q;
  std::unordered_set<std::string> seen;
  const Reachability initialReach = computeReachability(initial);
  q.push(initial);
  seen.insert(encode(initial, initialReach));

  while (!q.empty()) {
    LevelState current = q.front();
    q.pop();
    if (isSolved(current)) return true;

    const Reachability reach = computeReachability(current);
    for (LevelState& next : successors(current, reach)) {
      const Reachability nextReach = computeReachability(next);
      const std::string key = encode(next, nextReach);
      if (seen.insert(key).second) {
        q.push(std::move(next));
      }
    }
  }
  return false;
}
}  // namespace

int main() {
  const auto suiteStart = std::chrono::steady_clock::now();
  for (int i = 0; i < GameSokobanLevels::LEVEL_COUNT; i++) {
    const LevelState level = parseLevel(GameSokobanLevels::kLevels[i]);
    const auto levelStart = std::chrono::steady_clock::now();
    std::cout << "checking sokoban level " << (i + 1) << std::endl;
    assert(isSolvable(level) && "Built-in Sokoban level must be solvable");
    const auto levelMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - levelStart).count();
    std::cout << "level " << (i + 1) << " ok in " << levelMs << " ms" << std::endl;
  }

  const auto suiteMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - suiteStart).count();
  std::cout << "sokoban level tests passed in " << suiteMs << " ms\n";
  return 0;
}
