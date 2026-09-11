<div align="center">

### COMMAND AND CONQUER · GENERALS

# ZERO HOUR: REBORN

**Zero Hour, rebuilt from the source EA opened.**

![platform](https://img.shields.io/badge/platform-Windows%20·%20x86-0d1117?style=for-the-badge&labelColor=161b22)
![build](https://img.shields.io/badge/CMake-Visual%20Studio%202022-0d1117?style=for-the-badge&labelColor=161b22)
![changes](https://img.shields.io/badge/changes-119-00b894?style=for-the-badge&labelColor=161b22)
![bugs](https://img.shields.io/badge/EA%20bugs%20fixed-~60-e17055?style=for-the-badge&labelColor=161b22)
![license](https://img.shields.io/badge/license-GPL--3.0-0d1117?style=for-the-badge&labelColor=161b22)

</div>

---

EA opened the source of Generals and Zero Hour. The game itself never went away and still runs on
Steam.

**Reborn** is that source moved to Visual Studio 2022 and changed as a game: ~580 engine source
files ported, and around sixty original defects found and fixed. Not port damage. EA's own, shipped
in 2003 and never noticed.

You need to own the game; no game data ships here. Nothing changes unit stats, weapons or balance.

## Then and now

| | The source EA released | Reborn |
|:--|:--|:--|
| **Frame rate** | 33 fps, with game speed tied to it | uncapped picture, rules on their own clock |
| **Worst logic turn** | `2,976 ms` | `243 ms` |
| **Long route search** | `55,000` cells · `256 ms` | `10,000` cells · `25 ms` |
| **Pathfinding, per match** | `11.8 s` | `1.6 s` |
| **Skirmish AI** | urgent orders and one power plant | builds its base |
| **Attack-move** | drives past the enemy without firing | engages, rearms, resumes |
| **Base textures** | Zero Hour's downscaled copies | 481 originals at 4× the resolution |
| **Infantry shadows** | a flat blob | cast from the pose — arms, head, weapon |
| **When it crashes** | silence | symbolized report, file and line |

## What you actually notice

- **The frame rate cap is gone — and the game did not get faster.** The picture runs uncapped while
  the rules keep their own steady clock. A bad moment now costs you a dropped frame instead of a
  slow-motion match.
- **The computer opponent builds a base.** One wrong value meant the skirmish AI only ever built
  what its script marked urgent, plus a single power plant. Every skirmish anyone has played against
  this code since 2003 was against an opponent that could not build.
- **Attack-move actually attacks.** Four separate fixes, one of them found by putting probes into a
  live match: aircraft were being *penalised* for emptying their rack and flying home to rearm.
- **Long orders stopped hitching.** EA's coarse pathfinder pass never ran, so every long move
  searched the whole map — and every wall, fence and building permanently held one of the search's
  scratch records.
- **Your whole base's production, on one strip.** A single row above the command bar carries
  everything you are building anywhere, ordered by time left. Click a picture to jump the camera
  there.
- **Soldiers cast real shadows**, built from the pose instead of a flat blob. Scud trails, falling
  bombs, smoke clouds and all 128 tree types cast too — the tree shadows had never been drawn at all.
- **It fits your monitor.** Widescreen resolutions are back in the options menu, zoom-to-cursor
  works, and buildings snap to the pathfinder's own grid with the blocked squares crossed out.
- **It does not crash.** Poison clouds, mine clearing, garrison kills, full transports blowing up,
  and quitting the game — all traced and fixed.

> [!NOTE]
> **[CHANGELOG.md](CHANGELOG.md) is the full record** — all 119 changes, what was tried and
> reverted, and what is still missing.

## How it was proved

No CI, no debugger on the machine. The game tests itself instead:

- **It plays itself.** Eight computer opponents, started from one command line.
- **Headless, a 23-minute skirmish plays out in 38 seconds** — identically, every run.
- **It plays itself over a network too**, two copies and one real connection. That is what caught
  every multiplayer replay falsely accusing itself of desync since 2003.
- **Every fix was proved by putting the bug back** and watching the exact test go red.
- **Defects deliberately left alone are pinned by a test** that documents the behaviour, so a future
  change to them is a decision and not an accident.

---

## Build it

Win32 x86 only — the code is full of 32-bit inline assembly, and an x64 configure is rejected on
purpose. Visual Studio 2022 with the Desktop C++ workload is all you need.

```console
cmake -S GeneralsMD/Code -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`build.example.bat` wraps all three — copy it to `build.bat` and fill in the paths for your machine.

To play, put your Steam install's Zero Hour `*.big` files next to the built `generals.exe` in
`GeneralsMD/Run/`, and the **base game's** `*.big` files in `Run/ZH_Generals/` — Zero Hour is not
standalone and mounts both. No disc, no registry keys, no retail installer.

> [!IMPORTANT]
> Some third-party sources are not committed — EA stripped them. Drop in zlib 1.1.4, LZH-Light 1.0,
> a minimal DirectX 8 SDK, the GameSpy SDK and d3d8to9 before the first build. STLport, the 3DSMax 4
> SDK, NVASM, the Miles and Bink SDKs and the SafeDisk API are **not** needed to build the game —
> sound and video run through the DLLs your own install already ships.

<details>
<summary><b>Tuning — a few things are off by default</b></summary>

<br>

All of these live in `Options.ini`, in your Zero Hour Data folder. Back it up first.

| Key | What it does |
|:--|:--|
| `Bloom = 60` | bright things bleed light into the air; `BloomThreshold` sets how much of the picture joins in |
| `StaticGameLOD = Custom` + `MaxParticleCount = 10000` | four times the High preset — much denser explosions and smoke |
| `DynamicLOD = no` | the game never silently downgrades quality mid-match |
| `GridBuildPlacement = No` | restores free building placement |
| `NudgeBuildPlacement = No` | a blocked building stays red under the cursor instead of sliding to the nearest spot that fits |

The rest of the quality switches, all worth turning on: `UseShadowVolumes`, `UseShadowDecals`,
`UseCloudMap`, `UseLightMap`, `ShowSoftWaterEdge`, `ExtraAnimations`, `HeatEffects`, `ShowTrees`,
`BuildingOcclusion`, `TextureReduction = 0`, `AntiAliasing = 4`.

The new shadows and the opening camera are `GameData.ini` keys instead —
`UseShadowVolumesForSkins = No` puts the old flat infantry blobs back, and `StartAtMaxZoom = No`
restores the retail opening view.

</details>

## Not there yet

- Random maps generate and play, but have no menu entry and no reroll button yet.
- Online and LAN play are untested.

---

<details>
<summary><b>EA's original README</b></summary>

<br>

This repository includes source code for Command & Conquer Generals, and its expansion pack Zero Hour. This release provides support to the Steam Workshop for both games ([C&C Generals](https://steamcommunity.com/workshop/browse/?appid=2229870) and [C&C Generals - Zero Hour](https://steamcommunity.com/workshop/browse/?appid=2732960)).

### Dependencies

If you wish to rebuild the source code and tools successfully you will need to find or write new replacements (or remove the code using them entirely) for the following libraries;

- DirectX SDK (Version 9.0 or higher) (expected path `\Code\Libraries\DirectX\`)
- STLport (4.5.3) - (expected path `\Code\Libraries\STLport-4.5.3`)
- 3DSMax 4 SDK - (expected path `\Code\Libraries\Max4SDK\`)
- NVASM - (expected path `\Code\Tools\NVASM\`)
- BYTEmark - (expected path `\Code\Libraries\Source\Benchmark`)
- RAD Miles Sound System SDK - (expected path `\Code\Libraries\Source\WWVegas\Miles6\`)
- RAD Bink SDK - (expected path `\Code\GameEngineDevice\Include\VideoDevice\Bink`)
- SafeDisk API - (expected path `\Code\GameEngine\Include\Common\SafeDisk` and `\Code\Tools\Launcher\SafeDisk\`)
- Miles Sound System "Asimp3" - (expected path `\Code\Libraries\WPAudio\Asimp3`)
- GameSpy SDK - (expected path `\Code\Libraries\Source\GameSpy\`)
- ZLib (1.1.4) - (expected path `\Code\Libraries\Source\Compression\ZLib\`)
- LZH-Light (1.0) - (expected path `\Code\Libraries\Source\Compression\LZHCompress\CompLibSource` and `CompLibHeader`)

### Compiling (Win32 Only)

To use the compiled binaries, you must own the game. The C&C Ultimate Collection is available for purchase on [EA App](https://www.ea.com/en-gb/games/command-and-conquer/command-and-conquer-the-ultimate-collection/buy/pc) or [Steam](https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/).

The quickest way to build all configurations in the project is to open `rts.dsw` in Microsoft Visual Studio C++ 6.0 (SP6 recommended for binary matching to Generals patch 1.08 and Zero Hour patch 1.04) and select Build -> Batch Build, then hit the “Rebuild All” button.

If you wish to compile the code under a modern version of Microsoft Visual Studio, you can convert the legacy project file to a modern MSVC solution by opening `rts.dsw` in Microsoft Visual Studio .NET 2003, and then opening the newly created project and solution file in MSVC 2015 or newer.

NOTE: As modern versions of MSVC enforce newer revisions of the C++ standard, you will need to make extensive changes to the codebase before it successfully compiles, even more so if you plan on compiling for the Win64 platform.

When the workspace has finished building, the compiled binaries will be copied to the folder called `/Run/` found in the root of each games directory.

### Known Issues

Windows has a policy where executables that contain words “version”, “update” or “install” in their filename will require UAC Elevation to run. This will affect “versionUpdate” and “buildVersionUpdate” projects from running as post-build events. Renaming the output binary name for these projects to not include these words should resolve the issue for you.

### STLport

STLport will require changes to successfully compile this source code. The file [stlport.diff](stlport.diff) has been provided for you so you can review and apply these changes. Please make sure you are using STLport 4.5.3 before attempting to apply the patch.

### Contributing

This repository will not be accepting contributions (pull requests, issues, etc). If you wish to create changes to the source code and encourage collaboration, please create a fork of the repository under your GitHub user/organization space.

### Support

This repository is for preservation purposes only and is archived without support.

</details>

<div align="center">
<sub>

GPL v3 with additional terms — see [LICENSE.md](LICENSE.md).
Preservation release © Electronic Arts.

</sub>
</div>
