# Weave as a performance (LOOM-8), 2026-10-18

Part 1 is the design as agreed before (the 2026-10-18 handoff, "Agreed, not built"), written out. Part 2 reviews
it against LOOM-8 as it is at the end of 2026-10-18 and the rules the UI follows now. Part 3 is the revised design
the review leads to, and the questions left for Charles.

## 1. As agreed

- **What.** Weave today is an edit: SEL, COPY PATTERN on one pattern, then WEAVE WITH COPIED on another rewrites
  it (pattern.c `pattern_weave`: bars, steps, rhythm, notes). The performance weaves live instead: two patterns
  crossed while the song plays, nothing written.
- **A weave page and a weave track**, global, like the conductor.
- **Keys.** White keys 1-8 pick pattern A, 9-16 pattern B; the black keys pick the rule (rhythm, notes, steps,
  bars). **PLAY latches** the weave. **No knobs.**
- **REC** records the weaves into the weave track, so the song plays them back.
- **Patterns are never written.** A kit or slice pattern crossed with a melodic one weaves the rhythm only.
- **Storage:** compact weave steps (~1 KB). **2-3 tracks** woven at once.

## 2. Review against LOOM-8 now

The rules it is checked against (Charles, 2026-10-17 and today): each button is one place and each place has pages
of four knobs; ARP is pattern modification; ALGORITHM picks the track; no held modifiers (HOME no longer shifts
the keys, no "hold a step and press a button"); a tap and a hold are two gestures (steps, Mix); PLAY is start and
stop; the knob strip always shows what the four knobs do; one word per idea; what plays is visible.

| # | The design says | The problem now | Instead |
|---|---|---|---|
| 1 | A global weave page | Which tracks it acts on needs a routing (the conductor's black keys), a second idea on one page | **A page per track, on ARP** ("pattern modification"): the selected track's weave. ALGORITHM changes the track as everywhere; each track keeps its own weave, so two or three (or all) can weave at once with no routing |
| 2 | White keys 1-8 A, 9-16 B | A is the pattern the track already plays (SEQ's KNOB 1, the scene); picking it again here is a second way to do one thing | **White keys 1-8 pick B**, the pattern woven in; A is the track's pattern. White keys 9-16: free (or the rules, see 4) |
| 3 | PLAY latches | PLAY is start and stop on every page; a PLAY that means "latch" here breaks it | **A tap latches, a hold is momentary**: tap a B key, the weave stays; hold it, it weaves while held (as Mix's tap mute / hold solo, and Steps' tap / hold) |
| 4 | Black keys pick the rule | Fine, but 4 rules on 11 black keys leaves 7 keys meaning nothing | The rules on **KNOB 2** (and black keys 1-4 as a shortcut); the strip shows them |
| 5 | No knobs | The strip must show the four knobs; an empty strip on a performance page hides the state | **KNOB 1 WITH** (B: off, P1-P8), **KNOB 2 RULE** (bars, steps, rhythm, notes), **KNOB 3 EVERY** (the weave on every pass, or every 2nd / 4th: a variation that comes and goes), **KNOB 4 -** |
| 6 | A weave track; REC records into it | A new sequenced aux track is a new place on ALGORITHM, a new pattern store (NSEQT 10 -> 11: ~6 KB of pool, which has ~11.6 KB, wanted for the flash cache), and recording onto it duplicates what scenes already do: scenes are "which pattern each track plays, plus the mutes" | **The scene keeps the weave**, as it keeps the mutes: each track's B and rule (2 bytes a track, 16 a scene, 256 for all sixteen: no new track, no format beyond a bump). Weaving live changes the scene playing; a new scene (Arrange, an empty key) copies it with its weaves; the song then plays them back. REC is not needed for it |
| 7 | Patterns never written | Right, but then a weave you like has no way to become a pattern | SEL on the page: **KEEP AS PATTERN** (the woven result into an empty pattern slot of the track): the old WEAVE WITH COPIED, without the copy step |
| 8 | (not said) | What Steps shows while a track weaves: A's cells or the woven ones. Editing woven cells would edit A invisibly | Steps shows **A** (what you edit), with a line above the grid: "woven with P3, rhythm"; the playhead lights the steps that sound. The Weave page itself draws the woven bar |
| 9 | ~1 KB compact steps | (with 6, no weave steps at all) | The playback: bars and steps are picked live per step (A's or B's); rhythm and notes take B's notes in order: a 64-byte map per track (the k-th onset of A to B's note), rebuilt in the main loop when A, B or the rule changes, read by the sequencer. ~600 B for all eight tracks |
| 10 | Kit / slice x melodic: rhythm only | Holds; also kit x kit (all four rules) and the conductor and throw tracks | COND and THROW weave too (all four rules; a chord progression woven with another is useful). Rhythm and notes across a kit and a melodic track: rhythm only, the page says so |

## 3. Revised design

- **Where:** ARP's pages: Players, then **Weave** (for the selected track; COND and THROW included, since they
  play: ARP opens for them with the Weave page only, where today it is refused, "EDIT has its page"). ALGORITHM
  picks the track.
- **Keys:** white keys 1-8: the pattern woven in (B). A tap latches the weave (again: off), a hold weaves while
  held. Black keys 1-4: the rule (bars, steps, rhythm, notes). The key of B is lit; the rule's black key too.
- **Knobs:** WITH (off, P1-P8), RULE, EVERY (every pass, 2nd, 4th, 8th), -. The strip shows them.
- **Screen:** the woven bar as cells (A's steps dim, B's bright), "T2: P1 woven with P3, rhythm".
- **State:** in the scene (each track's B, rule and every). A scene copied keeps it; the song plays it; the Arrange
  page marks a woven track's row ("P1×3").
- **SEL:** KEEP AS PATTERN (into the track's first empty pattern), STOP WEAVING (every track of the scene).
- **Steps:** shows A, with "woven with P3, rhythm" above the grid.
- **Sequencer:** per track: the step source picked live (bars, steps) or through the onset map (rhythm, notes);
  the map rebuilt in the main loop on a change.
- **Costs:** scene_t +16 B (256 B the project), the maps ~600 B of RAM or pool; PROJ_FORMAT 8 (older projects do
  not load: pre-release). No new track; WEAVE WITH COPIED in Steps' list goes (KEEP AS PATTERN replaces it).

## 4. Questions for Charles

1. Weaves kept in the **scene** (as mutes), not on a weave track: agreed? It means "record a weave" is "make a
   scene with it", and the song arranges weaves as it arranges scenes.
2. **EVERY** (the weave on every 2nd / 4th / 8th pass): worth a knob, or simpler without?
3. The four **rules** stay as they are (bars, steps, rhythm, notes)?
4. WEAVE WITH COPIED replaced by KEEP AS PATTERN (one way to make a woven pattern)?
