# XY-1 licensing

XY-1 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full
text in `LICENSE`). It is built on Felucca's platform layer (through X0X and FoMni) and keeps
Felucca's licence. If you distribute XY-1, or firmware derived from it, you must give your
recipients its complete corresponding source under the same licence.

XY-1 is inspired by the teenage engineering OP-XY's way of working. It is not affiliated with,
endorsed by or derived from teenage engineering; the name is only used descriptively.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and [FoMni](https://github.com/charlesvestal/fm1-fomni) | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `firmware/hal/fm1_cpu1.*` (the second core), `host/xy_host.c`, `web/emu/` | adapted from X0X and NuSaw (themselves from Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| The engine's command queue, the overload guard, the two-core render split, `firmware/src/app/lz.c` and the project's packed stream, the sequencer's discipline | X0X | GPL-3.0-only |
| The menu paradigm (a button per set of knobs, SELECT turns the pages) | [Polly](https://github.com/charlesvestal/fm1-polly) | GPL-3.0-only |
| The screen reader protocol (`firmware/src/announce.c`, `tools/gen_words.py`) | [fm1-reader](https://github.com/charlesvestal/fm1-reader) | GPL-3.0-only |
| The idea of a browser emulator for FM-1 firmware (`web/emu/`) | [Groove OS](https://www.groove-os.com/) by Peter Gombos, whose site runs its firmware in the browser; no code from it | credit only |
| Everything else in `firmware/src/{app,dsp,seq}`, `tests/` | XY-1 | GPL-3.0-only |

## Third-party material

The package tool (`tools/fm1pkg_make.py`) uses three files of JieLi's AC79 SDK (Apache-2.0), which
are not part of this tree: see `BUILDING.md`.
