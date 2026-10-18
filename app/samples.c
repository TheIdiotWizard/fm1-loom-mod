/* SPDX-License-Identifier: GPL-3.0-only
 * Sample-bank commands adapted from X0X's editor.c (itself Felucca's editor protocol), Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* LOOM-8's user sample bank over USB-MIDI SysEx (tools/loom_samples.py): F0 7D 46 4C cmd args.. F7, the
 * Felucca editor's framing, 7-bit bytes, data packed 7 to 8 (a byte of high bits, then up to seven bytes).
 * One bank (slot 0), 216 KB over three flash windows (dsp/sample.h SMP_REG*); an offset is virtual, through the
 * windows end to end: a 2048-byte header (at 0xC1000), then the data.
 *   INFO        1                    -> "LOOM-8 <version>", 0 0 0 0 0 0
 *   SMP_BEGIN  11 slot               -> slot, rc     the bank's user samples vanish, its first sector is erased
 *   SMP_WRITE  12 slot off3 data     -> slot, off3, rc    256 bytes (packed) at off (>= 2048, 256-aligned) of the
 *                                       bank; a 4 KB boundary (a sector of a window) is erased first
 *   SMP_END    13 slot               -> slot, rc     the header staged by SMP_HDR checked (its CRC against the
 *                                       data in flash; no update record in the extra windows), written last:
 *                                       the commit
 *   SMP_ERASE  14 slot               -> slot, rc     the whole bank erased (54 sectors: a few seconds)
 *   SMP_INFO   15 [from]             -> 2, size KB (2 bytes, 7 bits each, low first), used KB (2), count, from, k,
 *                                       then samples from .. from + k - 1: name (0-terminated), rate (3), length
 *                                       in samples (3); k at most SB_INFO_K (ask again from from + k)
 *   SMP_HDR    16 slot part data     -> slot, part, rc    the header in parts of 320 bytes (packed), into RAM:
 *                                       parts 0..4 (the 1344 bytes used: 64 + 32 entries of 40)
 *   PING       25                    -> 0
 * rc: 0 ok, 1 size / offset, 2 erase, 3 write, 4 not begun (the bank is live), 5 header, 6 CRC, 7 no samples,
 * 8 an update record's mark ("AT" at 0xF06 of a sector in PLAT_XDATA0 / 1: the loader would erase it).
 * Every frame is answered (the tool waits for each). The erases and writes run with the interrupts off a
 * sector or a page at a time (the audio drops out while a bank is written): fm1-base's plat_data_erase /
 * plat_data_write (the data area and its extra windows); the bank is read in place (plat_data). Part of the app's
 * unity build, after ui.c; fm1-base hands it the SysEx frames it does not take (app_sysex). */
#define SB_HDR0 0x7D
#define SB_HDR1 0x46
#define SB_HDR2 0x4C
enum { SB_INFO = 1, SB_BEGIN = 11, SB_WRITE, SB_END, SB_ERASE, SB_SINFO, SB_HDR, SB_PING = 25 };
#define SB_PART 320u                                 /* SMP_HDR: a header part, bytes */
#define SB_NPART ((SMP_HDR_USED + SB_PART - 1u) / SB_PART)
#define SB_INFO_K 12u                                /* SMP_INFO: samples a reply (12 x 18 bytes at most) */

static uint8_t sb_out[288];
static uint32_t sb_n;
/* the staged header (1344 bytes used) and a page of data live in project.c's io_buf (3840 B): a project is
 * never loaded or saved between the frames of an upload (a save in the middle spoils the staged header: the
 * END then fails and the tool says so) */
#define sb_hdr io_buf
#define sb_buf (io_buf + SMP_BANK_HDR)
typedef char sb_fits[(SMP_BANK_HDR + 256u <= sizeof io_buf && SB_NPART * SB_PART <= SMP_BANK_HDR &&
                      SB_INFO_K * (SMP_NAME + 6u) + 16u <= sizeof sb_out) ? 1 : -1];
static uint8_t sb_open;                              /* begun: the bank is being written */

static void sb_begin_reply(uint32_t cmd)
{
    sb_out[0] = 0xF0;
    sb_out[1] = SB_HDR0;
    sb_out[2] = SB_HDR1;
    sb_out[3] = SB_HDR2;
    sb_out[4] = (uint8_t)cmd;
    sb_n = 5;
}
static void sb_b(uint32_t v)
{
    if (sb_n < sizeof sb_out - 1u)
        sb_out[sb_n++] = (uint8_t)(v & 0x7Fu);
}
static void sb_str(const char *s)
{
    for (uint32_t i = 0; s && s[i] && i < SMP_NAME; i++)
        sb_b((uint8_t)s[i] & 0x7Fu);
    sb_b(0);
}
static uint32_t sb_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static int sb_flash(void)                            /* the flash is there (fm1-base's data area and its windows) */
{
    for (int r = 0; r < SMP_NREG; r++)
        if (!plat_data(smp_reg_off(r)) || !plat_data(smp_reg_off(r) + smp_reg_size(r) - 1u))
            return 0;
    return 1;
}
static int sb_erase(uint32_t v)                      /* the 4 KB sector at virtual offset v (the audio silenced) */
{
    uint32_t off;
    return smp_reg_of(v, &off) < 0 ? -1 : plat_data_erase(off);
}
static int sb_write(uint32_t v, const uint8_t *p, uint32_t n)   /* n bytes at v, inside one window */
{
    uint32_t off;
    int r = smp_reg_of(v, &off);
    if (r < 0 || n > smp_reg_off(r) + smp_reg_size(r) - off)
        return -1;
    return plat_data_write(off, p, n);
}
/* an update record's mark in the bank's data: "AT" at bytes 0xF06..0xF07 of a sector of an extra window (the
 * update loader erases a sector it takes for a record: base/bsp/loader/ldr_core.c) */
static int sb_record_mark(uint32_t len)
{
    for (int r = 1; r < SMP_NREG; r++)
        for (uint32_t s = 0; s < smp_reg_size(r); s += 0x1000u) {
            uint32_t v = smp_reg_v(r) + s + 0xF06u;
            const uint8_t *p = plat_data(smp_reg_off(r) + s + 0xF06u);
            if (v + 2u <= SMP_BANK_HDR + len && p && p[0] == 'A' && p[1] == 'T')
                return 1;
        }
    return 0;
}
static uint32_t sb_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t sb_end(void)
{
    uint32_t len = sb_rd32(sb_hdr + 8), cnt = sb_hdr[6], crc;
    if (sb_rd32(sb_hdr) != SMP_BANK_MAGIC || sb_hdr[4] != SMP_BANK_VER || sb_hdr[5] || !cnt || cnt > SMP_NUSER ||
        len > SMP_BANK_SIZE - SMP_BANK_HDR || sb_rd32(sb_hdr + 16) != SMP_BANK_SIZE)
        return 5;
    if (smp_bank_crc(len, &crc) || crc != sb_rd32(sb_hdr + 12))
        return 6;
    if (sb_record_mark(len))
        return 8;
    if (plat_data_write(SMP_BANK_BASE, sb_hdr, SMP_ENT0 + cnt * SMP_ENT))
        return 3;
    sb_open = 0;
    return smp_user_scan() == (int)cnt ? 0u : 7u;
}

/* a frame (between F0 and F7): 1 = ours, answered */
static int smp_frame(const uint8_t *p, uint32_t n)
{
    uint32_t cmd, na, rc = 0, i;
    const uint8_t *a;
    if (n < 4u || p[0] != SB_HDR0 || p[1] != SB_HDR1 || p[2] != SB_HDR2)
        return 0;
    cmd = p[3];
    a = p + 4;
    na = n - 4u;
    sb_begin_reply(cmd);
    switch (cmd) {
    case SB_INFO: {
        const char *nm = "LOOM-8 " APP_VERSION;
        for (i = 0; nm[i] && i < 24u; i++)
            sb_b((uint8_t)nm[i]);
        sb_b(0);
        for (i = 0; i < 6u; i++)
            sb_b(0);
        break;
    }
    case SB_PING:
        sb_b(0);
        break;
    case SB_SINFO: {
        uint32_t from = na ? a[0] : 0u, used = (smp_user_bytes() + 1023u) / 1024u, cnt = (uint32_t)smp_user_count(), k;
        k = from < cnt ? cnt - from : 0u;
        k = k > SB_INFO_K ? SB_INFO_K : k;
        sb_b(2);
        sb_b(SMP_BANK_SIZE / 1024u);
        sb_b(SMP_BANK_SIZE / 1024u >> 7);
        sb_b(used);
        sb_b(used >> 7);
        sb_b(cnt);
        sb_b(from);
        sb_b(k);
        for (i = from; i < from + k; i++) {
            const smp_t *sm = smp_get(smp_factory() + (int)i);
            sb_str(smp_name(smp_factory() + (int)i));
            sb_b(sm->rate);
            sb_b(sm->rate >> 7u);
            sb_b(sm->rate >> 14u);
            sb_b(sm->n);
            sb_b(sm->n >> 7);
            sb_b(sm->n >> 14);
        }
        break;
    }
    case SB_BEGIN: case SB_ERASE:
        if (na < 1u || a[0] != 0u)
            return 1;
        sb_b(a[0]);
        if (!sb_flash()) {
            sb_b(2);
            break;
        }
        smp_user_drop();                             /* nothing may read the bank from here */
        sb_open = 1;
        for (i = 0; i < (cmd == SB_ERASE ? SMP_BANK_SIZE / 0x1000u : 1u) && !rc; i++)
            rc = sb_erase(i * 0x1000u) ? 2u : 0u;
        if (cmd == SB_ERASE) {
            sb_open = 0;
            ui_say("SAMPLES", "user bank erased");
        }
        sb_b(rc);
        break;
    case SB_WRITE: {
        uint32_t off, len;
        if (na < 5u || a[0] != 0u)
            return 1;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = sb_unpack7(a + 4, na - 4u, sb_buf, 256u);
        if (off < SMP_BANK_HDR || (off & 0xFFu) || !len || off + len > SMP_BANK_SIZE)
            rc = 1;
        else if (!sb_open || smp_user_count())
            rc = 4;
        else if (!sb_flash())
            rc = 3;
        else {                                       /* (a 256-byte page never crosses a window: they are 4 KB
                                                      * aligned) */
            if (!(off & 0xFFFu) && sb_erase(off))
                rc = 2;
            if (!rc && sb_write(off, sb_buf, len))
                rc = 3;
        }
        sb_b(a[0]);
        sb_b(off);
        sb_b(off >> 7);
        sb_b(off >> 14);
        sb_b(rc);
        break;
    }
    case SB_HDR: {
        uint32_t part;
        if (na < 2u || a[0] != 0u || a[1] >= SB_NPART)
            return 1;
        part = a[1];
        if (part == 0u)
            for (i = 0; i < SB_NPART * SB_PART; i++)
                sb_hdr[i] = 0;
        sb_unpack7(a + 2, na - 2u, sb_hdr + part * SB_PART, SB_PART);
        sb_b(a[0]);
        sb_b(part);
        sb_b(0);
        break;
    }
    case SB_END:
        if (na < 1u || a[0] != 0u)
            return 1;
        sb_b(a[0]);
        rc = !sb_flash() ? 3u : !sb_open ? 4u : sb_end();
        sb_b(rc);
        if (!rc) {
            char t[16];
            char *e = t;
            int c = smp_user_count();
            if (c >= 10)
                *e++ = (char)('0' + c / 10);
            *e++ = (char)('0' + c % 10);
            for (const char *w = c == 1 ? " sample" : " samples"; *w; w++)
                *e++ = *w;
            *e = 0;
            ui_say("LOADED", t);
        }
        break;
    default:
        return 0;                                    /* not ours */
    }
    sb_out[sb_n++] = 0xF7;
    plat_sysex_send(sb_out, sb_n);
    return 1;
}
