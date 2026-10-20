# Android

A native arm64 build of the recompiled game for Android phones: the SDK runtime without its Windows host, a Vulkan
renderer, SDL3 for the window, sound and controllers, touch controls and a launcher. Tested on a Samsung Galaxy S25.

Like the Windows build, this is source only. No game files, recompiled code or APKs are distributed: you build the APK
from your own USA Xbox `default.xbe`.

## Build (Linux or macOS)

Needs the Android SDK (platform 35, build-tools 35, NDK 27.2.12479018), CMake 3.22+, Ninja, Gradle 8, Python 3, and in
`$SRC`: checkouts of zydis v4.1.1, tomlplusplus v3.4.0 and imgui v1.91.0, plus SDL 3.4.16 and glslang 15.1.0 (with
SPIRV-Tools) built for arm64 and installed into `$SRC/out/android/prefix`.

```sh
GAME_XBE=/path/to/default.xbe ANDROID_HOME=~/android-sdk SRC=~/deps android/build.sh
```

The APK is `android/app/build/outputs/apk/release/app-release.apk`. A signing key is made in `build/` the first time;
keep it to install later builds over earlier ones.

The game is accepted by its code: every section of the XBE must match the one the build was recompiled from
(`android/native/xbe_identity.py`). A copy whose header was changed (for example a media patch for hard drive play)
still works.

## Playing

Copy the game to `Internal storage/MarvelNemesis`: the disc image (`.iso`), or `default.xbe` with the disc's folders
(`Anims`, `Bundles`, `Levels`, `Models` ...). Pick the folder in the launcher and press Play. Saves go to
`MarvelNemesis/saves`, mods to `MarvelNemesis/mods`, and with Debug logging on, logs to `MarvelNemesis/logs`.

Touch controls: a left stick to move, a small D-pad for menus, A/B/X/Y, White, Black, the triggers, Back and Start. The
launcher's editor moves and resizes them; they hide while a controller is connected. The game's own Options > PC
Settings menu works with the pad or the touch controls. The Windows build's MSAA, SMAA, ambient occlusion, bloom and
shadow quality options belong to its Direct3D 12 renderer and are off on Android.
