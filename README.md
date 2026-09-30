# Tempest 2000 for Apple TV (tvOS)

This is a fork of [Jeff Minter's source code for Tempest 2000](https://github.com/mwenge/tempest2k) (Atari Jaguar, 1994),
with additional code, along with an Xcode project, to allow anyone to build it for tvOS. It also builds as a native
macOS app.

It is the real game: Jeff's original 68000/GPU/DSP assembly source is assembled unchanged (the result is checked to be
**byte-for-byte identical to the original 1994 build**) and runs on the open-source
[Virtual Jaguar](https://github.com/libretro/virtualjaguar-libretro) emulation core, linked into a small native Apple TV app.

**The Xcode project is at [`mac/tvos/Tempest2000TV.xcodeproj`](mac/tvos/Tempest2000TV.xcodeproj).**

## What you need

* A Mac with **Xcode** (tested with Xcode 26; the app targets tvOS 15 or later) and the command line tools (`xcode-select --install`)
* The **tvOS platform component** for Xcode - download it once with `xcodebuild -downloadPlatform tvOS`
  (or Xcode > Settings > Components > tvOS)
* An **Apple TV 4K** and an **Apple ID** (a free one is fine: the app then has to be re-installed every 7 days)
* `git`, an internet connection (the first build downloads the assembler tools and the emulator core)

Nothing else: Homebrew and SDL are only needed for the macOS app.

## Build it for the Apple TV, step by step

### 1. Get the code
```sh
git clone https://github.com/Jnaina/tempest2000-mac-tvos.git
cd tempest2000-mac-tvos
```

### 2. Assemble the game (`t2000.abs`)
```sh
cd mac
make t2000.abs
```
This downloads and builds Jeff's assembler tools (`rmac` and `rln`) into the repository root, assembles the
source in [`src/`](src/) and finishes with
`t2000.abs: byte-identical to the original 1994 build` (about 10 seconds).

### 3. Fetch the emulator core and build it for tvOS
```sh
make third_party/virtualjaguar-libretro/Makefile     # downloads the Virtual Jaguar core at a pinned version and applies patches/virtualjaguar-blitter.patch
cd tvos
./build_core.sh                                      # builds libvjcore_dev.a (Apple TV) and libvjcore_sim.a (simulator)
```

### 4. Put the game inside the Xcode project
```sh
cp ../t2000.abs T2KTV/t2000.abs
```
(`mac/tvos/T2KTV/` holds the app: `main.m` is the whole front end, plus `Info.plist` and the icon assets.)

### 5. Pair your Apple TV with Xcode
1. On the Apple TV: **Settings > Remotes and Devices > Remote App and Devices** (leave it open). If the Apple TV asks you to turn on
   Developer Mode, it is under Settings > Privacy & Security.
2. On the Mac, in Xcode: **Window > Devices and Simulators**, select your Apple TV in the list (or click **+**), and enter the
   code it shows on the TV. Both must be on the same network.

### 6. Open the project and choose your signing team
```sh
open Tempest2000TV.xcodeproj          # from mac/tvos
```
1. Click the **Tempest2000TV** project in the left sidebar, select the **Tempest2000TV** target, open **Signing & Capabilities**.
2. Set **Team** to your Apple ID / developer team (add it in Xcode > Settings > Accounts if it is not listed).
3. Change **Bundle Identifier** to something unique to you, for example `com.yourname.tempest2000.tv`
   (the project ships with the author's team and bundle id, which Xcode will not let you use).

### 7. Run it
1. In the toolbar destination menu choose your **Apple TV**.
2. Press **Run** (Cmd+R). Xcode builds, installs and launches Tempest 2000 on the TV (the first run takes a minute or two).
3. The app stays on the Apple TV's Home screen with its icon. With a free Apple ID it stops launching after 7 days: press Run again to renew it.

### The same thing from the command line
```sh
cd mac/tvos
xcrun devicectl list devices                                  # note your Apple TV's identifier (UDID)
python3 gen_xcodeproj.py YOUR_TEAM_ID com.yourname.tempest2000.tv   # only if you did not set the team in Xcode
xcodebuild -project Tempest2000TV.xcodeproj -scheme Tempest2000TV -configuration Release \
           -destination "id=<UDID>" -allowProvisioningUpdates -derivedDataPath build build
xcrun devicectl device install app --device <UDID> "build/Build/Products/Release-appletvos/Tempest 2000.app"
xcrun devicectl device process launch --device <UDID> com.yourname.tempest2000.tv
```
(Your team id: Xcode > Settings > Accounts > select the team. The Apple TV must be awake to launch an app.)

### If something goes wrong
| Problem | Fix |
|---|---|
| `No available simulator runtimes for platform appletvsimulator` while compiling the icon | Install the tvOS platform: `xcodebuild -downloadPlatform tvOS` |
| `t2000.abs missing from the app bundle` shown on the TV | Step 4 was skipped: copy `t2000.abs` into `mac/tvos/T2KTV/` and build again |
| `library 'vjcore_dev' not found` / linker errors | Step 3 was skipped: run `./build_core.sh` in `mac/tvos` |
| Signing errors / "bundle identifier is not available" | Step 6: pick your team and use your own bundle identifier |
| Xcode does not list the Apple TV | Repeat step 5; check both devices are on the same network and the Apple TV is awake |
| App will not launch after a week | Free Apple IDs expire after 7 days: press Run again |

## Playing

Any game controller (PlayStation, Xbox, MFi) or the Siri Remote works.

| Action | Game controller | Siri Remote |
|---|---|---|
| Move round the web / menus | D-pad or left stick | swipe or click ring |
| Fire | A / Cross | click |
| Jump | B / Circle | Play/Pause |
| Superzapper | X, Y, shoulders or right trigger | - |
| Option (game options) | Menu / Start | Menu |
| Pause | Options / Select | - |

High scores are kept in the app's preferences. (Tempest 2000 lets you re-assign fire / jump / superzapper in its own options screen.)

## How it works

* [`src/`](src/) is Jeff Minter's source, untouched. [`mac/Makefile`](mac/Makefile) assembles it with `rmac`/`rln`.
* The Virtual Jaguar core is fetched at a pinned commit and patched ([`mac/patches/`](mac/patches/)): the blitter loop is specialised for the
  blit types Tempest 2000 uses, plus a direct screen-clear path - about twice as fast on the Apple TV, and checked to produce
  identical output (`mac/regress.sh`).
* [`mac/tvos/T2KTV/main.m`](mac/tvos/T2KTV/main.m) is the tvOS front end: the emulator runs on its own thread, the picture is drawn
  in a `CALayer`, sound goes through `AVAudioEngine`, input comes from `GameController`, and the idle-loop skip and fast blitter are turned on.
  More detail in [`mac/tvos/README-TVOS.md`](mac/tvos/README-TVOS.md).
* The app icon is generated from the game's title logo (`mac/tvos/make_tv_assets.py`).

## Building the macOS app instead
```sh
brew install sdl2
cd mac
make single        # ./t2k, a single self-contained executable
make run           # or ./t2k_host
```
See [`mac/README-MAC.md`](mac/README-MAC.md).

## Repository layout
| Path | What it is |
|---|---|
| `src/`, `orig/`, `unused/`, `utils/`, `Makefile` | Jeff Minter's Tempest 2000 source and its original assets/tools (unchanged) |
| `mac/` | the macOS build: SDL2 front end (`t2k_host.c`), Makefile that also fetches the emulator core, icon generator, regression test |
| `mac/patches/` | patch applied to the Virtual Jaguar core after it is fetched |
| `mac/tvos/` | **the tvOS app: Xcode project, source, icon assets, build scripts** |

## Credits
Tempest 2000 is by **Jeff Minter** (Llamasoft) and was published by Atari in 1994; the source code was released by Jeff and
is maintained at [mwenge/tempest2k](https://github.com/mwenge/tempest2k). The assembler tools are
[`rmac`](https://github.com/mwenge/rmac) and `rln`. The emulator core is
[Virtual Jaguar](https://github.com/libretro/virtualjaguar-libretro) (GPL-3.0). This repository only adds the Apple platform code.
The game and its assets belong to their owners; this project is not affiliated with them.
