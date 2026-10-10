# dantheman_nx — port notes

A port of **Dan the Man 1.2.1** (com.halfbrick.dantheman, versionCode 1210006,
armeabi-v7a) to the Nintendo Switch on the
[android32](https://github.com/aks796/android32) runtime (a submodule at
`runtime/`, commit `50b352c`).

State: **runs on hardware** — playable, with sound, saves, weekly events,
rumble and the system keyboard; it exits cleanly. Built in GitHub Actions
(`.github/workflows/build.yml`).

## The game

| | |
| --- | --- |
| Engine | Mortar, Halfbrick's own (`libmortargame.so`, 13 MB, Thumb-2, gnustl linked in) |
| Graphics | Plain OpenGL ES 2 (73 `gl*` functions, no EGL: the Java made the context) |
| `DT_NEEDED` | libc, libm, liblog, libGLESv2, libandroid, libdl — system libraries only |
| Imports | 297; the 224 that are not GL are ordinary libc/pthread/sockets |
| Symbols | **36,998 exported** (C++, named): internal functions can be called or replaced by name |
| Other libraries | `libjs.so`, `libadcolony.so` (ads), `libcrashlytics.so` — not loaded |
| Assets | read by the engine itself from inside the APK (`InitFileManager` is given the APK's path) |

The runtime already covers every import but 12: seven are `PASSTHROUGH`
entries in `tools/imports.cfg`, five have a shim in `source/dtm_libc.c`.

## Start-up sequence (from `GameManager` in classes2.dex)

1. `System.loadLibrary("mortargame")` → constructors (1006 entries in the init array) + `JNI_OnLoad`
2. `NativeGameLib.InitDeviceProperties()`
3. `InitFileManager(apk, filesDir + "/", cacheDir + "/", externalDir + "/", false)` — the order confirmed from the registers
4. `InitOpenSLSoundManager(AssetManager)`; if it returns `false`, `InitJavaSoundManager()`
5. `SystemInit(width, height, language)`
6. `GameInit()`
7. every frame: `keyEvent(...)` for the queued keys, then `step()`
   - `step() == false` → the game closes
   - `gameRequestedQuit() == true` → the "quit?" dialog → `confirmQuitRequest(bool)`
   - `gameRequestedRestart() == true` → the app restarts
8. the Activity's `onPause` → `NativeGameLib.onPause()` + `saveOnExit()`

The natives' signatures: `perl tools/dexinfo.pl <apk>/classes2.dex '^Lcom/halfbrick/' native`.

## Audio

The engine mixes by itself (`Mortar::Audio::AudioMixer`) and has two outputs:
`MAMAudioThread_AndroidSLES` (OpenSL ES) and `MAMAudioThread_AndroidJava`,
which hands PCM to the Java class `MortarAudioMixerOut` (a 16-bit stereo
`AudioTrack`). The port uses the second: OpenSL stays refused (the runtime's
default) and `dtm_audio.c` answers `Create` / `GetNativeSampleRate` (48 kHz) /
`Init` / `WriteData`, sending the blocks to audout.

Confirmed on hardware: `NativeGameLib.SupportsOpenSL()` answers `false`, the
engine creates `MAMAudioThread_AndroidJava` and writes 2004 stereo frames at a
time.

**Careful:** `Init(rate)` returns the rate it was given, not a buffer size.
The engine takes that value as the output rate and resamples its mix (44.1 kHz
internally) to it; any other number speeds the sound up and distorts it.

## Input

- Keys: `keyEvent(keyCode, down, flag, deviceId)` with Android's `KEYCODE_BUTTON_*` / `DPAD_*`.
- Touch: `touchEvent(action, time, pointer, x/width, y/height, pressure, size)` — coordinates normalised to 0..1.
- Controllers: `onGameControllerAttach(deviceId, name)` / `Detach(deviceId)`.
- Sticks: `motionEvent(deviceId, axis, x, y)` — **the axes' meaning is not decoded yet**; for now the left stick is the D-pad.

## To do

- [x] Build in GitHub Actions: libnx32 and mesa32 from their releases, `source/imports.c` generated at every build (224 imports, none missing), the NSP and NRO as the `dantheman_nx` artifact
- [x] First hardware test; the log's list of "unhandled" Java methods is `dtm_java.c`'s to-do list
- [x] Language: read from the console
- [x] Buttons by position: B jumps and confirms, Y hits (`swap_a_b` in config.ini swaps A and B)
- [ ] Sticks through `motionEvent`
- [x] The save persists between runs (`KeyStore` is kept in `data/keystore.txt`)

## Tools (`tools/`)

With no binutils and no Python on the machine, Perl scripts do the analysis:

- `elfinfo.pl <lib.so> [needed|exports|imports|jni|all]` — in place of `readelf`
- `dexinfo.pl <classes.dex> <class regex> [native | code [method regex]]` — in place of `dexdump`
- `thumbcalls.pl <lib.so> <symbol regex>` — what a Thumb function of the engine calls, and the constants around the calls
- `thumbxref.pl <lib.so> <symbol regex>` — who calls a function of the engine (directly or through the PLT)
- `imports_needed.txt` — the symbols the game imports (made with `elfinfo.pl`; symbol names, not game content)

## What never goes into the repository

The APK, the folder extracted from it and `_refs/` (clones of other ports,
for reading only) are in `.gitignore`.

## Findings from the hardware tests

- **Firebase**: the SDK aborts when it cannot load its Java classes. The game
  only uses it through its `FirebaseNS` layer, whose 18 functions are replaced
  by stubs (`dtm_firebase.c`); remote-config values are the defaults the game
  hands to `FirebaseNS::Init` (14 key/value pairs, the layout confirmed in the
  log).
- **The engine's threads**: each of the engine's pthreads calls
  `NativeGameLib.native_threadEntry(int)` through JNI; the handler passes it
  on to the native registered in `JNI_OnLoad`. Without that no engine thread
  does anything.
- **`HBSupport`**: the device queries are answered in `dtm_java.c` (constant
  IDs, Android 23, 240 dpi, multi-touch, neither a TV nor a tablet).
- **`data/app/…/base.apk/<file>` paths** in the log: the engine also looks for
  each file in a "mount" of the APK with a relative path; those attempts fail
  and it goes on to the right path. Noise only.
- `tools/thumbcalls.pl` is how the audio problem was found.

## Review before the first release

- **HOME / sleep**: the port called `NativeGameLib.onResume`, which on Android
  is for a lost GL context and unloads and reloads every texture
  (`DisplayManager_Android::UnloadAllResources` / `ReloadAllResources`), and
  it passed a null array besides. Replaced by Android's light path for a lost
  focus with the context kept: `onFocusLost` + `saveOnExit` on the way out,
  `onFocusRetrieved` on the way back.
- **Language**: read from the console (`set:sys`) and passed to `SystemInit`
  and to `HBSupport`; `[game] language` in config.ini forces another. The game
  has en, es, es-419, de, fr, it, ja, pt, ru, tr, zh (both scripts).
- **Loading**: `RT_BOOST_WATCH_THREAD 1` boosts the CPU during long frames
  (a level change took 2-3 s in a single frame).
- **Volume**: `GetMusicStreamVolume` / `MaxVolume` answer 15 of 15.

### Open

- **Level gates (the "gate system")**: the game has a system that unlocks
  levels by ads watched or by a wait (`gate_system_mins_per_ad`,
  `gate_system_max_ads_to_unlock`,
  `GameScreenStoryMap::InitGateSystemCountdownAssets`) and a "Premium"
  purchase that removes it. With the weekly-events clock the wait runs on the
  console's clock; with `[game] ad_rewards` the "watch an ad" way out of the
  wait answers too.
- Sticks through `motionEvent` (today the left one is the D-pad).
- Two players, docked mode (1080p) and touch have had little testing.
- The launcher's icon is the game's artwork, supplied by the port's author.

## Weekly events (the console's clock)

- The events come from a calendar inside the APK
  (`definitions/weekly_events`, in Halfbrick's binary XML, "bxml"), with no
  absolute dates: the day's event is worked out from the current date
  (`GameWeeklyEvents::GetCalendarCurrentDay`).
- What blocked them was `Game::IsServerTimeReliable`: every frame
  `Game::UpdateServerTime` asks Mortar's `ITimeService` for the time and
  whether it is reliable, and keeps both in the `Game` object (the time at
  +0x170, "reliable" at +0x184). With no server it is never reliable.
- `dtm_time.c` replaces `Game::UpdateServerTime` by its "reliable" branch with
  the console's clock (`Mortar::Timing::GetSecondsSinceEpoch`). It is applied
  only when the function's code is the 1.2.1 code the offsets were read from.
- The same check is made elsewhere (32 callers): the story map's time gate
  (`GameScreenStoryMap::IsLastLevelLockedByTime`), the offers (`GameOffers`),
  notifications, the store. With a reliable time those paths run on the
  console's clock. Tested on hardware by the port's author (build
  202610041409): the events work; released in 0.1.5.
- The event screen's video button (`AdButtonPressedHandler`) also needs a
  network and an ad: it stays unavailable unless `[game] ad_rewards` is
  turned on (below).

## Ad rewards without the ad (`dtm_ads.c`)

`[game] ad_rewards`, off by default. Up to 0.1.8 the port left the game's
rewarded videos unavailable; this began as the author's own build (the
`personal` branch) and was merged for 0.2.0. **Not tested on hardware yet.**

- What a rewarded video is in the game: the wait before a level on the story
  map, the continue, the checkpoint before a boss, the free gold. The
  screen's video button is offered when `GameAdvertising::AdPrepared` says an
  ad is loaded; pressing it, the handler asks `Mortar::Reachability` for a
  connection and calls `GameAdvertising::ShowAd` with a delegate; with no
  reliable time or no connection `ShowAd` ends at once through
  `iShowCompleted(false, ...)`, "not watched".
- Three changes, all in the game's own layer: `AdPrepared` answers yes;
  `Reachability` answers "connected" only to the video buttons' own functions
  (11 of them, told by the address the call returns to); and one byte in
  `ShowAd` (+0x18a, `movs r1, #0` → `#1`) makes its no-connection ending
  "watched". The byte is patched only where the code is the 1.2.1 code.
- The engine is never told it is online: account, analytics and store code
  stay offline. `GameStore::PurchaseItem` is not among the callers answered
  "connected": purchases stay impossible. Nor are the full-screen ads between
  levels, which reward nothing.
- It needs `[game] events = true`: `ShowAd` asks for a reliable time as well
  as a connection, and that is the weekly events' clock.

## Saves in the background, rumble and the keyboard

Released in 0.1.8. On hardware: the keyboard works, the rumble is felt, and
the log shows the saves written by the port's thread (41 files in one
session, none failed, nothing had to wait).

- **Saves (`dtm_saves.c`)**: `Mortar::IFile_Direct::Close` writes each file as
  `<name>.<ext>tmp`, removes the old file and renames. On the card that is
  four slow operations a file (creating it alone takes 20–80 ms), inside one
  frame. The port keeps the temporary file in memory and a thread does the
  same operations in the same order. The engine's `fopen`, `remove`, `rename`
  and `stat` go through the runtime's `port_imports`; whoever asks for a file
  still in the queue waits for the thread. `dtm_saves_flush()` when the focus
  is lost and on exit.
- **Rumble (`dtm_rumble.c`)**: the game never asks Android to vibrate. The
  trigger is `GameCamera::Shake(amount, seconds)` (damage, things breaking,
  the bosses' quakes), reimplemented from the 1.2.1 code (the camera's +0x58,
  +0x5c, +0x60) and followed by the rumble. Seen in a test: amounts of 5 and
  10, for 0.5 to 0.75 s.
- **Keyboard (`dtm_keyboard.c`)**: there is one text field, the custom
  character's name. `SoftKeyboard.ShowKeyboard` opens the system keyboard
  after the frame; the result goes back through `native_keyboardUpdateText` +
  `native_keyboardProcessDone` (or `…Cancelled`).

### Stutters that remain

Some frames still take 0.5 s and more (a save, a level change). The log's CPU
figures put almost all of that time on the main thread, not in file access:
it is the engine's own work. `[debug] profile_long_frames` (`dtm_prof.c`)
samples the main thread during such a frame and writes the engine functions
it was in to the log; no such log has been looked at yet.
