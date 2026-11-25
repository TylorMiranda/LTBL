# Let There Be Light (LTBL)

**An all-in-one lighting overhaul for Skyrim Special Edition and Anniversary Edition.**
One mod instead of dozens, built for [Skyrim MAX (Mythic Ascension eXperience)](https://claude.ai/artifact/KiHKbiPKi9EYveaSEfARat): ultra-realistic light with mythic wonder, true to Elder Scrolls lore.

LTBL is a native SKSE plugin built on the [Community Shaders](https://github.com/doodlum/skyrim-community-shaders) renderer, with a plain-language menu and every feature on by default.

> **Status: early development.** Builds compile and publish automatically. In-game testing and calibration are under way.

## Download

**[LetThereBeLight-AIO.7z (latest build)](https://github.com/TylorMiranda/LTBL/releases/download/latest/LetThereBeLight-AIO.7z)**

1. Save it to your Mod Organizer 2 `downloads` folder, or install it with **Install a new mod from an archive**.
2. Disable any existing Community Shaders install and any ENB. LTBL replaces both.
3. Requires [SKSE](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).
4. In game, press **END** to open the menu.

## What's in the current build


### Torch Bearers
NPCs and followers draw torches when it is dark where they stand, indoors or out.
- Based on the real light level at each NPC, with separate on and off thresholds so nobody flickers between equipping and unequipping.
- One torch per group. Put away when mounted, sitting, sleeping or sneaking, and never used inside wooden or civilized buildings.

### Candlelight Aura and Tomes
Candlelight lights a wide, soft area around you instead of a bright floating orb.
- It starts dim and grows brighter with each Candlelight tome you find, reaching full brightness at **10**.
- Tomes appear as **extra** loot in urns, coffins and chests of dungeons, caves and ruins: 1 to 2 in small ones, 2 to 4 in normal ones, 5 or more in huge ones. They are added at runtime, so no loot lists are edited and nothing is replaced.
- Once you have found 10, they stop appearing and any unlooted ones LTBL added are removed. Vendors keep stocking the tome.
- Followers never get the spell; they carry torches.

### Community Shaders underneath
Unlimited lights (Light Limit Fix), inverse-square falloff, screen-space global illumination, skylighting, volumetric lighting, PBR materials, HDR output, upscaling and frame generation, and the rest of the Community Shaders feature set, all enabled by default.

## Roadmap

- **Light Director:** automatic, correctly sized and shadowed lights for every flame, ember and glowing object in any load order, with no patches.
- **49 new light sources** that glow but don't emit light today, from word walls and standing stones to Dwarven centurion furnaces and fire salts.
- **Optical profiles:** each material handles light its own way, including animal eyeshine, iridescent scales, glossy versus light-absorbing black, and glowing life versus fire versus magic.
- **Interior and exterior light matching:** windows and openings carry the real sun, moon, weather and time of day indoors.
- **Particles:** every particle lit and shadowed by the scene, one shared wind, no upscaler ghosting.
- **Neural lighting tier:** lightweight and optional, with automatic VRAM budgeting read at startup and adjusted in real time.
- Genre presets, a Mythic slider group, and per-type colorblind modes.

## Building

LTBL builds on Windows with Visual Studio 2022+ and vcpkg, exactly like Community Shaders:

```bat
BuildRelease.bat
```

Every push to `main` is built by GitHub Actions, which publishes the all-in-one archive to the [latest release](https://github.com/TylorMiranda/LTBL/releases/tag/latest).

## Credits and license

LTBL is a fork of [Skyrim Community Shaders](https://github.com/doodlum/skyrim-community-shaders) by doodlum and the Community Shaders contributors, and is distributed under the same **GPL-3.0** license (see `COPYING` and `EXCEPTIONS.md`). Thanks to everyone whose work Community Shaders is built on.

LTBL by Tylor Miranda, for Skyrim MAX.
