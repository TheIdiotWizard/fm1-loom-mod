/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 audio engine; see engine.h. The shape is X0X's: a command queue from the main loop, the
 * sequencer stepped sample-accurately between renders, one stretch of frames rendered as the first
 * core's part plus the second core's job, summed in one fixed order so a device with one core and a
 * device with two produce the same samples, then the sends, the two FX tracks, the master.
 *
 * Who owns which bytes: the synth reads the project's par[] arrays directly (synth_init takes the
 * pointer; the main loop writes bytes). The drumlines and the FX tracks read the engine's
 * own copy (epar), so a parameter lock can override a knob without touching the project's value,
 * and the knob's value returns when the lock ends. The sequencer is only ever stepped here. */
#include "engine.h"
#include "plat.h"
#include "app.h"
#include "fastmath.h"

#define POOL PLAT_POOL
#if XY_RAM_DSP
/* the FX and the master run from RAM in this build (tools/size_fns.py RAM_DSP): out of a direct call's
 * reach from the XIP window, so they are called through a pointer the compiler cannot fold */
static inline void *dsp_far(void *p) { void *volatile q = p; return q; }
#define DSP_FAR(fn) ((__typeof__(&fn))dsp_far((void *)&fn))
#else
#define DSP_FAR(fn) fn
#endif

/* time per stage of the render, in plat_cycles() units: running totals (wrapping; the Status page
 * takes differences) and the frames rendered */
volatile uint32_t eng_prof[ENG_PROF_N], eng_prof_frames;
#define PROF_T(v) uint32_t v = plat_cycles()
#define PROF_ADD(k, t0) (eng_prof[k] += plat_cycles() - (t0))

/* ---------------------------------------------------------------- state --- */
seq_t seq;
volatile uint8_t eng_step[NSEQT];
volatile uint16_t eng_peak[NALLTRK];
volatile uint8_t eng_drum_hit[NDRUM];
volatile uint8_t eng_voices, eng_voices_max = NVOICE;
volatile uint32_t eng_guard_count;
volatile uint32_t eng_punch_drum, eng_punch_mel;
volatile float eng_pump_db;
volatile uint8_t eng_scene_now, eng_scene_cue = 0xFF;
int16_t eng_scope[SCOPE_N];
volatile uint32_t eng_scope_w;

/* the units, in the pool (their structs are opaque: a byte array each, 8-aligned for their floats) */
static uint8_t synth_mem[SYNTH_BYTES] POOL __attribute__((aligned(8)));
/* drumline units: NDL of them for the pool's sake (three drum tracks at once; a fourth drum track stays silent
 * until one of the others changes engine). Three, not four, since step_t grew to twelve bytes. */
#define NDL 3
static uint8_t dl_mem[NDL][DRUMLINE_BYTES] POOL __attribute__((aligned(8)));
static uint8_t dl_slot[NTRK];                              /* the unit a drum track owns, 0xFF none */
static uint8_t dl_owner[NDL];                              /* the track owning each unit, 0xFF free */
static uint8_t fx_mem[2][FXSLOT_BYTES] POOL __attribute__((aligned(8)));
static uint8_t mst_mem[MASTER_BYTES] __attribute__((aligned(8)));
#define SY ((synth_t *)(void *)synth_mem)
#define DL(t) ((drumline_t *)(void *)dl_mem[dl_slot[t]])
static int dl_take(int t)                                  /* a free unit for track t: 1 = got one */
{
    int k;
    if (dl_slot[t] != 0xFFu)
        return 1;
    for (k = 0; k < NDL; k++)
        if (dl_owner[k] == 0xFFu) {
            dl_owner[k] = (uint8_t)t;
            dl_slot[t] = (uint8_t)k;
            return 1;
        }
    return 0;
}
static void dl_drop(int t)
{
    if (dl_slot[t] != 0xFFu) {
        dl_owner[dl_slot[t]] = 0xFF;
        dl_slot[t] = 0xFF;
    }
}
#define FXS(k) ((fxslot_t *)(void *)fx_mem[k])
#define MST ((master_t *)(void *)mst_mem)
/* the lines (the memory plan, docs/superpowers/specs: one delay, one reverb, the master tape's). FX I has
 * the delay lines, FX II the reverb line: the notebook's default project (FX I delay, FX II reverb) and the
 * classic delay-into-reverb (FX I's return feeds FX II). A second delay pair (88 KB) or reverb line (64 KB:
 * the SPU modes at the console's size) does not fit beside the canvas and the patterns; fx_has() tells the
 * UI which effects a slot can do. */
static int16_t dly1_l[FX_DLY_LEN] POOL, dly1_r[FX_DLY_LEN] POOL;
static int16_t rv2_line[FX_RV_LEN] POOL;
static float mst_tape[2][MST_TAPE_DLY] POOL;

/* the engine's parameter copy (drumlines, FX read it; locks override it) and the project's
 * contiguous array (app.h proj_par: the synth and the sequencer read that one) */
static uint8_t epar[NALLTRK][NTP];
static uint8_t (*ppar)[NTP];
static uint8_t lock_on[NALLTRK][NTP / 8 + 1];     /* bit: the parameter is under a lock */
static uint8_t eng_drum[NTRK];                     /* the track's engine is the drumline (ISR's view) */
static uint8_t fx_type[2];                        /* each slot's effect, as set */
static uint32_t metro_left;                       /* the metronome's click: samples left, phase, rate */
static float metro_ph, metro_inc;
static int16_t bend_c[NTRK];                      /* the bend component's cents, per track */
static uint8_t perf_tail[NTRK], perf_mute[NTRK];  /* the drum tracks' punch state (the synth's perf_t is the synth's) */
static int8_t perf_pan[NTRK];
static int8_t perf_pan_base[NTRK];                /* the sequencer's pan offset (the arp's stereo), under the LFO */
static uint8_t dl_punch_hp[NTRK];                 /* a drum track under the punch filter (its dynamic filter raised) */
static float pan_lfo_ph;                          /* the punch pan LFO, ~2 Hz */
static float vib_ph;                              /* the punch vibrato, 6 Hz */

/* the second core */
static int (*c2_run)(void (*)(uint32_t), uint32_t);
static int (*c2_wait)(void);
void engine_set_core2(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void))
{
    c2_run = run;
    c2_wait = wait;
}
int engine_slice_now(int trk, int *slice, int32_t *pos)
{
    return trk >= 0 && trk < NTRK && synth_slice_now(SY, trk, slice, pos);
}
int engine_core2_state(void)
{
    if (!c2_run)
        return 0;
    if (plat_cpu2_barred())
        return 3;
    return plat_cpu2_on() ? 1 : 2;
}

/* ------------------------------------------------------- overload guard --- */
/* past the CPU (a block over 92 %, or 85 % on average) the synth plays eight voices instead of
 * sixteen (the quietest end early); after 3 s under 65 % it has them back. The platform reports each
 * block's load after rendering it (same context as engine_render); the change is made at the start
 * of the next block. */
#define GUARD_ON_LAST 92u
#define GUARD_ON_AVG 85u
#define GUARD_OFF 65u
#define GUARD_CALM_FRAMES (3u * 44100u)
#define GUARD_VOICES 8
/* before that, brocade's saws thin (synth_saws): past 80 % in a block or 72 % on average its voices lose
 * their outer pair, 9 -> 7 -> 5 -> 3 saws, a step every 50 ms while it lasts; after a second under 60 % a
 * pair comes back (each saw fades out or in over 12 ms: no click). Without brocade playing it changes nothing. */
#define THIN_ON_LAST 80u
#define THIN_ON_AVG 72u
#define THIN_OFF 60u
#define THIN_STEP_FRAMES (44100u / 20u)
#define THIN_CALM_FRAMES 44100u
#define THIN_FEWEST 3
static uint8_t guard_on, guard_applied;
static uint32_t guard_avg_q8, guard_calm, guard_frames;
volatile uint8_t eng_saws_max = 9, eng_saws_applied = 9;
static uint32_t thin_wait, thin_calm;
static void thin_load(uint32_t pct, uint32_t frames)
{
    if (pct > THIN_ON_LAST || guard_avg_q8 > THIN_ON_AVG * 256u) {
        thin_calm = 0;
        if (thin_wait > frames) {
            thin_wait -= frames;
        } else {
            thin_wait = THIN_STEP_FRAMES;
            if (eng_saws_max > THIN_FEWEST)
                eng_saws_max = (uint8_t)(eng_saws_max - 2);
        }
    } else if (pct < THIN_OFF && guard_avg_q8 < THIN_OFF * 256u) {
        thin_wait = 0;
        if ((thin_calm += frames) >= THIN_CALM_FRAMES) {
            thin_calm = 0;
            if (eng_saws_max < 9)
                eng_saws_max = (uint8_t)(eng_saws_max + 2);
        }
    } else {
        thin_calm = 0;
    }
}
void engine_load(uint32_t pct)
{
    uint32_t frames = guard_frames;
    guard_frames = 0;
    guard_avg_q8 = (guard_avg_q8 * 15u + pct * 256u) / 16u;
    thin_load(pct, frames);
    if (!guard_on) {
        if (pct > GUARD_ON_LAST || guard_avg_q8 > GUARD_ON_AVG * 256u) {
            guard_on = 1;
            guard_calm = 0;
            eng_guard_count++;
        }
    } else if (pct >= GUARD_OFF || guard_avg_q8 >= GUARD_OFF * 256u) {
        guard_calm = 0;
    } else if ((guard_calm += frames) >= GUARD_CALM_FRAMES) {
        guard_on = 0;
    }
}
static void guard_apply(void)
{
    if (eng_saws_max != eng_saws_applied) {
        eng_saws_applied = eng_saws_max;
        synth_saws(SY, eng_saws_applied);
    }
    if (guard_on == guard_applied)
        return;
    guard_applied = guard_on;
    eng_voices_max = (uint8_t)(guard_on ? GUARD_VOICES : NVOICE);
    synth_limit(SY, eng_voices_max);
}

/* ------------------------------------------------------- command queue --- */
/* uint32 commands, single producer (main loop) / single consumer (audio ISR): type:4 | a:8 | b:8 | c:12 */
enum { C_PARAM, C_ALL, C_KEY, C_KEYS_OFF, C_PUNCH, C_BRAIN_KEY, C_MASTER, C_FX, C_PANIC,
       C_METRO, C_TEMPO, C_GROOVE, C_SCENE, C_NOP };     /* at most sixteen: the type is four bits */
#define CQ 512u
static uint32_t cq[CQ];
static volatile uint32_t cq_w, cq_r;

static void cq_put(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t w = cq_w;
    if (w - cq_r >= CQ)
        return;                        /* full: drop (a knob flood; the project still has the value) */
    cq[w % CQ] = (type << 28) | ((a & 0xFFu) << 20) | ((b & 0xFFu) << 12) | (c & 0xFFFu);
    __asm__ volatile("" ::: "memory");
    cq_w = w + 1;
}

/* the FX settings travel beside the queue (too many bytes for one word): the main loop writes them,
 * posts C_FX, and the interrupt reads them whole at the next block. A knob caught between two writes
 * lasts one block. */
static struct { uint8_t type, knob[4], hp, lp; } fx_req[2];

/* the knob's value into the contiguous array the synth and the sequencer read (one byte: safe beside
 * the interrupt), then the command, for what needs doing with it (a kind change, a unit told) */
void engine_set(int trk, int param, int val)
{
    if (trk < 0 || trk >= NALLTRK || param < 0 || param >= NTP)
        return;
    if (val < 0)
        val = 0;
    if (val > 255)
        val = 255;
    ppar[trk][param] = (uint8_t)val;
    cq_put(C_PARAM, (uint32_t)trk, (uint32_t)param, (uint32_t)val);
}
void engine_set_all(int trk)
{
    int k;
    if (trk < 0 || trk >= NALLTRK)
        return;
    for (k = 0; k < NTP; k++)
        ppar[trk][k] = proj.trk[trk].par[k];
    cq_put(C_ALL, (uint32_t)trk, 0, 0);
}
void engine_key(int trk, int note, int on, int vel)
{
    cq_put(C_KEY, (uint32_t)trk, (uint32_t)note, (uint32_t)((on ? 0x80u : 0u) | ((uint32_t)vel & 0x7Fu)));
}
void engine_keys_off(int trk) { cq_put(C_KEYS_OFF, (uint32_t)trk, 0, 0); }
void engine_punch(int fx, int on, int target_trk)
{
    cq_put(C_PUNCH, (uint32_t)fx, (uint32_t)(on != 0), (uint32_t)(target_trk < 0 ? 0xFF : target_trk));
}
void engine_brain_key(int note, int on) { cq_put(C_BRAIN_KEY, (uint32_t)note, (uint32_t)(on != 0), 0); }
void engine_master(int p, int val)
{
    if (p >= 0 && p < NMS)
        cq_put(C_MASTER, (uint32_t)p, 0, (uint32_t)val);
}
void engine_fx(int slot, int type, const uint8_t knob[4], int hp, int lp)
{
    int k;
    if (slot < 0 || slot > 1)
        return;
    fx_req[slot].type = (uint8_t)type;
    for (k = 0; k < 4; k++)
        fx_req[slot].knob[k] = knob[k];
    fx_req[slot].hp = (uint8_t)hp;
    fx_req[slot].lp = (uint8_t)lp;
    cq_put(C_FX, (uint32_t)slot, 0, 0);
}
void engine_panic(void) { cq_put(C_PANIC, 0, 0, 0); }
void engine_metronome(int mode) { cq_put(C_METRO, (uint32_t)mode, 0, 0); }
void engine_tempo(int bpm_x10)                  /* 400..3000 fits the command's twelve-bit value */
{
    if (bpm_x10 < 400)
        bpm_x10 = 400;
    if (bpm_x10 > 3000)
        bpm_x10 = 3000;
    cq_put(C_TEMPO, 0, 0, (uint32_t)bpm_x10);
}
void engine_groove(int type)
{
    if (type >= 0 && type < NGROOVE)
        cq_put(C_GROOVE, (uint32_t)type, 0, 0);
}
void engine_scene(int scene, int cued)
{
    if (scene >= 0 && scene < NSCENE)
        cq_put(C_SCENE, (uint32_t)scene, (uint32_t)(cued != 0), 0);
}
void engine_transport(int what)                 /* the sequencer's flags: written from any context */
{
    switch (what) {
    case 0: seq_stop(&seq); break;
    case 1: seq_start(&seq, 0); break;
    case 2: seq_continue(&seq); break;
    default: seq_start(&seq, 1); break;
    }
}

/* --------------------------------------------------------------- mixing --- */
/* a gain that glides to its target (one pole, ~10 ms) instead of stepping each block, which zippered
 * on quick turns of a level or pan (X0X). cur < 0: never used, takes the target at once (no fade-in
 * at boot); settled, cur IS the target, so the product is the unsmoothed one exactly */
typedef struct { float cur, tgt; } glide_t;
#define GLIDE_A 0.0022651f                                /* 1 - exp(-1 / (10 ms * 44.1 kHz)) */
static void glide_set(glide_t *g, float t)
{
    g->tgt = t;
    if (g->cur < 0.0f)
        g->cur = t;
}
__attribute__((noinline)) static void add_glide(float *dst, const float *src, glide_t *g, uint32_t n)
{
    uint32_t i;
    float c = g->cur, t = g->tgt;
    if (c == t) {
        if (c != 0.0f)
            for (i = 0; i < n; i++)
                dst[i] += src[i] * c;
        return;
    }
    for (i = 0; i < n; i++) {
        c += (t - c) * GLIDE_A;
        dst[i] += src[i] * c;
    }
    if (fm_fabsf(t - c) < 1e-5f)
        c = t;
    g->cur = c;
}
/* a balance law: centre is full level on both sides, turning toward a side fades the other out along
 * a quarter cosine; the near side stays at 1 */
static void pan_gains(int pan, float *gl, float *gr)
{
    float x = ((float)(pan < 0 ? 0 : pan > 127 ? 127 : pan) - 64.0f) / 63.0f;   /* -1 .. 1 */
    if (x < 0.0f) {
        *gl = 1.0f;
        *gr = fm_cosf(-x * 1.5707963f);
    } else {
        *gr = 1.0f;
        *gl = fm_cosf(x * 1.5707963f);
    }
}
enum { GL_L, GL_R, GL_SEND, GL_N };
static glide_t trk_glide[NTRK][GL_N];             /* the drum tracks' level and pan (the synth has its own) */
static glide_t ret_glide[2][2];                   /* the FX returns */

static float trk_peak[NALLTRK];
__attribute__((noinline)) static void meter(int p, const float *x, uint32_t n)
{
    uint32_t i;
    float pk = trk_peak[p];
    for (i = 0; i < n; i++) {
        float a = fm_fabsf(x[i]);
        if (a > pk)
            pk = a;
    }
    trk_peak[p] = pk;
}

/* ------------------------------------------------------------- params --- */
/* a drum track's Filter page into its drumline (engine.h: the dynamic filter's four values, forced on
 * with a raised low cut while the punch filter is on it) */
static void dl_filter_update(int t)
{
    const uint8_t *p = epar[t];
    int type = p[TP_FTYPE], locut = p[TP_CUT], comp = p[TP_RES], drive = p[TP_FENV], hicut = p[TP_KTRK];
    if (!eng_drum[t])
        return;
    if (dl_punch_hp[t]) {
        if (type != FT_DYNAMIC) {                           /* neutral: only the low cut acts */
            comp = 0;
            drive = 0;
            hicut = 127;
            locut = 0;
        }
        type = FT_DYNAMIC;
        if (locut < PUNCH_HP)
            locut = PUNCH_HP;
    }
    drumline_filter(DL(t), type, locut, comp, drive, hicut);
}
static int is_filter_param(int param)
{
    return param == TP_FTYPE || param == TP_CUT || param == TP_RES || param == TP_FENV || param == TP_KTRK;
}
/* a lock or a knob reaching a unit that reads epar (the drumline reads it live; the FX are told) */
static void aux_changed(int trk, int param)
{
    const uint8_t *p = epar[trk];
    if ((trk == AUX_FX1 || trk == AUX_FX2) && param >= TP_E1 && param <= TP_E4)
        fx_set(FXS(trk - AUX_FX1), fx_type[trk - AUX_FX1], p + TP_E1);
}
static void apply_param(int trk, int param, int val)       /* ISR: C_PARAM */
{
    int locked = (lock_on[trk][param >> 3] >> (param & 7)) & 1;
    if (!locked)
        epar[trk][param] = (uint8_t)val;
    if (trk < NTRK && param == TP_ENGINE) {
        int drum = val == EG_DRUMLINE;
        if (drum != eng_drum[trk]) {                        /* the track changes kind: silence, then a fresh unit */
            synth_all_off(SY, trk);
            if (eng_drum[trk])
                drumline_choke(DL(trk));
            dl_drop(trk);
            eng_drum[trk] = (uint8_t)(drum && dl_take(trk));
            if (eng_drum[trk]) {
                drumline_init(DL(trk), epar[trk]);
                dl_filter_update(trk);
            }
        }
    } else if (trk < NTRK && eng_drum[trk] && is_filter_param(param) && !locked) {
        dl_filter_update(trk);
    } else if (trk == AUX_BRAIN && param == TP_E4) {
        seq.brain_link = (uint8_t)(val > NTRK ? NTRK : val);   /* the linked track 1..8, 0 none */
    } else if (trk >= NTRK && !locked) {
        aux_changed(trk, param);
    }
}
static void apply_all(int trk)                             /* ISR: C_ALL (a load, a preset) */
{
    int p;
    for (p = 0; p < NTP; p++)
        lock_on[trk][p >> 3] = 0;                          /* fresh values: no lock survives */
    for (p = 0; p < NTP; p++)
        epar[trk][p] = ppar[trk][p];
    if (trk < NTRK) {
        int drum = ppar[trk][TP_ENGINE] == EG_DRUMLINE;
        synth_all_off(SY, trk);
        if (eng_drum[trk])
            drumline_choke(DL(trk));
        dl_drop(trk);
        eng_drum[trk] = (uint8_t)(drum && dl_take(trk));
        if (eng_drum[trk]) {
            drumline_init(DL(trk), epar[trk]);
            dl_filter_update(trk);
        }
    } else {
        if (trk == AUX_BRAIN)
            seq.brain_link = (uint8_t)(epar[trk][TP_E4] > NTRK ? NTRK : epar[trk][TP_E4]);
        aux_changed(trk, TP_E1);
    }
}

/* ------------------------------------------------------- sequencer sink --- */
/* the master's PUMP keys: a kit's hit (a kick: its first key, or a key on the kick model) and any note of a
 * track; the master takes the one it follows (fx.h PB_*). Also from a muted track's steps (s_ghost) */
static void pump_drum(int trk, int sound, int vel)
{
    if (sound == 0 || epar[trk][TP_DRUM(sound, DP_MODEL)] == DM_KICK)
        master_key(MST, PB_KICK, vel);
    master_key(MST, PB_T1 + trk, vel);
}
static void s_note_on(void *x, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    (void)x;
    if (trk < 0 || trk >= NTRK || eng_drum[trk])
        return;
    synth_note_on(SY, trk, note, vel, glide_ms, tag);
    master_key(MST, PB_T1 + trk, vel);
}
static void s_ghost(void *x, int trk, int sound, int vel)   /* a muted track's step: it keys the pump, silently */
{
    (void)x;
    if (trk < 0 || trk >= NTRK)
        return;
    if (eng_drum[trk] && sound >= 0 && sound < NDRUM)
        pump_drum(trk, sound, vel);
    else if (!eng_drum[trk])
        master_key(MST, PB_T1 + trk, vel);
}
static void s_note_off(void *x, int trk, uint32_t tag)
{
    (void)x;
    if (trk >= 0 && trk < NTRK && !eng_drum[trk])
        synth_note_off(SY, trk, tag);
}
static void s_drum(void *x, int trk, int sound, int vel, int pitch_semi, int short_gate, int long_tail)
{
    (void)x;
    if (trk < 0 || trk >= NTRK || !eng_drum[trk] || sound < 0 || sound >= NDRUM)
        return;
    drumline_hit(DL(trk), sound, vel, pitch_semi, short_gate, long_tail || perf_tail[trk]);
    synth_duck_trigger(SY, trk);                       /* duck LFOs listening to this track drop */
    pump_drum(trk, sound, vel);
    if (trk == proj.trk_sel)
        eng_drum_hit[sound] = (uint8_t)(vel >= 127 ? 255 : vel * 2);
}
static void s_drum_off(void *x, int trk, int sound)    /* the key or the step ends: gated and loop modes stop */
{
    (void)x;
    if (trk < 0 || trk >= NTRK || !eng_drum[trk] || sound < 0 || sound >= NDRUM)
        return;
    drumline_release(DL(trk), sound);
}
static void s_pan(void *x, int trk, int pan)           /* the player's pan offset (the arp's stereo), -64..64 */
{
    (void)x;
    if (trk >= 0 && trk < NTRK)
        perf_pan_base[trk] = (int8_t)(pan < -64 ? -64 : pan > 64 ? 64 : pan);
}
static void s_lock(void *x, int trk, int param, int val)
{
    (void)x;
    if (trk < 0 || trk >= NALLTRK || param < 0 || param >= NTP)
        return;
    if (trk < NTRK && !eng_drum[trk]) {
        synth_lock(SY, trk, param, val);               /* the synth reads the project's par: it overlays itself */
        return;
    }
    if (val == LOCK_NONE) {
        lock_on[trk][param >> 3] &= (uint8_t)~(1u << (param & 7));
        epar[trk][param] = ppar[trk][param];
    } else {
        lock_on[trk][param >> 3] |= (uint8_t)(1u << (param & 7));
        epar[trk][param] = (uint8_t)val;
    }
    if (trk >= NTRK)
        aux_changed(trk, param);
    else if (is_filter_param(param))
        dl_filter_update(trk);
}
static void s_bend(void *x, int trk, int cents)
{
    (void)x;
    if (trk >= 0 && trk < NTRK)
        bend_c[trk] = (int16_t)cents;
}
/* the live FX state: per track, its group's mask (drum tracks the drum FX, the rest the melodic) into
 * the track's performance state. Both masks arrive as bits PX_* (the sequencer shifts the melodic
 * octave down; eng_punch_mel is the same). Here: mute, wobble (drums: the high-pass and a wobble on what is long;
 * melodic: a short vibrato), pan (the pan LFO), ring (the drums' tails). Stutter, repeat, echo, the fills,
 * octave and the note lengths are the sequencer's own. */
static uint32_t punch_d, punch_m;
static void s_punch(void *x, uint32_t drum_mask, uint32_t mel_mask)
{
    (void)x;
    punch_d = drum_mask;
    punch_m = mel_mask;
    eng_punch_drum = drum_mask;
    eng_punch_mel = mel_mask;
}
static void s_midi(void *x, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t cin = st >= 0xF0 ? 0x0Fu : (uint32_t)(st >> 4);
    (void)x;
    plat_midi_out(cin | ((uint32_t)st << 8) | ((uint32_t)d1 << 16) | ((uint32_t)d2 << 24));
}
static void s_step(void *x, int trk, int step)
{
    (void)x;
    if (trk >= 0 && trk < NSEQT)
        eng_step[trk] = (uint8_t)step;
}
static void s_scene(void *x, int scene)
{
    (void)x;
    eng_scene_now = (uint8_t)scene;
}
static void s_metro(void *x, int accent)
{
    (void)x;
    metro_left = 1800u;                                  /* 40 ms */
    metro_ph = 0.0f;
    metro_inc = (accent ? 1760.0f : 1320.0f) / 44100.0f;
    synth_duck_trigger(SY, DUCK_METRO);
    master_key(MST, PB_METRO, accent ? 127 : 100);
}
/* named members: the sequencer may grow its sink at the end without this changing */
static const seq_sink_t SINK = {
    .note_on = s_note_on, .note_off = s_note_off, .drum = s_drum, .lock = s_lock, .bend = s_bend, .punch = s_punch,
    .midi = s_midi, .step = s_step, .scene = s_scene, .metro = s_metro, .pan = s_pan, .drum_off = s_drum_off,
    .ghost = s_ghost, .ctx = 0};

/* the performance state of every instrument track from the punch masks and the bends: once a stretch */
#define PUNCH_VIB_DRUM 30.0f                               /* cents, the wobble under the drums' filter */
#define PUNCH_VIB_MEL 20.0f                                /* the melodic tracks' short vibrato */
static void perf_tick(uint32_t n)
{
    int t;
    float lfo, vib;
    pan_lfo_ph += (float)n * (2.0f / 44100.0f);
    if (pan_lfo_ph >= 1.0f)
        pan_lfo_ph -= 1.0f;
    vib_ph += (float)n * (6.0f / 44100.0f);
    if (vib_ph >= 1.0f)
        vib_ph -= 1.0f;
    lfo = fm_sin_turns(pan_lfo_ph);
    vib = fm_sin_turns(vib_ph);
    for (t = 0; t < NTRK; t++) {
        perf_t *p = synth_perf(SY, t);
        int drum = eng_drum[t];
        uint32_t m = drum ? punch_d : punch_m;
        int mute = (int)((m >> PX_MUTE) & 1u), pan_on = (int)((m >> PX_PAN) & 1u);
        int filt = (int)((m >> PX_WARBLE) & 1u), tail = (int)((m >> PX_RING) & 1u), pan;
        p->pitch_c = (int16_t)(bend_c[t] + (filt ? (int)(vib * (drum ? PUNCH_VIB_DRUM : PUNCH_VIB_MEL)) : 0));
        p->cut = 0;
        p->hp = (uint8_t)(drum && filt ? PUNCH_HP : 0);
        pan = perf_pan_base[t] + (pan_on ? (int)(lfo * 60.0f) : 0);
        p->pan = (int8_t)(pan < -127 ? -127 : pan > 127 ? 127 : pan);
        p->mute = (uint8_t)mute;
        p->send1 = 0;
        p->send2 = 0;
        p->long_tail = (uint8_t)tail;
        perf_tail[t] = (uint8_t)tail;
        perf_mute[t] = (uint8_t)mute;
        perf_pan[t] = p->pan;
        if (drum && dl_punch_hp[t] != (uint8_t)filt) {    /* the drumline's filter follows the punch state */
            dl_punch_hp[t] = (uint8_t)filt;
            dl_filter_update(t);
        }
    }
}

/* ---------------------------------------------------------------- MIDI in --- */
static int midi_track(int ch)                      /* the track on this channel, else the one selected */
{
    int t;
    for (t = 0; t < NTRK; t++)
        if (ppar[t][TP_CHAN] == ch + 1)
            return t;
    return proj.trk_sel < NTRK ? proj.trk_sel : 0;
}
static void midi_in(uint32_t pkt)
{
    uint8_t st = (uint8_t)(pkt >> 8), d1 = (uint8_t)(pkt >> 16) & 0x7F, d2 = (uint8_t)(pkt >> 24) & 0x7F;
    uint8_t ch = st & 0x0F, ty = st & 0xF0;
    switch (st) {
    case 0xF8: seq_ext_clock(&seq); return;
    case 0xFA: seq_start(&seq, 1); return;
    case 0xFB: seq_continue(&seq); return;
    case 0xFC: seq_stop(&seq); return;
    default: break;
    }
    if (ty == 0x90 && d2 == 0)
        ty = 0x80;
    if (ty == 0x90 || ty == 0x80)
        seq_key(&seq, midi_track(ch), d1, ty == 0x90, d2);
    else if (ty == 0xE0) {                         /* pitch bend: +-2 semitones onto the track's bend */
        int t = midi_track(ch);
        bend_c[t] = (int16_t)((((int)d2 << 7 | d1) - 8192) * 200 / 8192);
    }
}
void engine_midi_in(uint32_t pkt) { midi_in(pkt); }   /* (the render drains plat_midi_in itself) */

/* ---------------------------------------------------------------- render --- */
static void drain(void)
{
    uint32_t r = cq_r, pkt;
    int guard = 64;
    while (r != cq_w) {
        uint32_t c = cq[r % CQ], type = c >> 28, a = (c >> 20) & 0xFFu, b = (c >> 12) & 0xFFu, v = c & 0xFFFu;
        r++;
        switch (type) {
        case C_PARAM: apply_param((int)a, (int)b, (int)v); break;
        case C_ALL: apply_all((int)a); break;
        case C_KEY: seq_key(&seq, (int)a, (int)b, (int)((v >> 7) & 1u), (int)(v & 0x7Fu)); break;
        case C_KEYS_OFF: seq_key_all_off(&seq, (int)a); break;
        case C_PUNCH: seq_punch_key(&seq, (int)a, (int)b, v == 0xFFu ? -1 : (int)v); break;
        case C_BRAIN_KEY: seq_brain_key(&seq, (int)a, (int)b); break;
        case C_MASTER: master_set(MST, (int)a, (int)v); break;
        case C_FX:
            if (a < 2u) {
                int k, type = fx_req[a].type;
                if (!fx_has(FXS(a), type))
                    type = FXT_CHORUS;                      /* what the slot runs instead (fx_set's fallback) */
                /* (type: the pick, fx.h: the effect and the reverb's mode) */
                fx_type[a] = (uint8_t)type;
                for (k = 0; k < 4; k++)
                    epar[AUX_FX1 + a][TP_E1 + k] = fx_req[a].knob[k];
                epar[AUX_FX1 + a][TP_ENGINE] = (uint8_t)type;
                fx_set(FXS(a), type, fx_req[a].knob);
                fx_filter(FXS(a), fx_req[a].hp, fx_req[a].lp);
            }
            break;
        case C_PANIC: {
            int t;
            synth_all_off(SY, -1);
            for (t = 0; t < NTRK; t++) {
                seq_key_all_off(&seq, t);
                if (eng_drum[t])
                    drumline_choke(DL(t));
                bend_c[t] = 0;
            }
            break;
        }
        case C_METRO: seq.metronome = (uint8_t)a; break;
        case C_TEMPO: seq.bpm = (float)v * 0.1f; break;
        case C_GROOVE: seq.groove_type = (uint8_t)a; break;
        case C_SCENE:
            if (b)
                seq_cue_scene(&seq, (int)a);
            else
                seq_set_scene(&seq, (int)a);
            break;
        default: break;
        }
    }
    cq_r = r;
    while (guard-- && plat_midi_in(&pkt, 0))         /* USB or the TRS jack */
        midi_in(pkt);
}

/* the mix of one stretch between two events: the percussion group, the melodic group, the two
 * sends; the second core's own; the drumlines' and the FX returns' scratch. In .bss, not the pool
 * (the pool is for the lines). 256 frames each: the most one render asks for. */
static float perc_l[256], perc_r[256], mel_l[256], mel_r[256], s1[256], s2[256];
static float c2_l[256], c2_r[256], c2_s1[256], c2_s2[256];
static float tl[256], tr[256], ts1[256], ts2[256];
static float mix_l[256], mix_r[256];

__attribute__((noinline)) static void zerof(float *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0.0f;
}

/* the second core's job (or the first's, after its own part, with one core): the voices [0, k) of the
 * pool into its own buffers (k: arg bits 16..20, split_at); arg bit 8: not timed (the cycle counter is
 * the first core's). Nothing here is mixed: the first core does that afterwards, in one core's order.
 * The synth wants every range rendered each stretch after its prepare (synth.h), sounding or not */
static void core2_job(uint32_t arg)
{
    uint32_t n = arg & 0xFFu ? arg & 0xFFu : 256u, timed = !(arg & 0x100u), pt = timed ? plat_cycles() : 0;
    zerof(c2_l, n);
    zerof(c2_r, n);
    zerof(c2_s1, n);
    zerof(c2_s2, n);
    synth_render(SY, 0, (int)(arg >> 16 & 31u), c2_l, c2_r, c2_s1, c2_s2, n);
    if (timed)
        PROF_ADD(PR_SYNTH1, pt);
}

/* where the pool splits between the cores: the second renders voices [0, k), the first [k, NVOICE) and
 * the drum tracks, so k evens the two out. A free voice is the lowest one (synth.c find_voice): with a
 * fixed half, eight voices or fewer all landed on the second core while the first waited (and the
 * overload guard's eight did too). The cost in rough units: a synth voice 4 (brocade's more, by its saws,
 * and cane's bell about two: synth_voice_cost), a drum voice 2, a drum track's own filter and mix 1 (tests/host: ~180, ~95 and ~50
 * instructions a sample). Of the k that
 * do best, the one nearest the middle (the old fixed split). The same k with one core or two: the
 * samples stay the same either way (only the order of the halves' sum depends on k). */
static uint32_t split_at(void)
{
    int pre[NVOICE + 1], i, k, t, drum = 0, best = NVOICE / 2, cost = 1 << 30;
    for (t = 0; t < NTRK; t++)
        if (eng_drum[t]) {
            int nv = drumline_voices(DL(t));
            if (nv)
                drum += 1 + 2 * nv;
        }
    pre[0] = 0;
    for (i = 0; i < NVOICE; i++)
        pre[i + 1] = pre[i] + synth_voice_cost(SY, i);
    for (k = 0; k <= NVOICE; k++) {
        int a = pre[k], b = pre[NVOICE] - pre[k] + drum, c = a > b ? a : b;
        int dk = k > NVOICE / 2 ? k - NVOICE / 2 : NVOICE / 2 - k, db = best > NVOICE / 2 ? best - NVOICE / 2 : NVOICE / 2 - best;
        if (c < cost || (c == cost && dk < db)) {
            cost = c;
            best = k;
        }
    }
    return (uint32_t)best;
}

__attribute__((noinline)) static void add_in(float *dst, const float *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] += src[i];
}

/* the metronome: a short sine tick on the beat (higher on the bar), into the percussion group; the
 * sequencer says when (the sink's metro) */
static void metro_tick(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && metro_left; i++, metro_left--) {
        float env = (float)metro_left * (1.0f / 1800.0f), v = fm_sin_turns(metro_ph) * env * env * 0.5f;
        metro_ph += metro_inc;
        if (metro_ph >= 1.0f)
            metro_ph -= 1.0f;
        perc_l[i] += v;
        perc_r[i] += v;
    }
}

#ifdef PLAT_HOST
#define HOST_ON(b) (synth_host_solo >> (b) & 1u)  /* the host's stems (host.c LOOM_SOLO) */
#else
#define HOST_ON(b) 1
#endif
/* one stretch: the upper half of the voices and the drumlines here while the second core renders the
 * lower half; then the halves are summed in the order one core always used, the FX tracks, the master */
static void render_sub(uint32_t n)
{
    uint32_t arg = n & 0xFFu, pt, split;
    int on2, t, k;
    zerof(perc_l, n);
    zerof(perc_r, n);
    zerof(mel_l, n);
    zerof(mel_r, n);
    zerof(s1, n);
    zerof(s2, n);
    perf_tick(n);
    synth_prepare(SY, n);                                 /* the shared per-chunk values, before either core renders */
    split = split_at();
    arg |= split << 16;
    on2 = c2_run ? c2_run(core2_job, arg | 0x100u) : 0;
    {
        PROF_T(ps);
        synth_render(SY, (int)split, NVOICE, mel_l, mel_r, s1, s2, n);
        PROF_ADD(PR_SYNTH0, ps);
    }
    {
        PROF_T(pd);
        for (t = 0; t < NTRK; t++) {
            float g, gl, gr;
            if (!eng_drum[t] || !drumline_sounding(DL(t)))
                continue;
            zerof(tl, n);
            zerof(tr, n);
            zerof(ts1, n);
            zerof(ts2, n);
            drumline_render(DL(t), tl, tr, ts1, ts2, n);
            g = perf_mute[t] || !HOST_ON(t) ? 0.0f : (float)epar[t][TP_LEVEL] * (float)epar[t][TP_LEVEL] * (1.0f / 10000.0f);
            pan_gains(epar[t][TP_PAN] + perf_pan[t], &gl, &gr);
            glide_set(&trk_glide[t][GL_L], g * gl);
            glide_set(&trk_glide[t][GL_R], g * gr);
            glide_set(&trk_glide[t][GL_SEND], g);
            add_glide(perc_l, tl, &trk_glide[t][GL_L], n);
            add_glide(perc_r, tr, &trk_glide[t][GL_R], n);
            add_glide(s1, ts1, &trk_glide[t][GL_SEND], n);
            add_glide(s2, ts2, &trk_glide[t][GL_SEND], n);
            meter(t, tl, n);
        }
        metro_tick(n);
        PROF_ADD(PR_DRUM, pd);
    }
    pt = plat_cycles();
    if (!on2 || c2_wait())
        core2_job(arg);                                   /* one core (or the second failed): here */
    else
        PROF_ADD(PR_WAIT, pt);                            /* what the first core waited for the second */
    add_in(mel_l, c2_l, n);
    add_in(mel_r, c2_r, n);
    add_in(s1, c2_s1, n);
    add_in(s2, c2_s2, n);
    for (t = 0; t < NTRK; t++)                            /* the synth's meters, into the common ones */
        if (!eng_drum[t]) {
            float v = (float)synth_peak[t] * (1.0f / 32767.0f);
            if (v > trk_peak[t])
                trk_peak[t] = v;
        }
    {
        PROF_T(pf);
        for (k = 0; k < 2; k++) {                         /* FX I, then FX II (which hears FX I's return) */
            float g = HOST_ON(8 + k) ? (float)epar[AUX_FX1 + k][TP_LEVEL] * (float)epar[AUX_FX1 + k][TP_LEVEL] * (1.0f / 10000.0f) : 0.0f;
            zerof(tl, n);
            zerof(tr, n);
            DSP_FAR(fx_process)(FXS(k), k ? s2 : s1, tl, tr, n);
            glide_set(&ret_glide[k][0], g);
            glide_set(&ret_glide[k][1], g);
            add_glide(mel_l, tl, &ret_glide[k][0], n);
            add_glide(mel_r, tr, &ret_glide[k][1], n);
            meter(AUX_FX1 + k, tl, n);
            if (k == 0 && epar[AUX_FX1][TP_SEND2]) {      /* FX I into FX II: the FX I track's second send */
                float s = (float)epar[AUX_FX1][TP_SEND2] * (1.0f / 127.0f) * 0.5f;
                uint32_t i;
                for (i = 0; i < n; i++)
                    s2[i] += (tl[i] + tr[i]) * s;
            }
        }
        PROF_ADD(PR_FX, pf);
    }
}

void engine_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t done = 0, i;
    float vol = (float)gain_q12 * (1.0f / 4096.0f);
    int p;
    if (n > 256u)
        n = 256u;
    eng_prof_frames += n;
    guard_frames += n;
    guard_apply();
    {
        PROF_T(pt);
        float bpm;
        drain();
        bpm = seq_tempo(&seq);
        synth_tempo(SY, bpm);
        fx_tempo(FXS(0), bpm);
        fx_tempo(FXS(1), bpm);
        seq_advance(&seq, 0, &SINK);
        PROF_ADD(PR_SEQ, pt);
    }
    while (done < n) {
        uint32_t k = seq_until_event(&seq, n - done);
        if (k > n - done)
            k = n - done;
        if (!k)
            k = 1;
        render_sub(k);
        {
            PROF_T(pm);
            DSP_FAR(master_process)(MST, perc_l, perc_r, mel_l, mel_r, mix_l + done, mix_r + done, k);
            PROF_ADD(PR_MST, pm);
        }
        {
            PROF_T(pt);
            seq_advance(&seq, k, &SINK);
            PROF_ADD(PR_SEQ, pt);
        }
        done += k;
    }
    eng_scene_cue = seq.cue;
    eng_pump_db = master_pump_db(MST);
    for (i = 0; i < n; i++) {                            /* Felucca's -6 dBFS ceiling of the 24-bit codec (2^22) */
        float l = mix_l[i] * vol, r = mix_r[i] * vol;
        l = l > 2.0f ? 2.0f : l < -2.0f ? -2.0f : l;
        r = r > 2.0f ? 2.0f : r < -2.0f ? -2.0f : r;
        out_lr[2u * i] = (int32_t)(l * 4194303.0f);
        out_lr[2u * i + 1u] = (int32_t)(r * 4194303.0f);
        if (i & 1u)
            eng_scope[eng_scope_w++ & (SCOPE_N - 1u)] = (int16_t)((mix_l[i] + mix_r[i]) * 16383.5f);
    }
    for (p = 0; p < NALLTRK; p++) {                      /* meters: peak hold, the UI lets them fall */
        uint32_t q = (uint32_t)(fm_minf(trk_peak[p], 1.0f) * 32767.0f);
        if (q > eng_peak[p])
            eng_peak[p] = (uint16_t)q;
        trk_peak[p] = 0.0f;
    }
    eng_voices = (uint8_t)synth_voices_used(SY);
}

/* ----------------------------------------------------------------- init --- */
void engine_init(patterns_t *P, uint8_t (*par)[NTP])
{
    int t, k;
    ppar = par;
    for (t = 0; t < NALLTRK; t++) {
        for (k = 0; k < NTP; k++)
            epar[t][k] = par[t][k];
        for (k = 0; k < NTP / 8 + 1; k++)
            lock_on[t][k] = 0;
    }
    synth_init(SY, par);
    for (k = 0; k < NDL; k++)
        dl_owner[k] = 0xFF;
    for (t = 0; t < NTRK; t++) {
        dl_slot[t] = 0xFF;
        dl_punch_hp[t] = 0;
        perf_pan_base[t] = 0;
        eng_drum[t] = (uint8_t)(par[t][TP_ENGINE] == EG_DRUMLINE && dl_take(t));
        if (eng_drum[t]) {
            drumline_init(DL(t), epar[t]);
            dl_filter_update(t);
        }
        for (k = 0; k < GL_N; k++) {
            trk_glide[t][k].cur = -1.0f;
            trk_glide[t][k].tgt = 0.0f;
        }
        bend_c[t] = 0;
    }
    for (k = 0; k < 2; k++)
        ret_glide[k][0].cur = ret_glide[k][1].cur = -1.0f;
    fx_init(FXS(0), dly1_l, dly1_r, 0);                   /* FX I: the delay lines, no reverb */
    fx_init(FXS(1), 0, 0, rv2_line);                      /* FX II: the reverb line, no delay */
    fx_type[0] = epar[AUX_FX1][TP_ENGINE];
    fx_type[1] = epar[AUX_FX2][TP_ENGINE];
    fx_set(FXS(0), fx_type[0], epar[AUX_FX1] + TP_E1);
    fx_set(FXS(1), fx_type[1], epar[AUX_FX2] + TP_E1);
    master_init(MST, mst_tape[0], mst_tape[1]);
    seq_init(&seq, P, par);
    seq.groove_type = (uint8_t)(proj.groove < NGROOVE ? proj.groove : GR_SWING);
    seq.brain_link = (uint8_t)(par[AUX_BRAIN][TP_E4] > NTRK ? NTRK : par[AUX_BRAIN][TP_E4]);
    for (t = 0; t < NSEQT; t++)
        eng_step[t] = 0;
    eng_voices_max = NVOICE;
    eng_saws_max = eng_saws_applied = 9;
}

/* the UI asks before offering an effect: FX I has no reverb line, FX II no delay (the memory plan) */
int engine_fx_has(int slot, int pick) { return fx_has(FXS(slot & 1), pick); }
