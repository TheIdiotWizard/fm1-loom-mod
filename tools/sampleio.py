# SPDX-License-Identifier: GPL-3.0-only
# Portions adapted from X0X's tools/sampleio.py, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
# Instruments (Felucca's sample helpers).
"""LOOM-8's sample helpers, shared by tools/gen_samples.py (the factory bank compiled into the image) and
tools/loom_samples.py (the user bank, uploaded over USB): WAV reading, resampling, IMA ADPCM with
checkpoints, and the user bank's layout (app/dsp/sample.h).

A sample is mono IMA ADPCM at 22050 Hz, 4 bits a sample, the low nibble first, from predictor 0 / index 0.
Every SMP_BLK (64) samples a checkpoint: the decoder's state before that sample, (predictor & 0xFFFF) |
index << 16, one uint32 each, so a voice can start, loop and play backwards from any block (dsp/sample.c
decodes a block of 65 at a time into the voice's cache).

The user bank (flash 0xCF000, 52 KB): a 1024-byte header, then the data (each sample's checkpoints,
4-aligned, and its ADPCM). Header: magic "LSMP", version 1, count, data length, CRC-32 of the data, then
up to 16 entries of 36 bytes: name (12, NUL-padded), ADPCM offset, checkpoint offset (from the data's
start), samples, loop start, loop end (samples; end 0: none), root note, flags, bars (a loop of that many
bars: the slice engine follows the tempo with it; 0 = not stated, as in banks made before it was there),
one spare.
"""
import math
import re
import struct
import zlib

RATE = 22050
BLK = 64
BANK_SIZE = 0xD000              # 0xCF000 .. 0xDBFFF
BANK_HDR = 1024
BANK_MAX_DATA = BANK_SIZE - BANK_HDR
BANK_N = 16
BANK_MAGIC = 0x504D534C         # "LSMP"
NAME_LEN = 12
BARS_MAX = 16                   # (dsp/sample.h SMP_BARS_MAX)

IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


# ---- WAV in (X0X's sampleio.py)

def wav_chunks(d):
    """RIFF/WAVE chunks -> {id: (offset of the body, declared length)}; the first of each id wins"""
    if len(d) < 12 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    out, i = {}, 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        out.setdefault(cid, (i + 8, n))
        i += 8 + n + (n & 1)
    return out


def wav_format(d, ch):
    """-> (tag, channels, rate, bits); WAVE_FORMAT_EXTENSIBLE resolved to its subformat"""
    if b"fmt " not in ch:
        raise ValueError("WAV without a fmt chunk")
    o = ch[b"fmt "][0]
    tag, nch, sr = struct.unpack_from("<HHI", d, o)
    bits = struct.unpack_from("<H", d, o + 14)[0]
    if tag == 0xFFFE:
        tag = struct.unpack_from("<H", d, o + 24)[0]
    return tag, nch, sr, bits


def _channel_values(tag, bps, raw):
    n = len(raw) // bps
    if tag == 3:
        return list(struct.unpack("<%d%s" % (n, "f" if bps == 4 else "d"), raw))
    if bps == 2:
        return [v / 32768 for v in struct.unpack("<%dh" % n, raw)]
    if bps == 3:
        w = bytearray(4 * n)
        w[1::4], w[2::4], w[3::4] = raw[0::3], raw[1::3], raw[2::3]
        return [v / 2147483648 for v in struct.unpack("<%di" % n, w)]
    if bps == 4:
        return [v / 2147483648 for v in struct.unpack("<%di" % n, raw)]
    return [(b - 128) / 128 for b in raw]


def read_any_wav(path):
    """PCM 8/16/24/32 or float, any channel count, mixed to mono -> (rate, [float], loop or None).
    The loop: the first 'smpl' loop (start, end) in samples of the file's rate."""
    d = path.read_bytes()
    ch = wav_chunks(d)
    tag, nch, sr, bits = wav_format(d, ch)
    bps = bits // 8
    if tag not in (1, 3) or not bps or not nch:
        raise ValueError(f"{path}: unsupported WAV format (tag {tag}, {bits} bit)")
    o, n = ch.get(b"data", (0, 0))
    frame = bps * nch
    nfr = min(n, len(d) - o) // frame
    v = _channel_values(tag, bps, d[o:o + nfr * frame])
    if nch > 1:
        v = [sum(v[f:f + nch]) / nch for f in range(0, len(v), nch)]
    loop = None
    if b"smpl" in ch:
        so = ch[b"smpl"][0]
        if struct.unpack_from("<I", d, so + 28)[0]:
            loop = struct.unpack_from("<II", d, so + 36 + 8)
    return sr, v, loop


def _fir(x, h):
    """x through the symmetric FIR h (odd length), centred (no delay), zeros beyond the ends"""
    k = len(h) // 2
    try:
        import numpy as np
        return list(np.convolve(np.asarray(x, dtype=float), np.asarray(h, dtype=float), mode="same"))
    except ImportError:
        xp = [0.0] * k + list(x) + [0.0] * k
        return [sum(h[j] * xp[i + j] for j in range(len(h))) for i in range(len(x))]


def _lowpass(fc, sr, taps=63):
    """a windowed-sinc low-pass (Blackman) at fc Hz, unity at DC"""
    k = taps // 2
    h = []
    for n in range(-k, k + 1):
        s = 2.0 * fc / sr if n == 0 else math.sin(2.0 * math.pi * fc / sr * n) / (math.pi * n)
        w = 0.42 + 0.5 * math.cos(math.pi * n / k) + 0.08 * math.cos(2.0 * math.pi * n / k)
        h.append(s * w)
    g = sum(h)
    return [v / g for v in h]


# the playback's linear interpolation (dsp/sample.c smp_play) rolls the top off as sinc^2(f / 22050): -3.9 dB at
# 8 kHz, -6.3 at 10 kHz. This 5-tap pre-emphasis (least squares, DC 1) puts it back: flat within 0.3 dB to 9 kHz
EMPHASIS = [v / 1.0196 for v in (0.0517, -0.21, 1.3362, -0.21, 0.0517)]


def resample(x, sr, to):
    """to the rate `to`: a windowed-sinc anti-alias filter below its Nyquist when going down, then linear
    interpolation (an earlier version averaged neighbours instead: -2.4 dB at 10 kHz and the band above
    folded back)"""
    if sr == to:
        return list(x)
    if sr > to:
        x = _fir(x, _lowpass(0.47 * to, sr))
    step, out, p, last = sr / to, [], 0.0, len(x) - 1
    while p < last:
        i = int(p)
        f = p - i
        out.append(x[i] * (1 - f) + x[i + 1] * f)
        p += step
    return out


def emphasis(x):
    """at RATE: the pre-emphasis that the playback's linear interpolation takes back off (EMPHASIS)"""
    return _fir(x, EMPHASIS)


def to_int16(x, peak_to=30000):
    """mono float -> int16 list, the peak normalised to peak_to"""
    pk = max(1e-9, max(map(abs, x)))
    return [max(-32768, min(32767, int(round(v / pk * peak_to)))) for v in x]


def note_from_name(name, default=60):
    """"C4" / "F#3" / "Bb2" in a file name, C4 = 60, clamped to 0..127"""
    m = re.search(r"(?<![A-Za-z])([A-G])([#b]?)(-?\d)(?!\d)", name)
    if not m:
        return default
    pc = "C D EF G A B".index(m.group(1)) + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return max(0, min(127, (int(m.group(3)) + 1) * 12 + pc))


# ---- IMA ADPCM with checkpoints

def ima_encode(samples):
    """int16 samples -> (ADPCM bytes, checkpoints): the decoder's state before every BLK-th sample,
    (pred & 0xFFFF) | idx << 16. The encoder runs the decoder's own state, so the two agree exactly."""
    pred, idx, nib, ck = 0, 0, bytearray(), []
    for n, x in enumerate(samples):
        if n % BLK == 0:
            ck.append((pred & 0xFFFF) | idx << 16)
        step = IMA_STEP[idx]
        diff = x - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        vd = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vd += step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
            vd += step >> 1
        if diff >= step >> 2:
            code |= 1
            vd += step >> 2
        pred = pred - vd if code & 8 else pred + vd
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_IDX[code & 7]))
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    return bytes(a | (b << 4) for a, b in zip(nib[0::2], nib[1::2])), ck


def ima_decode(adp, n):
    """the firmware's decoder (dsp/sample.c), for the tests"""
    pred, idx, out = 0, 0, []
    for i in range(n):
        code = (adp[i >> 1] >> ((i & 1) * 4)) & 15
        step = IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = pred - vd if code & 8 else pred + vd
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_IDX[code & 7]))
        out.append(pred)
    return out


def clean_name(name):
    """a name for the screen: lower case, printable, at most 11 characters"""
    s = re.sub(r"[^a-z0-9 _.-]", "", name.lower().replace("_", " ")).strip()
    return (s or "sample")[:NAME_LEN - 1]


def user_bank(samples):
    """samples: [(name, int16 list at RATE, root, (loop start, loop end) or None[, bars])] -> the bank image
    (header + data, ready for flash 0xCF000), without the padding to BANK_SIZE"""
    if not 1 <= len(samples) <= BANK_N:
        raise ValueError(f"1..{BANK_N} samples in the bank")
    ents, data = [], bytearray()
    for name, s, root, loop, *more in samples:
        bars = more[0] if more else 0
        if not 0 <= bars <= BARS_MAX:
            raise ValueError(f"{name}: {bars} bars (0..{BARS_MAX})")
        if not s:
            raise ValueError(f"{name}: no audio")
        adp, ck = ima_encode(s)
        ck_off = len(data)
        data += struct.pack("<%dI" % len(ck), *ck)
        adp_off = len(data)
        data += adp
        while len(data) & 3:
            data.append(0)
        ls, le = (loop if loop else (0, 0))
        le = min(le, len(s))
        if le <= ls:
            ls = le = 0
        ents.append(struct.pack("<12sIIIIIBBBx", clean_name(name).encode().ljust(NAME_LEN, b"\0"), adp_off, ck_off,
                                len(s), ls, le, max(0, min(127, root)), 0, bars))
    if len(data) > BANK_MAX_DATA:
        raise ValueError(f"too long: {len(data)} B, the bank holds {BANK_MAX_DATA} B "
                         f"(about {BANK_MAX_DATA * 2 * 8 / 9 / RATE:.1f} s in all)")
    hdr = struct.pack("<IHBBII16x", BANK_MAGIC, 1, len(ents), 0, len(data), zlib.crc32(data) & 0xFFFFFFFF)
    hdr += b"".join(ents)
    return hdr.ljust(BANK_HDR, b"\0") + bytes(data)


def midi_hz(n):
    return 440.0 * math.pow(2.0, (n - 69) / 12.0)
