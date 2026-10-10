# dantheman_nx release completion list

The source list for release notes.

Process:
1. Add finished work under "Ready for changelog", one concise, player-facing
   line each.
2. When cutting a release, move the shipped lines into the release notes and
   under the release's heading below.
3. Keep what is not done under "Carry forward".

## Ready for changelog

(nothing yet)

## Released

### 0.2.0

- Ad rewards without the ad: `[game] ad_rewards = true` in config.ini gives
  what the game gives for watching an ad (the wait before a level, the
  continue, the checkpoint, the free gold) without the ad, which a Switch
  cannot show. Off by default; it needs `[game] events = true`. Purchases
  are not affected. **Not yet tested on hardware.**

### 0.1.8

- Rumble: player 1's controller rumbles when the game shakes the screen (a
  hit, something breaking, a boss's quake). `[controls] rumble` in
  config.ini turns it off.
- The custom character's name can be typed, with the system keyboard.
- Saves are written to the SD card in the background instead of inside the
  game's frame (`[performance] background_saves` turns it off).
- `[debug] profile_long_frames`: for stutter reports, what the game was doing
  in each frame of 0.3 s or more.

### 0.1.5

- Weekly events: the day's event can be played, on the console's clock
  instead of Halfbrick's time server (`[game] events` in config.ini turns it
  off). The screen's video button stays unavailable: there are no ads on a
  Switch, and what the game gives for watching one is not handed out.
- The README lists what A, X and L do; config.ini's help for the buttons
  says the same.

### 0.1.0

- First release: Dan the Man 1.2.1 (armeabi-v7a) runs from the player's own
  APK, at 60 fps, with sound, saves and a clean exit.
- Controllers by button position: B jumps and confirms, Y hits; the left
  stick works as the D-pad. The touch screen works as on a phone.
- The game's language follows the console's (`[game] language` in config.ini
  overrides it).
- HOME and sleep pause and resume the game without reloading it; the save is
  written when the game leaves the screen.
- Nothing online: Firebase, ads, purchases and analytics are off.

## Carry forward

- [ ] Level gates: the game can ask for ads or a wait before a level; with
      no ads on a Switch, find out whether a gate appears offline.
- [ ] Ad rewards (`[game] ad_rewards`): a hardware test of each video button
      (the level wait, the continue, the checkpoint, the free gold, the
      event screen).
- [ ] Stutters: some frames still take 0.5 s and more (a save, a level
      change). The time is the engine's own work on the main thread, not file
      access; `profile_long_frames` is there to find out what.
- [ ] Analogue stick values (today the left stick is the D-pad).
- [ ] Two players, docked 1080p and touch need a longer play test.
- [ ] Other versions of the APK than 1.2.1 are untested.
