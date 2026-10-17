# LOOM-8 for the M-VAVE FM-1

LOOM-8 turns the M-VAVE FM-1 into a pocket groovebox: eight instrument tracks, each in its own colour, on
eight synth engines and a drum kit; a 64-step sequencer with step tricks and parameter locks; players (arp,
strum, hold); weave, which crosses two patterns (bar by bar, step by step, or one's rhythm with the other's
notes); throws (momentary effects); a conductor track that moves the song through a progression; two
send-effect tracks (the reverb with the PlayStation's reverbs among its modes), a mixer with EQ, a tape
(TapeScam) and a compressor on the master, scenes and a song. Built on
Felucca's platform layer with X0X's sequencer discipline, NuSaw's second core and Polly's menu paradigm.

- **Read the manual**: [docs/MANUAL.md](docs/MANUAL.md), with a ten-minute quick start, every page and gesture, and screenshots
- **Try it in your browser** (no FM-1 needed): <https://charlesvestal.github.io/fm1-loom/emu/>
- **Install it** (Chrome or Edge, with the FM-1 plugged in): <https://charlesvestal.github.io/fm1-loom/install/>
- **Download the firmware file**: [releases](https://github.com/charlesvestal/fm1-loom/releases)

Installing is at your own risk, though it's hard to get stuck: the web installer can put M-VAVE's
firmware back, and an FM-1 that keeps crashing starts in a safe mode.

The design is in `docs/superpowers/specs/2026-10-15-xy1-design.md`.

## Building and testing

See `BUILDING.md`. `tests/run_tests.sh` runs everything on the host, silently (the audio goes to
WAV files). `LICENSING.md` says where the code comes from.

LOOM-8's way of working owes a great deal to teenage engineering's OP-XY; LOOM-8 is not affiliated with
teenage engineering.
