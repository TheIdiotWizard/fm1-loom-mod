#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Measure LOOM-8's audio load on an attached FM-1, as the Status page reads it (GLO, SELECT x5: "audio load
# N percent, voices N"), driving the FM-1 over USB-MIDI with fm1-base's tools/fm1_remote.py (its remote
# control and the screen reader's channel). Nothing is flashed.
#
#   tools/measure_load.sh [--label NAME] [--no-kit] [--quick]
#
# Run it on a NEW project (GLO, SELECT to Setup, SEL, NEW PROJECT): it reads the default project's tracks
# (T1 kit, T2 stack, T3 weft, T4 wavetable, T5 orbit, T6 tines, T7 organ, T8 sync). The cases:
#   idle                     stopped, nothing held
#   <engine> xN              N keys held (1, 3, 6) on T2..T8 (a mono track is set to poly for it and back)
#   kit playing              a beat written on T1's empty pattern 1 (kick, snare, hats), playing
#   kit + weft x6            the beat and six weft voices
#   tape off / new / full    the beat and three weft voices, the master tape's knobs at 0 / colour 24 /
#                            all four at 64 (they are put back as they were)
# --no-kit leaves the pattern alone (and skips the cases that play it); the beat it writes stays on T1
# (the project shows unsaved: do not save it). Each case waits for the load's average to settle and reads
# it three times (the median). A table is printed and appended to build/measure_load.csv with the label
# (the firmware, e.g. --label "main" or --label "ram-dsp"), so builds can be compared:
#   ./build.sh && python3 base/tools/fm1_install.py app/build/loom.fwsc --yes && tools/measure_load.sh --label default
# FM1_REMOTE: the path of fm1_remote.py (default base/tools/fm1_remote.py); needs mido + python-rtmidi.
set -eu
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
REMOTE="${FM1_REMOTE:-base/tools/fm1_remote.py}"
[ -f "$REMOTE" ] || { echo "measure_load: no $REMOTE (set FM1_REMOTE)"; exit 1; }
mkdir -p build
exec "$PY" - "$REMOTE" "$@" <<'PY'
import csv, os, re, statistics, sys, time
remote = sys.argv[1]
sys.path.insert(0, os.path.dirname(os.path.abspath(remote)))
import fm1_remote as fr  # noqa: E402

args = sys.argv[2:]
label = args[args.index("--label") + 1] if "--label" in args else time.strftime("%Y-%m-%d %H:%M")
kit = "--no-kit" not in args
quick = "--quick" in args
WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
CHORD = [WHITE_K[i] for i in (0, 2, 4, 6, 8, 10)]          # six keys a third or so apart
ENGINES = {2: "stack", 3: "weft", 4: "wavetable", 5: "orbit", 6: "tines", 7: "organ", 8: "sync"}

d = fr.FM1()
rows = []


def run(*cmds):
    d.run([str(c) for c in cmds])


def where():
    """what the screen says (the screen reader's 'where am I')"""
    d.sysex(fr.SR + [1, 1])
    time.sleep(0.2)
    for _ in d.inp.iter_pending():
        pass
    d.sysex(fr.SR + [3])
    for _ in range(2):
        s = " ".join(f[2:].decode("latin1", "replace") for f in d.frames(fr.SR, 1.0) if f and f[0] == 16)
        if s:
            return s
        d.sysex(fr.SR + [3])
    return ""


def turn(enc, n):
    for _ in range(abs(n)):                   # one detent at a time: no acceleration, every page seen
        run("turn", enc, 1 if n > 0 else -1, "wait", 220)


def num(text, name):
    m = re.search(r"\b%s (\d+)" % re.escape(name), text)
    return int(m.group(1)) if m else None


def set_knob(enc, name, want):
    """turn a knob until the screen says NAME WANT; returns what it was"""
    was = num(where(), name)
    if was is None:
        raise SystemExit(f"measure_load: no '{name}' on the screen: {where()!r}")
    for _ in range(40):
        now = num(where(), name)
        if now == want:
            break
        diff = want - now                     # (big steps while far, single detents near: an encoder may
        step = diff if abs(diff) <= 2 else (1 if diff > 0 else -1) * min(abs(diff) // 2, 8)   # accelerate)
        run("turn", enc, step, "wait", 250)
    else:
        print(f"  ! {name} did not reach {want}")
    return was


def track(t):
    """select track t (1..8) on its Engine page"""
    run("tap", "EDIT", "wait", 400)
    for _ in range(14):
        m = re.search(r"\bT(\d+) ", where())
        if m and int(m.group(1)) == t:
            return
        cur = int(m.group(1)) if m else 0
        turn("ALGO", 1 if cur < t else -1)
    raise SystemExit(f"measure_load: could not select T{t}")


def status_from(page_now):
    """from SELECT page page_now (0 = Mix) of GLO to Status (page 5)"""
    turn("SELECT", 5 - page_now)


def read_status(name):
    time.sleep(1.5 if not quick else 0.8)    # the load's average (16 halves) settles
    loads, voices = [], []
    for _ in range(3 if not quick else 1):
        s = where()
        m = re.search(r"audio load (\d+) percent, voices (\d+)", s)
        if not m:
            raise SystemExit(f"measure_load: not on Status: {s!r}")
        loads.append(int(m.group(1)))
        voices.append(int(m.group(2)))
        time.sleep(0.5)
    load, v = statistics.median(loads), max(voices)
    rows.append((name, load, v))
    print(f"  {name:<26} load {load:>5.1f} %   voices {v:>2}   (reads {loads})")


def go_status():
    if " of 6." in where():                  # on a GLO page already: GLO again would turn its page
        run("tap", "EDIT", "wait", 400)
    run("tap", "GLO", "wait", 400)
    status_from(0)


def mode_poly(t):
    """the track's voice mode to poly (Play page, KNOB 1): the turns to undo it"""
    run("tap", "EDIT", "wait", 300)
    turn("SELECT", 1)
    s = where()
    n = 0
    while "mode poly" not in s and n < 3:
        run("turn", "K1", -1, "wait", 300)
        n += 1
        s = where()
    run("tap", "EDIT", "wait", 300)
    return n


def mode_back(n):
    if n:
        run("tap", "EDIT", "wait", 300)
        turn("SELECT", 1)
        run("turn", "K1", n, "wait", 300)
        run("tap", "EDIT", "wait", 300)


def hold(keys):
    for k in keys:
        run("key", k, "down")
    time.sleep(0.3)


def release_all():
    run("release-all", "wait", 300)


print(f"LOOM-8 audio load ({label})")
print("  screen:", where()[:100])
release_all()
go_status()
read_status("idle")

for t, eng in ENGINES.items():
    if quick and t not in (2, 3):
        continue
    track(t)
    n = mode_poly(t)
    for nv in (1, 3, 6):
        hold(CHORD[:nv])
        go_status()
        read_status(f"{eng} x{nv}")
        release_all()
        run("tap", "EDIT", "wait", 1500)     # (the release tails die)
    mode_back(n)

if kit:
    track(1)
    run("tap", "SEQ", "wait", 400)
    s = where()
    if "steps on: none" not in s:
        print("  ! T1's pattern is not empty: the kit cases are skipped (run on a new project)")
    else:
        for w in (0, 4, 8, 12):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tapkey", BLACK_K[1], "wait", 200)
        for w in (4, 12):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tapkey", BLACK_K[2], "wait", 200)
        for w in range(0, 16, 2):
            run("tapkey", WHITE_K[w], "wait", 120)
        run("tap", "PLAY", "wait", 1000)
        go_status()
        read_status("kit playing")
        track(3)
        n = mode_poly(3)
        hold(CHORD)
        go_status()
        read_status("kit + weft x6")
        release_all()
        run("tap", "EDIT", "wait", 1500)
        hold(CHORD[:3])
        run("tap", "GLO", "wait", 400)
        turn("SELECT", 2)                    # Master tape: drive, color, wobble, noise
        before = {}
        for k, name, want in (("K1", "drive", 0), ("K2", "color", 0), ("K3", "wobble", 0), ("K4", "noise", 0)):
            before[(k, name)] = set_knob(k, name, want)
        status_from(2)
        read_status("kit + weft x3, tape off")
        turn("SELECT", -3)
        set_knob("K2", "color", 24)
        status_from(2)
        read_status("kit + weft x3, tape new")
        turn("SELECT", -3)
        for k, name in (("K1", "drive"), ("K2", "color"), ("K3", "wobble"), ("K4", "noise")):
            set_knob(k, name, 64)
        status_from(2)
        read_status("kit + weft x3, tape full")
        turn("SELECT", -3)
        for (k, name), v in before.items():
            set_knob(k, name, v)
        release_all()
        run("tap", "PLAY", "wait", 300)      # stop
        mode_back(n)
release_all()

out = os.path.join("build", "measure_load.csv")
new = not os.path.exists(out)
with open(out, "a", newline="") as f:
    w = csv.writer(f)
    if new:
        w.writerow(["label", "case", "load_pct", "voices"])
    for name, load, v in rows:
        w.writerow([label, name, load, v])
print(f"appended {len(rows)} rows to {out}")
PY
