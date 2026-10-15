# XY-1 manual

XY-1 turns the M-VAVE FM-1 into a pocket groovebox, inspired by the Teenage Engineering OP-XY. Eight tracks
with synth engines and a drum synth, a 64-step sequencer with step components and parameter locks, players,
punch-in effects, a brain that moves the song through a progression, a tape loop, two send effects, a mixer
and master section, sixteen scenes and a song.

The loop XY-1 is made for: write a short pattern, add a few components, let the brain play a progression,
season it with punch-in effects while it plays, then put the scenes in order.

- [Cheat sheet](#cheat-sheet)
- [Getting around](#getting-around)
- [Steps](#steps)
- [Sounds](#sounds)
- [Players](#players)
- [Arrange, scenes and the song](#arrange-scenes-and-the-song)
- [The aux tracks](#the-aux-tracks)
- [Mix and master](#mix-and-master)
- [Saving](#saving)
- [Page by page](#page-by-page)
- [Screen reader](#screen-reader)

## Cheat sheet

### The words

- **Track** (T1 to T8): one sound and its patterns. After T8 come the aux tracks: BRAIN, PUNCH, TAPE, FX I
  and FX II.
- **Pattern** (P1 to P8): a track's steps, up to 64 of them in four bars. Each track has eight.
- **Step**: one cell of a pattern. It holds up to four notes, a component, a chance and parameter locks.
- **Component**: a trick a step plays, such as pulse, hold, multiply or jump. The eleven black keys are
  the eleven components.
- **Lock**: a knob value a step sets as it plays.
- **Scene** (S1 to S16): which pattern each track plays, plus the mutes and the brain's key.
- **Song**: a chain of scenes, each played a number of times.
- **Engine**: the way a track makes its sound. Eight synths and the drumline drum synth.
- **Preset**: a sound to load. 64 come with XY-1, and 16 are yours.
- **Player**: what the keys do on a track: arp, maestro or hold.
- **Brain**: the aux track that moves the other tracks through a key and scale.
- **Punch-in**: a momentary effect on the drums or the melodic tracks, from a key.

### How do I…

White keys are numbered from the left, 1 to 16, and black keys the same way, 1 to 11. "Hold **HOME** and
press…" means keep **HOME** down while you press the next thing.

| How do I… | Do this |
| --- | --- |
| pick a track | Turn **ALGORITHM** on any page. On Steps, press black key 1 to 8. |
| write a beat | Press **SEQ**. On a drum track, black key 1 is the kick: press it, then white keys 1, 5, 9 and 13. Black key 2 is the snare. |
| write a bass note | Press **EDIT** and play the note. Press **SEQ** and press a white key: the note goes on that step. |
| write a chord | Hold the notes together on a play page, then on Steps press a step. |
| see a step | On Steps, hold its white key. The knobs show its velocity, length, chance and nudge. |
| make a note longer | Hold its step and press a later white key. |
| add a component | Hold the step and press a black key. Black key 3 is multiply, 1 is pulse, 11 is skip. Turn **KNOB 4** for its value. Press the same black key again to take it off. |
| give a step a chance | Hold the step and turn **KNOB 3**. Past 99 % come the A:B cycles. |
| lock a knob on a step | Hold the step and press **EDIT**, **ENV**, **FX** or **LFO**. Turn a knob. **SAVE** clears the step's locks. |
| see the next bar | Press **OCT+** on Steps. |
| change the pattern | On Steps, turn **KNOB 1**. |
| change the sound | Press **EDIT** and turn **PRESETS**. **SEL** lists the engines, scramble and reset. |
| play | Press **PLAY**. Press it twice quickly to start from the top. |
| record live | While playing, press **REC** and play the keys. **REC** again stops recording. |
| record step by step | Stopped, press **REC**. Each key you play writes the cursor's step and moves on. |
| record with a count-in | Stopped, press **REC**, then **PLAY**. |
| undo | Hold **HOME** and press **REC**. Hold **HOME** and press **PLAY** to redo. |
| set the tempo | Press **HOME** and turn **KNOB 1**. On any page, hold **HOME** and turn **SELECT**. |
| use a punch-in effect | Hold **HOME** and press a key. The low twelve keys work on the drums, the upper fifteen on the melodic tracks. |
| make a new scene | Press **HOME** and press an empty white key. It becomes a copy of the scene playing. |
| mute a track | On Arrange, press its black key. |
| play a progression | Turn **ALGORITHM** to BRAIN. Press **SEQ** and write degrees on its steps: black key 6 is 0, 7 is +1. Press **EDIT** and set the key and scale. Tracks follow it when "FOLLOWS BRAIN" in their Engine list is on. |
| save | Press **SAVE**. Hold **SAVE** and press white key 1 to 4 to save to that slot. |
| keep a sound | On a sound page, hold **SAVE** and press a white key. The sound becomes that user preset. |
| load a project | Press **GLO**, turn **SELECT** to Setup, press **SEL** and pick LOAD PROJECT. |

### The buttons

| Control | What it does |
| --- | --- |
| **PLAY** | Starts and stops. Twice quickly: from the top. On the Song page, it plays the song. |
| **REC** | Records. Playing: live, as you play. Stopped: step by step. On Players with maestro, it records the chord. |
| **SEL** | Opens a list of this page's settings and actions. When XY-1 asks a question, **SEL** means yes. |
| **HOME** | Opens Arrange. On Arrange, it opens the Song page. Hold it for the shift layer. When XY-1 asks a question, **HOME** means no. |
| **SEQ** | Opens Steps. Its page 2 is Bar. |
| **EDIT** | Opens Engine. Its page 2 is Play. |
| **ENV** | Opens Envelope. Its page 2 is the filter envelope. |
| **FX** | Opens Filter. Its page 2 is Sends. |
| **LFO** | Opens LFO. Its page 2 has the LFO type's own setting. |
| **ARP** | Opens Players. Its page 2 has hold. |
| **GLO** | Opens Mix. Turn **SELECT** for Master EQ, Master drive, Master out, Setup and Status. |
| **SAVE** | Saves. Hold it and press a white key to save to a slot, or to keep a sound. |
| **OCT−** / **OCT+** | Move the keys an octave. On Steps, show the bar before or after. In step record, move the cursor. |
| **SELECT** | Turns to a button's next page. In a list, it moves up and down. |
| **ALGORITHM** | Picks the track. In a list, it changes the row's value. |
| **PRESETS** | Steps to the next item of whatever the page shows: a preset, a pattern, a drum sound, a scene. |
| **KNOB 1–4** | Set the four values at the bottom of the screen. |

Hold any button for a second, and a card explains it. The two lines at the bottom of the screen always say
what each control does on the page you're on.

## Getting around

### The screen

From top to bottom, the screen has five parts.

- **The header** shows the page's button and name, the tempo, a coral dot while recording, and "unsaved".
  Messages and questions cover it for a moment.
- **The middle** is the page itself.
- **The tag line** says where you are: the track and its engine, the pattern and the scene, such as
  "T3 prism › P2 › S1". The dots at the right say which of the button's pages you're on.
- **The knob strip** shows the four knobs' names and values. A grey swatch beside each name gives the
  knob's shade, from dark to white, the way the OP-XY's manual names its knobs. The knob you just turned
  shows coral.
- **The two bottom lines** say what the encoders and the keys do here.

Teal is what sounds now: the playhead, the step playing, the scene playing. Coral is you: the held step, the
cursor, the knob you turned, REC. Ink and its greys are everything that stays put.

### Messages

Short messages cover the header for a moment. **i** is news, **✓** is something done, an arrow is an undo
or redo, **?** is a question that waits for **SEL** or **HOME**, and **!** means XY-1 could not do it and
says why.

### The tracks

Turn **ALGORITHM** to pick a track: T1 to T8, then BRAIN, PUNCH, TAPE, FX I and FX II. The keys play the
track you picked on the Engine, Play, Envelope, Filter, LFO and Players pages. On a drum track, the black
keys are the eleven sounds, and the white keys play them too.

### The lights

**PLAY** is green while playing and red while recording. **REC** lights while recording or armed. **SAVE**
lights when there is something to save. The page's button is lit. On Steps, the steps with notes glow and the
step playing is bright. On Arrange, the scenes in use glow and the one playing is bright. The muted tracks'
black keys glow. With Setup's LIGHTS on, the other buttons glow too.

## Steps

Press **SEQ**. The main band shows one bar of the pattern as 16 cells, and the 16 white keys are those
cells. The top line says what a press writes: on a drum track, the sound shown; on a synth track, the
remembered notes, which are the last keys you held together on a play page. The four squares at the right
are the bars. **OCT−** and **OCT+** show the bar before or after.

- **Press a white key** to put the remembered notes on that step, or to clear the step.
- **Hold a white key** to see the step. **KNOB 1** is its velocity, **KNOB 2** its length in steps, **KNOB 3**
  its chance (always, a percentage, then the A:B cycles: on the A-th of every B passes) and **KNOB 4** its
  nudge, or the component's value once it has one.
- **Hold a step and press a later white key** to make its note reach that step.
- **Hold a step and press a black key** to add a component. The same black key takes it off again.

| Black key | Component | KNOB 4 |
| --- | --- | --- |
| 1 | pulse: repeat the step without moving on | 1 to 9 times, random |
| 2 | hold: keep the note over the next steps | 1 to 9 steps, random |
| 3 | multiply: retrigger the step | 1 to 9 hits, random |
| 4 | velocity | 4, 8, 16, 32, 64, 100, 112, 127, 0, random |
| 5 | ramp up: the note climbs the scale each pass | 2 to 6 degrees, an octave or three |
| 6 | ramp down | the same |
| 7 | random: another note of the scale each pass | 2 to 6 degrees, an octave or three |
| 8 | portamento: glide into the note | 10 % to 90 %, random |
| 9 | bend | down-up, up-down, bumps, springs, fades, random |
| 10 | jump: move the playhead | to step 1, 5, 9, 13, forward, back, either, stay, align, random |
| 11 | skip: play the step every Nth pass | 1st to 9th, random |

The cell shows the notes, the component's letter at the bottom right, a percent sign when the step has a
chance, and a coral dot when it has locks.

### Locks

Hold a step and press **EDIT**, **ENV**, **FX** or **LFO**. The knob strip becomes that page's four knobs
for the held step, showing "–" where the step has no lock. Turn a knob to lock a value on the step. Let go
of the step to go back. Hold a step and press **SAVE** to clear its locks. While recording live, a knob you
turn writes locks on the steps as they play. The Bar page's SHAPE knob smooths between locks.

### The Steps knobs and the Bar page

With no step held, **KNOB 1** picks the pattern (P1 to P8), **KNOB 2** the length in steps, **KNOB 3** the
track scale (how many sixteenths a step takes, from 1/2 to 16) and **KNOB 4** the swing.

Turn **SELECT** for the Bar page: quantise for live recording, the note length of new steps, the groove and
the shape. Press **SEL** on either page for the actions: clear notes, clear locks, clear both, copy and
paste the pattern, rotate it a step forward or back, duplicate the bar into the next.

### Recording

While playing, press **REC**: the keys write steps as you play, quantised by the Bar page. Press **REC** again
to stop. Stopped, press **REC** for step record: the cursor is on step 1, and every key you play on a play
page writes that step and moves the cursor on. **OCT−** and **OCT+** move the cursor. Press **PLAY** while
armed for a count-in, then recording.

Hold **HOME** and press **REC** to undo the last change to the pattern. Hold **HOME** and press **PLAY** to
redo it. One stage, the selected track's pattern; locks stay as they are.

## Sounds

Press **EDIT**. The Engine page shows the track's engine, a picture of it, and its four knobs. Turn
**PRESETS** for the next sound: the factory presets, then your user presets. Press **SEL** for the list:
pick an engine, pick a preset, SCRAMBLE for a random sound, SAVE AS PRESET, RESET SOUND, and whether the
track FOLLOWS BRAIN.

| Engine | Knobs |
| --- | --- |
| simple | shape, pw, noise, stereo |
| prism | shape, ratio, detune, stereo |
| wavetable | table, position, warp, drift |
| axis | tone, ratio, shape, tremolo |
| epiano | tone, texture, punch, tine |
| organ | type, bass, tremolo, speed |
| hardsync | freq, sub, noise, lowcut |
| dissolve | swarm, am, fm, detune |
| drumline | model, tune, tone, decay of the sound shown |

On a drumline track, the black keys play the eleven sounds and pick the one the knobs edit. The sounds are
kick, snare, hat, open hat, clap, low tom, high tom, conga, shaker, fm and resonator.

Turn **SELECT** for Play: poly, mono or legato, portamento, bend range and the track's volume.

**ENV** has the amp envelope, and on page 2 the filter envelope. **FX** has the filter: cutoff, resonance,
envelope amount and key tracking; page 2 has the filter type (LP12, LP24, HP, BP) and the sends to FX I,
FX II and the tape. **LFO** has the type (value, tremolo, random, duck), speed, amount and destination;
page 2 has the type's own setting: the shape, the vibrato, the smoothing, or the duck's source track.

## Players

Press **ARP**. **KNOB 1** picks the player: off, arp, maestro or hold.

- **arp** plays the held notes one after another: speed, pattern and range on the knobs, hold on page 2.
- **maestro** plays a chord from one key. Press **REC**, then hold the chord: it is recorded. From then on
  any key plays it, moved to that key.
- **hold** keeps the notes you play until the next.

## Arrange, scenes and the song

Press **HOME**. Each row is a track with its pattern and a playhead; muted tracks are dimmed. The scene's
number is at the right, with the next one when a scene is cued. The 16 white keys are the 16 scenes:
press one to switch, at the next bar while playing. An empty scene becomes a copy of the scene playing.
Hold a white key for a second to clear that scene. The black keys mute: 1 to 8 the tracks, 9 to 11 brain,
punch and tape. Hold **HOME** and turn **PRESETS** to change scenes from any page.

**KNOB 1** is the tempo, **KNOB 2** the swing, **KNOB 3** the groove and **KNOB 4** the click: off, while
recording, or on.

Turn **SELECT** for the Song page. Press a white key to add that scene at the cursor. **KNOB 1** moves the
cursor, **KNOB 2** changes the slot's scene, **KNOB 3** how many times it plays, and **KNOB 4** whether the
song loops or stops at the end. **PLAY** on the Song page plays the song from slot 1. **SEL** inserts the
scene playing, deletes a slot or clears the song.

## The aux tracks

Turn **ALGORITHM** past T8. Each aux track has its own page under **EDIT** and its own steps under **SEQ**.

- **BRAIN**: mode (manual, or auto: the key is detected from the tracks that follow), key, scale and link.
  The keys move the followers by degrees of the scale. Its steps hold degrees: on Steps, press black key 6
  for 0, 7 for +1, 5 for −1, then a white key. Set its track scale to 4 so a step is a beat, or 16 for a bar.
  Which tracks follow, and how (diatonic or transpose only), is in its **SEL** list. Drum tracks never
  follow.
- **PUNCH**: the keys fire the punch-in effects directly, and its steps play them in time. The low twelve
  keys are the drum effects (mute, stutter, fill, pan, half fill, octave up, echo, kick and snare fill,
  short, hats fill, long tails, random sound), the upper fifteen the melodic ones (mute, stutter, loop 4,
  pan, octave up, octave down, delay up, reverb up, filter down, filter up, ramp, random notes, half time,
  double time, reverse). **KNOB 1** is latch: a key then toggles its effect. On any other page, hold **HOME**
  and press a key for the same effects.
- **TAPE**: a loop of the last 16 steps of the tracks that send to it. A white key loops that segment while
  held. Pitch, speed, length and mix on the knobs.
- **FX I** and **FX II**: a send effect each: chorus, delay, distortion, lofi, phaser or reverb, picked with
  **KNOB 1** or **PRESETS**, with its first three knobs. Page 2 has the fourth knob, a high-pass, a low-pass
  and, on FX I, the send into FX II. The keys play T1 so you can hear the effect.

## Mix and master

Press **GLO**. The Mix page shows a meter for each track; the black keys pick one, and the knobs set its
level, pan and sends to FX I and FX II. Turn **SELECT** for Master EQ (low, mid, high and a blend from
flat to the EQ), Master drive (gain, clip, tone, blend), and Master out (the drums' level, the melodic
tracks' level, the compressor and the output level). Setup has the lights, MIDI clock and notes out, and
one or two cores; its **SEL** list loads a project, starts a new one, and sets the USB audio level. Status
shows the audio load, the voices and the second core.

## Saving

Press **SAVE** to save the project to its slot. Hold **SAVE** and press white key 1 to 4 to save it to that
slot; XY-1 asks first if the slot holds another project. The header says "unsaved" and **SAVE** lights
while there are changes to save. XY-1 does not save by itself.

On a sound page, hold **SAVE** and press white key 1 to 16 to keep the track's sound as that user preset.

## Page by page

| Button | Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | Keys |
| --- | --- | --- | --- | --- | --- | --- |
| HOME | Arrange | tempo | swing | groove | click | white: scenes, black: mutes |
| HOME, SELECT | Song | slot | scene | repeat | at end | white: add a scene |
| SEQ | Steps | pattern | length | scale | swing | white: steps, black: component, sound or track |
| SEQ, a step held | Steps | velocity | length | chance | nudge or the component's value | black: component, white: length |
| SEQ, SELECT | Bar | quantise | note length | groove | shape | as Steps |
| EDIT | Engine | the engine's four | | | | play |
| EDIT, SELECT | Play | mode | porta | bend | volume | play |
| ENV | Envelope | attack | decay | sustain | release | play |
| ENV, SELECT | Filter env | attack | decay | sustain | release | play |
| FX | Filter | cutoff | reso | env | keytrack | play |
| FX, SELECT | Sends | type | fx I | fx II | tape | play |
| LFO | LFO | type | speed | amount | dest | play |
| LFO, SELECT | LFO 2 | the type's setting | | | | play |
| ARP | Players | player | its first | its second | its third | play, through the player |
| ARP, SELECT | Players 2 | hold | | | | play |
| GLO | Mix | level | pan | FX I | FX II | black: the track |
| GLO, SELECT | Master EQ | low | mid | high | blend | |
| GLO, SELECT | Master drive | gain | clip | tone | blend | |
| GLO, SELECT | Master out | perc | mel | comp | level | |
| GLO, SELECT | Setup | lights | clock | notes | cores | |
| GLO, SELECT | Status | | | | | |

The aux tracks' Engine page: BRAIN has mode, key, scale and link; PUNCH has latch; TAPE has pitch, speed,
length and mix; FX I and FX II have the effect and its knobs, and on page 2 the fourth knob, hp, lp and the
send to FX II.

## Screen reader

Connect the FM-1 by USB and open https://charlesvestal.github.io/fm1-reader/ in Chrome or Edge. XY-1 says
each page, knob value, step, scene and message as it changes, and the whole screen when you ask. The words
it uses are published with this site, so every version reads its own screen correctly.

## Credits

Inspired by the Teenage Engineering OP-XY. Built on Felucca (Hügelton Instruments, GPL-3.0-only), with
Polly's menus, X0X's sequencer discipline and NuSaw's second core.
