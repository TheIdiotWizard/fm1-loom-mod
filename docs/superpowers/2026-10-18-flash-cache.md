# LOOM-8: the flash cache, the biggest cost left (handoff, 2026-10-18)

For a fresh session. State: `main` at 2fa5052 (pushed). Read `2026-10-18-handoff.md` first for the rest.

## The finding

On the FM-1 a voice costs four times as much in a song as on its own. The difference is the XIP (flash) instruction
cache: most code runs straight from the flash through a small cache, and in a song with five or six engines each
voice's engine code pushes the others' out, so every voice pays to fetch its code again.

| measure | weft voice | how |
|---|---|---|
| alone, each voice after the first | ~2.5 % of a block | `tools/measure_load.sh` (weft x1 16 %, x3 22 %, x6 27 %; idle 10 %) |
| in a song (jungle's slowest block) | 10-12 % of a block | the profiler's per-voice record of the slowest block |

The same holds for every engine: stack ~2.5 % alone vs 8-9.5 % in a song, brocade (7 saws) ~5 % vs 8-15 %, cane's
bell vs 13.6 %. A block is 256 frames, 5.8 ms; the load is what the Status page shows.

Things already done that work with the cache (all on `main`):
- voices rendered grouped by engine on each core (`synth_render_on`, FM-1 only), so an engine's code is fetched once
  and reused by its next voices
- a stereo voice's filter one channel at a time: 6 KB less code (`flt_loop` / `flt_channel`)
- the engines' sample loops made smaller and cheaper (three agent passes)
- the effects and the master already run from RAM (`tools/size_fns.py` RAM_DSP); measured then: "the master as fast
  either way, the rest ~10 % faster"

Together with the other work the six demo songs went from peaks of 88-125 % (72 dropouts) to 71-98 % (none), but
the cache is still where most of a busy block goes.

## Where the code is: the memory map (our choice, not the chip's)

The AC79 has ~512 KB of SRAM in one block; fm1-base's linker script `base/device/app.ld` divides it:

| region | address | size | holds | LOOM-8 uses |
|---|---|---|---|---|
| RAMTEXT (rwx) | 0x01C00000 | 24 KB | code run from RAM: `.ram_text` (flash driver, ~3 KB), `.c1_text` (core 2's loop), LOOM's `.c1_text.dsp` (effects, master) | 17.6 KB (`app/build/ramtext.bin`), ~6.9 KB free |
| gap | 0x01C06000 | 8 KB | "stays for RAM-run debug" | - |
| RAM (rwx) | 0x01C08000 | 96 KB | .data / .bss | 93.3 KB |
| POOL (rw) | 0x01C20000 | 336 KB | big buffers: delay and reverb lines, canvas, patterns, sample windows | 332.5 KB, 11.6 KB free |
| NOINIT | 0x01C7C000 | ~15 KB | crash records etc. | - |

XIP (the flash, through the cache) at 0x02000120 holds everything else. The cache's size is not documented in what
we have; the SDK's `cpu/wl82/sdk_ld_*.c` mention `CACHE_LINE_COUNT 32` only. Measuring it (or finding it) would tell
how much of the hot set can stay resident.

## The hot code (sizes from `nm -S --size-sort app/build/loom.elf`, image 434,520 B)

| function | size | runs |
|---|---|---|
| `eng_run` (every engine's sample loop, inlined into one) | 23.2 KB | per sub-block (16 samples) per voice |
| `voice_chunk` (every engine's per-chunk set-up, char_prep, voice_pitch, inlined) | 20.0 KB | per chunk (64 samples) per voice |
| `synth_render_on` (voice loop, voice_samples, sample_loop / out_loop variants inlined) | 7.8 KB | per voice per stretch |
| `drumline_render` | 9.1 KB | per drum track per stretch |
| `seq_advance` | 12.5 KB | per stretch (heavy at steps) |
| `char_run`, `char_coefs` | 2.6 KB, 2.7 KB | per voice with a character stage |
| `flt_channel` | 0.9 KB | stereo voices |
| `weft_osc_res`, `weft_osc_pd`, `stack_osc` | 1.2, 0.7, 0.4 KB | their engines |
| `synth_prepare` | 2.7 KB | per stretch |

A single voice touches only its own engine's part of `eng_run` / `voice_chunk` (2-4 KB), but a song's five or six
engines plus the shared voice path, the drums and the sequencer are well over any small cache.

## What to do next, in order

1. **Move the line: a bigger RAMTEXT.** Take the 8 KB debug gap and some of the pool's free 11.6 KB (or shrink a pool
   buffer) so RAMTEXT becomes ~35-40 KB, and put the per-sample voice path there: `synth_render_on`'s voice loop,
   `voice_samples`, `sample_loop` / `flt_loop` / `out_loop`, and the busiest engines' loops (brocade, tines, voile,
   weft, slice/sample by the demos' engine shares). This is a base change plus LOOM's placement:
   - `base/device/app.ld`: RAMTEXT's length (and RAM / POOL origins if they move); `.ram_text` is copied from the
     flash at boot (`_rt_load`, `fm1_cstart` in base/device/main_fm1.c).
   - the PC guard (`fm1_guard`, `base/bsp/chip/fm1_guard.h`): code may only execute in its window; widen it to the new
     RAMTEXT. Core 2's start opens the PC limits briefly (`app/cpu1/loom_cpu1.h`).
   - `tools/size_fns.py`: RAM placement is by function list (RAM_DSP, RAM_INLINE, RAM_UNITS, section `.c1_text.dsp`).
     A direct call cannot reach between the XIP window and RAM (R_PI32V2_LONG_JUMP_23M2: 4 MB), so the RAM set must be
     closed under calls: whatever it calls is inlined into it (RAM_INLINE), and the flash calls in through a pointer
     (engine.c's DSP_FAR). The linker refuses a missed call. `synth.c` is an o2 unit (`app/fm1.toml`), so it would
     join RAM_UNITS; its hot functions call `fm_*` (inline), `smp_*` (dsp/sample.c: the sample engines' flash reads -
     those must stay where the flash driver allows), `braids.h` (inline).
   - RAM code must never touch the flash while it is off (flash writes turn it off with interrupts off; the audio
     ISR does not run then), and the check that RAM code "calls nothing in the flash" (`.ram_text: N insns, no calls`
     in the build) covers `.ram_text`, not `.c1_text*`.
   - Measure with `tools/demos/device_load.py` (the six songs; see the handoff for the DEVTEST build) and
     `tools/measure_load.sh` per engine. Expect the song numbers to approach the solo numbers.
2. **Keep each engine's code together and small.** `eng_run` and `voice_chunk` inline every engine into one function
   each; per-engine functions (`noinline`), laid out next to each other per engine, would keep one engine's hot code
   contiguous (fewer cache lines, less conflict). Measure code size per engine; `tools/size_fns.py` builds the cold
   code for size (COLD), the DSP at -O2.
3. **Cache-aware order across the two cores.** The cores share the caches (`app/cpu1/loom_cpu1.h`: "the cores share
   the caches"). Grouping by engine is per core; giving each core different engines (or the same engine at the same
   time) changes how they evict each other. Untested.

## How to see it

- `tools/perf_read.py` (and `device_load.py`, which prints it per song): the slowest block's voices, each one's time,
  engine, whether it started there, and its core (`its voices: ...`), plus the split and the stretches.
- `tools/measure_load.sh --engines weft,stack,...`: each engine alone with 1 / 3 / 6 voices (the warm-cache cost).
- The gap between the two is the cache's share.

## Related open items (from the main handoff)

- "Never peak" needs a voice budget at note-on (predicted block cost from the measured voice costs), not only speed;
  ask the user what gives way first.
- `tests/scenarios/loadmodel.xy` fails: refit the host load model after measure_load.sh on the FM-1.
