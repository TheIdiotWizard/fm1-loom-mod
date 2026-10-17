# LOOM-8 manual

LOOM-8 turns the M-VAVE FM-1 into a pocket groovebox: eight threads of sound on a 64-step loom. Eight tracks
with synth engines and a drum synth, a 64-step sequencer with fourteen step tricks and parameter locks,
players, twelve throws (momentary effects) per octave, a conductor that moves the song through a progression, a
tape loop, two send effects, a mixer and master section, sixteen scenes and a song. Weave two patterns into a
third, bar by bar, step by step, or one's rhythm with the other's notes.

The loop LOOM-8 is made for: write a short pattern, add a few tricks, weave it with another, let the conductor
play a progression, season it with throws while it plays, then put the scenes in order.

- [Quick start](#quick-start)
- [Cheat sheet](#cheat-sheet)
- [Getting around](#getting-around)
- [Steps](#steps)
- [Recording](#recording)
- [Sounds](#sounds)
- [Players](#players)
- [Arrange, scenes and the song](#arrange-scenes-and-the-song)
- [The aux tracks](#the-aux-tracks)
- [Mix and master](#mix-and-master)
- [Saving](#saving)
- [Page by page](#page-by-page)
- [Screen reader](#screen-reader)

## Quick start

Ten minutes from a blank project to a jam. White keys are numbered 1
to 16 from the left, black keys 1 to 11.

1. **A beat.** Turn **ALGORITHM** until the tag line says T1 kit. Press **SEQ**. Black key 1 is the
   kick: press it, then white keys 1, 5, 9 and 13. Press black key 2 for the snare and press white keys 5 and
   13. Press black key 3 for the hat and press every other white key. Press **PLAY**.

   <img src="img/screen-beat-playing.png" width="240" alt="Steps on T1 kit, playing: kick on 1, 5, 9 and 13, snare on 5 and 13, hats between; a tick for every sound on a step, the playhead lit" title="The beat playing">
2. **A bass line.** Turn **ALGORITHM** to T2 stack. On the Steps page, hold **SEQ** and play a low note:
   the keys are a keyboard while **SEQ** is down, and the note is remembered. Let go and press white keys 1, 4,
   7 and 11. Hold **SEQ**, play another note, let go and press white key 14. Or tap **REC** and play the
   notes one after another: each one goes on the next step. Press **EDIT** and turn **KNOB 1** while it plays
   to hear the shape change.

   <img src="img/screen-seq-held.png" width="240" alt="Steps on T2 stack with SEQ held: the keys play notes, and the line above the grid says the notes a step will write" title="SEQ held on Steps"> <img src="img/screen-step-record2.png" width="240" alt="Step record on T2: the red cursor on the next step after the notes written so far; the footer says KEYS write, OCT+ rest, OCT- back, REC done" title="Step record">
3. **Tricks.** Still on T2's Steps page, hold **HOME**, hold white key 7, and press white key 3: multiply.
   Press black key 3 for three hits. Let go. Hold **HOME**, hold white key 14, press white key 11: jump, and
   black key 7 for "forward or back": the pattern starts to wander. Hold **HOME**, hold white key 1 and press
   white key 14: skip trigger, so step 1 plays every other pass.

   <img src="img/screen-trick.png" width="240" alt="HOME and step 7 held: multiply 3 on the step; the knob strip shows its two trick slots and their values" title="A trick on step 7">
4. **A chord track.** Turn **ALGORITHM** to T3. Press **EDIT** and turn **PRESETS** until a pad you like
   comes up. Press **SEQ**, hold **SEQ** and hold three keys together, let go and press white key 1. Hold white
   key 1 and press white key 8 so the chord lasts half the bar: a bar under the cells shows how far it
   reaches. Hold **SEQ** and three keys a little higher, let go and press white key 9. The cells name the
   chords.

   <img src="img/screen-chords.png" width="240" alt="Steps on T3: a C chord on step 1 and a D minor on step 9, each named in its cell, with a bar under the steps they last" title="Two chords">
5. **The conductor.** Turn **ALGORITHM** to COND (the conductor). Press **EDIT**: the key is C major and T2 and T3 follow.
   Play an F, then a G, then a C while it plays: everything moves with you, in the scale. Press **SEQ**, turn
   **SELECT** for the Bar page and press black key 4 so a step is a beat. Back on Steps, hold **SEQ** and play
   a chord, let go and press a step, and so on: four steps are a bar of progression.

   <img src="img/screen-conductor.png" width="240" alt="The conductor&#x27;s Engine page: C major, degree 0, the tracks that follow lit; knobs mode, key, scale, link" title="The conductor">
6. **Throws.** Go to any track's **EDIT** page. Hold **HOME** and press a low key: F3 ducks the
   drums, G3 loops them, B3 echoes, C4 and D4 play fills, E4 a random fill. Hold **HOME** and press a high
   key for the same on the melodic tracks. Let go and the pattern comes back.

   <img src="img/screen-throws.png" width="240" alt="The throws&#x27; page: the two octaves of the keyboard, drums below and melodic above, with the effect held lit" title="Throws">
7. **A scene.** Press **HOME**. White key 1 is the scene playing. Press white key 2: a copy. Now change
   something: mute the drums with black key 1 on Arrange, or go to T2's Steps and turn **KNOB 1** to
   P2 for a new pattern. Press white key 1 and 2 in turn to switch between them at once, or hold **HOME** and
   press one to switch at the bar.

   <img src="img/screen-arrange.png" width="240" alt="Arrange: each track&#x27;s pattern with its playhead, scene 2 playing; knobs tempo, groove, amount, click" title="Arrange">
8. **Save.** Press **SAVE**.

From here: hold a step to see its velocity, length, chance and nudge; hold a step and press **EDIT** to lock a
knob on it; press **ARP** on T2 and turn **KNOB 1** to arp; turn **ALGORITHM** to TAPE and hold white keys
while it plays. Hold any button for a second and a card explains it.

## Cheat sheet

### The words

- **Track** (T1 to T8): one sound and its patterns. After T8 come the aux tracks: COND (the conductor), THROW (the throws), TAPE, FX I
  and FX II.
- **Pattern** (P1 to P8): a track's steps, up to 64 of them in four bars. Each track has eight.
- **Step**: one cell of a pattern. It holds up to four notes, two tricks, a chance and parameter locks.
- **Trick**: something a step does as it plays, such as pulse, hold, multiply or jump. There are fourteen, on the
  white keys while you hold HOME and a step.
- **Lock**: a knob value a step sets as it plays.
- **Scene** (S1 to S16): which pattern each track plays, plus the mutes and the conductor's key.
- **Song**: a chain of scenes, each played a number of times.
- **Engine**: the way a track makes its sound. Eight synths and the kit, a drum synth.
- **Preset**: a sound to load. 64 come with LOOM-8, and 16 are yours.
- **Player**: what the keys do on a track: arp, strum or hold.
- **Conductor**: the aux track that moves the other tracks through a key and scale.
- **Throw**: a momentary effect on the drums or the melodic tracks, from a key while you hold HOME.
- **Weave**: a pattern crossed with a copied one: bars or steps in turn, or one's rhythm with the other's notes.
- **Groove**: a timing and velocity feel. The project picks the type, each track sets how much.

### How do I…

White keys are numbered from the left, 1 to 16, and black keys the same way, 1 to 11. "Hold **HOME** and
press…" means keep **HOME** down while you press the next thing.

| How do I… | Do this |
| --- | --- |
| pick a track | Turn **ALGORITHM** on any page. |
| write a beat | Press **SEQ**. On a drum track, black key 1 is the kick: press it, then white keys 1, 5, 9 and 13. Black key 2 is the snare. |
| write a note | On Steps, hold **SEQ** and play the note, let go, and press a white key: the note goes on that step. |
| write a chord | On Steps, hold **SEQ** and hold the notes together, let go, and press a step. |
| pick a drum sound | On a drum track's Steps page, press its black key, or hold **SEQ** and press any key. |
| see a step | On Steps, hold its white key. The knobs show its velocity, length, chance and nudge. |
| make a note longer | Hold its step and press a later white key. A bar under the cells shows how far a note reaches. |
| nudge a step | Hold its step and press **OCT−** or **OCT+**. |
| put a second sound on a drum step | Hold the step and press the sound's black key. |
| take one note off a step | Hold the step, press **SEL**, pick the note and press **SEL**. |
| add a trick | Hold **HOME**, hold the step, and press a white key: 1 pulse, 2 hold, 3 multiply, 4 velocity, 5 ramp up, 6 ramp down, 7 random, 8 portamento, 9 bend, 10 tonality, 11 jump, 12 skip lock, 13 skip trick, 14 skip trigger. Press a black key for its value: 1 to 9, black key 10 for random. The same white key again takes it off. |
| set a skip's pattern | With **HOME** and the step held, black key 11 cycles between "on the last pass", "not the last pass" and "only the first". |
| give a step a chance | Hold the step and turn **KNOB 3**. Past 99 % come the A:B cycles. |
| lock a knob on a step | Hold the step and press **EDIT**, **ENV**, **FX** or **LFO**. Turn a knob. **SAVE** clears the step's locks. |
| see the next bar | Press **OCT+** on Steps. |
| set the track scale | On the Bar page, press a black key: 1, 2, 3, 4, 5, 6, 8, 16 sixteenths a step, or black key 9 for half a sixteenth. |
| change the pattern | On Steps, turn **KNOB 1**. |
| weave two patterns | Copy one (**SEL**, COPY PATTERN), go to the other, **SEL**, WEAVE WITH COPIED: **ALGORITHM** picks bars, steps, rhythm or notes. **SEL** does it. |
| change the sound | Press **EDIT** and turn **PRESETS**: the engine's own sounds, "PRESET 3/8". **SEL** changes the engine. |
| edit a drum sound | On a drum track, press **EDIT** and press the sound's black key. The knobs are the model's own four. Turn **SELECT** for the model, random, pan and volume; its **SEL** list has the play mode. |
| change the filter | Press **ENV** and turn **SELECT** to Filter. **PRESETS** turns the filter type. |
| use the effects | Press **FX**: the track's sends. Turn **SELECT** for FX I and FX II; **PRESETS** picks the effect. |
| play | Press **PLAY**. Press it twice quickly to start from the top. |
| record live | Hold **REC** and press **PLAY**. Step 1 blinks red until you play the first key. |
| record while playing | Hold **REC** and play: it records while you hold it. Hold **REC** and press **PLAY** to keep it on. |
| record step by step | Stopped, on Steps, tap **REC**. The red cursor starts after the last note; each key (or chord) you play writes it and moves on. **OCT+** leaves a rest, **OCT−** goes back. **REC** again ends. |
| undo | Hold **HOME** and press **REC**. Hold **HOME** and press **PLAY** to redo. |
| set the tempo | Press **HOME** and turn **KNOB 1**. Hold **HOME** and turn it for tenths. On any page, hold **HOME** and turn **SELECT**. |
| use a throw | Hold **HOME** and press a key. The lower octave works on the track you are on, the upper octave on its whole group. |
| make a new scene | Press **HOME** and press an empty white key. It becomes a copy of the scene playing. |
| cue a scene at the bar | On Arrange, hold **HOME** and press the scene's white key. |
| mute a track | On Arrange, press its black key. On Mix, hold **HOME** and press its black key. |
| solo a track | On Mix, hold its black key. Let go, and the mutes come back. |
| play a progression | Turn **ALGORITHM** to COND (the conductor). Press **EDIT** and play a chord: it moves the tracks that follow. Press **SEQ** and press a step: the chord goes on it. Set the scale to 4 on the Bar page so a step is a beat. |
| save | Press **SAVE**. Hold **SAVE** and press white key 1 to 4 to save to that slot. |
| keep a sound | On a sound page, hold **SAVE** and press a white key. The sound becomes that user preset. |
| load a project | Press **GLO**, turn **SELECT** to Setup, press **SEL** and pick LOAD PROJECT. |

### The buttons

| Control | What it does |
| --- | --- |
| **PLAY** | Starts and stops. Twice quickly: from the top. On the Song page, it plays the song. With **REC** held, it starts live recording. |
| **REC** | Held while playing: records while held. Stopped: a tap arms step record. On Players with strum, it records the chord. |
| **SEL** | Opens a list of this page's settings and actions. With a step held, its notes. When LOOM-8 asks a question, **SEL** means yes. |
| **HOME** | Opens Arrange. On Arrange, it opens the Song page. Held, it is the shift key: tricks, throws, undo and redo, the fine tempo, scenes, mutes on Mix. When LOOM-8 asks a question, **HOME** means no. |
| **SEQ** | Opens Steps. Its page 2 is Bar. Hold it on Steps and the keys play the track and set what a step writes. |
| **EDIT** | Opens Engine. Its page 2 is Play; on a drum track, the key's model, random, pan and volume. |
| **ENV** | Opens Envelope (a drum track: funk, attack, random, length), then Filter, then the filter's envelope. |
| **FX** | Opens Sends, the track's sends to FX I, FX II and the tape. Then FX I and FX II, the two effects. |
| **LFO** | Opens LFO. Its page 2 has the type's fourth and fifth knobs. |
| **ARP** | Opens Players. With the arp, page 2 is its second set of knobs and page 3 is hold. |
| **GLO** | Opens Mix. Turn **SELECT** for Master EQ, Master drive, Master out, Setup and Status. |
| **SAVE** | Saves. Hold it and press a white key to save to a slot, or to keep a sound. With a step held, clears its locks. |
| **OCT−** / **OCT+** | Move the keys an octave. On Steps, show the bar before or after; with a step held, nudge it. In step record, move the cursor. |
| **SELECT** | Turns to a button's next page. In a list, it moves up and down. |
| **ALGORITHM** | Picks the track. In a list, it changes the row's value. |
| **PRESETS** | Steps to the next item of whatever the page shows: a preset, a drum key, an effect, a pattern, a drum sound, a scene. |
| **KNOB 1–4** | Set the four values at the bottom of the screen. |

Hold any button for a second, and a card explains it. The two lines at the bottom of the screen always say
what each control does on the page you're on.

## Getting around

### The screen

From top to bottom, the screen has five parts.

<img src="img/screen-beat.png" width="240" alt="The Steps page stopped: the header with the page and tempo, the line of what a step press writes and the bars, the 16 cells, the tag line, the knob strip and the two hint lines" title="The Steps page">

- **The header** shows the page's button and name, the tempo, a red dot while recording, and "unsaved".
  Messages and questions cover it for a moment.
- **The middle** is the page itself.
- **The tag line** says where you are: the track and its engine, the pattern and the scene, such as
  "T3 stack › P2 › S1". The dots at the right say which of the button's pages you're on.
- **The knob strip** shows the four knobs' names and values, each with a bar in the track's colour. The
  knob you just turned shows white.
- **The two bottom lines** say what the encoders and the keys do here.

The screen is dark, and every instrument track has its own colour: T1 pink, T2 teal, T3 yellow, T4 blue,
T5 orange, T6 green, T7 violet, T8 sand. A track's steps with notes are its colour, dimmed; what sounds now is
its colour, bright: the step playing, the scene playing, a meter. On Arrange and Mix every track keeps its
colour, so you can tell them apart at a glance. White outlines are you: the held step, the cursor, the knob
you turned. Red is only for recording and for locks.

### Messages

Short messages cover the header for a moment. **i** is news, **✓** is something done, an arrow is an undo
or redo, **?** on red is a question that waits for **SEL** or **HOME**, and **!** means LOOM-8 could not do it
and says why.

<img src="img/screen-card.png" width="240" alt="SEQ held alone for a second: a card explaining the Steps page; let go and nothing changes" title="A help card">

### The tracks

Turn **ALGORITHM** to pick a track: T1 to T8, then COND (the conductor), THROW (the throws), TAPE, FX I and FX II. The keys play the
track you picked on the Engine, Play, Envelope, Filter, LFO and Players pages. On a drum track, the black
keys are the eleven drum keys, and the white keys play them too. On FX I and FX II, the keys play the
instrument track you picked last, so you hear the effect.

### The lights

**PLAY** is green while playing and red while recording. **REC** lights while recording or armed. **SAVE**
lights when there is something to save. The page's button is lit. On Steps, the steps with notes glow and the
step playing is bright; with **HOME** and a step held, the white keys of its tricks light. On Arrange,
the scenes in use glow and the one playing is bright. The muted tracks' black keys glow. With Setup's LIGHTS
on, the other buttons glow too.

## Steps

Press **SEQ**. The main band shows one bar of the pattern as 16 cells, and the 16 white keys are those
cells. The top line says what a press writes: on a drum track, the sound shown; on a synth track, the
remembered notes, which are the last keys you held together on a play page; on the THROW track, the last
effect you played; on the TAPE track, the last segment. The four squares at the right are the bars.
**OCT−** and **OCT+** show the bar before or after.

<img src="img/screen-step-held.png" width="240" alt="Step 13 held on T1: its velocity, length, chance and nudge on the knobs; the footer offers its length, its sounds, the nudge and the lock pages" title="A step held">

- **Hold SEQ and play keys** to choose what a step writes: the keys play the track, and the ones you hold
  together become the remembered notes (a chord). On a drum track, the key you play becomes the sound shown.
  Let go of **SEQ** and nothing else changes.
- **Press a white key** to put the remembered notes on that step, or to clear the step.
- **Hold a white key** to see the step. **KNOB 1** is its velocity, **KNOB 2** its length in steps, **KNOB 3**
  its chance (always, a percentage, then the A:B cycles: on the A-th of every B passes) and **KNOB 4** its
  nudge. **OCT−** and **OCT+** nudge it too.
- **Hold a step and press a later white key** to make its note reach that step.
- **Hold a step and press a black key** on a drum track to put that sound on the step, or take it off.
- **Hold a step and press SEL** for a list of its notes. Pick one and press **SEL** to take it off.

### Tricks

Hold **HOME** and hold a step. The white keys are now the fourteen tricks, and the
black keys are their values. Press a white key to add its trick; press it again to take it off. A step
holds two; a third replaces the second. While you hold, the knob strip shows the step's two tricks and
their values, and the white keys of the ones it has are lit.

| White key | Trick | Black keys 1 to 9, 10 = random |
| --- | --- | --- |
| 1 | pulse: repeat the step without moving on | 1 to 9 times |
| 2 | hold: keep the note over the next steps | 1 to 9 steps |
| 3 | multiply: retrigger the step | 1 to 9 hits |
| 4 | velocity | 4, 8, 16, 32, 64, 100, 112, 127, 0 |
| 5 | ramp up: the note climbs the scale each pass | 2 to 6 steps over one octave, then over three |
| 6 | ramp down | the same |
| 7 | random: another note of the scale each pass | the same |
| 8 | portamento: glide into the note | 10 % to 80 %, then off |
| 9 | bend | down-up, up-down, bumps, springs, fades, random 1 and 2 |
| 10 | tonality: how the step takes the conductor | ignore, transpose only, octave up, fifth up, third up, chromatic up, chromatic down, quantize 33 %, 66 %, 100 % |
| 11 | jump: move the playhead | to step 1, 5, 9, 13, forward, back, either, stay, align |
| 12 | skip lock: the locks play every Nth pass | a cycle of 1 to 9 |
| 13 | skip trick: the other trick plays every Nth pass | a cycle of 1 to 9 |
| 14 | skip trigger: the step plays every Nth pass | a cycle of 1 to 9 |

For the three skips, black key 11 cycles which pass of the cycle plays: the last, all but the last, or only
the first. A new trick starts at its default: pulse and hold 4, multiply 2, velocity 64, the
ramps and random 4, portamento 40 %, bend down-up, tonality fifth up, jump forward, the skips a cycle of 2.

A cell shows its notes, or a chord's name with its bass note under it. On a drum track, every sound on the
step has a tick, the shown sound's bright, so the whole beat is visible while you edit one sound. A bar along
the foot of the cells shows how far a long note reaches; an arrow at the bar's end means it goes on. The
tricks' letters sit at the bottom right, a percent sign marks a chance, and a red dot marks locks.

### Locks

Hold a step and press **EDIT**, **ENV**, **FX** or **LFO**. The knob strip becomes that module's four knobs
for the held step (turn **SELECT** for its other pages: **ENV** has the envelope, the filter and the filter's
envelope; **FX** the sends), showing "–" where the step has no lock and the locked values in red. Turn a knob to lock
a value on the step. Let go of the step to go back. Hold a step and press **SAVE** to clear its locks. While
recording, a knob you turn writes locks on the steps as they play. The Bar page's SHAPE knob smooths between
locks.

<img src="img/screen-lock.png" width="240" alt="Step 1 held with ENV&#x27;s filter page: the cutoff locked at 481 Hz in red; the other knobs show a dash, unlocked" title="A lock">

### The Steps knobs and the Bar page

With no step held, **KNOB 1** picks the pattern (P1 to P8), **KNOB 2** the length in steps, **KNOB 3** the
track scale (how many sixteenths a step takes, from 1/2 to 16) and **KNOB 4** the track's groove amount.

<img src="img/screen-list.png" width="240" alt="SEL on Steps: quantise, note length and shape, then clear notes, clear locks, copy, rotate and duplicate" title="The Steps list">

Turn **SELECT** for the Bar page: quantise for live recording, the note length of new steps as a percentage
of a step, the groove amount and the shape. The black keys set the track scale: 1, 2, 3, 4, 5, 6, 8, 16
sixteenths a step on black keys 1 to 8, half a sixteenth on black key 9. Press **SEL** on either page for the
actions: clear notes, clear locks, clear both, copy and paste the pattern, rotate it a step forward or back,
duplicate the bar into the next. Once a pattern is copied, the list also has WEAVE WITH COPIED.

### Weave

Weave crosses the pattern you are on with the one you copied. Copy a pattern (**SEL**, COPY PATTERN), go to
another (**KNOB 1**), press **SEL** and pick WEAVE WITH COPIED. Turn **ALGORITHM** for how, then press
**SEL**:

| Way | What this pattern becomes |
| --- | --- |
| bars | its bars 1 and 3, the copy's bars 2 and 4 |
| steps | its odd steps, the copy's even steps |
| rhythm | its rhythm, playing the copy's notes in their order, round and round |
| notes | the copy's rhythm, playing this pattern's notes |

With bars and steps the shorter pattern loops to the longer one's length. The pattern keeps its own scale,
groove and note length, and its locks. Not what you wanted? Hold **HOME** and press **REC** to undo, then try
another way. Weave a pattern with a copy of itself shifted (copy, ROTATE FORWARD, weave by steps) for
variations; weave a drum pattern's rhythm with a melody's notes for a riff.

<img src="img/screen-weave.png" width="240" alt="The Steps list with WEAVE WITH COPIED picked, its way set to rhythm" title="Weave"> <img src="img/screen-woven.png" width="240" alt="T3&#x27;s pattern 2 after weaving by rhythm: its steps 3, 4, 8 and 11 now play pattern 1&#x27;s chords in turn, C and Dm" title="Woven">

## Recording

- **Live recording**: hold **REC** and press **PLAY**. Step 1 blinks red; recording starts with the first key
  you play, and the keys write steps as you play, quantised by the Bar page. Press **PLAY** to stop.
- **While playing**: hold **REC** and play. It records only while you hold **REC**. Hold **REC** and press
  **PLAY** to keep it on; press **REC** again to turn it off.
- **Step by step**: stopped, tap **REC**. On Steps the keys become a keyboard and a red cursor marks the
  step they write, starting after the pattern's last note. A key, or keys held together, writes the step and
  the cursor moves on when you let go. **OCT+** leaves a rest, **OCT−** goes back a step. On a drum track the
  sound is added to the step. Tap **REC** again to stop. It works on the sound pages too, with a strip of the
  bar along the foot of the screen.
- **Throws** you hold while recording are written into the THROW track's pattern.
- **Count-in**: Setup's list has COUNT-IN. On, live recording starts with a bar of clicks instead of waiting
  for the first key.

<img src="img/screen-step-record.png" width="240" alt="REC tapped on Steps: the red cursor waits on the step after the last note" title="Step record armed">

Hold **HOME** and press **REC** to undo the last change to the pattern. Hold **HOME** and press **PLAY** to
redo it. One stage, the selected track's pattern; locks stay as they are.

## Sounds

Press **EDIT**. The Engine page shows the track's engine, a picture of it that moves with the knobs, and its
four knobs with their values from 00 to 99. Turn **PRESETS** for the engine's next
sound: its factory presets, then the user presets saved from it; the message says where you are, as in
"PRESET 3/8". Press **SEL** for the list: ENGINE changes the engine and loads its first sound, then a preset,
SCRAMBLE for a random sound, SAVE AS PRESET, RESET SOUND, and whether the track FOLLOWS CONDUCTOR.

<img src="img/screen-engine.png" width="240" alt="Engine on T2: stack, its picture, the preset name, and its four knobs shape, ratio, detune, stereo from 00 to 99" title="The Engine page"> <img src="img/screen-play.png" width="240" alt="Play: voice mode, portamento, bend range and volume" title="The Play page">

| Engine | Knobs |
| --- | --- |
| simple | shape, pw, noise, stereo |
| stack | shape, ratio, detune, stereo |
| wavetable | table, position, warp, drift |
| orbit | tone, ratio, shape, tremolo |
| epiano | tone, texture, punch, tine |
| organ | type, bass, tremolo, speed |
| sync | freq, sub, noise, lowcut |
| swarm | swarm, am, fm, detune |
| kit | the shown key's model knobs |

### Kit

A drum track has eleven keys on the black keys, each with a drum model: kick, snare, hi-hat, clap, shaker,
tambourine, resonator, congas, tom or fm. Press a black key, or turn **PRESETS**, to pick the key the
knobs edit. The Engine page's four knobs are the model's own:

<img src="img/screen-drum-engine.png" width="240" alt="Engine on T1 kit: the eleven keys as pads, the snare picked; its knobs pitch, length, snare, punch" title="A drum key&#x27;s knobs"> <img src="img/screen-drum-key.png" width="240" alt="The Key page: the snare&#x27;s model, random, pan and volume" title="The Key page"> <img src="img/screen-drum-envelope.png" width="240" alt="ENV on a drum track: funk, attack, random and length for the whole kit" title="The drum envelope">

| Model | Knobs |
| --- | --- |
| kick | pitch, length, punch, tone |
| snare | pitch, length, snare, punch |
| hi-hat | pitch, length, era, tone |
| clap | tone, length, spread, contour |
| shaker | color, length, attack, onset |
| tambourine | tone, length, shake, ring |
| resonator | pitch, length, tone, strike |
| congas | pitch, length, slap, mush |
| tom | pitch, length, sweep, rate |
| fm | pitch, length, mod, feedback |

On every sound page of a drum track, a black key picks the key the knobs edit. Turn **SELECT** for the
key's page: the model, random (a variation on every hit), pan and volume in dB; its **SEL** list has the play
mode: one-shot, gated, loop, or choke groups 1 to 3. **ENV** on a drum track is the kit's envelope: funk
(a delay before the hit), attack, random and length. Its Filter page with the dynamic type is low cut,
compression, drive and hi cut.

### The synth pages

Turn **SELECT** on Engine for Play: poly, mono or legato, portamento, the bend range (off, 1 to 7
semitones, an octave) and the track's volume.

<img src="img/screen-envelope.png" width="240" alt="ENV: the amp envelope&#x27;s attack, decay, sustain and release, drawn" title="The Envelope page"> <img src="img/screen-filter.png" width="240" alt="ENV&#x27;s Filter page: a ladder filter at 702 Hz, its curve drawn; PRESETS turns the type" title="The Filter page"> <img src="img/screen-lfo.png" width="240" alt="LFO: a value LFO at 1/4 moving the filter cutoff, its shape drawn" title="The LFO page">

**ENV** has the amp envelope, then the filter: cutoff in Hz, resonance, envelope amount and key tracking,
with **PRESETS** turning the type (svf, ladder, z lowpass, z hipass, dynamic); then the filter's envelope.
**FX** has the track's sends to FX I, FX II and the tape, and its level, then the two effects. **LFO** has the
type and its first three knobs; page 2 its fourth and fifth. A new type starts from its own defaults:

| Type | Page 1 | Page 2 |
| --- | --- | --- |
| value | speed, amount, destination | parameter, shape |
| random | speed, amount, destination | parameter, envelope |
| tremolo | rate, vibrato, volume | envelope, shape |
| duck | source, amount, hold | release |

The speed knob is synced from 2/1 to 1/64 turned left, and free in Hz turned right. A destination with
"free" after it runs on without restarting on each note.

## Players

Press **ARP**. **KNOB 1** picks the player: off, arp, strum or hold.

<img src="img/screen-players.png" width="240" alt="Players: the arp at 1/16, up, one octave, with C4 E4 G4 held" title="The arp">

- **arp** plays the held notes one after another: speed (1/4 to 1/64, with triplets), pattern (up, down, up
  down 1, up down 2, as played, random) and range on page 1; page 2 has the note length, a style, glide and
  stereo; page 3 is hold.
- **strum** plays a chord from one key. Press **REC**, then hold the chord, up to eight keys: it is
  recorded. From then on any key plays it, moved to that key. Roll strums it, up to one note per step.
- **hold** keeps the notes you play until the next.

## Arrange, scenes and the song

Press **HOME**. Each row is a track with its pattern and a playhead; muted tracks are dimmed. The scene's
number is at the right, with the next one when a scene is cued. The 16 white keys are the 16 scenes:
press one to switch at once. Hold **HOME** and press one to cue it for the next bar. An empty scene
becomes a copy of the scene playing. Hold a white key for a second to clear that scene. The black keys
mute: 1 to 8 the tracks, 9 to 11 conductor, throws and tape. Hold **HOME** and turn **PRESETS** to change scenes
from any page.

<img src="img/screen-song.png" width="240" alt="The Song page: S1, S1 and S2 in a row, looping" title="The song">

**KNOB 1** is the tempo (hold **HOME** for tenths), **KNOB 2** the groove type (shuffle, half shuffle,
danish, bombora, wobbly, gaussian, accents, island nod, disfunk, rollover, prophetic), **KNOB 3** the
selected track's groove amount from −99 to 99, and **KNOB 4** the click: off, while recording, or on.

Turn **SELECT** for the Song page. Press a white key to add that scene at the cursor. **KNOB 1** moves the
cursor, **KNOB 2** changes the slot's scene, **KNOB 3** how many times it plays, and **KNOB 4** whether the
song loops or stops at the end. **PLAY** on the Song page plays the song from slot 1. **SEL** inserts the
scene playing, deletes a slot or clears the song.

## The aux tracks

Turn **ALGORITHM** past T8. Each aux track has its own page under **EDIT** and its own steps under **SEQ**.

<img src="img/screen-tape.png" width="240" alt="TAPE&#x27;s page: the sixteen segments in a ring; pitch, speed, length and mix" title="The tape"> <img src="img/screen-fx1.png" width="240" alt="FX&#x27;s FX I page: the delay with size, fine, feedback and dry" title="An effect">

- **COND** (the conductor): mode (manual, or auto: the key is detected from the tracks that follow), key, scale and link,
  an instrument track that also plays the conductor's keys. Play a chord on its keys: the tracks that follow
  move to it. Its steps hold the chords you played: press **SEQ** and a white key. Set the track scale to 4
  so a step is a beat, or 16 for a bar. Page 2 is the routing: black keys 1 to 8 toggle a track. Its
  **SEL** list sets how each track follows, diatonic or transpose only. Drum tracks never follow.
- **THROW** (the throws): the keys fire the throws on the drum tracks (lower octave) and the melodic tracks
  (upper octave). The last one you played is the one a step writes. **KNOB 1** is latch: a key then
  toggles its effect.

| Key of the octave | Effect |
| --- | --- |
| white 1 | duck: silences the group |
| white 2 | loop 1: a fast loop from where the throw starts |
| white 3 | loop 2: a slow loop |
| white 4 | echo: the loop repeats and fades |
| white 5 | fill 1 |
| white 6 | fill 2 |
| white 7 | random fill |
| black 1 | filter: high-passed drums, vibrato on melodic notes |
| black 2 | stereo: the sounds move across the field |
| black 3 | pitch: drums an octave up, melodic tracks an octave down |
| black 4 | short notes |
| black 5 | long notes |

  On any other page, hold **HOME** and press a key: the lower octave works on the track you are on, the
  upper octave on its whole group.

- **TAPE**: a loop of the last 16 steps of the tracks that send to it. A white key loops that segment while
  held, and the last one you played is the one a step writes. Pitch in octaves, speed from 50 % to 200 %,
  length in steps and mix on the knobs.
- **FX I** and **FX II**: a send effect each: chorus, delay, distortion, lofi, phaser or reverb, with its
  four knobs. Press **FX** and turn **SELECT** to reach them from any track: **PRESETS** picks the effect, and
  **SEL** has its high-pass, low-pass and, on FX I, the send into FX II. The keys play the track you were on.
  Turned to with **ALGORITHM**, they have the same knobs on **EDIT**.

## Mix and master

Press **GLO**. The Mix page shows a meter for each track; the black keys pick one, and hold one to solo it.
Hold **HOME** and press a black key to mute. The knobs are the sends to FX I and FX II, the pan and the
level. Turn **SELECT** for Master EQ (low, mid, high and a blend from flat to the EQ), Master drive (gain,
clip, tone, blend), and Master out (the drums' level, the melodic tracks' level, the compressor and the
output level). Setup has the lights, MIDI clock and notes out, and one or two cores; its **SEL** list loads
a project, starts a new one, and sets the USB audio level and the count-in. Status shows the audio load, the
voices and the second core.

<img src="img/screen-mix.png" width="240" alt="Mix: a meter for each track while playing; knobs fx I, fx II, pan and level" title="The mixer"> <img src="img/screen-sends.png" width="240" alt="FX&#x27;s Sends page: the track&#x27;s sends to FX I (delay), FX II (reverb) and the tape, and its level" title="The sends"> <img src="img/screen-master.png" width="240" alt="Master EQ: low, mid, high and the blend from flat to the EQ" title="Master EQ"> <img src="img/screen-status.png" width="240" alt="Status: audio load, peak, voices, the second core, guard trips and dropouts" title="Status">

## Saving

Press **SAVE** to save the project to its slot. Hold **SAVE** and press white key 1 to 4 to save it to that
slot; LOOM-8 asks first if the slot holds another project. The header says "unsaved" and **SAVE** lights
while there are changes to save. LOOM-8 does not save by itself.

On a sound page, hold **SAVE** and press white key 1 to 16 to keep the track's sound as that user preset.

## Page by page

| Button | Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | Keys |
| --- | --- | --- | --- | --- | --- | --- |
| HOME | Arrange | tempo | groove | amount | click | white: scenes, black: mutes |
| HOME, SELECT | Song | slot | scene | repeat | at end | white: add a scene |
| SEQ | Steps | pattern | length | scale | groove | white: steps, black: sound (drums); SEQ held: a keyboard |
| SEQ, a step held | Steps | velocity | length | chance | nudge | white: length, black: sounds |
| SEQ, HOME + a step | Steps | the tricks and values shown | | | | white: trick, black: value |
| SEQ, SELECT | Bar | quant | note | groove | shape | black: scale |
| EDIT | Engine | the engine's four | | | | play |
| EDIT, SELECT | Play | mode | porta | bend | volume | play |
| EDIT, drum track | Engine, Key | the key's knobs; model, random, pan, volume | | | | black: the key |
| ENV | Envelope | attack | decay | sustain | release | play |
| ENV, SELECT | Filter | cutoff | reso | env | keytrack | play; PRESETS: the type |
| ENV, SELECT twice | Filter env | attack | decay | sustain | release | play |
| FX | Sends | fx I | fx II | tape | level | play |
| FX, SELECT | FX I, FX II | the effect's four | | | | play; PRESETS: the effect |
| LFO | LFO | type | the type's three | | | play |
| LFO, SELECT | LFO 2 | the type's fourth and fifth | | | | play |
| ARP | Players | player | its first | its second | its third | play, through the player |
| ARP, SELECT | Players 2, Hold | the arp's length, style, glide, stereo; hold | | | | play |
| GLO | Mix | FX I | FX II | pan | level | black: the track, held: solo |
| GLO, SELECT | Master EQ | low | mid | high | blend | |
| GLO, SELECT | Master drive | gain | clip | tone | blend | |
| GLO, SELECT | Master out | perc | mel | comp | level | |
| GLO, SELECT | Setup | lights | clock | notes | cores | |
| GLO, SELECT | Status | | | | | |

The aux tracks' Engine page: COND has mode, key, scale and link, and page 2 the routing; THROW has latch;
TAPE has pitch, speed, length and mix; FX I and FX II have the effect's four knobs, and on page 2 hp, lp
and the send to FX II.

## Screen reader

Connect the FM-1 by USB and open https://charlesvestal.github.io/fm1-reader/ in Chrome or Edge. LOOM-8 says
each page, knob value, step, scene and message as it changes, and the whole screen when you ask. The words
it uses are published with this site, so every version reads its own screen correctly.

## Credits

LOOM-8's way of working owes a great deal to teenage engineering's OP-XY; LOOM-8 is not affiliated with
teenage engineering. Built on Felucca (Hügelton Instruments, GPL-3.0-only), with
Polly's menus, X0X's sequencer discipline and NuSaw's second core.
