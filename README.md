# Maze Cube for OctaviOS

Port of the SDK 6.3 Maze Cube (`cube-maze-app`) to OctaviOS. A ball rolls through a 4×4 maze on each of the
24 screens; tilt to roll it, twist to line up the portals between screens. Same rules as the original: pat
toggles Easy/Hard, double pat cycles Classic/Wood/Ancient, a twist starts the round, three themes per
difficulty, Easy then Hard, results with a 3-2-1 countdown, double pat on results returns to the menu.

## Build

```powershell
./build.ps1             # assets, simulator plugin (out\sim) and the verified device package dist\app_maze_cube.oct
./build.ps1 -Sim        # assets + simulator plugin only
./build.ps1 -Autoplay   # simulator plugin with a bot that steers the ball to the goal (checks only, never ship)
```

Play in the simulator: `~\.wowcube\sim\1.2\windows\wowcube-sim.exe --game C:\wowcube\maze_cube`.
Builds against the WowCube plugin's current public game API (it has the save API the best times need).

## Layout

| Path | What |
|---|---|
| `src/app_maze_cube.h` | the game: scenes, input, twists, rendering, saves |
| `src/maze_logic.h` | maze generation and ball physics, ported from `MazeLogic.cpp` (no std headers, no `double`) |
| `src/app_metadata.h`, `src/app_save.h` | pack header (title, version, GUID) and the save record |
| `assets_src/` | source art and sound from the original app |
| `tools/export_assets.py` | turns `assets_src` into sprites + the pack manifest (`build.ps1` runs it) |

## Differences from the SDK 6.3 build

- No networking: OctaviOS runs the same game on all 8 modules in lockstep.
- Leaderboard replaced by local records (best and last time per difficulty); OctaviOS has no leaderboard API.
- Wall shadows and the wood goal's pulsing ring are not drawn; walls are sprites, so their lighting turns with them.
- Records saved with the OctaviOS save API; old SDK 6.3 saves cannot be carried over.
