#include "../src/audio/mixer.c"
#include "test.h"

// Sample 0: 8 mono frames of 1000. Sample 1: 4 stereo frames, left 2000 and right
// -2000, looping over all four. Sample 2: the mono frames with an empty loop.
static const int16_t pcm[16] = {1000, 1000,  1000, 1000,  1000, 1000,  1000, 1000,
                                2000, -2000, 2000, -2000, 2000, -2000, 2000, -2000};
static const orb_sample_desc samples[3] = {
    {.first = 0, .count = 8, .rate = 48000, .channels = 1},
    {.first = 8, .count = 4, .loop_start = 0, .loop_end = 4, .rate = 48000, .channels = 2},
    {.first = 0, .count = 8, .loop_start = 2, .loop_end = 2, .rate = 48000, .channels = 1},
};
static const uint64_t sample_ids[3] = {11, 22, 33};
static const orb_song_desc songs[2] = {
    {.sample = 1, .millibpm = 120000},
    {.sample = 2, .millibpm = 90000}
};
static const uint64_t song_ids[2] = {44, 55};
static orb_assets assets = {
    .samples = samples,
    .sample_count = 3,
    .pcm = pcm,
    .pcm_count = 16,
    .songs = songs,
    .song_count = 2,
    .sample_ids = sample_ids,
    .song_ids = song_ids
};
static orb_mixer mixer;
static int16_t out[1024 * 2];

static void render(int frames) {
    orb_mixer_render(&mixer, out, frames);
}

static int playing_count(void) {
    int n = 0;

    for (int i = ORB_GAME_VOICE_FIRST; i < ORB_VOICE_COUNT; i++)
        if (atomic_load(&mixer.voices[i].playing)) n++;

    return n;
}

static bool song_playing(void) {
    return atomic_load(&mixer.voices[ORB_SONG_VOICE].playing) != 0;
}

int main(void) {
    CHECK_EQ(sizeof(orb_mixer_command), 32); // the ring holds 256 of them

    orb_sound_params full = {.volume = 1};
    orb_voice voice;

    mixer = (orb_mixer)ORB_MIXER_INIT;
    CHECK(
        mixer.volumes.master == ORB_MIXER_ONE && mixer.volumes.song == ORB_MIXER_ONE &&
        mixer.volumes.sound == ORB_MIXER_ONE
    );
    CHECK(orb_mixer_song_position(&mixer).ms == -1);  // before any render
    CHECK(atomic_is_lock_free(&mixer.song_position)); // the main thread reads it with one load

    // silence before any assets; the render counters advance
    CHECK(!orb_mixer_rendered(&mixer, 1));
    render(4);
    CHECK_EQ(out[0], 0);
    CHECK(orb_mixer_rendered(&mixer, 1));
    CHECK(!orb_mixer_rendered(&mixer, 2));
    CHECK_EQ(orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0).v, ORB_NO_VOICE.v);
    CHECK_EQ(orb_mixer_set_assets(&mixer, &assets), 0); // no render in flight, nothing to wait for

    // a one-shot plays its 8 frames at full scale on both sides, then frees its voice
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);
    CHECK_EQ(MIXER_VOICE_INDEX(voice), ORB_GAME_VOICE_FIRST);
    CHECK_EQ(MIXER_VOICE_GEN(voice), 1);
    CHECK_EQ(playing_count(), 1);
    render(8);
    CHECK_EQ(out[0], 1000);
    CHECK_EQ(out[1], 1000);
    CHECK_EQ(out[15], 1000);
    CHECK_EQ(playing_count(), 1);
    render(1);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(playing_count(), 0);

    // pan right silences the left; a stereo sample lands each channel on its side
    voice =
        orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), (orb_sound_params) {.volume = 1, .pan = 1}, 0);
    render(1);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 1000);
    orb_mixer_sound_stop(&mixer, voice);
    render(1);
    CHECK_EQ(out[1], 0);
    CHECK_EQ(playing_count(), 0);
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(1), full, 0);
    render(1);
    CHECK_EQ(out[0], 2000);
    CHECK_EQ(out[1], -2000);
    orb_mixer_sound_stop(&mixer, voice);

    // sound and master volumes scale a one-shot; song volume does not
    orb_mixer_volume_set(&mixer, (orb_volumes) {.master = 0.5f, .song = 0.1f, .sound = 0.5f});
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);
    render(1);
    CHECK_EQ(out[0], 250);
    orb_mixer_sound_stop(&mixer, voice);
    orb_mixer_volume_set(&mixer, (orb_volumes) {1, 1, 1});
    render(1);
    CHECK_EQ(playing_count(), 0);

    // an octave up walks the sample twice as fast
    voice = orb_mixer_sound_play(
        &mixer, ORB_SAMPLE(0), (orb_sound_params) {.volume = 1, .pitch_cents = 1200}, 0
    );
    render(4);
    CHECK_EQ(playing_count(), 1);
    render(1);
    CHECK_EQ(playing_count(), 0);

    // a one-shot never loops, even with file loop points; a stale handle is a no-op
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(1), full, 0);
    render(5);
    CHECK_EQ(playing_count(), 0);
    orb_mixer_sound_set(&mixer, voice, full);
    orb_mixer_sound_stop(&mixer, voice);
    CHECK_EQ(mixer.dropped, 0);
    render(1);
    CHECK_EQ(playing_count(), 0);

    // sixteen voices, then stealing: lowest priority at or below ours, oldest first
    orb_voice held[16];

    for (int i = 0; i < 16; i++)
        held[i] = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, i < 8 ? 5 : 9);

    CHECK_EQ(playing_count(), 16);
    CHECK_EQ(orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 3).v, ORB_NO_VOICE.v);
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 5);
    CHECK_EQ(MIXER_VOICE_INDEX(voice), ORB_GAME_VOICE_FIRST); // the oldest priority-5 voice
    CHECK_EQ(MIXER_VOICE_GEN(voice), MIXER_VOICE_GEN(held[0]) + 1);
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 9);
    CHECK_EQ(
        MIXER_VOICE_INDEX(voice), ORB_GAME_VOICE_FIRST + 1
    );                                     // 5 before 9; the first is newest now
    orb_mixer_sound_stop(&mixer, held[0]); // stale: the first voice belongs to a newer claim
    render(1);
    CHECK_EQ(playing_count(), 16);
    render(20);
    CHECK_EQ(playing_count(), 0);

    // a claim that lands while the audio thread is still on the old sound survives that sound's end
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);
    render(1);

    for (int i = 0; i < 15; i++)
        orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);

    orb_voice stolen_voice =
        orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0); // steals the first; play still queued
    orb_voice_state* first = &mixer.voices[ORB_GAME_VOICE_FIRST];

    CHECK_EQ(MIXER_VOICE_INDEX(stolen_voice), ORB_GAME_VOICE_FIRST);
    mixer_voice_end(first); // the old sound ends before the play is consumed
    CHECK_EQ(atomic_load(&first->playing), MIXER_VOICE_GEN(stolen_voice));
    render(1);
    CHECK_EQ(out[0], 16000); // all sixteen at their first frame
    render(20);
    CHECK_EQ(playing_count(), 0);

    // a full ring drops the command and claims no voice
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);

    for (int i = 0; i < ORB_MIXER_RING; i++)
        orb_mixer_sound_set(&mixer, voice, full);

    CHECK_EQ(mixer.dropped, 1);
    CHECK_EQ(orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0).v, ORB_NO_VOICE.v);
    CHECK_EQ(mixer.dropped, 2);
    CHECK_EQ(playing_count(), 1);
    render(20);
    CHECK_EQ(playing_count(), 0);

    // the song loops on its file points, pauses in place, fades out, and restarts at full gain
    orb_mixer_song_play(&mixer, ORB_SONG(7), true); // no such song
    render(1);
    CHECK(!song_playing());
    orb_mixer_song_play(&mixer, ORB_SONG(0), true);
    render(6);
    CHECK(song_playing());
    CHECK_EQ(out[0], 2000);
    CHECK_EQ(out[1], -2000);
    CHECK_EQ(out[10], 2000); // frame 5 is frame 1 again after the wrap at 4
    orb_mixer_volume_set(&mixer, (orb_volumes) {.master = 1, .song = 0.5f, .sound = 1});
    render(1);
    CHECK_EQ(out[0], 1000);
    orb_mixer_volume_set(&mixer, (orb_volumes) {1, 1, 1});
    orb_mixer_song_pause(&mixer, true);
    render(2);
    CHECK_EQ(out[0], 0);
    CHECK(song_playing());
    orb_mixer_song_pause(&mixer, false);
    render(1);
    CHECK_EQ(out[0], 2000);
    orb_mixer_song_stop(&mixer, 1); // 1 ms: 48 frames of fade
    render(48);
    CHECK(out[0] == 2000);
    CHECK(out[46 * 2] > 0 && out[46 * 2] < 200);
    CHECK(out[47 * 2] > 0 && out[47 * 2] < 100);
    render(1);
    CHECK(!song_playing());
    CHECK_EQ(out[0], 0);
    orb_mixer_song_play(&mixer, ORB_SONG(0), true);
    orb_mixer_song_stop(&mixer, 1);
    render(10);
    orb_mixer_song_play(&mixer, ORB_SONG(0), true); // during the fade
    render(1);
    CHECK_EQ(out[0], 2000);
    orb_mixer_song_stop(&mixer, 0);
    render(1);
    CHECK(!song_playing());
    CHECK_EQ(out[0], 0);

    // a song without loop points loops the whole sample when asked, and plays through when not
    orb_mixer_song_play(&mixer, ORB_SONG(1), true);
    render(10);
    CHECK_EQ(out[9 * 2], 1000);
    CHECK(song_playing());
    orb_mixer_song_play(&mixer, ORB_SONG(1), false);
    render(9);
    CHECK(!song_playing());

    // song_position is the render head in the file as ms and millibeats, -1 with no song;
    // song 1's sample at 1 kHz, so a file frame is a millisecond and 48 output frames
    static const orb_sample_desc slow[3] = {
        {.first = 0, .count = 8, .rate = 48000, .channels = 1},
        {.first = 8, .count = 4, .loop_start = 0, .loop_end = 4, .rate = 48000, .channels = 2},
        {.first = 0, .count = 8, .loop_start = 2, .loop_end = 2, .rate = 1000, .channels = 1},
    };
    static orb_assets slowed;
    slowed = assets;
    slowed.samples = slow;
    (void)orb_mixer_set_assets(&mixer, &slowed);
    CHECK(orb_mixer_song_position(&mixer).ms == -1);
    CHECK(orb_mixer_song_position(&mixer).millibeats == -1);
    orb_mixer_song_play(&mixer, ORB_SONG(1), true); // 90 BPM
    render(289);                                    // just past file frame 6
    CHECK_EQ(orb_mixer_song_position(&mixer).ms, 6);
    CHECK_EQ(orb_mixer_song_position(&mixer).millibeats, 9);
    render(192); // wraps at 8: the playhead is at frame 2 again
    CHECK_EQ(orb_mixer_song_position(&mixer).ms, 2);
    CHECK_EQ(orb_mixer_song_position(&mixer).millibeats, 3);
    orb_mixer_song_pause(&mixer, true);
    render(5);
    CHECK_EQ(orb_mixer_song_position(&mixer).ms, 2);
    orb_mixer_song_pause(&mixer, false);
    orb_mixer_song_stop(&mixer, 0);
    render(1);
    CHECK(orb_mixer_song_position(&mixer).ms == -1);
    (void)orb_mixer_set_assets(&mixer, &assets);

    // whole octaves are exact, and a fade over seconds still steps every frame
    CHECK_EQ(mixer_step(48000, 1200), (uint64_t)2 << 32);
    CHECK_EQ(mixer_step(48000, 0), (uint64_t)1 << 32);
    CHECK_EQ(mixer_step(48000, -1200), (uint64_t)1 << 31);
    orb_mixer_song_play(&mixer, ORB_SONG(0), true);
    orb_mixer_song_stop(&mixer, 10000);
    render(1);
    uint32_t fade_before = mixer.voices[ORB_SONG_VOICE].fade_gain;
    render(1);
    CHECK(mixer.voices[ORB_SONG_VOICE].fade_gain < fade_before);
    CHECK(song_playing());
    orb_mixer_song_stop(&mixer, 0);
    render(1);

    // a recast that puts a different id at a sample's index silences the voices using it
    orb_mixer_song_play(&mixer, ORB_SONG(0), true);
    voice = orb_mixer_sound_play(&mixer, ORB_SAMPLE(0), full, 0);
    render(1);
    CHECK_EQ(playing_count(), 1);

    static const uint64_t sound_changed[3] = {99, 22, 33};
    static orb_assets swapped;
    swapped = assets;
    swapped.sample_ids = sound_changed;
    CHECK_EQ(orb_mixer_set_assets(&mixer, &swapped), 0);
    render(1);
    CHECK_EQ(playing_count(), 0);
    CHECK(song_playing());
    CHECK_EQ(out[0], 2000);

    // a recast that shrinks the looping song's sample under the playhead wraps, not ends
    (void)orb_mixer_set_assets(&mixer, &assets);
    orb_mixer_song_play(&mixer, ORB_SONG(0), true);
    render(3);
    static const orb_sample_desc shrunk[3] = {
        {.first = 0, .count = 8, .rate = 48000, .channels = 1},
        {.first = 8, .count = 2, .loop_start = 0, .loop_end = 2, .rate = 48000, .channels = 2},
        {.first = 0, .count = 8, .loop_start = 2, .loop_end = 2, .rate = 48000, .channels = 1},
    };
    static orb_assets shorter;
    shorter = assets;
    shorter.samples = shrunk;
    (void)orb_mixer_set_assets(&mixer, &shorter);
    render(1);
    CHECK(song_playing());
    CHECK_EQ(out[0], 2000);                          // frame 3 wrapped to 1 of the 2-frame loop
    CHECK_EQ(orb_mixer_song_position(&mixer).ms, 0); // and then to 0 after advancing
    (void)orb_mixer_set_assets(&mixer, &assets);

    static const uint64_t song_changed[3] = {11, 98, 33};
    static orb_assets swapped_song;
    swapped_song = assets;
    swapped_song.sample_ids = song_changed;
    (void)orb_mixer_set_assets(&mixer, &swapped_song);
    render(1);
    CHECK(!song_playing());
    CHECK_EQ(out[0], 0);

    // handles carry generations: one from before a recast plays nothing
    static const uint8_t gens[3] = {0, 1, 0};
    assets.sample_gens = gens;
    (void)orb_mixer_set_assets(&mixer, &assets);
    CHECK_EQ(orb_mixer_sound_play(&mixer, ORB_SAMPLE(1), full, 0).v, ORB_NO_VOICE.v);
    CHECK(orb_mixer_sound_play(&mixer, ORB_SAMPLE(1 | 1u << 24), full, 0).v != ORB_NO_VOICE.v);

    return 0;
}
