#pragma once
// Pack-header defines. A leaf header: the packer compiles it alone, so include only these three.
#include "oct_app_version.h"
#include "oct_app_categories.h"
#include "app_save.h"

#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 1
#define APP_VERSION_PATCH 0
#define APP_VERSION OCT_APP_SEMVER(APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH)

// Permanent identity of Maze Cube on the cube, in the store and for its save area: never change or reuse it.
#define APP_GUID1 0xFAF2CBFC2A778356ULL

#define APP_TITLE "MAZE CUBE"
#define APP_CATEGORIES (APP_CATEGORY_GAME)
#define APP_COLORS 0x00000000
