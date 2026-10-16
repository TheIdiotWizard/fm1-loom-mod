# XY-1: a UX review (2026-10-16)

Four flows walked as a new player who reads only the screen, on the simulator: `build/ux/*.xy` run with
`build/host/xy_host SCRIPT build/ux/X` (screenshots in `build/ux/a..d/`, contact sheets `build/ux/*_sheet.png`;
not committed). Priorities as in Polly's pass: **must** (blocks or misleads), **should** (friction), **could**.

## The flows, presses

| Flow | Presses | Page changes | What went wrong |
|---|---|---|---|
| A beat (kick, snare, hats) | 12 | 0 | The fastest flow. But the grid shows one sound at a time: with the hats shown, the snare on 5 and 13 is invisible. |
| A melody, the remembered note | 4 a note (16 for four) | 2 a note | On Steps all 27 keys are steps, so every pitch means EDIT, a key, SEQ, a step. |
| A melody, step record | 2 + a key a note | 1, then blind | On Steps "KEYS write step" is shown but a key toggles a step. On EDIT the keys write, with no grid and no cursor; it began at step 1 and overwrote the melody. |
| A melody, live | 3 + playing | 1 | Works, but the grid is on another page while you play. |
| Two chords, one held long | 14 | 4 | The long chord's length is invisible: step 1 reaches step 8 and nothing shows it. |
| Edit a sound | 1 turn to change a preset | 0 | One PRESETS detent turned the prism bass into a simple lead (another engine). FX opens the filter, not effects. |
| Edit the snare | not reachable as the manual says | 0 | Its black key plays the snare; the knobs keep editing the kick. |

## Must

1. **A melody needs four presses a note and two page changes.** `b01..b04`. The Steps page uses every key for
   steps, so a pitch can only be chosen on another page. Proposal: **hold SEQ and play keys** on Steps: the keys
   play the track and set the remembered notes (one key or a chord), and a step press then writes them. Two
   presses a note, no page change. The footer says so ("hold SEQ: play notes").
2. **Step record is blind or wrong.** `b05..b09`. On Steps it claims "KEYS write step" but the keys still toggle
   steps; on EDIT they write, but the grid and cursor are gone, and the cursor starts at step 1 over existing
   notes. Proposal: REC on Steps turns the keys into a keyboard **on the Steps page**: each key or chord writes
   the cursor step, the cursor (red) moves on, OCT+ is a rest, OCT− goes back, REC ends. Start the cursor at the
   step after the last note.
3. **The beat cannot be seen.** `a04..a06`. A drum cell names only the sound shown (or the step's first sound):
   kick and snare on step 5 read "kic". Proposal: every sound on a step gets a mark (a small row of ticks, the
   shown sound's tick bright), so the whole beat is visible while one sound is edited.
4. **Note and chord lengths are invisible.** `c06`. Proposal: draw a bar from the step across the steps its
   note covers, as a piano roll would, and show length in steps ("8 steps") on the held step's KNOB 2.
5. **Bug: a drum key cannot be picked on its own Engine page.** `d11..d13`. Black key 2 plays the snare, but
   the knobs, the tag and the outline stay on the kick, and the turn changes the kick's pitch. The manual says
   the black key picks it. Fix: on a drum track's sound pages a black key selects the key it plays.
6. **Bug: changing the LFO type keeps the old type's numbers.** `d08..d09`. Tremolo to random shows
   "dest 64" and "to 64 40". Fix: a type change sets that type's knobs to their defaults.
7. **FX opens the filter.** `d05`. The panel says FX; the effects live behind ALGORITHM past T8. Proposal: FX
   opens the sends and the two effects (FX I, FX II, their four knobs), the filter moves to ENV's second page
   (the envelope and the filter are one module on many synths) or to EDIT's.

## Should

8. **PRESETS crosses engines.** `d02`. One detent from "prism bass" lands on "saw lead" (simple). Proposal:
   PRESETS walks the track's own engine; SEL's ENGINE row changes engine. Show "3/8" in the message.
9. **Black keys change meaning by track type.** On Steps they pick a drum sound on T1 but switch tracks on T2
   (`b01` footer "BLACK track"). Pressing one to hear a note jumps to another track. Proposal: on Steps the black
   keys never switch tracks (ALGORITHM does); on synth tracks they do nothing, or join hold-SEQ's keyboard.
10. **"LENGTH pattern" on a held step.** `c05`. Reads as nonsense. Proposal: "auto" or the actual length
    ("½ step").
11. **Where the remembered notes come from is never said.** `b01`: a fresh T2 shows "notes: C4" with no hint
    how to change it. Fixed by item 1's footer.
12. **The footers repeat themselves.** Every sound page ends "KEYS play · REC held: record" (`d*`). The second
    line is the place to teach the page's one non-obvious gesture.

## Could

13. Chord cells: the chord's name ("C") above its notes. The Engine pictures do not move with the knobs.
    The drum Mode page holds a single knob; it could join the Key page.

## Screen audit

- **Steps**: the knob strip (pattern, length, scale, groove) is set once per pattern and takes a quarter of
  the screen on the page used most; the line "sound: kick" repeats the tag's last word.
- **Engine**: the engine's name appears twice (top left, tag line); the picture takes the middle third and does
  not respond.
- **Every page**: the tag line "T1 drumline › P1 › S1 › kick" carries the track and pattern; the header already
  names the page. Keep the tag, drop the duplicates around it.

## The larger picture

The OP-XY spreads its grammar over 68 buttons: 16 step keys, a 24-key keyboard, 4 modules, 8 tracks, shift,
bar, players. XY-1 folds that onto 27 keys and 14 buttons, so the same key means a step, a note, a sound, a
track, a scene, a mute, a component or a value depending on the page and on HOME. The fixes above keep the
grammar but make the three core jobs (a beat, a melody, chords) happen on one page, the Steps page, with what
you wrote always visible. That page is where a new player should be able to stay.
