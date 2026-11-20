#pragma once
// Maze Cube for OctaviOS. Same game as the SDK 6.3 build in ../src: a ball rolls through a 4x4 maze on each of the
// 24 screens; twisting the cube changes which screens' portals line up.
//
// How it maps onto OctaviOS:
//  - Every copy of the game (one per module, 8 in lockstep) runs the full simulation; there is no network code.
//  - Each maze screen ("tile") is a set of twistable sprites: a floor, its walls and the goal. Twists carry them,
//    so the tile's floor sprite is its frame of reference: tile-local (x right, y down, 0..240) <-> plane space.
//  - After every twist the portal topology is recomputed from where the floors ended up.
#include "oct_api.h"
#include "app_maze_cube_ids.h"
#include "maze_logic.h"

#ifndef MZ_DEBUG_AUTOPLAY
#define MZ_DEBUG_AUTOPLAY 0 // 1 = starts rounds and finishes them by itself (hidden-simulator checks only)
#endif
#ifndef MZ_MUTE
#define MZ_MUTE 0           // 1 = no sound
#endif

#define SPRITES_CAP 1400
#define GAP 18
#define QUAD_FILL 119.5f    // screen-filling art centre (pivot of an even-sized sprite is its middle pixel)

static const float SCREEN_STEP = 240.0f + 2.0f * GAP; // centre-to-centre distance of neighbouring screens
static const int32_t SETTLE_TICKS = 10;               // ball rests 0.5 s after a twist, as on SDK 6.3 (520 ms)
static const int32_t PRETWIST_FREEZE_TICKS = 40;      // bound: a twist may be abandoned without on_twisted
static const uint32_t DOUBLE_TAP_WAIT_TICKS = 11;     // a series' taps are <= 536 ms apart
static const uint32_t RESULTS_MS = 3000;

enum { L_BG = 1, L_FLOOR = 2, L_GOAL = 3, L_WALL = 4, L_BALL = 6, L_UI = 8, L_TEXT = 9 };
enum Scene : uint8_t { SCENE_MENU, SCENE_MAZE, SCENE_RESULTS };

struct appObject_t : octSprite_t {};

struct appvars_t {
    uint32_t tick;
    Maze::GameState st;
    Maze::MazeLayout layout;
    Maze::TopologyMap topo;
    appObject_t* floor[Maze::kTileCount];
    appObject_t* ball;
    appObject_t* countdown[SIDES_COUNT];
    Scene scene;
    uint32_t sceneTick;
    int32_t freezeTicks;
    uint32_t singleTapAt;          // tick a deferred single tap applies at; 0 = none
    int32_t menuTop;               // top face captured on menu entry
    int32_t up[SIDES_COUNT];       // upright angle per face for the current menu/results layout
    uint32_t latestMs[2];
    uint32_t bestMs[2];
    bool saveValid;
};

TL static appObject_t gObjects[SPRITES_CAP];
TL static appvars_t vars;

static const int32_t FLOOR_BMP[Maze::kThemeCount] = {BMP_floor_classic, BMP_floor_wood, BMP_floor_ancient};
static const int32_t WALL_BMP[Maze::kThemeCount][3] = {
    {BMP_wall_c1, BMP_wall_c2, BMP_wall_c3}, {BMP_wall_w1, BMP_wall_w2, BMP_wall_w3}, {BMP_wall_a1, BMP_wall_a2, BMP_wall_a3}};
static const int32_t BALL_BMP[Maze::kThemeCount] = {BMP_ball_classic, BMP_ball_metal, BMP_ball_metal};
static const int32_t GOAL_BMP[Maze::kThemeCount] = {BMP_goal_classic, BMP_goal_wood, BMP_goal_ancient};
static const int32_t BOARD_BMP[Maze::kThemeCount] = {BMP_board_classic, BMP_board_wood, BMP_board_ancient};
static const char* const THEME_NAME[Maze::kThemeCount] = {"CLASSIC", "WOOD", "ANCIENT"};
static const int32_t OPPOSITE_PLANE[SIDES_COUNT] = {5, 3, 4, 1, 2, 0};


////////////////////////////////
//          HELPERS           //
////////////////////////////////

static inline appObject_t* spriteAt(int32_t id) { return (id > 0 && id < (int32_t)SPRITES_CAP) ? &gObjects[id] : NULL; }

// Deletes every top-level sprite (children, label glyphs and text slots go with their parents).
static inline void clearScene() {
    for (size_t i = 1; i < SPRITES_CAP; i++) {
        appObject_t* obj = &gObjects[i];
        if ((size_t)obj->Idx != i || obj->Parent != 0) continue;
        OCT_del(obj);
    }
    vars.ball = NULL;
    for (size_t t = 0; t < Maze::kTileCount; t++) vars.floor[t] = NULL;
    for (size_t p = 0; p < SIDES_COUNT; p++) vars.countdown[p] = NULL;
}

static inline int32_t snapAngle(int32_t a) {
    a = ((a % 360) + 360) % 360;
    return ((a + 45) / 90 * 90) % 360;
}
static inline float cos90(int32_t a) { return a == 0 ? 1.0f : (a == 180 ? -1.0f : 0.0f); }
static inline float sin90(int32_t a) { return a == 90 ? 1.0f : (a == 270 ? -1.0f : 0.0f); }

// Player-view offset (ux right, uy up) to plane x,y for content whose up angle is a (template's uprightOffset).
static inline void uprightOffset(int32_t a, float ux, float uy, float* x, float* y) {
    switch (a) {
        case 0:   *x = ux;  *y = uy;  break;
        case 90:  *x = -uy; *y = ux;  break;
        case 180: *x = -ux; *y = -uy; break;
        default:  *x = uy;  *y = -ux; break;
    }
}

static inline void formatTime(uint32_t ms, char* text, size_t size) {
    if (ms == 0) { snprintf(text, size, "--"); return; }
    snprintf(text, size, "%d:%02d.%02d", (int)(ms / 60000u), (int)((ms / 1000u) % 60u), (int)((ms / 10u) % 100u));
}

static inline void playSound(int32_t id, int32_t volume) {
#if MZ_MUTE
    (void)id; (void)volume;
#else
    if (id > 0) SND_play(id, volume);
#endif
}


////////////////////////////////
//           SAVES            //
////////////////////////////////

// Records are display-only (they never steer the game), so each module reads its own card.
static inline void saveLoad() {
    saveRecord_t rec;
    int32_t got = (int32_t)OCT_save_read(&rec, 0U, (uint32_t)sizeof(rec));
    bool mine = got == (int32_t)sizeof(rec) && rec.header.magic == SAVE_MAGIC;
    bool current = mine && rec.header.version == SAVE_VERSION && rec.header.size == sizeof(rec);
    vars.saveValid = got >= 0 && (!mine || current); // never write over a layout this build does not know
    if (current) {
        for (int m = 0; m < 2; m++) {
            if (rec.latestMs[m] > 0 && rec.bestMs[m] > 0 && rec.bestMs[m] <= rec.latestMs[m]) {
                vars.latestMs[m] = rec.latestMs[m];
                vars.bestMs[m] = rec.bestMs[m];
            }
        }
    }
}

static inline void saveResult() {
    Maze::GameState& st = vars.st;
    if (st.elapsedMs == 0) st.elapsedMs = 1; // zero means "no record"
    const int m = (int)st.difficulty;
    vars.latestMs[m] = st.elapsedMs;
    if (vars.bestMs[m] == 0 || st.elapsedMs < vars.bestMs[m]) vars.bestMs[m] = st.elapsedMs;
    st.bestTimeMs = vars.bestMs[m];
    if (!vars.saveValid) return;
    saveRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.header.magic = SAVE_MAGIC;
    rec.header.version = SAVE_VERSION;
    rec.header.size = (uint16_t)sizeof(rec);
    for (int i = 0; i < 2; i++) { rec.latestMs[i] = vars.latestMs[i]; rec.bestMs[i] = vars.bestMs[i]; }
    OCT_save_write(&rec, 0U, (uint32_t)sizeof(rec));
}


////////////////////////////////
//       TILES ON SCREENS     //
////////////////////////////////

// Tile-local point (x right, y down, 0..240) -> raw plane coordinates of the screen the tile is on now.
// The tile's right = (cos A, sin A) and its down = (sin A, -cos A), A = its floor sprite's angle.
static inline void tileToPlane(uint8_t tile, float lx, float ly, float* x, float* y, int32_t* plane, int32_t* angle) {
    const octTm_t& c = vars.floor[tile]->Tm;
    int32_t a = snapAngle(c.A);
    float cs = cos90(a), sn = sin90(a), u = lx - 120.0f, v = ly - 120.0f;
    *x = c.X + u * cs + v * sn;
    *y = c.Y + u * sn - v * cs;
    *plane = c.Plane;
    *angle = a;
}

// Scene quad next to the tile's screen in tile direction d (Top, Right, Bottom, Left).
static inline int32_t neighborQuad(uint8_t tile, uint8_t d) {
    octTm_t tm = vars.floor[tile]->Tm;
    int32_t a = snapAngle(tm.A + 90 - 90 * (int32_t)d);
    tm.X += SCREEN_STEP * cos90(a);
    tm.Y += SCREEN_STEP * sin90(a);
    OCT_TM_wrap(&tm);
    return OCT_TM_quad(&tm);
}

// Rebuilds the portal links from the floors' current screens; keeps the old map if anything is inconsistent.
static inline bool refreshTopology() {
    int8_t tileAt[VIEWPORTS_COUNT];
    int32_t quadOf[Maze::kTileCount];
    int32_t nq[Maze::kTileCount][4];
    for (int q = 0; q < VIEWPORTS_COUNT; q++) tileAt[q] = -1;
    for (uint8_t t = 0; t < Maze::kTileCount; t++) {
        if (vars.floor[t] == NULL) return false;
        int32_t q = OCT_TM_quad(&vars.floor[t]->Tm);
        if (q < 0 || q >= VIEWPORTS_COUNT || tileAt[q] >= 0) return false;
        tileAt[q] = (int8_t)t;
        quadOf[t] = q;
    }
    for (uint8_t t = 0; t < Maze::kTileCount; t++)
        for (uint8_t d = 0; d < 4; d++) nq[t][d] = neighborQuad(t, d);
    Maze::TopologyMap map;
    for (uint8_t t = 0; t < Maze::kTileCount; t++) {
        for (uint8_t d = 0; d < 4; d++) {
            int32_t q = nq[t][d];
            if (q < 0 || q >= VIEWPORTS_COUNT || tileAt[q] < 0) return false;
            uint8_t u = (uint8_t)tileAt[q];
            int32_t entry = -1;
            for (uint8_t e = 0; e < 4; e++) if (nq[u][e] == quadOf[t]) entry = e;
            if (entry < 0) return false;
            map[t][d] = {u, (Maze::Direction)entry, true};
        }
    }
    vars.topo = map;
    return true;
}

// The synced gravity on the ball's screen, in its tile's axes (x right, y down, z = face normal), x1000.
static inline Maze::AccelerationSample tileGravity(uint8_t tile) {
    const octTm_t& c = vars.floor[tile]->Tm;
    int32_t a = snapAngle(c.A);
    float cs = cos90(a), sn = sin90(a);
    float gx = OCT_TM_gravity_x(c.Plane), gy = OCT_TM_gravity_y(c.Plane), gn = OCT_TM_gravity_n(c.Plane);
    return {(int32_t)((gx * cs + gy * sn) * 1000.0f), (int32_t)((gx * sn - gy * cs) * 1000.0f), (int32_t)(gn * 1000.0f)};
}

static inline appObject_t* addOnTile(int32_t layer, uint8_t tile, float lx, float ly, int32_t extraAngle, int32_t bmp) {
    float x, y;
    int32_t plane, a;
    tileToPlane(tile, lx, ly, &x, &y, &plane, &a);
    return spriteAt(OCT_add(layer, true, OCT_PLANE_WITH_BORDER + plane, x, y, snapAngle(a + extraAngle), false, bmp, bmp, 0));
}

// Walls of one tile as runs of up to three cells (one sprite each), like the SDK 6.3 renderer's runs.
static inline void addWalls(uint8_t tile) {
    const Maze::ScreenMaze& maze = vars.layout[tile];
    const int32_t* bmps = WALL_BMP[(int)vars.st.theme];
    for (int vertical = 0; vertical < 2; vertical++) {
        for (int edge = 0; edge <= Maze::kGridSize; edge++) {
            int runStart = -1;
            for (int cell = 0; cell <= Maze::kGridSize; cell++) {
                bool wall = false;
                if (cell < Maze::kGridSize) {
                    int inner = edge < Maze::kGridSize ? edge : Maze::kGridSize - 1;
                    uint8_t index = Maze::CellIndex((uint8_t)(vertical ? inner : cell), (uint8_t)(vertical ? cell : inner));
                    uint8_t mask = vertical ? (edge == Maze::kGridSize ? Maze::WallRight : Maze::WallLeft)
                                            : (edge == Maze::kGridSize ? Maze::WallBottom : Maze::WallTop);
                    wall = (maze[index] & mask) != 0;
                }
                if (wall && runStart < 0) { runStart = cell; continue; }
                if (wall || runStart < 0) continue;
                for (int s = runStart; s < cell;) {
                    int n = cell - s < 3 ? cell - s : 3;
                    float along = (float)(s * Maze::kCellSize + n * Maze::kCellSize / 2);
                    float across = (float)(edge * Maze::kCellSize);
                    addOnTile(L_WALL, tile, vertical ? across : along, vertical ? along : across, vertical ? 90 : 0, bmps[n - 1]);
                    s += n;
                }
                runStart = -1;
            }
        }
    }
}

// Ball sprite from the game state, including the portal animation (exit 100 ms, hidden 200 ms, entry 100 ms).
static inline void updateBall() {
    const Maze::GameState& st = vars.st;
    appObject_t* b = vars.ball;
    if (b == NULL) return;
    uint8_t tile = st.ball.tile;
    float x = st.ball.x, y = st.ball.y;
    if (st.transition.active) {
        const uint32_t remaining = st.transition.remainingMs < Maze::kPortalTransitionMs ? st.transition.remainingMs : Maze::kPortalTransitionMs;
        const uint32_t elapsed = Maze::kPortalTransitionMs - remaining;
        const uint32_t entryStart = Maze::kPortalExitMs + Maze::kPortalHiddenMs;
        if (elapsed >= Maze::kPortalExitMs && elapsed < entryStart) { b->Hidden = 1; return; }
        const bool leaving = elapsed < Maze::kPortalExitMs;
        const float phase = leaving ? (float)elapsed / (float)Maze::kPortalExitMs
                                    : (float)(elapsed - entryStart) / (float)Maze::kPortalEntryMs;
        const float eased = phase * phase * (3.0f - 2.0f * phase);
        const Maze::Direction dir = leaving ? st.transition.exit : st.transition.entry;
        tile = leaving ? st.transition.fromTile : st.transition.toTile;
        float sx = leaving ? st.transitionSourceX : st.ball.x, sy = leaving ? st.transitionSourceY : st.ball.y;
        float ox = sx, oy = sy;
        const float out = Maze::kBallRadius + 1.0f;
        switch (dir) {
            case Maze::Direction::Top: oy = -out; break;
            case Maze::Direction::Right: ox = Maze::kDisplaySize + out; break;
            case Maze::Direction::Bottom: oy = Maze::kDisplaySize + out; break;
            default: ox = -out; break;
        }
        const float travel = leaving ? eased : 1.0f - eased;
        x = sx + (ox - sx) * travel;
        y = sy + (oy - sy) * travel;
    }
    float px, py;
    int32_t plane, a;
    tileToPlane(tile, x, y, &px, &py, &plane, &a);
    OCT_TM_set(&b->Tm, px, py, 0, plane);
    b->Hidden = 0;
}


////////////////////////////////
//        MENU / RESULTS      //
////////////////////////////////

// Panel slot k on face p: 0 upper left, 1 upper right, 2 lower left, 3 lower right (as the player sees it).
static inline void panelPos(int32_t p, int32_t k, float oy, float* x, float* y) {
    uprightOffset(vars.up[p], k % 2 ? 120.0f : -120.0f, (k < 2 ? 120.0f : -120.0f) + oy, x, y);
}

static inline appObject_t* panelText(int32_t p, int32_t k, float oy, int32_t font, const char* text) {
    float x, y;
    panelPos(p, k, oy, &x, &y);
    appObject_t* l = spriteAt(OCT_add_label(L_TEXT, false, p, x, y, vars.up[p], font, ALIGN_CENTER));
    if (l != NULL) OCT_label_set(l, text);
    return l;
}

static inline void panelImage(int32_t p, int32_t k, float oy, int32_t from, int32_t to, bool loop, int32_t framelen) {
    float x, y;
    panelPos(p, k, oy, &x, &y);
    OCT_add(L_UI, false, p, x, y, vars.up[p], loop, from, to, framelen);
}

static inline void addBackgrounds() {
    for (int32_t p = 0; p < SIDES_COUNT; p++)
        for (int32_t q = 0; q < SECTORS_COUNT; q++)
            OCT_add(L_BG, false, p, (float)XSIGN[q] * QUAD_FILL, (float)YSIGN[q] * QUAD_FILL, 0, false, BMP_menu_bg, BMP_menu_bg, 0);
}

static inline void captureUp() {
    for (int32_t p = 0; p < SIDES_COUNT; p++) vars.up[p] = snapAngle(OCT_TM_face_up(p));
}

// Controls on the top and bottom faces, records on one pair of opposite side faces, information on the other.
static inline void showMenu(bool recapture) {
    if (recapture) {
        vars.menuTop = OCT_TM_top_side();
        if (vars.menuTop < 0 || vars.menuTop >= SIDES_COUNT) vars.menuTop = OCT_PLANE_TOP;
        captureUp();
    }
    clearScene();
    vars.scene = SCENE_MENU;
    vars.sceneTick = vars.tick;
    addBackgrounds();
    const int32_t top = vars.menuTop, bottom = OPPOSITE_PLANE[top];
    int32_t recordsA = -1;
    for (int32_t p = 0; p < SIDES_COUNT && recordsA < 0; p++) if (p != top && p != bottom) recordsA = p;
    const int theme = (int)vars.st.theme;
    char a[24], b[24];
    for (int32_t p = 0; p < SIDES_COUNT; p++) {
        if (p == top || p == bottom) {
            panelImage(p, 0, 40.0f, BMP_icon_pat, BMP_icon_pat, false, 0);
            panelText(p, 0, -45.0f, FONT_3, "PAT");
            panelText(p, 0, -80.0f, FONT_3, vars.st.difficulty == Maze::Difficulty::Easy ? "EASY" : "HARD");
            panelImage(p, 1, 40.0f, BMP_icon_pat, BMP_icon_pat, false, 0);
            panelText(p, 1, -45.0f, FONT_3, "DOUBLE PAT");
            panelText(p, 1, -80.0f, FONT_3, THEME_NAME[theme]);
            panelImage(p, 2, 40.0f, BMP_twist_00, BMP_twist_01, true, 9); // two frames, 500 ms each
            panelText(p, 2, -45.0f, FONT_3, "TWIST");
            panelText(p, 2, -80.0f, FONT_3, "TO START");
            panelImage(p, 3, 0.0f, BOARD_BMP[theme], BOARD_BMP[theme], false, 0);
        } else if (p == recordsA || p == OPPOSITE_PLANE[recordsA]) {
            for (int32_t k = 0; k < 4; k++) {
                const int m = k % 2;
                formatTime(k < 2 ? vars.bestMs[m] : vars.latestMs[m], a, sizeof(a));
                panelText(p, k, 25.0f, FONT_2, a);
                panelText(p, k, -25.0f, FONT_3, k < 2 ? "BEST" : "LAST");
                panelText(p, k, -60.0f, FONT_3, m == 0 ? "EASY" : "HARD");
            }
        } else {
            panelImage(p, 0, 0.0f, BMP_qr_code, BMP_qr_code, false, 0);
            panelImage(p, 1, 0.0f, BMP_logo, BMP_logo, false, 0);
            snprintf(b, sizeof(b), "%d.%d.%d", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);
            panelText(p, 2, 20.0f, FONT_2, b);
            panelText(p, 2, -30.0f, FONT_3, "VERSION");
        }
    }
}

static inline Maze::Difficulty nextDifficulty() {
    return vars.st.themeRound == Maze::kThemeCount - 1 ? Maze::Difficulty::Hard : vars.st.difficulty;
}

static inline void setCountdownText() {
    char text[8];
    snprintf(text, sizeof(text), "%d", (int)((vars.st.nextRoundMs + 999u) / 1000u > 0 ? (vars.st.nextRoundMs + 999u) / 1000u : 1u));
    for (int32_t p = 0; p < SIDES_COUNT; p++) if (vars.countdown[p] != NULL) OCT_label_set(vars.countdown[p], text);
}

// RESULT, BEST, the countdown to the next round and TWISTS on every face, upright for the current pose.
static inline void showResults() {
    captureUp();
    clearScene();
    vars.scene = SCENE_RESULTS;
    vars.sceneTick = vars.tick;
    addBackgrounds();
    char time[24], best[24], twists[16], next[24];
    formatTime(vars.st.elapsedMs, time, sizeof(time));
    formatTime(vars.st.bestTimeMs, best, sizeof(best));
    snprintf(twists, sizeof(twists), "%d", (int)vars.st.moveCount);
    snprintf(next, sizeof(next), "NEXT: %s", nextDifficulty() == Maze::Difficulty::Easy ? "EASY" : "HARD");
    const char* nextTheme = THEME_NAME[((int)vars.st.theme + 1) % Maze::kThemeCount];
    for (int32_t p = 0; p < SIDES_COUNT; p++) {
        panelText(p, 0, 20.0f, FONT_2, time);
        panelText(p, 0, -30.0f, FONT_3, "RESULT");
        panelText(p, 1, 20.0f, FONT_2, best);
        panelText(p, 1, -30.0f, FONT_3, "BEST");
        vars.countdown[p] = panelText(p, 2, 45.0f, FONT_1, "3");
        panelText(p, 2, -25.0f, FONT_3, next);
        panelText(p, 2, -60.0f, FONT_3, nextTheme);
        panelText(p, 3, 20.0f, FONT_2, twists);
        panelText(p, 3, -30.0f, FONT_3, "TWISTS");
    }
    setCountdownText();
}


////////////////////////////////
//           ROUNDS           //
////////////////////////////////

static inline void startRound(bool advancing) {
    Maze::GameState& st = vars.st;
    const Maze::Difficulty difficulty = advancing ? nextDifficulty() : st.difficulty;
    const Maze::Theme theme = advancing ? (Maze::Theme)(((int)st.theme + 1) % Maze::kThemeCount) : st.theme;
    clearScene();
    vars.singleTapAt = 0;
    for (uint8_t t = 0; t < Maze::kTileCount; t++) {
        const int q = t % SECTORS_COUNT;
        vars.floor[t] = spriteAt(OCT_add(L_FLOOR, true, t / SECTORS_COUNT, (float)XSIGN[q] * QUAD_FILL, (float)YSIGN[q] * QUAD_FILL,
                                         0, false, FLOOR_BMP[(int)theme], FLOOR_BMP[(int)theme], 0));
        if (vars.floor[t] == NULL) { showMenu(true); return; }
    }
    // The same synced random draws on all 8 modules, mixed with the synced tick.
    const uint32_t seed = ((uint32_t)OCT_random(0, 65536) << 16) ^ (uint32_t)OCT_random(0, 65536) ^ (vars.tick * 0x9E3779B9u);
    Maze::CellAddress start{}, goal{};
    uint16_t distance = 0;
    uint32_t selected = 0;
    if (!refreshTopology() || !Maze::GenerateRound(seed, vars.topo, vars.layout, start, goal, distance, selected, difficulty)) {
        showMenu(true);
        return;
    }
    st.themeRound = advancing ? (uint8_t)((st.themeRound + 1) % Maze::kThemeCount) : 0;
    st.theme = theme;
    st.nextRoundMs = 0;
    Maze::PrepareRound(st, selected, start, goal, distance, (uint16_t)(st.roundId + 1u), difficulty);
    st.bestTimeMs = vars.bestMs[(int)difficulty];
    for (uint8_t t = 0; t < Maze::kTileCount; t++) addWalls(t);
    addOnTile(L_GOAL, goal.tile, Maze::CellCenter(Maze::CellColumn(goal.cell)), Maze::CellCenter(Maze::CellRow(goal.cell)), 0, GOAL_BMP[(int)theme]);
    vars.ball = addOnTile(L_BALL, start.tile, st.ball.x, st.ball.y, 0, BALL_BMP[(int)theme]);
    vars.freezeTicks = 0;
    vars.scene = SCENE_MAZE;
    vars.sceneTick = vars.tick;
    updateBall();
}

static inline void win() {
    playSound(SND_success, 100);
    saveResult();
    vars.st.nextRoundMs = (uint16_t)RESULTS_MS;
    showResults();
}

#if MZ_DEBUG_AUTOPLAY
// Test hook: a tilt that steers the ball along the shortest path to the goal (through portals), so hidden runs
// exercise physics, portals and topology. Never in device builds.
static inline Maze::AccelerationSample botTilt() {
    const Maze::GameState& st = vars.st;
    const Maze::CellAddress cur = Maze::BallCell(st.ball);
    float tx = Maze::CellCenter(Maze::CellColumn(cur.cell)), ty = Maze::CellCenter(Maze::CellRow(cur.cell));
    Maze::BuildDistances(vars.layout, vars.topo, st.goal);
    const uint16_t here = Maze::gDistances[cur.tile * Maze::kCellsPerTile + cur.cell];
    const float dx = st.ball.x - tx, dy = st.ball.y - ty;
    for (uint8_t d = 0; d < 4; d++) { // the neighbour one step closer to the goal
        Maze::CellAddress next{};
        if (!Maze::CellNeighbor(vars.layout, vars.topo, cur, (Maze::Direction)d, next)) continue;
        if (Maze::gDistances[next.tile * Maze::kCellsPerTile + next.cell] >= here) continue;
        const bool horizontal = d == 1 || d == 3;
        if ((horizontal ? dy * dy : dx * dx) > 36.0f) break; // line up on the corridor first, or it clips a corner
        const float reach = next.tile == cur.tile ? (float)Maze::kCellSize : 2.0f * Maze::kCellSize;
        tx += d == 1 ? reach : (d == 3 ? -reach : 0.0f);
        ty += d == 2 ? reach : (d == 0 ? -reach : 0.0f);
        break;
    }
    const float ax = (tx - st.ball.x) * 40.0f - st.ball.velocityX * 8.0f;
    const float ay = (ty - st.ball.y) * 40.0f - st.ball.velocityY * 8.0f;
    return {(int32_t)ax, (int32_t)ay, 1000};
}
#endif

static inline void stepPlay() {
    Maze::GameState& st = vars.st;
    st.elapsedMs += OCT_FRAME_MS;
    if (vars.freezeTicks > 0) {
        vars.freezeTicks--;
    } else {
        // Three physics steps per 50 ms tick: the SDK 6.3 build stepped every 16 ms.
        static const uint32_t STEPS_MS[3] = {17, 17, 16};
        for (int i = 0; i < 3 && st.phase == Maze::GamePhase::Playing; i++) {
#if MZ_DEBUG_AUTOPLAY
            const Maze::AccelerationSample tilt = botTilt();
#else
            const Maze::AccelerationSample tilt = tileGravity(st.ball.tile);
#endif
            Maze::PhysicsResult r = Maze::StepPhysics(st, vars.layout, vars.topo, tilt, STEPS_MS[i]);
#if MZ_DEBUG_AUTOPLAY
            if (r.transitionStarted) OCT_trace(0, "portal: tile %d -> %d\n", (int)st.transition.fromTile, (int)st.transition.toTile);
#endif
            if (r.won) { win(); return; }
        }
    }
    updateBall();
}


////////////////////////////////
//          HANDLERS          //
////////////////////////////////

OCT_CALLBACK void on_init() {
    OCT_restart((int*)gObjects, SPRITES_CAP, (int32_t)sizeof(appObject_t));
    OCT_viewports_layout(SCHEME_CUBE, GAP, GAP);
    OCT_background(0x20E3); // #241C18, the menu's dark wood
    memset(&vars, 0, sizeof(vars));
    vars.st.phase = Maze::GamePhase::Ready;
    vars.st.difficulty = Maze::Difficulty::Easy;
    vars.st.theme = Maze::Theme::Wood;
    saveLoad();
    showMenu(true);
}

OCT_CALLBACK void on_pretwisted(int32_t side) {
    (void)side;
    if (vars.st.phase == Maze::GamePhase::Playing) vars.freezeTicks = PRETWIST_FREEZE_TICKS;
}

OCT_CALLBACK void on_twisted(int32_t twid, uint32_t disconnected_ms) {
    Maze::GameState& st = vars.st;
    if (twid >= OCT_TWIST_HALF) { // turned part way and back: nothing moved
        if (st.phase == Maze::GamePhase::Playing) vars.freezeTicks = 0;
        return;
    }
    if (st.phase == Maze::GamePhase::Ready) { startRound(false); return; }
    if (st.phase != Maze::GamePhase::Playing) return; // twists never skip the results countdown
    if (disconnected_ms != 0) Maze::RecordTwist(st); // 0 = second half of a 180: one twist, as on SDK 6.3
    Maze::StopBall(st);
    if (!refreshTopology()) OCT_trace(-1, "topology: inconsistent after twist %d, previous links kept\n", (int)twid);
    Maze::RefreshDistance(st, vars.layout, vars.topo);
    vars.freezeTicks = SETTLE_TICKS;
    updateBall();
}

OCT_CALLBACK void on_tap(int32_t tapid, int32_t count) {
    (void)tapid;
    Maze::GameState& st = vars.st;
    if (st.phase == Maze::GamePhase::Win) {
        if (count == 2) { // back to setup, keeping the selections
            st.phase = Maze::GamePhase::Ready;
            st.nextRoundMs = 0;
            st.themeRound = 0;
            showMenu(true);
        }
        return;
    }
    if (st.phase != Maze::GamePhase::Ready) return; // pats never affect the ball or a round
    if (count == 1) {
        vars.singleTapAt = vars.tick + DOUBLE_TAP_WAIT_TICKS; // count 1 also opens every double tap
    } else if (count == 2) {
        vars.singleTapAt = 0;
        st.theme = (Maze::Theme)(((int)st.theme + 1) % Maze::kThemeCount);
        showMenu(false);
    }
}

OCT_CALLBACK void on_tick() {
    Maze::GameState& st = vars.st;
    vars.tick++;
    switch (st.phase) {
        case Maze::GamePhase::Ready:
            if (vars.singleTapAt != 0 && vars.tick >= vars.singleTapAt) {
                vars.singleTapAt = 0;
                st.difficulty = st.difficulty == Maze::Difficulty::Easy ? Maze::Difficulty::Hard : Maze::Difficulty::Easy;
                showMenu(false);
            }
#if MZ_DEBUG_AUTOPLAY
            if (vars.tick - vars.sceneTick >= 3u * OCT_1SEC_TICKS) startRound(false);
#endif
            break;
        case Maze::GamePhase::Playing:
            stepPlay();
            break;
        case Maze::GamePhase::Win: {
            st.nextRoundMs = (uint16_t)(st.nextRoundMs > OCT_FRAME_MS ? st.nextRoundMs - OCT_FRAME_MS : 0);
            if (st.nextRoundMs == 0) { startRound(true); break; }
            setCountdownText();
            // Results follow the live pose; rebuild when any face's upright angle changed.
            for (int32_t p = 0; p < SIDES_COUNT; p++) {
                if (snapAngle(OCT_TM_face_up(p)) != vars.up[p]) { showResults(); break; }
            }
            break;
        }
    }
}

OCT_CALLBACK void on_shake(int32_t shakeid) {
    (void)shakeid; // never called: a shake always exits a game
}
