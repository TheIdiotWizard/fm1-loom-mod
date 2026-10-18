# Weave as a performance (LOOM-8), 2026-10-18

Part 1 is the design as agreed before (the 2026-10-18 handoff, "Agreed, not built"), written out. Part 2 reviews
it against LOOM-8 as it is at the end of 2026-10-18 and the rules the UI follows now. Part 3 is the design after
Charles's notes on the review (the WEAVE track kept), and part 4 the questions left.

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

## 3. Revised design (after Charles's notes, 2026-10-18)

Two notes changed part 2's answer to row 6: weaves kept only in the scene cannot be punched in, and the THROW
track is the wrong home for them (its name, and throws and weaves could not be cleared or arranged apart). So the
WEAVE track stays, as first agreed, with compact steps; the rest of the review holds. One idea for the player: the
weaves are set up on each track, played on the WEAVE track.

**Setting up: ARP's Weave page (per track).** ARP's pages for the selected track: Players, then **Weave**.
KNOB 1 **WITH** (off, P1-P8: the pattern woven in, B; A is the pattern the track plays), KNOB 2 **RULE** (bars,
steps, rhythm, notes), KNOB 3 **EVERY** (every pass, 2nd, 4th, 8th; question 2), KNOB 4 -. The keys play the track
as on its other pages. The screen draws the woven bar (A's steps dim, B's bright) and "T2: P1 with P3, rhythm".
SEL: **KEEP AS PATTERN** (the woven result into the track's first empty pattern; WEAVE WITH COPIED in Steps' list
goes). COND and THROW have the page too (ARP opens for them with this page only; today it is refused). A kit or
slice track and a melodic B: rhythm only, the page says so.

**Playing: the WEAVE track.** An aux track after THROW on ALGORITHM (T1-T8, COND, THROW, WEAVE), sequenced like
them. Its EDIT page: white keys 1-8 are T1-T8: **hold** one and that track weaves (as its Weave page says) while
held; KNOB 1 **LATCH** (as THROW's): a **tap** turns a track's weave on, another off. The keys of the tracks
weaving are lit, and the page lists them ("T2 with P3, rhythm"). A track whose WITH is off: its key does nothing,
the page says "set WITH on ARP". White keys 9-10: COND and THROW.

**Recording.** **REC** held while it plays, on the WEAVE page: what is held or latched is written onto the WEAVE
track's pattern, from the step it started for as long as it lasted (as throws onto THROW). The song plays it
back. Its pattern is picked per scene like any track's, so a section can have its own weaves.

**Steps.** SEQ on the WEAVE track: its steps, each cell the tracks it weaves ("T2 T5"); a step held: KNOB 2 its
length; the black keys 1-8 toggle a track on the step (as a kit's sounds). On a woven track's own Steps page: A's
cells, which are what you edit, with "woven with P3, rhythm" above the grid.

**Storage.** A weave step: the tracks woven (one bit each, T1-T8, COND, THROW: 2 bytes) and its length (1 byte);
the WEAVE track keeps 8 patterns of 64 such steps (~1.5 KB, not the ~6 KB of a full track's steps); each track's
WITH, RULE, EVERY in its settings (trk_t rsv). The sequencer: per woven track, the step picked live (bars, steps)
or through an onset map (rhythm, notes: B's k-th note for A's k-th onset, 64 bytes a track, rebuilt in the main
loop when A, B or the rule change). PROJ_FORMAT 8 (older projects do not load: pre-release).

## 4. Questions for Charles

1. WITH and RULE are a track's, for the whole project. Should a scene be able to change them (the bass woven with
   P3 in the verse, P5 in the chorus), or is picking another WEAVE pattern, or another A, enough?
2. **EVERY** (the weave on every 2nd / 4th / 8th pass): worth a knob, or simpler without?
3. The four **rules** stay as they are (bars, steps, rhythm, notes)?
4. KEEP AS PATTERN replaces WEAVE WITH COPIED (one way to make a woven pattern)?
