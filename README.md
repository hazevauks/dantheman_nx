# dantheman_nx

**Dan the Man for Nintendo Switch** — a port of **version 1.2.1** of the
32-bit Android game, built on the
[android32](https://github.com/aks796/android32) runtime.

The port is a wrapper: it loads the game's own code from your APK and gives
it what it expects from Android. **No game files are included.** You need
your own copy of the game.

## What you need

- A Switch with Atmosphère and [sphaira](https://github.com/ITotalJustice/sphaira)
- Your own APK of **Dan the Man 1.2.1** (`com.halfbrick.dantheman`,
  versionCode 1210006, the `armeabi-v7a` build). The port was made for and
  tested with this version only: other versions are untested and may not
  work.

## Installing

1. Copy `dantheman_nx.nro` to `sd:/switch/dantheman_nx/`.
2. Copy your APK into the same folder (any file name ending in `.apk`).
3. In sphaira: **Homebrew › Dan the Man › Install Forwarder**.
4. Start the new icon on the HOME menu. The first start unpacks the game's
   engine from the APK.

To update, replace the NRO in the folder and start the icon: the port
updates itself.

To remove it, delete `sd:/switch/dantheman_nx/` and the folder under
`atmosphere/contents/` named in `title_id.txt`.

## Controls

| Switch | Game |
| --- | --- |
| D-pad / left stick | move |
| B | jump, confirm |
| Y | hit |
| A | use the secondary weapon |
| X | switch weapons |
| L | take an in-game picture |
| R, ZL, ZR | the game's other shoulder buttons |
| + | start / pause |
| − | back |
| Touch screen | as on a phone (handheld) |

A second controller joins as player 2.

## Settings

`sd:/switch/dantheman_nx/config.ini` is written on the first start. Each
option is explained in the file; among them the language (the console's by
default), the rendering resolution, and swapping A and B.

## What is different from Android

- Nothing online: no ads, purchases, leaderboards, cloud saves or sharing.
  The game believes it has no network connection.
- Saves are kept in `sd:/switch/dantheman_nx/data/`.

## Reporting a problem

Send `debug.log` (and `crash.log`, if there is one) from the game's folder.

## Building

Every push builds in GitHub Actions (`.github/workflows/build.yml`), in the
runtime's toolchain containers; the NRO is in the run's artifacts. Locally,
with Docker: [libnx32](https://github.com/aks796/libnx32) next to this
folder, [mesa32](https://github.com/aks796/mesa32)'s `lib/` and `include/` in
`portlibs32/`, then `python3 runtime/tools/gen_imports.py`, `./build.sh` and
`launcher/build.sh`.

## Credits

- The game: Halfbrick Studios. This port is not affiliated with or endorsed
  by them. "Dan the Man" and the launcher's icon are Halfbrick Studios' name
  and artwork, shown only to identify the game.
- [android32](https://github.com/aks796/android32),
  [libnx32](https://github.com/aks796/libnx32) and
  [mesa32](https://github.com/aks796/mesa32) by aks796, and the projects
  they credit (the `.so` loader by TheOfficialFloW and fgsfds, vita2hos by
  xerpi, libnx by switchbrew, Mesa).
- The port: hazevauks.

## License

MIT for the port's own code: see [LICENSE](LICENSE). The game, its name and
its artwork (`launcher/icon.jpg`) are not covered by it.
