#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the unity build's main-loop code for size (tools/build.py): the functions defined in SIZE_FILES
(the UI, the pages, the drawing, the stores, the update, the screen reader) get LLVM's `minsize` (about
-Oz) while the rest of firmware/src/main.c (the audio and timer interrupts, the engine glue, USB, MIDI,
libc) keeps -Os. The idea is Melodee's (github.com/keremimo/melodee, tools/size_fns.py): the JieLi clang
has no `#pragma clang attribute`, so build.py compiles main.c to LLVM IR without the optimiser, this
script adds `minsize` to those definitions, and the IR is then compiled at -Os (without the edit the
round trip is byte-identical to compiling main.c directly).

Safety net: nothing an interrupt can reach is marked. The roots are the C bodies of the ISRs
(hal/fm1_isr.S: *_irq), the crash handlers and the second core's code; from them every function and
global they name is followed (a function pointer in a table counts as a call), and whatever is reached
keeps -Os, whichever file it is in. Functions placed in a section (.ram_text, .c1_text) are left alone.

    size_fns.py IN.ll OUT.ll        (XY_SIZE=0: copy unchanged)"""
import os
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "firmware" / "src"
SIZE_FILES = ["app/ui.c", "app/pages.c", "app/project.c", "app/lz.c", "app/panel.c", "app/samples.c",
              "app/main_fm1.c", "gfx.c", "storage.c", "ota.c", "announce.c"]
ROOTS = re.compile(r"^(?:\w+_irq|fm1_fault_c|fm1_fatal_common|core2_job)$")
DEF = re.compile(r"^(?:static\s+|inline\s+|const\s+|unsigned\s+|signed\s+|struct\s+\w+\s+|__attribute__\s*\(\(.*?\)\)\s*)*"
                 r"(?:void|int|char|float|uint\d+_t|int\d+_t|size_t|\w+_t)\s*\**\s*([A-Za-z_]\w*)\s*\(", re.M)
IR_DEF = re.compile(r"^define [^\n]*?@\"?([\w.]+)\"?\(", re.M)


def definitions(text):
    """the names of the functions a C file defines (a signature at column 0 whose ')' is followed by '{')"""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    names = []
    for m in DEF.finditer(text):
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        rest = re.sub(r"^(__attribute__\s*\(\(.*?\)\)\s*)+", "", text[i:i + 200].lstrip())
        if rest.startswith("{"):
            names.append(m.group(1))
    return names


def ir_bodies(ir):
    """name -> (header line, body text) of every function defined in the IR; name -> initialiser of every global"""
    funcs, glob = {}, {}
    for m in re.finditer(r"^(define [^\n]*\{)\n(.*?)^\}", ir, re.M | re.S):
        n = IR_DEF.match(m.group(1))
        funcs[n.group(1)] = (m.group(1), m.group(2))
    for m in re.finditer(r"^@\"?([\w.]+)\"? = ([^\n]*)$", ir, re.M):
        glob[m.group(1)] = m.group(2)
    return funcs, glob


def reachable(funcs, glob):
    """everything the interrupt roots name, transitively (functions and globals)"""
    refs = {n: set(re.findall(r"@\"?([\w.]+)", b)) for n, (_, b) in funcs.items()}
    refs.update({n: set(re.findall(r"@\"?([\w.]+)", v)) for n, v in glob.items()})
    todo = [n for n in funcs if ROOTS.match(n)]
    seen = set(todo)
    while todo:
        for r in refs.get(todo.pop(), ()):
            if r not in seen:
                seen.add(r)
                todo.append(r)
    return seen


def main(src, dst):
    ir = Path(src).read_text()
    if os.environ.get("XY_SIZE") == "0":
        Path(dst).write_text(ir)
        return 0
    funcs, glob = ir_bodies(ir)
    hot = reachable(funcs, glob)
    names = set()
    for f in SIZE_FILES:
        names.update(definitions((SRC / f).read_text()))
    pick = {n for n in names & set(funcs) if n not in hot and " section " not in funcs[n][0]}

    # each picked definition gets its own attribute group: the old one plus minsize
    groups = dict(re.findall(r"^attributes #(\d+) = \{ (.*) \}$", ir, re.M))
    nxt = max(map(int, groups)) + 1 if groups else 0
    made = {}

    def mark(m):
        nonlocal nxt
        n = IR_DEF.match(m.group(0)).group(1)
        if n not in pick:
            return m.group(0)
        g = m.group(1)
        if g not in made:
            made[g] = nxt
            nxt += 1
        return m.group(0)[:m.start(1) - m.start(0)] + str(made[g]) + m.group(0)[m.end(1) - m.start(0):]
    ir = re.sub(r"^define [^\n]* #(\d+) [^\n]*\{$", mark, ir, flags=re.M)
    for g, new in made.items():
        ir += f"\nattributes #{new} = {{ minsize {groups[g]} }}\n"
    Path(dst).write_text(ir)
    print(f"size: {len(pick)} main-loop functions built for size; {len(names & set(funcs) & hot)} in those "
          f"files reachable from an interrupt keep -Os")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
