#pragma once

#include "../cast/file.h"
#include "../orb.h"

#include <stdatomic.h>

constexpr int ORB_VOICE_COUNT = 32;
constexpr int ORB_SONG_VOICE = 0;        // voices 0..15 belong to the sequencer; 0 streams the song
constexpr int ORB_GAME_VOICE_FIRST = 16; // voices 16..31 belong to the game
constexpr int ORB_GAME_VOICE_COUNT = 16;
constexpr int ORB_MIXER_RING = 256;
constexpr int ORB_MIXER_CHUNK = 512;

enum {
    ORB_MIXER_PLAY,
    ORB_MIXER_SET,
    ORB_MIXER_STOP,
    ORB_MIXER_SONG_PLAY,
    ORB_MIXER_SONG_STOP,
    ORB_MIXER_SONG_PAUSE,
    ORB_MIXER_SONG_RESUME,
    ORB_MIXER_VOLUMES
};

typedef struct orb_mixer_command {
    uint8_t kind;
    uint8_t voice;
    uint16_t generation; // play, set, stop: the handle's
    uint32_t index;      // play: the sample; song_play: the song, resolved on the audio thread
    uint64_t id;         // its id, so a swapped asset is caught
    union {
        orb_sound_params params; // play, set
        bool loop;               // song_play
        int32_t fade_ms;         // song_stop
        orb_volumes volumes;     // volumes
    };
} orb_mixer_command;

// A sampler voice. playing is the one field both threads touch: the main thread
// stores the generation that claims a game voice, the audio thread clears it with
// a compare-exchange when the sound ends. Every other field is the audio
// thread's, written only while rendering.
typedef struct orb_voice_state {
    atomic_uint playing;
    uint16_t generation; // from the last play this voice applied
    uint32_t sample;
    uint64_t sample_id;
    uint32_t song; // the song voice: what it streams
    uint64_t song_id;
    uint64_t position; // 32.32 fixed-point frame
    uint64_t step;     // 32.32 frames per output frame
    float volume, pan;
    float fade, fade_gain; // fade is the per-frame decrement, 0 when not fading
    bool loop, paused;
} orb_voice_state;

typedef struct orb_mixer {
    orb_voice_state voices[ORB_VOICE_COUNT];
    // The main thread's book for game voices, never read by the audio thread.
    uint16_t issued[ORB_GAME_VOICE_COUNT]; // the last generation handed out; wraps skipping 0
    uint8_t priority[ORB_GAME_VOICE_COUNT];
    uint32_t age[ORB_GAME_VOICE_COUNT];
    uint32_t next_age;
    uint32_t dropped; // commands a full ring refused; api.c reads and logs it
    orb_mixer_command ring[ORB_MIXER_RING];
    atomic_uint head, tail; // the main thread writes head, the audio thread tail
    _Atomic(const orb_assets*) assets;
    const orb_assets* seen; // audio thread: what the last render used
    atomic_uint render_begin, render_end;
    _Atomic orb_song_position song_position; // stored after each render
    orb_volumes volumes;                     // the audio thread's copy
} orb_mixer;

// The defaults. api.c's static mixer starts from it too, since assets are
// published before orb_api_init runs.
#define ORB_MIXER_INIT {.song_position = {-1, -1}, .volumes = {1, 1, 1}}

void orb_mixer_init(orb_mixer* m);
void orb_mixer_render(orb_mixer* m, int16_t* out, int frames); // the audio thread; the rest is main
bool orb_mixer_rendered(const orb_mixer* m, uint32_t render);
uint32_t orb_mixer_set_assets(
    orb_mixer* m,
    const orb_assets* assets
); // the render to wait for, 0 if none
void orb_mixer_song_pause(orb_mixer* m);
void orb_mixer_song_play(orb_mixer* m, orb_song s, bool loop);
orb_song_position orb_mixer_song_position(const orb_mixer* m);
void orb_mixer_song_resume(orb_mixer* m);
void orb_mixer_song_stop(orb_mixer* m, int fade_ms);
orb_voice orb_mixer_sound_play(orb_mixer* m, orb_sample s, orb_sound_params p, int priority);
void orb_mixer_sound_set(orb_mixer* m, orb_voice v, orb_sound_params p);
void orb_mixer_sound_stop(orb_mixer* m, orb_voice v);
void orb_mixer_volume_set(orb_mixer* m, orb_volumes v);
