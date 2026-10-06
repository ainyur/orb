#include "mixer.h"
#include "../orb_math.h"

#include <string.h>

#define MIXER_VOICE(index, gen) ((orb_voice) {(u32)(index) | (u32)(gen) << 16})
#define MIXER_VOICE_INDEX(handle) ((handle).v & 0xffffu)
#define MIXER_VOICE_GEN(handle) ((handle).v >> 16)

// 2^(i/12) and 2^(i/1200) as 16.16, for the pitch step.
static const u32 mixer_semitones[12] = {65536, 69433, 73562,  77936,  82570,  87480,
                                        92682, 98193, 104032, 110218, 116772, 123715};
static const u32 mixer_cents[100] = {
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
static i32 mixer_fraction(f32 value) {
    return (i32)(value * ORB_MIXER_ONE + (value < 0 ? -0.5f : 0.5f));
}

static i32 mixer_mul(i32 value, i32 fraction) {
    return value * fraction >> 15;
}

static orb_mixer_params mixer_params_of(orb_sound_params params) {
    return (orb_mixer_params) {
        .volume = mixer_fraction(orb_clamp(params.volume, 0.0f, 1.0f)),
        .pan = mixer_fraction(orb_clamp(params.pan, -1.0f, 1.0f)),
        .pitch_cents = orb_clamp(params.pitch_cents, -2400, 2400)
    };
}

static bool mixer_id_valid(const u64* ids, u32 count, u32 index, u64 id) {
    return index < count && ids[index] == id;
}

static bool mixer_sample_valid(const orb_assets* assets, u32 sample, u64 id) {
    return assets && mixer_id_valid(assets->sample_ids, assets->samples.len, sample, id);
}

// rate / ORB_AUDIO_RATE * 2^(cents / 1200) as 32.32: whole octaves shift, the rest
// is the two tables.
static u64 mixer_step(u32 rate, int pitch_cents) {
    int cents = pitch_cents + 2400, octave = cents / 1200 - 2, rest = cents % 1200;
    u64 step = ((u64)rate << 32) / ORB_AUDIO_RATE;

    step = step * mixer_semitones[rest / 100] >> 16;
    step = step * mixer_cents[rest % 100] >> 16;
    return octave < 0 ? step >> -octave : step << octave;
}

// Main thread: room for one more command.
static bool mixer_room(const orb_mixer* mixer) {
    u32 head = atomic_load_explicit(&mixer->head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&mixer->tail, memory_order_acquire);

    return head - tail < ORB_MIXER_RING;
}

static void mixer_push(orb_mixer* mixer, orb_mixer_command command) {
    if (!mixer_room(mixer)) {
        mixer->dropped++;
        return;
    }

    u32 head = atomic_load_explicit(&mixer->head, memory_order_relaxed);

    mixer->ring[head % ORB_MIXER_RING] = command;
    atomic_store_explicit(&mixer->head, head + 1, memory_order_release);
}

// Audio thread.
static bool mixer_pop(orb_mixer* mixer, orb_mixer_command* command) {
    u32 tail = atomic_load_explicit(&mixer->tail, memory_order_relaxed);
    u32 head = atomic_load_explicit(&mixer->head, memory_order_acquire);

    if (tail == head) return false;

    *command = mixer->ring[tail % ORB_MIXER_RING];
    atomic_store_explicit(&mixer->tail, tail + 1, memory_order_release);
    return true;
}

// The audio thread is rendering its own sound on this voice: the claim it holds
// is the generation of the last play it applied.
static bool mixer_owns(const orb_voice_state* voice, unsigned gen) {
    return gen != 0 && voice->gen == gen &&
           atomic_load_explicit(&voice->playing, memory_order_relaxed) == gen;
}

// The audio thread releases a voice it finished with. The exchange fails, and
// the voice stays claimed, when the main thread has claimed it again meanwhile.
static void mixer_voice_end(orb_voice_state* voice) {
    unsigned expected = voice->gen;

    atomic_compare_exchange_strong(&voice->playing, &expected, 0);
}

static void mixer_voice_start(
    orb_voice_state* voice,
    const orb_assets* assets,
    u16 gen,
    u32 sample,
    orb_mixer_params params
) {
    voice->gen = gen;
    voice->sample = sample;
    voice->sample_id = assets->sample_ids[sample];
    voice->position = 0;
    voice->step = mixer_step(assets->samples.elems[sample].rate, params.pitch_cents);
    voice->volume = params.volume;
    voice->pan = params.pan;
    voice->fade = 0;
    voice->fade_gain = ORB_MIXER_FADE_ONE;
    voice->loop = false;
    voice->paused = false;
}

static void mixer_apply(
    orb_mixer* mixer,
    const orb_assets* assets,
    const orb_mixer_command* command
) {
    orb_voice_state* voice = &mixer->voices[command->voice];

    switch (command->kind) {
    case ORB_MIXER_PLAY:
        voice->gen = command->gen;

        if (mixer_sample_valid(assets, command->index, command->id))
            mixer_voice_start(voice, assets, command->gen, command->index, command->params);
        else
            mixer_voice_end(voice); // the sample went away since the claim: release the voice

        break;
    case ORB_MIXER_SET:
        if (!mixer_owns(voice, command->gen)) break;

        // assets is non-null here: an owned voice implies a successful play, and
        // mixer_revalidate runs before the drain, so a null or stale asset swap
        // would already have ended the voice above.
        voice->volume = command->params.volume;
        voice->pan = command->params.pan;
        voice->step =
            mixer_step(assets->samples.elems[voice->sample].rate, command->params.pitch_cents);
        break;
    case ORB_MIXER_STOP:
        if (mixer_owns(voice, command->gen)) mixer_voice_end(voice);

        break;
    case ORB_MIXER_SONG_PLAY: {
        if (!assets ||
            !mixer_id_valid(assets->song_ids, assets->songs.len, command->index, command->id))
            break;

        mixer_voice_start(
            voice, assets, 1, assets->songs.elems[command->index].sample,
            (orb_mixer_params) {.volume = ORB_MIXER_ONE}
        );
        voice->song = command->index;
        voice->song_id = command->id;
        voice->loop = command->loop;
        atomic_store_explicit(&voice->playing, 1, memory_order_release);
        break;
    }
    case ORB_MIXER_SONG_STOP:
        if (!mixer_owns(voice, 1)) break;

        if (command->fade_ms <= 0)
            mixer_voice_end(voice);
        else
            voice->fade = orb_max(
                1u, (u32)((u64)ORB_MIXER_FADE_ONE * 1000 / ((u64)command->fade_ms * ORB_AUDIO_RATE))
            );

        break;
    case ORB_MIXER_SONG_PAUSE:
        voice->paused = command->paused;
        break;
    case ORB_MIXER_VOLUMES:
        mixer->volumes = command->volumes;
        break;
    }
}

static void mixer_wrap(orb_voice_state* voice, u32 loop_start, u32 end) {
    u64 start = (u64)loop_start << 32;
    u64 length = (u64)(end - loop_start) << 32;

    if (voice->position >= start + length)
        voice->position = start + (voice->position - start) % length;
}

// One voice into the accumulator: linear interpolation, the fade, and pan, group,
// and master folded into a gain per side. Ends the voice when its sample runs out.
static void mixer_render_voice(
    orb_mixer* mixer,
    const orb_assets* assets,
    int index,
    i32* acc,
    int frames
) {
    orb_voice_state* voice = &mixer->voices[index];

    if (voice->paused) return;

    if (voice->sample >= assets->samples.len) {
        mixer_voice_end(voice);
        return;
    }

    const orb_sample_desc* sample = &assets->samples.elems[voice->sample];
    const i16* pcm = assets->pcm.elems + sample->first;
    u32 channels = sample->channels;
    bool file_loops = sample->loop_end > sample->loop_start; // an empty loop is none
    u32 loop_start = voice->loop && file_loops ? sample->loop_start : 0;
    u32 end = voice->loop && file_loops ? sample->loop_end : sample->count;
    i32 group = index == ORB_SONG_VOICE ? mixer->volumes.song : mixer->volumes.sound;
    i32 gain = mixer_mul(mixer_mul(voice->volume, group), mixer->volumes.master);
    i32 left = mixer_mul(gain, voice->pan > 0 ? ORB_MIXER_ONE - voice->pan : ORB_MIXER_ONE);
    i32 right = mixer_mul(gain, voice->pan < 0 ? ORB_MIXER_ONE + voice->pan : ORB_MIXER_ONE);

    for (int i = 0; i < frames; i++) {
        u32 at = (u32)(voice->position >> 32);

        if (at >= end) { // a one-shot's end, or a recast shrank a looping sample under it
            if (!voice->loop || end <= loop_start) {
                mixer_voice_end(voice);
                return;
            }

            mixer_wrap(voice, loop_start, end);
            at = (u32)(voice->position >> 32);
        }

        u32 next = at + 1;
        bool has_next = next < end || voice->loop;
        i32 frac = (i32)(voice->position >> 17) & 0x7fff; // the fraction's top 15 bits
        i32 fade_now = (i32)(voice->fade_gain >> 16);

        if (next >= end) next = loop_start;

        for (u32 channel = 0; channel < channels; channel++) {
            i32 a = pcm[at * channels + channel];
            i32 b = has_next ? pcm[next * channels + channel] : 0;
            i32 sample_value = mixer_mul(a + mixer_mul(b - a, frac), fade_now);

            if (channels == 1) {
                acc[i * 2] += mixer_mul(sample_value, left);
                acc[i * 2 + 1] += mixer_mul(sample_value, right);
            } else
                acc[i * 2 + channel] += mixer_mul(sample_value, channel == 0 ? left : right);
        }

        voice->position += voice->step;

        if (voice->loop)
            mixer_wrap(voice, loop_start, end); // now, so the published frame is in the loop

        if (voice->fade > 0) {
            if (voice->fade_gain <= voice->fade) {
                mixer_voice_end(voice);
                return;
            }

            voice->fade_gain -= voice->fade;
        }
    }
}

// After an asset swap, a voice whose sample (or song) is gone or has a different
// id at its index is silenced.
static void mixer_revalidate(orb_mixer* mixer, const orb_assets* assets) {
    for (int i = 0; i < ORB_VOICE_COUNT; i++) {
        orb_voice_state* voice = &mixer->voices[i];

        if (!mixer_owns(voice, voice->gen)) continue;

        bool ok = mixer_sample_valid(assets, voice->sample, voice->sample_id);

        if (ok && i == ORB_SONG_VOICE)
            ok = mixer_id_valid(assets->song_ids, assets->songs.len, voice->song, voice->song_id);

        if (!ok) mixer_voice_end(voice);
    }
}

// Main thread: the first free game voice, else the lowest-priority voice at or
// below the given priority, oldest first among equals. Claims it and returns it, or -1.
static int mixer_claim(orb_mixer* mixer, u8 priority) {
    int best = -1;

    for (int i = ORB_GAME_VOICE_FIRST; i < ORB_VOICE_COUNT; i++) {
        if (atomic_load_explicit(&mixer->voices[i].playing, memory_order_acquire) == 0) {
            best = i;
            break;
        }

        if (mixer->priority[i] > priority) continue;
        if (best < 0 || mixer->priority[i] < mixer->priority[best] ||
            (mixer->priority[i] == mixer->priority[best] && mixer->age[i] < mixer->age[best]))
            best = i;
    }

    if (best < 0) return -1;

    mixer->issued[best]++;

    if (mixer->issued[best] == 0) mixer->issued[best] = 1;

    mixer->priority[best] = priority;
    mixer->age[best] = mixer->next_age++;
    atomic_store_explicit(&mixer->voices[best].playing, mixer->issued[best], memory_order_release);
    return best;
}

// Main thread: a set or stop for a game voice the handle still holds.
static void mixer_push_voice(orb_mixer* mixer, orb_voice voice, u8 kind, orb_mixer_params params) {
    u32 index = MIXER_VOICE_INDEX(voice), gen = MIXER_VOICE_GEN(voice);

    if (index < ORB_GAME_VOICE_FIRST || index >= ORB_VOICE_COUNT || gen == 0) return;
    if (atomic_load_explicit(&mixer->voices[index].playing, memory_order_acquire) != gen) return;

    mixer_push(
        mixer,
        (orb_mixer_command) {.kind = kind, .voice = (u8)index, .gen = (u16)gen, .params = params}
    );
}

// The song voice's frame as milliseconds and millibeats, one atomic store so the
// main thread never sees one without the other. -1 for both when no song plays.
static void mixer_publish_position(orb_mixer* mixer, const orb_assets* assets) {
    const orb_voice_state* voice = &mixer->voices[ORB_SONG_VOICE];
    orb_song_position position = ORB_NO_SONG_POSITION;

    if (assets && mixer_owns(voice, voice->gen)) {
        u64 frame = voice->position >> 32, rate = assets->samples.elems[voice->sample].rate;

        position.ms = (i32)(frame * 1000 / rate);
        position.millibeats =
            (i32)(frame * assets->songs.elems[voice->song].millibpm / (60 * rate));
    }

    atomic_store_explicit(&mixer->song_position, position, memory_order_relaxed);
}

u32 orb_mixer_set_assets(orb_mixer* mixer, const orb_assets* assets) {
    atomic_store(&mixer->assets, assets);

    u32 begin = atomic_load(&mixer->render_begin), end = atomic_load(&mixer->render_end);

    return begin == end ? 0 : begin;
}

orb_voice orb_mixer_sound_play(
    orb_mixer* mixer,
    orb_sample sample,
    orb_sound_params params,
    int priority
) {
    const orb_assets* assets = atomic_load(&mixer->assets);
    if (!assets) return ORB_NO_VOICE;

    u32 index = orb_asset_index_of(assets, sample);

    if (index == ORB_NO_INDEX) return ORB_NO_VOICE;

    if (!mixer_room(mixer)) { // checked before the claim, so a refused play leaves no voice claimed
        mixer->dropped++;
        return ORB_NO_VOICE;
    }

    int voice = mixer_claim(mixer, (u8)orb_clamp(priority, 0, 255));

    if (voice < 0) return ORB_NO_VOICE;

    mixer_push(
        mixer, (orb_mixer_command) {
                   .kind = ORB_MIXER_PLAY,
                   .voice = (u8)voice,
                   .gen = mixer->issued[voice],
                   .index = index,
                   .id = assets->sample_ids[index],
                   .params = mixer_params_of(params)
               }
    );
    return MIXER_VOICE(voice, mixer->issued[voice]);
}

void orb_mixer_sound_set(orb_mixer* mixer, orb_voice voice, orb_sound_params params) {
    mixer_push_voice(mixer, voice, ORB_MIXER_SET, mixer_params_of(params));
}

void orb_mixer_sound_stop(orb_mixer* mixer, orb_voice voice) {
    mixer_push_voice(mixer, voice, ORB_MIXER_STOP, (orb_mixer_params) {});
}

void orb_mixer_song_play(orb_mixer* mixer, orb_song song, bool loop) {
    const orb_assets* assets = atomic_load(&mixer->assets);
    if (!assets) return;

    u32 index = orb_asset_index_of(assets, song);

    if (index == ORB_NO_INDEX) return;

    mixer_push(
        mixer,
        (orb_mixer_command) {
            .kind = ORB_MIXER_SONG_PLAY, .index = index, .id = assets->song_ids[index], .loop = loop
        }
    );
}

void orb_mixer_song_stop(orb_mixer* mixer, int fade_ms) {
    mixer_push(mixer, (orb_mixer_command) {.kind = ORB_MIXER_SONG_STOP, .fade_ms = fade_ms});
}

void orb_mixer_song_pause(orb_mixer* mixer, bool paused) {
    mixer_push(mixer, (orb_mixer_command) {.kind = ORB_MIXER_SONG_PAUSE, .paused = paused});
}

orb_song_position orb_mixer_song_position(const orb_mixer* mixer) {
    return atomic_load_explicit(&mixer->song_position, memory_order_relaxed);
}

void orb_mixer_volume_set(orb_mixer* mixer, orb_volumes volumes) {
    mixer_push(
        mixer, (orb_mixer_command) {
                   .kind = ORB_MIXER_VOLUMES,
                   .volumes = {
                       mixer_fraction(orb_clamp(volumes.master, 0.0f, 1.0f)),
                       mixer_fraction(orb_clamp(volumes.song, 0.0f, 1.0f)),
                       mixer_fraction(orb_clamp(volumes.sound, 0.0f, 1.0f))
                   }
               }
    );
}

void orb_mixer_render(orb_mixer* mixer, i16* out, int frames) {
    atomic_fetch_add(&mixer->render_begin, 1);

    const orb_assets* assets = atomic_load(&mixer->assets);

    if (assets != mixer->last_assets) {
        mixer_revalidate(mixer, assets);
        mixer->last_assets = assets;
    }

    orb_mixer_command command;

    while (mixer_pop(mixer, &command))
        mixer_apply(mixer, assets, &command);

    while (frames > 0) {
        int n = orb_min(frames, ORB_MIXER_CHUNK);
        i32 acc[ORB_MIXER_CHUNK * 2];

        memset(acc, 0, sizeof(i32) * n * 2);

        for (int i = 0; assets && i < ORB_VOICE_COUNT; i++)
            if (mixer_owns(&mixer->voices[i], mixer->voices[i].gen))
                mixer_render_voice(mixer, assets, i, acc, n);

        for (int i = 0; i < n * 2; i++)
            out[i] = (i16)orb_clamp(acc[i], -32768, 32767);

        out += n * 2;
        frames -= n;
    }

    mixer_publish_position(mixer, assets);
    atomic_fetch_add(&mixer->render_end, 1);
}

bool orb_mixer_rendered(const orb_mixer* mixer, u32 render) {
    return (i32)(atomic_load(&mixer->render_end) - render) >= 0;
}

bool orb_mixer_idle(orb_mixer* mixer, u64 elapsed_ns, u64* rendered) {
    static i16 silence[ORB_MIXER_CHUNK * ORB_AUDIO_CHANNELS];
    u64 elapsed_frames =
        elapsed_ns / ORB_NS_PER_SECOND * ORB_AUDIO_RATE + // split so it never wraps
        elapsed_ns % ORB_NS_PER_SECOND * ORB_AUDIO_RATE / ORB_NS_PER_SECOND;
    u64 rendered_before = *rendered;

    for (; *rendered + ORB_MIXER_CHUNK <= elapsed_frames; *rendered += ORB_MIXER_CHUNK)
        orb_mixer_render(mixer, silence, ORB_MIXER_CHUNK);

    return rendered_before / ORB_AUDIO_RATE != *rendered / ORB_AUDIO_RATE;
}
