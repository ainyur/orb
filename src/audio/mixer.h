#pragma once

#include "../core/asset.h"
#include "../orb.h"

#include <stdatomic.h>

constexpr int ORB_SONG_VOICE = 0;
constexpr int ORB_GAME_VOICE_FIRST = 1;
constexpr int ORB_GAME_VOICE_COUNT = 16;
constexpr int ORB_VOICE_COUNT = ORB_GAME_VOICE_FIRST + ORB_GAME_VOICE_COUNT;

constexpr int ORB_MIXER_CHUNK = 512;
constexpr int ORB_MIXER_RING = 256;

constexpr u32 ORB_MIXER_FADE_ONE = 1u << 31;
constexpr i32 ORB_MIXER_ONE = 1 << 15;

typedef enum orb_mixer_kind {
    ORB_MIXER_PLAY,
    ORB_MIXER_SET,
    ORB_MIXER_STOP,
    ORB_MIXER_SONG_PLAY,
    ORB_MIXER_SONG_STOP,
    ORB_MIXER_SONG_PAUSE,
    ORB_MIXER_VOLUMES
} orb_mixer_kind;

typedef struct orb_mixer_params {
    i32 volume, pan; // 0..ONE, -ONE..ONE
    i32 pitch_cents;
} orb_mixer_params;

typedef struct orb_mixer_volumes {
    i32 master, song, sound;
} orb_mixer_volumes;

typedef struct orb_mixer_command {
    u8 kind;
    u8 voice;
    u16 gen;   // play, set, stop: the handle's
    u32 index; // play: the sample; song_play: the song, resolved on the audio thread
    u64 id;    // its id, so a swapped asset is caught
    union {
        orb_mixer_params params; // play, set
        bool loop;               // song_play
        bool paused;             // song_pause
        i32 fade_ms;             // song_stop
        orb_mixer_volumes volumes;
    };
} orb_mixer_command;
static_assert(sizeof(orb_mixer_command) == 32, "orb_mixer_command layout");

// A sampler voice. playing is the one field both threads touch: the main thread
// stores the generation that claims a game voice, the audio thread clears it with
// a compare-exchange when the sound ends. Every other field is the audio
// thread's, written only while rendering.
typedef struct orb_voice_state {
    atomic_uint playing;
    u16 gen; // from the last play this voice applied
    u32 sample;
    u64 sample_id;
    u32 song; // the song voice: what it streams
    u64 song_id;
    u64 position; // 32.32 fixed-point frame
    u64 step;     // 32.32 frames per output frame
    i32 volume, pan;
    u32 fade, fade_gain; // fade is the per-frame decrement, 0 when not fading
    bool loop, paused;
} orb_voice_state;

typedef struct orb_mixer {
    orb_voice_state voices[ORB_VOICE_COUNT];
    // The main thread's book for game voices, never read by the audio thread.
    u16 issued[ORB_VOICE_COUNT]; // per voice: the last generation handed out; wraps skipping 0
    u8 priority[ORB_VOICE_COUNT];
    u32 age[ORB_VOICE_COUNT];
    u32 next_age;
    u32 dropped; // commands a full ring refused; api.c reads and logs it
    orb_mixer_command ring[ORB_MIXER_RING];
    atomic_uint head, tail; // the main thread writes head, the audio thread tail
    _Atomic(const orb_assets*) assets;
    const orb_assets* last_assets; // the audio thread's
    atomic_uint render_begin, render_end;
    _Atomic orb_song_position song_position; // stored after each render
    orb_mixer_volumes volumes;               // the audio thread's copy
} orb_mixer;

// The defaults, as an initializer: api.c's mixer is a static, and assets are
// published to it before anything else runs.
#define ORB_MIXER_INIT                                                                             \
    {                                                                                              \
        .song_position = ORB_NO_SONG_POSITION, .volumes = {                                        \
            ORB_MIXER_ONE,                                                                         \
            ORB_MIXER_ONE,                                                                         \
            ORB_MIXER_ONE                                                                          \
        }                                                                                          \
    }

// Returns the render to wait for, 0 if none.
[[nodiscard]] u32 orb_mixer_set_assets(orb_mixer* mixer, const orb_assets* assets);
orb_voice orb_mixer_sound_play(
    orb_mixer* mixer,
    orb_sample sample,
    orb_sound_params params,
    int priority
);
void orb_mixer_sound_set(orb_mixer* mixer, orb_voice voice, orb_sound_params params);
void orb_mixer_sound_stop(orb_mixer* mixer, orb_voice voice);
void orb_mixer_song_play(orb_mixer* mixer, orb_song song, bool loop);
void orb_mixer_song_stop(orb_mixer* mixer, int fade_ms);
void orb_mixer_song_pause(orb_mixer* mixer, bool paused);
orb_song_position orb_mixer_song_position(const orb_mixer* mixer);
void orb_mixer_volume_set(orb_mixer* mixer, orb_volumes volumes);

// The audio thread; the rest is main.
void orb_mixer_render(orb_mixer* mixer, i16* out, int frames);
bool orb_mixer_rendered(const orb_mixer* mixer, u32 render);
// With no device: renders silence in chunks until *rendered frames cover
// elapsed_ns, so sounds end, the ring drains, and song_position keeps real time.
// True once a second, when the OS layer should try its device again.
bool orb_mixer_idle(orb_mixer* mixer, u64 elapsed_ns, u64* rendered);
