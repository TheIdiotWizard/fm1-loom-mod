# LOOM-8 licensing

LOOM-8 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full
text in `LICENSE`). It is built on Felucca's platform layer (through X0X and FoMni) and keeps
Felucca's licence. If you distribute LOOM-8, or firmware derived from it, you must give your
recipients its complete corresponding source under the same licence.

LOOM-8 is inspired by the teenage engineering OP-XY's way of working. It is not affiliated with,
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
| `firmware/src/dsp/tapescam.h` (the master's tape) | ported from [schwung-tapescam](https://github.com/charlesvestal/move-everything-tapescam), Copyright (c) 2025 Charles Vestal | MIT (below) |
| `firmware/src/dsp/psxrev.h` (the reverb's PlayStation modes) | ported from [schwung-psxverb](https://github.com/charlesvestal/move-everything-psxverb), Copyright (c) 2025 Charles Vestal; the register sets and the resampling filter are psx-spx's documentation of the PlayStation's SPU | MIT (below) |
| Everything else in `firmware/src/{app,dsp,seq}`, `tests/` | LOOM-8 | GPL-3.0-only |

## Third-party material

The package tool (`tools/fm1pkg_make.py`) uses three files of JieLi's AC79 SDK (Apache-2.0), which
are not part of this tree: see `BUILDING.md`.

The master's tape and the reverb's PlayStation modes come from schwung-tapescam and schwung-psxverb, under
the MIT License:

```
Copyright (c) 2025 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
