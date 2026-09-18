#include "mixer.h"
#include "../core/macros.h"

#include <string.h>

#define MIXER_VOICE(index, gen) ((orb_voice) {(uint32_t)(index) | (uint32_t)(gen) << 16})
#define MIXER_VOICE_INDEX(h) ((h).v & 0xffffu)
#define MIXER_VOICE_GEN(h) ((h).v >> 16)

// 2^(i/12) and 2^(i/1200) as 16.16, for the pitch step.
static const uint32_t mixer_semitones[12] = {65536, 69433, 73562,  77936,  82570,  87480,
                                             92682, 98193, 104032, 110218, 116772, 123715};
static const uint32_t mixer_cents[100] = {
    65536, 65574, 65612, 65650, 65688, 65726, 65764, 65802, 65840, 65878, 65916, 65954, 65992,
    66030, 66068, 66106, 66144, 66183, 66221, 66259, 66297, 66336, 66374, 66412, 66451, 66489,
    66528, 66566, 66605, 66643, 66682, 66720, 66759, 66797, 66836, 66874, 66913, 66952, 66990,
    67029, 67068, 67107, 67145, 67184, 67223, 67262, 67301, 67340, 67378, 67417, 67456, 67495,
    67534, 67573, 67612, 67651, 67691, 67730, 67769, 67808, 67847, 67886, 67926, 67965, 68004,
    68043, 68083, 68122, 68161, 68201, 68240, 68280, 68319, 68359, 68398, 68438, 68477, 68517,
    68556, 68596, 68635, 68675, 68715, 68755, 68794, 68834, 68874, 68914, 68953, 68993, 69033,
    69073, 69113, 69153, 69193, 69233, 69273, 69313, 69353, 69393
};

// An API float in -1..1 as a 15-bit fraction, rounded.
static int32_t mixer_fraction(float x) {
    return (int32_t)(x * ORB_MIXER_ONE + (x < 0 ? -0.5f : 0.5f));
}

static int32_t mixer_mul(int32_t a, int32_t fraction) {
    return a * fraction >> 15;
}

static orb_mixer_params mixer_params_of(orb_sound_params p) {
    return (orb_mixer_params) {
        .volume = mixer_fraction(orb_clamp(p.volume, 0.0f, 1.0f)),
        .pan = mixer_fraction(orb_clamp(p.pan, -1.0f, 1.0f)),
        .pitch_cents = orb_clamp(p.pitch_cents, -2400, 2400)
    };
}

static bool mixer_id_valid(const uint64_t* ids, uint32_t count, uint32_t index, uint64_t id) {
    return index < count && ids[index] == id;
}

static bool mixer_sample_valid(const orb_assets* assets, uint32_t sample, uint64_t id) {
    return assets && mixer_id_valid(assets->sample_ids, assets->sample_count, sample, id);
}

// rate / ORB_AUDIO_RATE * 2^(cents / 1200) as 32.32: whole octaves shift, the rest
// is the two tables.
static uint64_t mixer_step(uint32_t rate, int pitch_cents) {
    int cents = pitch_cents + 2400, octave = cents / 1200 - 2, rest = cents % 1200;
    uint64_t step = ((uint64_t)rate << 32) / ORB_AUDIO_RATE;

    step = step * mixer_semitones[rest / 100] >> 16;
    step = step * mixer_cents[rest % 100] >> 16;
    return octave < 0 ? step >> -octave : step << octave;
}

// Main thread: room for one more command.
static bool mixer_room(const orb_mixer* m) {
    uint32_t head = atomic_load_explicit(&m->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&m->tail, memory_order_acquire);

    return head - tail < ORB_MIXER_RING;
}

static void mixer_push(orb_mixer* m, orb_mixer_command c) {
    if (!mixer_room(m)) {
        m->dropped++;
        return;
    }

    uint32_t head = atomic_load_explicit(&m->head, memory_order_relaxed);

    m->ring[head % ORB_MIXER_RING] = c;
    atomic_store_explicit(&m->head, head + 1, memory_order_release);
}

// Audio thread.
static bool mixer_pop(orb_mixer* m, orb_mixer_command* c) {
    uint32_t tail = atomic_load_explicit(&m->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&m->head, memory_order_acquire);

    if (tail == head) return false;

    *c = m->ring[tail % ORB_MIXER_RING];
    atomic_store_explicit(&m->tail, tail + 1, memory_order_release);
    return true;
}

// The audio thread is rendering its own sound on this voice: the claim it holds
// is the generation of the last play it applied.
static bool mixer_owns(const orb_voice_state* v, unsigned gen) {
    return gen != 0 && v->gen == gen &&
           atomic_load_explicit(&v->playing, memory_order_relaxed) == gen;
}

// The audio thread releases a voice it finished with. The exchange fails, and
// the voice stays claimed, when the main thread has claimed it again meanwhile.
static void mixer_voice_end(orb_voice_state* v) {
    unsigned expected = v->gen;

    atomic_compare_exchange_strong(&v->playing, &expected, 0);
}

static void mixer_voice_start(
    orb_voice_state* v,
    const orb_assets* assets,
    uint16_t gen,
    uint32_t sample,
    orb_mixer_params p
) {
    v->gen = gen;
    v->sample = sample;
    v->sample_id = assets->sample_ids[sample];
    v->position = 0;
    v->step = mixer_step(assets->samples[sample].rate, p.pitch_cents);
    v->volume = p.volume;
    v->pan = p.pan;
    v->fade = 0;
    v->fade_gain = ORB_MIXER_FADE_ONE;
    v->loop = false;
    v->paused = false;
}

static void mixer_apply(orb_mixer* m, const orb_assets* assets, const orb_mixer_command* c) {
    orb_voice_state* v = &m->voices[c->voice];

    switch (c->kind) {
    case ORB_MIXER_PLAY:
        v->gen = c->gen;

        if (mixer_sample_valid(assets, c->index, c->id))
            mixer_voice_start(v, assets, c->gen, c->index, c->params);
        else
            mixer_voice_end(v); // the sample went away since the claim: release the voice

        break;
    case ORB_MIXER_SET:
        if (!mixer_owns(v, c->gen)) break;

        // assets is non-null here: an owned voice implies a successful play, and
        // mixer_revalidate runs before the drain, so a null or stale asset swap
        // would already have ended the voice above.
        v->volume = c->params.volume;
        v->pan = c->params.pan;
        v->step = mixer_step(assets->samples[v->sample].rate, c->params.pitch_cents);
        break;
    case ORB_MIXER_STOP:
        if (mixer_owns(v, c->gen)) mixer_voice_end(v);

        break;
    case ORB_MIXER_SONG_PLAY: {
        if (!assets || !mixer_id_valid(assets->song_ids, assets->song_count, c->index, c->id))
            break;

        mixer_voice_start(
            v, assets, 1, assets->songs[c->index].sample,
            (orb_mixer_params) {.volume = ORB_MIXER_ONE}
        );
        v->song = c->index;
        v->song_id = c->id;
        v->loop = c->loop;
        atomic_store_explicit(&v->playing, 1, memory_order_release);
        break;
    }
    case ORB_MIXER_SONG_STOP:
        if (!mixer_owns(v, 1)) break;

        if (c->fade_ms <= 0)
            mixer_voice_end(v);
        else
            v->fade = orb_max(
                1u, (uint32_t)((uint64_t)ORB_MIXER_FADE_ONE * 1000 /
                               ((uint64_t)c->fade_ms * ORB_AUDIO_RATE))
            );

        break;
    case ORB_MIXER_SONG_PAUSE:
        v->paused = c->paused;
        break;
    case ORB_MIXER_VOLUMES:
        m->volumes = c->volumes;
        break;
    }
}

static void mixer_wrap(orb_voice_state* v, uint32_t loop_start, uint32_t end) {
    uint64_t start = (uint64_t)loop_start << 32;
    uint64_t length = (uint64_t)(end - loop_start) << 32;

    if (v->position >= start + length) v->position = start + (v->position - start) % length;
}

// One voice into the accumulator: linear interpolation, the fade, and pan, group,
// and master folded into a gain per side. Ends the voice when its sample runs out.
static void mixer_render_voice(
    orb_mixer* m,
    const orb_assets* assets,
    int index,
    int32_t* acc,
    int frames
) {
    orb_voice_state* v = &m->voices[index];

    if (v->paused) return;

    if (v->sample >= assets->sample_count) {
        mixer_voice_end(v);
        return;
    }

    const orb_sample_desc* d = &assets->samples[v->sample];
    const int16_t* pcm = assets->pcm + d->first;
    uint32_t channels = d->channels;
    bool file_loops = d->loop_end > d->loop_start; // an empty loop is none
    uint32_t loop_start = v->loop && file_loops ? d->loop_start : 0;
    uint32_t end = v->loop && file_loops ? d->loop_end : d->count;
    int32_t group = index == ORB_SONG_VOICE ? m->volumes.song : m->volumes.sound;
    int32_t gain = mixer_mul(mixer_mul(v->volume, group), m->volumes.master);
    int32_t left = mixer_mul(gain, v->pan > 0 ? ORB_MIXER_ONE - v->pan : ORB_MIXER_ONE);
    int32_t right = mixer_mul(gain, v->pan < 0 ? ORB_MIXER_ONE + v->pan : ORB_MIXER_ONE);

    for (int i = 0; i < frames; i++) {
        uint32_t at = (uint32_t)(v->position >> 32);

        if (at >= end) { // a one-shot's end, or a recast shrank a looping sample under it
            if (!v->loop || end <= loop_start) {
                mixer_voice_end(v);
                return;
            }

            mixer_wrap(v, loop_start, end);
            at = (uint32_t)(v->position >> 32);
        }

        uint32_t next = at + 1;
        bool has_next = next < end || v->loop;
        int32_t frac = (int32_t)(v->position >> 17) & 0x7fff; // the fraction's top 15 bits
        int32_t fade_now = (int32_t)(v->fade_gain >> 16);

        if (next >= end) next = loop_start;

        for (uint32_t c = 0; c < channels; c++) {
            int32_t a = pcm[at * channels + c];
            int32_t b = has_next ? pcm[next * channels + c] : 0;
            int32_t s = mixer_mul(a + mixer_mul(b - a, frac), fade_now);

            if (channels == 1) {
                acc[i * 2] += mixer_mul(s, left);
                acc[i * 2 + 1] += mixer_mul(s, right);
            } else
                acc[i * 2 + c] += mixer_mul(s, c == 0 ? left : right);
        }

        v->position += v->step;

        if (v->loop) mixer_wrap(v, loop_start, end); // now, so the published frame is in the loop

        if (v->fade > 0) {
            if (v->fade_gain <= v->fade) {
                mixer_voice_end(v);
                return;
            }

            v->fade_gain -= v->fade;
        }
    }
}

// After an asset swap, a voice whose sample (or song) is gone or has a different
// id at its index is silenced.
static void mixer_revalidate(orb_mixer* m, const orb_assets* assets) {
    for (int i = 0; i < ORB_VOICE_COUNT; i++) {
        orb_voice_state* v = &m->voices[i];

        if (!mixer_owns(v, v->gen)) continue;

        bool ok = mixer_sample_valid(assets, v->sample, v->sample_id);

        if (ok && i == ORB_SONG_VOICE)
            ok = mixer_id_valid(assets->song_ids, assets->song_count, v->song, v->song_id);

        if (!ok) mixer_voice_end(v);
    }
}

// Main thread: the first free game voice, else the lowest-priority voice at or
// below the given priority, oldest first among equals. Claims it and returns it, or -1.
static int mixer_claim(orb_mixer* m, uint8_t priority) {
    int best = -1;

    for (int i = ORB_GAME_VOICE_FIRST; i < ORB_VOICE_COUNT; i++) {
        if (atomic_load_explicit(&m->voices[i].playing, memory_order_acquire) == 0) {
            best = i;
            break;
        }

        if (m->priority[i] > priority) continue;
        if (best < 0 || m->priority[i] < m->priority[best] ||
            (m->priority[i] == m->priority[best] && m->age[i] < m->age[best]))
            best = i;
    }

    if (best < 0) return -1;

    m->issued[best]++;

    if (m->issued[best] == 0) m->issued[best] = 1;

    m->priority[best] = priority;
    m->age[best] = m->next_age++;
    atomic_store_explicit(&m->voices[best].playing, m->issued[best], memory_order_release);
    return best;
}

// Main thread: a set or stop for a game voice the handle still holds.
static void mixer_push_voice(orb_mixer* m, orb_voice v, uint8_t kind, orb_mixer_params p) {
    uint32_t index = MIXER_VOICE_INDEX(v), gen = MIXER_VOICE_GEN(v);

    if (index < ORB_GAME_VOICE_FIRST || index >= ORB_VOICE_COUNT || gen == 0) return;
    if (atomic_load_explicit(&m->voices[index].playing, memory_order_acquire) != gen) return;

    mixer_push(
        m, (orb_mixer_command) {
               .kind = kind, .voice = (uint8_t)index, .gen = (uint16_t)gen, .params = p
           }
    );
}

// The song voice's frame as milliseconds and millibeats, one atomic store so the
// main thread never sees one without the other. -1 for both when no song plays.
static void mixer_publish_position(orb_mixer* m, const orb_assets* assets) {
    const orb_voice_state* v = &m->voices[ORB_SONG_VOICE];
    orb_song_position p = {-1, -1};

    if (assets && mixer_owns(v, v->gen)) {
        uint64_t frame = v->position >> 32, rate = assets->samples[v->sample].rate;

        p.ms = (int32_t)(frame * 1000 / rate);
        p.millibeats = (int32_t)(frame * assets->songs[v->song].millibpm / (60 * rate));
    }

    atomic_store_explicit(&m->song_position, p, memory_order_relaxed);
}

uint32_t orb_mixer_set_assets(orb_mixer* m, const orb_assets* assets) {
    atomic_store(&m->assets, assets);

    uint32_t begin = atomic_load(&m->render_begin), end = atomic_load(&m->render_end);

    return begin == end ? 0 : begin;
}

orb_voice orb_mixer_sound_play(orb_mixer* m, orb_sample s, orb_sound_params p, int priority) {
    const orb_assets* assets = atomic_load(&m->assets);
    if (!assets) return ORB_NO_VOICE;

    uint32_t index = orb_asset_index_of(assets, s);

    if (index == ORB_NO_INDEX) return ORB_NO_VOICE;

    if (!mixer_room(m)) { // checked before the claim, so a refused play leaves no voice claimed
        m->dropped++;
        return ORB_NO_VOICE;
    }

    int voice = mixer_claim(m, (uint8_t)orb_clamp(priority, 0, 255));

    if (voice < 0) return ORB_NO_VOICE;

    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_PLAY,
               .voice = (uint8_t)voice,
               .gen = m->issued[voice],
               .index = index,
               .id = assets->sample_ids[index],
               .params = mixer_params_of(p)
           }
    );
    return MIXER_VOICE(voice, m->issued[voice]);
}

void orb_mixer_sound_set(orb_mixer* m, orb_voice v, orb_sound_params p) {
    mixer_push_voice(m, v, ORB_MIXER_SET, mixer_params_of(p));
}

void orb_mixer_sound_stop(orb_mixer* m, orb_voice v) {
    mixer_push_voice(m, v, ORB_MIXER_STOP, (orb_mixer_params) {});
}

void orb_mixer_song_play(orb_mixer* m, orb_song s, bool loop) {
    const orb_assets* assets = atomic_load(&m->assets);
    if (!assets) return;

    uint32_t index = orb_asset_index_of(assets, s);

    if (index == ORB_NO_INDEX) return;

    mixer_push(
        m,
        (orb_mixer_command) {
            .kind = ORB_MIXER_SONG_PLAY, .index = index, .id = assets->song_ids[index], .loop = loop
        }
    );
}

void orb_mixer_song_stop(orb_mixer* m, int fade_ms) {
    mixer_push(m, (orb_mixer_command) {.kind = ORB_MIXER_SONG_STOP, .fade_ms = fade_ms});
}

void orb_mixer_song_pause(orb_mixer* m, bool paused) {
    mixer_push(m, (orb_mixer_command) {.kind = ORB_MIXER_SONG_PAUSE, .paused = paused});
}

orb_song_position orb_mixer_song_position(const orb_mixer* m) {
    return atomic_load_explicit(&m->song_position, memory_order_relaxed);
}

void orb_mixer_volume_set(orb_mixer* m, orb_volumes v) {
    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_VOLUMES,
               .volumes = {
                   mixer_fraction(orb_clamp(v.master, 0.0f, 1.0f)),
                   mixer_fraction(orb_clamp(v.song, 0.0f, 1.0f)),
                   mixer_fraction(orb_clamp(v.sound, 0.0f, 1.0f))
               }
           }
    );
}

void orb_mixer_render(orb_mixer* m, int16_t* out, int frames) {
    atomic_fetch_add(&m->render_begin, 1);

    const orb_assets* assets = atomic_load(&m->assets);

    if (assets != m->last_assets) {
        mixer_revalidate(m, assets);
        m->last_assets = assets;
    }

    orb_mixer_command c;

    while (mixer_pop(m, &c))
        mixer_apply(m, assets, &c);

    while (frames > 0) {
        int n = orb_min(frames, ORB_MIXER_CHUNK);
        int32_t acc[ORB_MIXER_CHUNK * 2];

        memset(acc, 0, sizeof(int32_t) * n * 2);

        for (int i = 0; assets && i < ORB_VOICE_COUNT; i++)
            if (mixer_owns(&m->voices[i], m->voices[i].gen))
                mixer_render_voice(m, assets, i, acc, n);

        for (int i = 0; i < n * 2; i++)
            out[i] = (int16_t)orb_clamp(acc[i], -32768, 32767);

        out += n * 2;
        frames -= n;
    }

    mixer_publish_position(m, assets);
    atomic_fetch_add(&m->render_end, 1);
}

bool orb_mixer_rendered(const orb_mixer* m, uint32_t render) {
    return (int32_t)(atomic_load(&m->render_end) - render) >= 0;
}

bool orb_mixer_idle(orb_mixer* m, uint64_t elapsed_ns, uint64_t* rendered) {
    static int16_t silence[ORB_MIXER_CHUNK * ORB_AUDIO_CHANNELS];
    uint64_t elapsed_frames =
        elapsed_ns / ORB_NS_PER_SECOND * ORB_AUDIO_RATE + // split so it never wraps
        elapsed_ns % ORB_NS_PER_SECOND * ORB_AUDIO_RATE / ORB_NS_PER_SECOND;
    uint64_t rendered_before = *rendered;

    for (; *rendered + ORB_MIXER_CHUNK <= elapsed_frames; *rendered += ORB_MIXER_CHUNK)
        orb_mixer_render(m, silence, ORB_MIXER_CHUNK);

    return rendered_before / ORB_AUDIO_RATE != *rendered / ORB_AUDIO_RATE;
}
