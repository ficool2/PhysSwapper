# PhysSwapper
Standalone plugin to switch physics engines at runtime, particularly on a per-map basis. Compatible with Windows and Linux on listen servers and dedicated servers. Only tested on Team Fortress 2.

The following physics engines are supported:
- Havok (`vphysics`) (Default)
- [Jolt (aka Volt)](https://github.com/Joshua-Ashton/VPhysics-Jolt) (`vphysics_jolt`)
- [Bullet](https://github.com/dyanikoglu/source-sdk-bullet-physics) (`vphysics_bullet`) **NOT TESTED!**
# Usage
The plugin checks for the existence of a `maps/<MAP NAME>_physics.cfg` file on disk or packed inside the BSP. Inside the file should be a single line of text stating which physics engine to use, for example `jolt`. If the file exists, the physics engine will be swapped to the one stated. Otherwise, it is reverted to the default physics engine (Havok) if a different engine was set before, unless overriden by `phys_override`.

The physics engine can also be overriden permanently using the `phys_override <name>` command, though it can be reverted back to Havok afterwards. This command is admin-restricted and will only work while a map is active.

A convar named `phys_engine` is exposed, which stores the current physics engine name. VScript can query this convar using `Convars.GetStr("phys_engine")`. It can also be used to test if the plugin is loaded, if it returns `null` the plugin is not loaded.

If experiencing crashes or issues, the `phys_swap_debug` convar can be set to 1 to enable debug messages when swapping physics engines.

Currently, physics engines will only be swapped between level changes.

# Installation
1) In the game directiory (example: `Team Fortress 2/tf/...`), create a folder named `addons` if it doesn't exist already.
2) Download the [latest build](https://github.com/ficool2/PhysSwapper/releases/). Volt binaries are included in the release.
3) Extract the 'sourcemod' folder to the addons folder (i.e `addons/sourcemod/...`)
4) If SourceMod is installed: you don't need to do anything else. **NOTE:** The physics engine will not be swapped on first map load as SourceMod loads extensions too late. To workaround this, switch to another map after the first map is loaded.
5) If SourceMod is NOT installed, download the [PhysSwapper.vdf](https://github.com/ficool2/PhysSwapper/blob/main/PhysSwapper.vdf) file and put it inside the addons folder. Done! SourceMod is not required for this plugin to work.
### Non-dedicated servers
To load this plugin locally, you must launch the game with the '-insecure' launch option. Note that you cannot join online servers when this option is active, but other players can join you. Therefore to disable this plugin, simply remove the '-insecure' launch option.
# Build
Although the plugin is standalone, it is built ontop of the SourceMod repository for convenience.
1) Follow [instructions to build SourceMod](https://wiki.alliedmods.net/Building_sourcemod) successfully
2) Clone the repository into the `alliedmodders/sourcemod/public/PhysSwapper` folder
3) Modify the hl2sdk for the game you will build for. This plugin requires accessing private variables in a certain class, so a change is required to make those public. The file is `public/appframework/IAppSystemGroup.h`. In the `CAppSystemGroup` class, add a `public:` line before the `struct Module_t` definition.
3) Create a folder inside named 'build'. Configure the build first with `python ../configure.py` (might be `python3` on some systems)
4) Run the `ambuild` command. It should now build successfully.
5) To build Volt, follow the [guide on their repo](https://github.com/Joshua-Ashton/VPhysics-Jolt/blob/main/build.md). To build VPhysics-Bullet, follow the [guide here](https://github.com/dyanikoglu/source-sdk-bullet-physics).
6) Rename the physics binary to the naming convention listed at the top of this README, e.g. `vphysics_jolt`. Note that Volt has different SIMD modules for more CPU support, do not rename those. Only rename the main `vphysics` binary. It may be useful to modify the VPC `$OUTBIN` to do this automatically. 
7) **LINUX ONLY:** The rpath in the physics engine binaries must be patched to load dependencies relative to the game's bin folder instead. This can be done by installing the `patchelf` package and then running the [PhysSwapper_linux_fix_rpath.sh](https://github.com/ficool2/PhysSwapper/blob/main/PhysSwapper_linux_fix_rpath.sh) batch script from this repository. Alternatively in GCC/Clang, the rpath can also be set using the `-rpath` parameter, see the script for which path to use.

# Credits
- Joshua Ashton (🐸✨) (`@phys_ballsocket`) and Josh Dowell (`@Slartibarty`) for making [Volt](https://github.com/Joshua-Ashton/VPhysics-Jolt)
- `ozxybox` for the [SpeedyKeyV](https://github.com/ozxybox/SpeedyKeyV) parser (modified)
