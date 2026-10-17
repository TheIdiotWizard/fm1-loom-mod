#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from X0X's tools/fm1_sample_upload.py, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
# Instruments (Felucca's sample upload).
"""LOOM-8's user sample bank: WAV files into the FM-1's flash over USB-MIDI, beside the factory samples.

  loom_samples.py info                          what the bank holds now
  loom_samples.py load [NAME=]file.wav[:ROOT] ...   replace the bank with these (up to 16)
  loom_samples.py erase                         empty the bank
  loom_samples.py build OUT.bin [NAME=]file.wav[:ROOT] ...   no device: the bank's image into OUT.bin
                                                (the host simulator loads it: "samples OUT.bin")

Each file becomes one sample: mixed to mono, resampled to 22050 Hz, its peak normalised, IMA ADPCM with a
checkpoint every 64 samples (tools/sampleio.py, as the factory bank). NAME is what the screen shows (lower
case, 11 characters; default: the file's name); ROOT the MIDI note it plays at its own pitch on a synth
track (default: a note in the file's name, "piano_C4.wav", else 60 = C4). A loop in the WAV's 'smpl' chunk
is kept (the sample engine's loop mode plays it). The bank holds 51 KB of data: about 4.1 s of sound in all.
On a track, the sample engine's (and the kit's sample model's) sample knob counts the factory samples first,
then user 1, user 2 ...: a load replaces them all, in the order given.

Protocol (firmware/src/app/samples.c): F0 7D 46 4C cmd ... F7; BEGIN 11, WRITE 12, HDR 16, END 13, ERASE 14,
INFO 15. Needs mido and a backend (pip install mido python-rtmidi) for the device commands.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

HDR = [0x7D, 0x46, 0x4C]
RC = {1: "size or offset", 2: "erase", 3: "write", 4: "not begun", 5: "header", 6: "data CRC", 7: "no samples"}
PART = 320


def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def parse(spec):
    """"[NAME=]file.wav[:ROOT]" -> (name, int16 samples at 22050 Hz, root, loop)"""
    name = None
    if "=" in spec:
        name, spec = spec.split("=", 1)
    root = None
    if ":" in spec and spec.rsplit(":", 1)[1].isdigit():
        spec, r = spec.rsplit(":", 1)
        root = int(r)
    path = Path(spec)
    sr, x, loop = sio.read_any_wav(path)
    if not x:
        raise ValueError(f"{path}: no audio")
    if loop:
        loop = (int(loop[0] * sio.RATE / sr), int((loop[1] + 1) * sio.RATE / sr))
    x = sio.resample(x, sr, sio.RATE)
    return (name or path.stem, sio.to_int16(x), root if root is not None else sio.note_from_name(path.stem), loop)


def build(specs):
    return sio.user_bank([parse(s) for s in specs])


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido = mido
        outs = [n for n in mido.get_output_names() if "LOOM" in n]
        ins = [n for n in mido.get_input_names() if "LOOM" in n]
        if not outs or not ins:
            sys.exit("no MIDI port named LOOM: connect the FM-1 (running LOOM-8) by USB")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])
        for _ in range(3):                          # the first frame after opening the port can be lost
            try:
                self.req(25, [], 1.0)
                break
            except TimeoutError:
                pass

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}")


def upload(link, image):
    hdr, data = image[:sio.BANK_HDR], image[sio.BANK_HDR:]
    r = link.req(11, [0], 10)
    if r[1]:
        sys.exit(f"begin failed ({RC.get(r[1], r[1])})")
    t0 = time.time()
    for off in range(0, len(data), 256):
        a = sio.BANK_HDR + off
        r = link.req(12, [0, a & 0x7F, (a >> 7) & 0x7F, (a >> 14) & 0x7F] + pack7(data[off:off + 256]), 5)
        if r[4]:
            sys.exit(f"write at {a:#x} failed ({RC.get(r[4], r[4])})")
        print(f"\r  {min(off + 256, len(data))} / {len(data)} B", end="", flush=True)
    for part in range(2):
        r = link.req(16, [0, part] + pack7(hdr[part * PART:(part + 1) * PART]))
        if r[2]:
            sys.exit(f"header part {part} failed")
    r = link.req(13, [0], 10)
    print(f"\n  {time.time() - t0:.1f} s:", "loaded" if r[1] == 0 else f"FAILED ({RC.get(r[1], r[1])})")
    return r[1] == 0


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("info", "load", "erase", "build"):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "build":
        if len(sys.argv) < 4:
            sys.exit(__doc__)
        try:
            img = build(sys.argv[3:])
        except (OSError, ValueError) as e:
            sys.exit(f"cannot build the bank: {e}")
        Path(sys.argv[2]).write_bytes(img)
        print(f"{sys.argv[2]}: {img[6]} samples, {len(img) - sio.BANK_HDR} of {sio.BANK_MAX_DATA} B")
        return
    if cmd == "load":
        if len(sys.argv) < 3:
            sys.exit(__doc__)
        try:
            img = build(sys.argv[2:])
        except (OSError, ValueError) as e:
            sys.exit(f"cannot build the bank: {e}")
        print(f"bank: {img[6]} samples, {len(img) - sio.BANK_HDR} of {sio.BANK_MAX_DATA} B")
        sys.exit(0 if upload(Link(), img) else 1)
    link = Link()
    if cmd == "erase":
        r = link.req(14, [0], 20)
        print("erase:", "ok" if r[1] == 0 else f"FAILED ({RC.get(r[1], r[1])})")
        return
    r = link.req(15, [])
    size, used, n = r[1], r[2], r[3]
    names, i = [], 4
    for _ in range(n):
        j = r.index(0, i)
        names.append(bytes(r[i:j]).decode(errors="replace"))
        i = j + 1
    print(f"user bank: {n} samples, {used} of {size} KB" + "".join(f"\n  user {k + 1}: {s}" for k, s in enumerate(names)))


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is LOOM-8 running, and no other app using its MIDI port?")
