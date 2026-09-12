#include "mixer.h"
#include "../core/macros.h"

#include <math.h>
#include <string.h>

#define MIXER_VOICE(index, generation)                                                             \
    ((orb_voice) {(uint32_t)(index) | (uint32_t)(generation) << 16})
#define MIXER_VOICE_INDEX(h) ((h).v & 0xffffu)
#define MIXER_VOICE_GENERATION(h) ((h).v >> 16)

static orb_sound_params mixer_clamp_params(orb_sound_params p) {
    return (orb_sound_params) {
        .volume = orb_clamp(p.volume, 0.0f, 1.0f),
        .pan = orb_clamp(p.pan, -1.0f, 1.0f),
        .pitch_cents = orb_clamp(p.pitch_cents, -2400, 2400)
    };
}

static bool mixer_sample_valid(const orb_assets* assets, uint32_t sample, uint64_t id) {
    return assets && sample < assets->sample_count && assets->sample_ids[sample] == id;
}

static uint64_t mixer_step(uint32_t rate, int pitch_cents) {
    float ratio = (float)rate / ORB_AUDIO_RATE * exp2f((float)pitch_cents / 1200.0f);

    return (uint64_t)(ratio * 4294967296.0);
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
static bool mixer_owns(const orb_voice_state* v, unsigned generation) {
    return generation != 0 && v->generation == generation &&
           atomic_load_explicit(&v->playing, memory_order_relaxed) == generation;
}

// The audio thread releases a voice it finished with. The exchange fails, and
// the voice stays claimed, when the main thread has claimed it again meanwhile.
static void mixer_voice_end(orb_voice_state* v) {
    unsigned expected = v->generation;

    atomic_compare_exchange_strong(&v->playing, &expected, 0);
}

static void mixer_voice_start(
    orb_voice_state* v,
    const orb_assets* assets,
    uint16_t generation,
    uint32_t sample,
    orb_sound_params p
) {
    v->generation = generation;
    v->sample = sample;
    v->sample_id = assets->sample_ids[sample];
    v->position = 0;
    v->step = mixer_step(assets->samples[sample].rate, p.pitch_cents);
    v->volume = p.volume;
    v->pan = p.pan;
    v->fade = 0;
    v->fade_gain = 1;
    v->loop = false;
    v->paused = false;
}

static void mixer_apply(orb_mixer* m, const orb_assets* assets, const orb_mixer_command* c) {
    orb_voice_state* v = &m->voices[c->voice];

    switch (c->kind) {
    case ORB_MIXER_PLAY:
        v->generation = c->generation;

        if (mixer_sample_valid(assets, c->index, c->id))
            mixer_voice_start(v, assets, c->generation, c->index, c->params);
        else
            mixer_voice_end(v); // the sample went away since the claim: release the voice

        break;
    case ORB_MIXER_SET:
        if (!mixer_owns(v, c->generation)) break;

        // assets is non-null here: an owned voice implies a successful play, and
        // mixer_revalidate runs before the drain, so a null or stale asset swap
        // would already have ended the voice above.
        v->volume = c->params.volume;
        v->pan = c->params.pan;
        v->step = mixer_step(assets->samples[v->sample].rate, c->params.pitch_cents);
        break;
    case ORB_MIXER_STOP:
        if (mixer_owns(v, c->generation)) mixer_voice_end(v);

        break;
    case ORB_MIXER_SONG_PLAY: {
        if (!assets || c->index >= assets->song_count || assets->song_ids[c->index] != c->id) break;

        uint32_t sample = assets->songs[c->index].sample;

        if (sample >= assets->sample_count) break;

        mixer_voice_start(v, assets, 1, sample, (orb_sound_params) {.volume = 1});
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
            v->fade = 1000.0f / ((float)c->fade_ms * ORB_AUDIO_RATE);

        break;
    case ORB_MIXER_SONG_PAUSE:
        v->paused = true;
        break;
    case ORB_MIXER_SONG_RESUME:
        v->paused = false;
        break;
    case ORB_MIXER_VOLUMES:
        m->volumes = c->volumes;
        break;
    }
}

// Folds a looping voice's position back into its loop.
static void mixer_wrap(orb_voice_state* v, uint32_t loop_start, uint32_t end) {
    uint64_t start = (uint64_t)loop_start << 32;
    uint64_t length = (uint64_t)(end - loop_start) << 32;

    if (v->position >= start + length) v->position = start + (v->position - start) % length;
}

// One voice into the float accumulator: linear interpolation, pan as a pair of
// gains, the group volume, and the fade. Ends the voice when its sample runs out.
static void mixer_render_voice(
    orb_mixer* m,
    const orb_assets* assets,
    int index,
    float* acc,
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
    bool points = d->loop_end > d->loop_start; // file loop points; an empty loop is none
    uint32_t loop_start = v->loop && points ? d->loop_start : 0;
    uint32_t end = v->loop && points ? d->loop_end : d->count;
    float group = index == ORB_SONG_VOICE ? m->volumes.song : m->volumes.sound;
    float left = v->volume * group * (v->pan > 0 ? 1 - v->pan : 1);
    float right = v->volume * group * (v->pan < 0 ? 1 + v->pan : 1);

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
        float frac = (float)(uint32_t)v->position / 4294967296.0f;
        float gain = v->fade_gain;

        if (next >= end) next = loop_start;

        for (uint32_t c = 0; c < channels; c++) {
            float a = pcm[at * channels + c];
            float b = has_next ? pcm[next * channels + c] : 0;
            float s = (a + (b - a) * frac) * gain;

            if (channels == 1) {
                acc[i * 2] += s * left;
                acc[i * 2 + 1] += s * right;
            } else
                acc[i * 2 + c] += s * (c == 0 ? left : right);
        }

        v->position += v->step;

        if (v->loop) mixer_wrap(v, loop_start, end); // now, so the published frame is in the loop

        if (v->fade > 0) {
            v->fade_gain -= v->fade;

            if (v->fade_gain <= 0) {
                mixer_voice_end(v);
                return;
            }
        }
    }
}

// After an asset swap, a voice whose sample (or song) is gone or has a different
// id at its index is silenced.
static void mixer_revalidate(orb_mixer* m, const orb_assets* assets) {
    for (int i = 0; i < ORB_VOICE_COUNT; i++) {
        orb_voice_state* v = &m->voices[i];

        if (!mixer_owns(v, v->generation)) continue;

        bool ok = mixer_sample_valid(assets, v->sample, v->sample_id);

        if (ok && i == ORB_SONG_VOICE)
            ok = v->song < assets->song_count && assets->song_ids[v->song] == v->song_id;

        if (!ok) mixer_voice_end(v);
    }
}

// Main thread: a free game voice, else the lowest-priority voice at or below the
// given priority, oldest first among equals. Claims it and returns its slot, or -1.
static int mixer_claim(orb_mixer* m, uint8_t priority) {
    int best = -1;

    for (int i = 0; i < ORB_GAME_VOICE_COUNT && best < 0; i++) {
        orb_voice_state* v = &m->voices[ORB_GAME_VOICE_FIRST + i];

        if (atomic_load_explicit(&v->playing, memory_order_acquire) == 0) best = i;
    }

    if (best < 0) {
        for (int i = 0; i < ORB_GAME_VOICE_COUNT; i++) {
            if (m->priority[i] > priority) continue;
            if (best < 0 || m->priority[i] < m->priority[best] ||
                (m->priority[i] == m->priority[best] && m->age[i] < m->age[best]))
                best = i;
        }
    }

    if (best < 0) return -1;

    m->issued[best]++;

    if (m->issued[best] == 0) m->issued[best] = 1;

    m->priority[best] = priority;
    m->age[best] = m->next_age++;
    atomic_store_explicit(
        &m->voices[ORB_GAME_VOICE_FIRST + best].playing, m->issued[best], memory_order_release
    );
    return best;
}

// Main thread: the voice a handle names, if it is a game voice still claimed at
// the handle's generation.
static orb_voice_state* mixer_held(orb_mixer* m, orb_voice handle) {
    uint32_t index = MIXER_VOICE_INDEX(handle), generation = MIXER_VOICE_GENERATION(handle);

    if (index < ORB_GAME_VOICE_FIRST || index >= ORB_VOICE_COUNT || generation == 0) return nullptr;
    if (atomic_load_explicit(&m->voices[index].playing, memory_order_acquire) != generation)
        return nullptr;

    return &m->voices[index];
}

// The song voice's frame as seconds and beats, one atomic store so the main
// thread never sees one without the other. -1 for both when no song plays.
static void mixer_publish_position(orb_mixer* m, const orb_assets* assets) {
    const orb_voice_state* v = &m->voices[ORB_SONG_VOICE];
    orb_song_position p = {-1, -1};

    if (assets && mixer_owns(v, v->generation)) {
        p.seconds = (float)(v->position >> 32) / (float)assets->samples[v->sample].rate;
        p.beats = p.seconds * assets->songs[v->song].bpm / 60;
    }

    atomic_store_explicit(&m->song_position, p, memory_order_relaxed);
}

void orb_mixer_init(orb_mixer* m) {
    *m = (orb_mixer)ORB_MIXER_INIT;
}

void orb_mixer_render(orb_mixer* m, int16_t* out, int frames) {
    atomic_fetch_add(&m->render_begin, 1);

    const orb_assets* assets = atomic_load(&m->assets);

    if (assets != m->seen) {
        mixer_revalidate(m, assets);
        m->seen = assets;
    }

    orb_mixer_command c;

    while (mixer_pop(m, &c))
        mixer_apply(m, assets, &c);

    while (frames > 0) {
        int n = frames < ORB_MIXER_CHUNK ? frames : ORB_MIXER_CHUNK;
        float acc[ORB_MIXER_CHUNK * 2];

        memset(acc, 0, sizeof(float) * n * 2);

        for (int i = 0; assets && i < ORB_VOICE_COUNT; i++)
            if (mixer_owns(&m->voices[i], m->voices[i].generation))
                mixer_render_voice(m, assets, i, acc, n);

        for (int i = 0; i < n * 2; i++) {
            float s = acc[i] * m->volumes.master;

            out[i] = (int16_t)orb_clamp(s, -32768.0f, 32767.0f);
        }

        out += n * 2;
        frames -= n;
    }

    mixer_publish_position(m, assets);
    atomic_fetch_add(&m->render_end, 1);
}

bool orb_mixer_rendered(const orb_mixer* m, uint32_t render) {
    return (int32_t)(atomic_load(&m->render_end) - render) >= 0;
}

uint32_t orb_mixer_set_assets(orb_mixer* m, const orb_assets* assets) {
    atomic_store(&m->assets, assets);

    uint32_t begin = atomic_load(&m->render_begin), end = atomic_load(&m->render_end);

    return begin == end ? 0 : begin;
}

void orb_mixer_song_pause(orb_mixer* m) {
    mixer_push(m, (orb_mixer_command) {.kind = ORB_MIXER_SONG_PAUSE});
}

void orb_mixer_song_play(orb_mixer* m, orb_song s, bool loop) {
    const orb_assets* assets = atomic_load(&m->assets);
    if (!assets) return;

    uint32_t index = orb_handle_index(assets->song_generations, assets->song_count, s.v);

    if (index == 0xffffffu) return;

    mixer_push(
        m,
        (orb_mixer_command) {
            .kind = ORB_MIXER_SONG_PLAY, .index = index, .id = assets->song_ids[index], .loop = loop
        }
    );
}

orb_song_position orb_mixer_song_position(const orb_mixer* m) {
    return atomic_load_explicit(&m->song_position, memory_order_relaxed);
}

void orb_mixer_song_resume(orb_mixer* m) {
    mixer_push(m, (orb_mixer_command) {.kind = ORB_MIXER_SONG_RESUME});
}

void orb_mixer_song_stop(orb_mixer* m, int fade_ms) {
    mixer_push(m, (orb_mixer_command) {.kind = ORB_MIXER_SONG_STOP, .fade_ms = fade_ms});
}

orb_voice orb_mixer_sound_play(orb_mixer* m, orb_sample s, orb_sound_params p, int priority) {
    const orb_assets* assets = atomic_load(&m->assets);
    if (!assets) return ORB_NO_VOICE;

    uint32_t index = orb_handle_index(assets->sample_generations, assets->sample_count, s.v);

    if (index == 0xffffffu) return ORB_NO_VOICE;

    if (!mixer_room(m)) { // checked before the claim, so a refused play leaves no voice claimed
        m->dropped++;
        return ORB_NO_VOICE;
    }

    int slot = mixer_claim(m, (uint8_t)orb_clamp(priority, 0, 255));

    if (slot < 0) return ORB_NO_VOICE;

    uint8_t voice = (uint8_t)(ORB_GAME_VOICE_FIRST + slot);

    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_PLAY,
               .voice = voice,
               .generation = m->issued[slot],
               .index = index,
               .id = assets->sample_ids[index],
               .params = mixer_clamp_params(p)
           }
    );
    return MIXER_VOICE(voice, m->issued[slot]);
}

void orb_mixer_sound_set(orb_mixer* m, orb_voice v, orb_sound_params p) {
    if (!mixer_held(m, v)) return;

    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_SET,
               .voice = (uint8_t)MIXER_VOICE_INDEX(v),
               .generation = (uint16_t)MIXER_VOICE_GENERATION(v),
               .params = mixer_clamp_params(p)
           }
    );
}

void orb_mixer_sound_stop(orb_mixer* m, orb_voice v) {
    if (!mixer_held(m, v)) return;

    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_STOP,
               .voice = (uint8_t)MIXER_VOICE_INDEX(v),
               .generation = (uint16_t)MIXER_VOICE_GENERATION(v)
           }
    );
}

void orb_mixer_volume_set(orb_mixer* m, orb_volumes v) {
    mixer_push(
        m, (orb_mixer_command) {
               .kind = ORB_MIXER_VOLUMES,
               .volumes = {
                   orb_clamp(v.master, 0.0f, 1.0f), orb_clamp(v.song, 0.0f, 1.0f),
                   orb_clamp(v.sound, 0.0f, 1.0f)
               }
           }
    );
}
