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
- [ ] Analogue stick values (today the left stick is the D-pad).
- [ ] Two players, docked 1080p and touch need a longer play test.
- [ ] Other versions of the APK than 1.2.1 are untested.
