#pragma once
// Maze generation, topology search and ball physics, ported from src/MazeLogic.{h,cpp} (SDK 6.3 build).
// Same algorithms and constants. Changes for the cube module: no C++ standard headers (the module poisons
// `double`, which every libstdc++ header names), float-only maths, and no network snapshot/prediction code:
// OctaviOS runs the same game on all 8 modules in lockstep, so nothing is sent between them.
// Include after oct_api.h (TL and sqrtf come from the engine headers).

#include <stdint.h>
#include <stddef.h>

namespace Maze {

// Fixed-size array with the std::array members this file uses.
template <typename T, size_t N> struct Arr {
  T v[N];
  T &operator[](size_t i) { return v[i]; }
  const T &operator[](size_t i) const { return v[i]; }
  void fill(const T &x) { for (size_t i = 0; i < N; ++i) v[i] = x; }
  T *begin() { return v; }
  T *end() { return v + N; }
  const T *begin() const { return v; }
  const T *end() const { return v + N; }
};

template <typename T> inline T Min(T a, T b) { return b < a ? b : a; }
template <typename T> inline T Max(T a, T b) { return a < b ? b : a; }
inline float Abs(float x) { return x < 0.0f ? -x : x; }
inline int CeilPositive(float x) { const int n = static_cast<int>(x); return static_cast<float>(n) < x ? n + 1 : n; }

constexpr uint8_t kTileCount = 24;
constexpr uint8_t kGridSize = 4;
constexpr uint8_t kCellsPerTile = kGridSize * kGridSize;
constexpr uint8_t kOpenPortalsPerTile = 3;
constexpr uint16_t kGlobalCellCount = kTileCount * kCellsPerTile;
constexpr int kDisplaySize = 240;
constexpr int kCellSize = kDisplaySize / kGridSize;
constexpr int kWallThickness = 14;
// On the even 4x4 grid, the portal is centered in a cell (150), not at
// the display midpoint (120), which falls on a wall junction.
constexpr int kPortalCenter = (kGridSize / 2) * kCellSize + kCellSize / 2;
constexpr float kBallRadius = 17.5f;
// Keep the 35 px artwork; allow its rim to overlap walls by 2 px.
constexpr float kBallCollisionRadius = kBallRadius - 2.0f;
constexpr float kWallClearance = kBallCollisionRadius + kWallThickness * 0.5f;
constexpr float kGoalCaptureRadius = 10.0f;
constexpr uint32_t kPortalExitMs = 100;
constexpr uint32_t kPortalHiddenMs = 200;
constexpr uint32_t kPortalEntryMs = 100;
constexpr uint32_t kPortalTransitionMs = kPortalExitMs + kPortalHiddenMs + kPortalEntryMs;

enum class Direction : uint8_t { Top = 0, Right, Bottom, Left, Invalid = 0xFF };
enum class Difficulty : uint8_t { Easy = 0, Hard };
enum class Theme : uint8_t { Classic = 0, Wood, Ancient };
constexpr uint8_t kThemeCount = static_cast<uint8_t>(Theme::Ancient) + 1;
enum class GamePhase : uint8_t { Ready = 0, Playing, Win };

enum Wall : uint8_t {
  WallTop = 1u << 0,
  WallRight = 1u << 1,
  WallBottom = 1u << 2,
  WallLeft = 1u << 3,
  WallAll = WallTop | WallRight | WallBottom | WallLeft,
};

struct CellAddress { uint8_t tile; uint8_t cell; };
struct NeighborLink { uint8_t tile; Direction entry; bool connected; };

using ScreenMaze = Arr<uint8_t, kCellsPerTile>;
using MazeLayout = Arr<ScreenMaze, kTileCount>;
using TopologyMap = Arr<Arr<NeighborLink, 4>, kTileCount>;

struct BallState { uint8_t tile; float x; float y; float velocityX; float velocityY; };

struct TransitionState {
  bool active;
  uint8_t fromTile;
  uint8_t toTile;
  Direction exit;
  Direction entry;
  uint32_t remainingMs;
};

struct GameState {
  GamePhase phase;
  BallState ball;
  CellAddress start;
  CellAddress goal;
  TransitionState transition;
  float transitionSourceX;
  float transitionSourceY;
  uint32_t mazeSeed;
  Difficulty difficulty;
  Theme theme;
  uint8_t themeRound;
  uint16_t nextRoundMs;
  uint32_t moveCount;
  uint32_t elapsedMs;
  uint32_t bestTimeMs;
  uint16_t initialDistance;
  uint16_t currentDistance;
  uint16_t roundId;
};

struct AccelerationSample { int32_t x; int32_t y; int32_t z; };

struct PhysicsResult {
  bool cellChanged;
  bool transitionStarted;
  bool transitionFinished;
  bool hitWall;
  bool won;
};

constexpr uint16_t kUnreachable = 0xFFFFu;
constexpr uint16_t kPreferredInitialDistance = 40;
constexpr uint8_t kGenerationAttempts = 32;
// Tune the perceived weight through tilt response; there is no mass simulation.
constexpr float kGravityAcceleration = 2000.0f;
constexpr float kGravityDeadZone = 0.05f;
constexpr float kDampingPerSecond = 0.9f;
constexpr float kMaximumSpeed = 610.0f;
constexpr float kBounceFactor = 0.20f;
constexpr float kBounceImpactThreshold = 45.0f;
constexpr float kMinimumBounceSpeed = 28.0f;
constexpr float kMaximumBounceSpeed = 65.0f;
constexpr float kMaximumSubstep = 3.0f;

static_assert(kOpenPortalsPerTile > 0 && kOpenPortalsPerTile <= 4,
              "A screen must expose between one and four portals");
static_assert(kCellSize > 2 * kWallClearance,
              "Corridors must fit the ball between the visible walls");

// Breadth-first search buffers, shared (searches never nest); kept off the module's small stack.
TL static Arr<uint16_t, kGlobalCellCount> gDistances;
TL static Arr<uint16_t, kGlobalCellCount> gQueue;

inline uint8_t CellIndex(uint8_t column, uint8_t row) { return static_cast<uint8_t>(row * kGridSize + column); }
inline uint8_t CellColumn(uint8_t cell) { return static_cast<uint8_t>(cell % kGridSize); }
inline uint8_t CellRow(uint8_t cell) { return static_cast<uint8_t>(cell / kGridSize); }
inline bool IsValidDirection(Direction direction) { return static_cast<uint8_t>(direction) < 4; }

inline Direction Opposite(Direction direction) {
  return IsValidDirection(direction)
             ? static_cast<Direction>((static_cast<uint8_t>(direction) + 2u) % 4u)
             : Direction::Invalid;
}

inline uint8_t WallFor(Direction direction) {
  return IsValidDirection(direction) ? static_cast<uint8_t>(1u << static_cast<uint8_t>(direction)) : 0;
}

inline uint8_t PortalCell(Direction direction) {
  switch (direction) {
  case Direction::Top: return CellIndex(kGridSize / 2, 0);
  case Direction::Right: return CellIndex(kGridSize - 1, kGridSize / 2);
  case Direction::Bottom: return CellIndex(kGridSize / 2, kGridSize - 1);
  case Direction::Left: return CellIndex(0, kGridSize / 2);
  default: return 0;
  }
}

inline float ReboundVelocity(float velocity) {
  const float speed = Abs(velocity);
  // Gentle resting contact should settle, not chatter against the wall.
  // Actual impacts receive a small visible rebound with a capped strength.
  if (speed < kBounceImpactThreshold) return 0.0f;
  const float rebound = Min(kMaximumBounceSpeed, Max(kMinimumBounceSpeed, speed * kBounceFactor));
  return velocity > 0.0f ? -rebound : rebound;
}

inline uint32_t NextRandom(uint32_t &state) {
  if (state == 0) state = 0x6D2B79F5u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

inline uint32_t HashSeed(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value == 0 ? 0xA341316Cu : value;
}

inline bool IsValidCell(const CellAddress &address) {
  return address.tile < kTileCount && address.cell < kCellsPerTile;
}

inline bool CellNeighbor(const MazeLayout &layout, const TopologyMap &topology,
                         const CellAddress &from, Direction direction, CellAddress &to) {
  if (!IsValidCell(from) || !IsValidDirection(direction) ||
      (layout[from.tile][from.cell] & WallFor(direction)) != 0) {
    return false;
  }
  int nextColumn = CellColumn(from.cell);
  int nextRow = CellRow(from.cell);
  switch (direction) {
  case Direction::Top: --nextRow; break;
  case Direction::Right: ++nextColumn; break;
  case Direction::Bottom: ++nextRow; break;
  case Direction::Left: --nextColumn; break;
  default: return false;
  }
  if (nextColumn >= 0 && nextColumn < kGridSize && nextRow >= 0 && nextRow < kGridSize) {
    to = {from.tile, CellIndex(static_cast<uint8_t>(nextColumn), static_cast<uint8_t>(nextRow))};
    return (layout[to.tile][to.cell] & WallFor(Opposite(direction))) == 0;
  }
  const NeighborLink &link = topology[from.tile][static_cast<uint8_t>(direction)];
  if (!link.connected || link.tile >= kTileCount || !IsValidDirection(link.entry)) return false;
  to = {link.tile, PortalCell(link.entry)};
  return (layout[to.tile][to.cell] & WallFor(link.entry)) == 0;
}

// Fills gDistances with the step count from origin to every cell.
inline bool BuildDistances(const MazeLayout &layout, const TopologyMap &topology, const CellAddress &origin) {
  gDistances.fill(kUnreachable);
  if (!IsValidCell(origin)) return false;
  uint16_t head = 0;
  uint16_t tail = 0;
  const uint16_t originIndex = static_cast<uint16_t>(origin.tile * kCellsPerTile + origin.cell);
  gDistances[originIndex] = 0;
  gQueue[tail++] = originIndex;
  while (head < tail) {
    const uint16_t packed = gQueue[head++];
    const CellAddress current = {static_cast<uint8_t>(packed / kCellsPerTile),
                                 static_cast<uint8_t>(packed % kCellsPerTile)};
    for (uint8_t rawDirection = 0; rawDirection < 4; ++rawDirection) {
      CellAddress next{};
      if (!CellNeighbor(layout, topology, current, static_cast<Direction>(rawDirection), next)) continue;
      const uint16_t nextIndex = static_cast<uint16_t>(next.tile * kCellsPerTile + next.cell);
      if (gDistances[nextIndex] != kUnreachable) continue;
      gDistances[nextIndex] = static_cast<uint16_t>(gDistances[packed] + 1u);
      gQueue[tail++] = nextIndex;
    }
  }
  return true;
}

inline bool AllCellsReachable(const MazeLayout &layout, const TopologyMap &topology, const CellAddress &origin) {
  if (!BuildDistances(layout, topology, origin)) return false;
  for (uint16_t distance : gDistances) {
    if (distance == kUnreachable) return false;
  }
  return true;
}

inline float ClampFloat(float value, float minimum, float maximum) { return Max(minimum, Min(maximum, value)); }

inline int ClampCellCoordinate(float value) {
  const int coordinate = static_cast<int>(value / kCellSize);
  return Max(0, Min<int>(kGridSize - 1, coordinate));
}

inline CellAddress BallCell(const BallState &ball) {
  return {ball.tile, CellIndex(static_cast<uint8_t>(ClampCellCoordinate(ball.x)),
                               static_cast<uint8_t>(ClampCellCoordinate(ball.y)))};
}

inline void RotateVelocity(Direction from, Direction to, float &x, float &y) {
  if (!IsValidDirection(from) || !IsValidDirection(to)) return;
  int turns = static_cast<int>(to) - static_cast<int>(from);
  turns = (turns % 4 + 4) % 4;
  for (int turn = 0; turn < turns; ++turn) {
    const float previousX = x;
    x = -y;
    y = previousX;
  }
}

inline bool BeginTransition(GameState &state, const MazeLayout &layout, const TopologyMap &topology,
                            Direction exit) {
  if (!IsValidDirection(exit) || state.ball.tile >= kTileCount) return false;
  const NeighborLink &link = topology[state.ball.tile][static_cast<uint8_t>(exit)];
  if (!link.connected || link.tile >= kTileCount || !IsValidDirection(link.entry)) return false;
  // A seam is traversable only when both maze sections expose the same
  // physical opening.  This keeps the ball on the current screen when the
  // neighbouring section has a wall at its incoming portal.
  const uint8_t destinationPortal = PortalCell(link.entry);
  if ((layout[link.tile][destinationPortal] & WallFor(link.entry)) != 0) return false;
  const NeighborLink &returnLink = topology[link.tile][static_cast<uint8_t>(link.entry)];
  if (!returnLink.connected || returnLink.tile != state.ball.tile || returnLink.entry != exit) return false;

  const uint8_t previousTile = state.ball.tile;
  state.transitionSourceX = state.ball.x;
  state.transitionSourceY = state.ball.y;
  state.transition = {true, previousTile, link.tile, exit, link.entry, kPortalTransitionMs};
  state.ball.tile = link.tile;

  const float inset = kBallRadius + 1.0f;
  switch (link.entry) {
  case Direction::Top: state.ball.x = kPortalCenter; state.ball.y = inset; break;
  case Direction::Right: state.ball.x = kDisplaySize - inset; state.ball.y = kPortalCenter; break;
  case Direction::Bottom: state.ball.x = kPortalCenter; state.ball.y = kDisplaySize - inset; break;
  case Direction::Left: state.ball.x = inset; state.ball.y = kPortalCenter; break;
  default: return false;
  }
  RotateVelocity(exit, Opposite(link.entry), state.ball.velocityX, state.ball.velocityY);
  return true;
}

// Wall tops extend half their thickness beyond both ends, just like the drawn walls.
// Include neighbouring cells: the ball's rim can reach a wall before its centre
// enters the cell that owns it.
template <typename Visitor>
inline void VisitWalls(const ScreenMaze &maze, float minX, float minY, float maxX, float maxY, Visitor visit) {
  const float half = kWallThickness * 0.5f;
  const int firstColumn = ClampCellCoordinate(minX - kWallClearance);
  const int lastColumn = ClampCellCoordinate(maxX + kWallClearance);
  const int firstRow = ClampCellCoordinate(minY - kWallClearance);
  const int lastRow = ClampCellCoordinate(maxY + kWallClearance);
  for (int row = firstRow; row <= lastRow; ++row) {
    for (int column = firstColumn; column <= lastColumn; ++column) {
      const uint8_t cell = CellIndex(static_cast<uint8_t>(column), static_cast<uint8_t>(row));
      const float x = static_cast<float>(column * kCellSize);
      const float y = static_cast<float>(row * kCellSize);
      const uint8_t walls = maze[cell];
      if ((walls & WallLeft) && !(column > firstColumn && (maze[cell - 1] & WallRight)))
        visit(x - half, y - half, x + half, y + kCellSize + half);
      if (walls & WallRight) visit(x + kCellSize - half, y - half, x + kCellSize + half, y + kCellSize + half);
      if ((walls & WallTop) && !(row > firstRow && (maze[cell - kGridSize] & WallBottom)))
        visit(x - half, y - half, x + kCellSize + half, y + half);
      if (walls & WallBottom) visit(x - half, y + kCellSize - half, x + kCellSize + half, y + kCellSize + half);
    }
  }
}

inline float LimitWallMovement(const BallState &ball, const MazeLayout &layout, float amount, bool horizontal) {
  const float position = horizontal ? ball.x : ball.y;
  const float across = horizontal ? ball.y : ball.x;
  float target = position + amount;
  VisitWalls(layout[ball.tile], horizontal ? Min(position, target) : ball.x,
             horizontal ? ball.y : Min(position, target),
             horizontal ? Max(position, target) : ball.x,
             horizontal ? ball.y : Max(position, target),
             [&](float left, float top, float right, float bottom) {
               const float low = horizontal ? top : left;
               const float high = horizontal ? bottom : right;
               const float gap = Max(low - across, Max(0.0f, across - high));
               if (gap >= kBallCollisionRadius) return;
               const float wallStart = horizontal ? left : top;
               const float wallEnd = horizontal ? right : bottom;
               if (Max(position, target) < wallStart - kBallCollisionRadius ||
                   Min(position, target) > wallEnd + kBallCollisionRadius) return;
               // Cross-section of the rectangle expanded by a circle: flat sides and
               // rounded corners, so clear space around a corner remains traversable.
               const float reach = gap == 0.0f ? kBallCollisionRadius
                                               : sqrtf(kBallCollisionRadius * kBallCollisionRadius - gap * gap);
               const float nearEdge = wallStart - reach;
               const float farEdge = wallEnd + reach;
               if (amount > 0 && position <= wallStart && target > nearEdge) target = nearEdge;
               if (amount < 0 && position >= wallEnd && target < farEdge) target = farEdge;
             });
  return target;
}

inline bool MoveAxis(GameState &state, const MazeLayout &layout, const TopologyMap &topology, float amount,
                     bool horizontal, PhysicsResult &result) {
  if (amount == 0.0f) return false;
  BallState &ball = state.ball;
  float &pos = horizontal ? ball.x : ball.y;
  float &vel = horizontal ? ball.velocityX : ball.velocityY;
  const float target = LimitWallMovement(ball, layout, amount, horizontal);
  if (target != pos + amount) {
    pos = target;
    vel = ReboundVelocity(vel);
    result.hitWall = true;
    return true;
  }
  const int column = ClampCellCoordinate(ball.x);
  const int row = ClampCellCoordinate(ball.y);
  const int index = horizontal ? column : row;
  const uint8_t cell = CellIndex(static_cast<uint8_t>(column), static_cast<uint8_t>(row));
  const Direction direction = horizontal ? (amount > 0.0f ? Direction::Right : Direction::Left)
                                         : (amount > 0.0f ? Direction::Bottom : Direction::Top);
  const float boundary = static_cast<float>(amount > 0.0f ? (index + 1) * kCellSize : index * kCellSize);
  const bool hasWall = (layout[ball.tile][cell] & WallFor(direction)) != 0;

  if (hasWall) {
    const float limit = amount > 0.0f ? boundary - kWallClearance : boundary + kWallClearance;
    if ((amount > 0.0f && pos + amount > limit) || (amount < 0.0f && pos + amount < limit)) {
      pos = limit;
      vel = ReboundVelocity(vel);
      result.hitWall = true;
      return true;
    }
  }
  if (!hasWall && ((amount > 0.0f && index == kGridSize - 1 && pos + amount >= kDisplaySize - kBallRadius) ||
                   (amount < 0.0f && index == 0 && pos + amount <= kBallRadius))) {
    if (BeginTransition(state, layout, topology, direction)) {
      result.transitionStarted = true;
      result.cellChanged = true;
      return true;
    }
    pos = amount > 0.0f ? kDisplaySize - kBallRadius : kBallRadius;
    vel = ReboundVelocity(vel);
    result.hitWall = true;
    return true;
  }
  pos += amount;
  return false;
}

inline void GenerateLayout(uint32_t seed, MazeLayout &layout, Difficulty difficulty) {
  for (uint8_t tile = 0; tile < kTileCount; ++tile) {
    ScreenMaze &maze = layout[tile];
    maze.fill(WallAll);
    Arr<bool, kCellsPerTile> visited{};
    Arr<uint8_t, kCellsPerTile> stack{};
    uint8_t stackSize = 1;
    uint32_t randomState = HashSeed(seed ^ (0x9E3779B9u * (tile + 1u)));
    stack[0] = static_cast<uint8_t>(NextRandom(randomState) % kCellsPerTile);
    visited[stack[0]] = true;

    while (stackSize > 0) {
      const uint8_t current = stack[stackSize - 1];
      const int column = CellColumn(current);
      const int row = CellRow(current);
      Arr<Direction, 4> candidates{};
      uint8_t candidateCount = 0;
      if (row > 0 && !visited[CellIndex(column, row - 1)]) candidates[candidateCount++] = Direction::Top;
      if (column + 1 < kGridSize && !visited[CellIndex(column + 1, row)]) candidates[candidateCount++] = Direction::Right;
      if (row + 1 < kGridSize && !visited[CellIndex(column, row + 1)]) candidates[candidateCount++] = Direction::Bottom;
      if (column > 0 && !visited[CellIndex(column - 1, row)]) candidates[candidateCount++] = Direction::Left;
      if (candidateCount == 0) {
        --stackSize;
        continue;
      }
      const Direction direction = candidates[NextRandom(randomState) % candidateCount];
      int nextColumn = column;
      int nextRow = row;
      if (direction == Direction::Top) --nextRow;
      else if (direction == Direction::Right) ++nextColumn;
      else if (direction == Direction::Bottom) ++nextRow;
      else --nextColumn;
      const uint8_t next = CellIndex(static_cast<uint8_t>(nextColumn), static_cast<uint8_t>(nextRow));
      maze[current] &= static_cast<uint8_t>(~WallFor(direction));
      maze[next] &= static_cast<uint8_t>(~WallFor(Opposite(direction)));
      visited[next] = true;
      stack[stackSize++] = next;
    }

    // Expose only a limited number of entrances.  The missing side makes the
    // current cube topology matter and encourages a twist when the useful
    // neighbouring screen is aligned with it.
    const uint8_t firstOpenPortal = static_cast<uint8_t>(NextRandom(randomState) % 4u);
    const uint8_t openPortals = difficulty == Difficulty::Easy ? 4 : kOpenPortalsPerTile;
    for (uint8_t offset = 0; offset < openPortals; ++offset) {
      const Direction direction = static_cast<Direction>((firstOpenPortal + offset) % 4u);
      maze[PortalCell(direction)] &= static_cast<uint8_t>(~WallFor(direction));
    }
  }
}

inline bool FindFarthestCell(const MazeLayout &layout, const TopologyMap &topology, const CellAddress &goal,
                             CellAddress &farthest, uint16_t &distance) {
  if (!BuildDistances(layout, topology, goal)) return false;
  distance = 0;
  farthest = goal;
  for (uint16_t index = 0; index < kGlobalCellCount; ++index) {
    if (gDistances[index] != kUnreachable && gDistances[index] > distance) {
      distance = gDistances[index];
      farthest = {static_cast<uint8_t>(index / kCellsPerTile), static_cast<uint8_t>(index % kCellsPerTile)};
    }
  }
  return distance > 0;
}

inline uint16_t ShortestDistance(const MazeLayout &layout, const TopologyMap &topology, const CellAddress &from,
                                 const CellAddress &to) {
  if (!IsValidCell(from) || !IsValidCell(to)) return kUnreachable;
  if (!BuildDistances(layout, topology, to)) return kUnreachable;
  return gDistances[static_cast<uint16_t>(from.tile * kCellsPerTile + from.cell)];
}

inline bool GenerateRound(uint32_t baseSeed, const TopologyMap &topology, MazeLayout &layout, CellAddress &start,
                          CellAddress &goal, uint16_t &initialDistance, uint32_t &selectedSeed,
                          Difficulty difficulty) {
  bool hasBest = false;
  uint16_t bestDistance = 0;
  MazeLayout candidateLayout{};
  for (uint8_t attempt = 0; attempt < kGenerationAttempts; ++attempt) {
    const uint32_t candidateSeed = HashSeed(baseSeed + 0x9E3779B9u * static_cast<uint32_t>(attempt + 1));
    GenerateLayout(candidateSeed, candidateLayout, difficulty);
    uint32_t randomState = HashSeed(candidateSeed ^ 0x51ED270Bu);
    const CellAddress candidateGoal = {
        static_cast<uint8_t>(NextRandom(randomState) % kTileCount),
        CellIndex(static_cast<uint8_t>(1u + NextRandom(randomState) % 3u),
                  static_cast<uint8_t>(1u + NextRandom(randomState) % 3u))};
    if (!AllCellsReachable(candidateLayout, topology, candidateGoal)) continue;
    CellAddress candidateStart{};
    uint16_t candidateDistance = 0;
    if (!FindFarthestCell(candidateLayout, topology, candidateGoal, candidateStart, candidateDistance)) continue;
    if (!hasBest || candidateDistance > bestDistance) {
      hasBest = true;
      bestDistance = candidateDistance;
      layout = candidateLayout;
      start = candidateStart;
      goal = candidateGoal;
      selectedSeed = candidateSeed;
    }
    if (candidateDistance >= kPreferredInitialDistance) {
      initialDistance = candidateDistance;
      return true;
    }
  }
  initialDistance = bestDistance;
  return hasBest;
}

inline float CellCenter(uint8_t index) { return (static_cast<float>(index) + 0.5f) * kCellSize; }

inline void PrepareRound(GameState &state, uint32_t seed, const CellAddress &start, const CellAddress &goal,
                         uint16_t initialDistance, uint16_t roundId, Difficulty difficulty) {
  state.phase = GamePhase::Playing;
  state.start = start;
  state.goal = goal;
  state.ball = {start.tile, CellCenter(CellColumn(start.cell)), CellCenter(CellRow(start.cell)), 0.0f, 0.0f};
  state.transition = {false, start.tile, start.tile, Direction::Invalid, Direction::Invalid, 0};
  state.mazeSeed = seed;
  state.transitionSourceX = state.ball.x;
  state.transitionSourceY = state.ball.y;
  state.difficulty = difficulty;
  state.moveCount = 0;
  state.elapsedMs = 0;
  state.initialDistance = initialDistance;
  state.currentDistance = initialDistance;
  state.roundId = roundId;
}

inline void RecordTwist(GameState &state) {
  if (state.phase == GamePhase::Playing) ++state.moveCount;
}

inline void StopBall(GameState &state) {
  state.ball.velocityX = 0.0f;
  state.ball.velocityY = 0.0f;
}

inline bool IsAtGoal(const GameState &state) {
  if (state.ball.tile != state.goal.tile || state.goal.cell >= kCellsPerTile) return false;
  const float deltaX = state.ball.x - CellCenter(CellColumn(state.goal.cell));
  const float deltaY = state.ball.y - CellCenter(CellRow(state.goal.cell));
  return deltaX * deltaX + deltaY * deltaY <= kGoalCaptureRadius * kGoalCaptureRadius;
}

inline void RefreshDistance(GameState &state, const MazeLayout &layout, const TopologyMap &topology) {
  const uint16_t distance = ShortestDistance(layout, topology, BallCell(state.ball), state.goal);
  if (distance != kUnreachable) state.currentDistance = distance;
}

inline PhysicsResult StepPhysics(GameState &state, const MazeLayout &layout, const TopologyMap &topology,
                                 const AccelerationSample &acceleration, uint32_t deltaMs) {
  PhysicsResult result{false, false, false, false, false};
  if (state.phase != GamePhase::Playing || deltaMs == 0) return result;

  if (state.transition.active) {
    if (deltaMs < state.transition.remainingMs) {
      state.transition.remainingMs -= deltaMs;
      return result;
    }
    state.transition.active = false;
    state.transition.remainingMs = 0;
    result.transitionFinished = true;
    if (IsAtGoal(state)) {
      state.phase = GamePhase::Win;
      StopBall(state);
      result.won = true;
    }
    return result;
  }

  deltaMs = Min<uint32_t>(deltaMs, 50u);
  const float dt = static_cast<float>(deltaMs) / 1000.0f;
  const float ax = static_cast<float>(acceleration.x);
  const float ay = static_cast<float>(acceleration.y);
  const float az = static_cast<float>(acceleration.z);
  const float magnitudeSquared = ax * ax + ay * ay + az * az;
  float gravityX = 0.0f;
  float gravityY = 0.0f;
  if (magnitudeSquared > 1.0f) {
    const float inverseMagnitude = 1.0f / sqrtf(magnitudeSquared);
    gravityX = ax * inverseMagnitude;
    gravityY = ay * inverseMagnitude;
    if (gravityX * gravityX + gravityY * gravityY < kGravityDeadZone * kGravityDeadZone) {
      gravityX = 0.0f;
      gravityY = 0.0f;
    }
  }

  state.ball.velocityX += gravityX * kGravityAcceleration * dt;
  state.ball.velocityY += gravityY * kGravityAcceleration * dt;
  const float damping = 1.0f / (1.0f + kDampingPerSecond * dt);
  state.ball.velocityX *= damping;
  state.ball.velocityY *= damping;
  const float speedSquared = state.ball.velocityX * state.ball.velocityX + state.ball.velocityY * state.ball.velocityY;
  if (speedSquared > kMaximumSpeed * kMaximumSpeed) {
    const float scale = kMaximumSpeed / sqrtf(speedSquared);
    state.ball.velocityX *= scale;
    state.ball.velocityY *= scale;
  }

  const CellAddress cellBefore = BallCell(state.ball);
  const float totalX = state.ball.velocityX * dt;
  const float totalY = state.ball.velocityY * dt;
  const uint8_t substeps =
      static_cast<uint8_t>(Max<int>(1, CeilPositive(Max(Abs(totalX), Abs(totalY)) / kMaximumSubstep)));
  const float stepX = totalX / substeps;
  const float stepY = totalY / substeps;

  bool horizontalBlocked = false;
  bool verticalBlocked = false;
  for (uint8_t step = 0; step < substeps; ++step) {
    if (!horizontalBlocked && MoveAxis(state, layout, topology, stepX, true, result)) {
      if (state.transition.active) break;
      horizontalBlocked = true;
    }
    if (!verticalBlocked && MoveAxis(state, layout, topology, stepY, false, result)) {
      if (state.transition.active) break;
      verticalBlocked = true;
    }
    if (IsAtGoal(state)) {
      state.phase = GamePhase::Win;
      StopBall(state);
      result.won = true;
      break;
    }
  }

  const CellAddress cellAfter = BallCell(state.ball);
  result.cellChanged = result.cellChanged || cellAfter.tile != cellBefore.tile || cellAfter.cell != cellBefore.cell;
  if (result.cellChanged) RefreshDistance(state, layout, topology);
  return result;
}

} // namespace Maze
