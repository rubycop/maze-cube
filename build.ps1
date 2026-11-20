# Maze Cube for OctaviOS: one-command build.
#   ./build.ps1                 assets + simulator plugin (out\sim) + device module and verified .oct (dist\)
#   ./build.ps1 -Sim            assets + simulator plugin only
#   ./build.ps1 -Autoplay       simulator plugin with the steering bot (hidden-simulator checks; never ship it)
# Needs the WowCube Claude plugin (its build tools and public game API), Python with Pillow, the Arm GNU toolchain,
# CMake and Ninja. The device build targets the plugin's current public API (saves need it).
param([switch]$Sim, [switch]$Autoplay, [switch]$SkipAssets)
$ErrorActionPreference = 'Continue' # tools print warnings on stderr; failures are caught by their exit codes
$app = $PSScriptRoot
$kit = Get-ChildItem "$env:USERPROFILE\.claude\plugins\cache\wowcube-skills\wowcube" -Directory | Sort-Object Name | Select-Object -Last 1
if (-not $kit) { throw 'WowCube plugin not found under ~/.claude/plugins/cache/wowcube-skills' }
$kit = $kit.FullName

if (-not $SkipAssets) {
    python "$app\tools\export_assets.py"; if ($LASTEXITCODE) { throw 'asset export failed' }
    python "$kit\scripts\build_pipeline.py" pack --game maze_cube --workspace "$app\art_src" --src-dir "$app\src" `
        --app-dir $app --manifest "$app\maze_assets.json" --icon "$app\assets_src\icon.png"
    if ($LASTEXITCODE) { throw 'pack failed' }
}

$defines = if ($Autoplay) { @('-Define', 'MZ_DEBUG_AUTOPLAY=1,MZ_MUTE=1') } else { @() }
powershell -NoProfile -ExecutionPolicy Bypass -File "$kit\tools\sim\build_game.ps1" -AppDir $app @defines
if ($LASTEXITCODE) { throw 'simulator plugin build failed' }
if ($Sim -or $Autoplay) { return }

powershell -NoProfile -ExecutionPolicy Bypass -File "$kit\scripts\build_device.ps1" -AppDir $app -Out "$app\dist\app_maze_cube.oct"
if ($LASTEXITCODE) { throw 'device build failed' }
