# LOOM-8's presets: the classic sounds behind them

LOOM-8 has 224 factory presets, sixteen for each engine. This note says what each engine does well, which
classic sounds it can make, and how the presets are told apart. The presets themselves are in
`app/dsp/synth.c` (PRESETS[]), with a comment above each engine naming each preset's inspiration. Those
names appear only in the comments. No preset is named after a product or a trademark.

## What makes a classic recognisable

Each classic sound has one gesture that gives it away. A preset needs that gesture. It doesn't need the
whole instrument.

| Classic | The gesture | Recipe |
| --- | --- | --- |
| Juno strings / brass | PWM and chorus | pulse, LFO on the width 0.5-1 Hz; 24 dB low-pass with key tracking; A 150-400 ms, S full, R about 1 s |
| Alpha Juno "hoover" | PWM saw, pitch swoop, chorus | PWM at about 5.5 Hz; detuned copies; glide; filter about 3 kHz |
| Prophet-5 brass | the filter's swell | 2 saws 5-8 cents apart; 4-pole low-pass, filter envelope about 60 %, A 50-100 ms, D 300-600 ms, S 50-70 %; a late vibrato |
| Sync lead ("Let's Go") | the follower swept by an envelope | follower +9 st, an envelope sweeping it up and back over seconds; mono, legato |
| OB-X stab ("Jump") | detuned saws, no attack, long decay | saws spread about 9 cents; cutoff high, resonance 25 %; A 0, D long, S 0; driven |
| Jupiter-8 pad | slow saws, the filter's envelope | 2 saws about 7 cents apart, cutoff 60-70 %, A about 1 s, R about 2 s |
| CS-80 brass (Blade Runner) | the swell, the big room | pulses an octave apart, PWM; filter A 650 ms; amp A 250 ms; a large reverb |
| Solina strings | the ensemble | saws 8' + 4', A 100-300 ms, R 1 s, a chorus |
| Minimoog bass / lead | ladder, glide, overdrive | 2-3 oscillators an octave apart, ladder about 120 Hz (bass), envelope decay 200-400 ms; lead: glide 50-150 ms, a late 5-6 Hz vibrato |
| TB-303 acid | resonance, envelope decay, slide | saw or square, mono; resonance 70-90 %, envelope amount 50-80 %, decay 200-600 ms, no filter sustain; glide about 60 ms; distortion after |
| Reese | beating detune | 2+ saws 5-15 cents apart, low-pass 0.8-2 kHz, mono, the detune wandering |
| 808 | a sine with a long decay (and a pitch drop) | sine, decay 0.5-2 s, saturation; the pitch drop needs a pitch envelope (see the gaps) |
| DX7 E.PIANO 1 | the 14:1 tine on velocity | 1:1 body plus a 14:1 tine that dies fast; velocity on the modulator |
| DX7 BASS 1 | a half-ratio modulator, plucked | 0.5 modulator, a percussive modulator envelope |
| FM bells / marimba | inharmonic ratios, the modulator decaying first | bells 1:3.5 / 1:1.4, long; marimba 1:4 or 1:10, 20-50 ms modulator, 300-600 ms carrier |
| M1 piano / organ 2 | thin and bright, stabbed; organ as a percussive bass | piano: bright, short, 2-4 kHz up, lows cut; organ 2: A 0, D 300-600 ms, S about 30 % |
| PPG / Waldorf pad | the table scanned by an envelope or LFO | the position swept, a gentle low-pass, slow attack |
| Supersaw (JP-8000) | seven detuned saws, random phases | detune 40-60 %, mix 70 %; a keytracked high-pass |
| Karplus pluck / harp / koto | the decaying string | harp: long, soft; koto: short and bright |
| Rhodes / Wurlitzer / clavinet | bark, tremolo, a narrow pulse | Rhodes: a sine with the tine, driven by velocity, a tremolo or autopan at 3-5 Hz; Wurli: odd harmonics, choppy tremolo at 5-6 Hz; clav: a 10-25 % pulse, A 0, short, S 0 |
| Hammond / combo / pipe | drawbars, percussion, the Leslie | 888000000 jazz, 888888888 full; the 3rd harmonic's percussion; Leslie at 0.8 / 6.5 Hz; combo: squares at 16'/8'/4' with vibrato; pipe: no vibrato, a big hall |
| Choir / Mellotron | formants, wow | /a/ about 730 / 1090 / 2440 Hz; A 300 ms-1 s; Mellotron: wow and flutter, notes stop at about 8 s |
| NES | pulse duty, triangle bass, arpeggios at frame rate | 12.5 / 25 / 50 % pulses, a 4-bit triangle bass, noise drums, arpeggios at 60 Hz |

Sources:
- soundonsound.com: Synth Secrets; "Synthesizing strings: PWM"; "Synthesizing brass instruments"; "Synthesizing tonewheel organs"
- syntorial.com: the preset recipes for "Let's Go", "Jump" and "Blade Runner"
- synthmania.com: the Juno-60 and Juno-106 pages
- musicradar.com: Minimoog bass; the M1's house sounds; 80s wavetable sounds
- Adam Szabo, "How to emulate the super saw" (PDF)
- Stanford CCRMA: the Karplus-Strong algorithm
- nesdev.org: APU Pulse
- dairiki.org: Hammond wiki, drawbar registrations
- Wikipedia: hoover sound; Mellotron; formant; Korg M1; Prophet-5

## The engines and the classics they can play

| Engine | Strengths | Classics it plays |
| --- | --- | --- |
| weft (CZ phase distortion) | the bend from sine to saw, resonant waves | CZ e-piano and resonant bass, a 303 line, an 808-ish sine, a screaming resonant lead, clav, Prophet-style brass, CZ strings, a dub siren, a laser |
| stack (two oscillators, ratio N:1) | octaves, fifths, beating, PWM by the LFO | Minimoog bass and lead, the Reese, the hoover, a Juno-ish keys, the "Jump" stab, Prophet brass, Juno strings, a Jupiter pad, a metallic SOPHIE-like pluck |
| wavetable (nine tables, scan) | moving spectra | PPG pads and plucks (the scan swept), a growl bass, a sync-like lead, vocal and glass tables |
| orbit (two-operator FM) | ratios, feedback | DX bass, FM brass, the M1 house organ, glassy DX keys, marimba, bells, kalimba, an FM stab, metal noise |
| tines (FM e-piano) | the tine, bark, bell | E.PIANO 1, Rhodes, Wurlitzer (tremolo), clav, the M1 house piano, music box, vibraphone, kalimba, lo-fi keys |
| organ (six drawbars, rotor) | registrations | combo, jazz, gospel, the M1 Organ 2 bass, accordion, harmonium, calliope, Mellotron flute, pipe and church, organ stabs |
| sync (hard sync, sub, hiss) | the follower's ratio | the Cars' lead, a screaming sync lead, sync brass and stabs, clav, wind (the hiss) |
| swarm (sines pushed by noise) | breath, drift | theremin, pan flute, sine sub, an "ooh" choir, wind, FM plucks and bells |
| sample (the bank's sounds) | real sounds pitched | piano, M1 piano stab, the kick as a bass or an 808, choirs, risers, percussion |
| slice (a break cut up) | breaks | straight, chopped, jungle, boom bap, lo-fi / SP, stutter, tape stop, dub |
| brocade (supersaw, after NuSaw) | many detuned saws | the trance lead, pluck, stab and pad, a Reese, a single-saw lead, brass, strings, a riser |
| cane (Braids' physical models) | strings, reeds, bells, drums | harp, koto, sitar-like pluck, plucked and bowed bass, violin, cello, clarinet, shawm, flute, bells, gong, toms |
| voile (Braids' voices) | formants | choirs (bass to soprano), VOSIM bass and bells, a talk box, a vowel pluck, an orchestra-hit stab, robot chords, whispers |
| gingham (Braids' toys and chips) | chips, chords, arpeggios | NES pulse lead and triangle bass, chip arpeggios, chip snare and hats, a toy piano, ring-modulated bells, a filter wah |

## How the sixteen of an engine are told apart

Each engine's sixteen cover different roles. The role shows in the PRESETS message ("BASS 2/16") and sets
the phrase it plays in `tools/render_presets.sh --roles`. The roles are bass (sub, acid, plucked, Reese),
lead (sync and screaming among them), keys, pluck, bell, stab, brass, strings, pad, voice, chord, fx,
drum and loop.

Within each role, the presets differ on these axes:

- **Envelope**: percussive, plucked, gated, swelling, ringing, with a short or a long release.
- **Spectrum**: sine, dark, open, nasal (band-pass), thin (high-pass), metallic.
- **Movement**: still, vibrato, PWM, the table scanned, tremolo, stepping.
- **Space**: most presets are dry or nearly so. Pads, strings, voices and bells send to the reverb. Some
  plucks and leads send to the delay.

`tools/preset_distance.py` measures how alike two presets sound. It compares the average spectrum's shape
and the level envelope of one shared phrase. The dry render (the voices alone) and the chain render (FX and
master) are measured separately.

## Gaps

- **The 808's pitch drop.** No engine has a pitch envelope. An LFO can't make a one-shot drop: it either
  repeats or wanders at random. The 808 presets (weft "poplin sub", sample "kenaf") are a long sine and the
  kick, with no drop.
- **Accent.** The 303's accent, which opens the cutoff, resonance and level together, comes from velocity
  only. Velocity scales the filter envelope (0.5 + 0.5 × velocity) and the level.
