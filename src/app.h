#pragma once
// Entry header of every build (ARM module and simulators). Pack-header defines live in app_metadata.h.
#include "app_metadata.h"
#include "app_maze_cube.h"

// Windows simulator only: the app folder relative to its bin\ folder.
#define APP_DIR "..\\..\\maze_cube"
