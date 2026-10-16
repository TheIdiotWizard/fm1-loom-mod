# XY-1 for the M-VAVE FM-1

XY-1 turns the M-VAVE FM-1 into a pocket groovebox, inspired by the OP-XY's way of working: eight
instrument tracks with synth engines and a drum machine, a 64-step sequencer with step components
and parameter locks, players (arp, maestro, hold), punch-in FX, a brain track that transposes the
song, a tape track, two send-effect tracks, a mixer with EQ, saturator and compressor, scenes and a
song. Built on Felucca's platform layer with X0X's sequencer discipline, NuSaw's second core and
Polly's menu paradigm.

- **Read the manual**: [docs/MANUAL.md](docs/MANUAL.md), with a ten-minute quick start, every page and gesture, and screenshots
- **Try it in your browser** (no FM-1 needed): <https://charlesvestal.github.io/fm1-xy/emu/>
- **Install it** (Chrome or Edge, with the FM-1 plugged in): <https://charlesvestal.github.io/fm1-xy/install/>
- **Download the firmware file**: [releases](https://github.com/charlesvestal/fm1-xy/releases)

Installing is at your own risk, though it's hard to get stuck: the web installer can put M-VAVE's
firmware back, and an FM-1 that keeps crashing starts in a safe mode.

The design is in `docs/superpowers/specs/2026-10-15-xy1-design.md`.

## Building and testing

See `BUILDING.md`. `tests/run_tests.sh` runs everything on the host, silently (the audio goes to
WAV files). `LICENSING.md` says where the code comes from.

XY-1 is not affiliated with teenage engineering; "OP-XY" is used only to say what inspired it.
