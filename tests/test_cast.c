#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#define DIR "build/scratch/fixture"
#define ART "../../../tests/fixtures/"

int main(void) {
    static alignas(16) uint8_t scratch_mem[4 << 20], out_mem[1 << 20];
    orb_arena scratch, out;

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    orb_error err;
    CHECK(orb_os_make_dir(DIR));

    const char* manifest =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
        " \"sprites\": [\"" ART "player.aseprite\"],\n"
        " \"sounds\": [\"" ART "beep.wav\"],\n"
        " \"songs\": [{\"bpm\": 120, \"path\": \"" ART "loop.wav\"}]}\n";

    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

    orb_manifest m;
    orb_cast_result r;
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strcmp(m.id, "fixture") == 0);
    CHECK_EQ(m.size_w, 64);
    CHECK_EQ(m.size_h, 32);
    CHECK_EQ(m.asset_headroom, 1048576);
    CHECK_EQ(m.sprite_count, 1);
    CHECK(r.file.ptr >= out_mem && r.file.ptr < out_mem + sizeof out_mem);
    CHECK_EQ(r.sprite_count, 2);
    CHECK_EQ(r.animation_count, 1);

    orb_assets as;
    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.palette[1 * 4 + 2], 64);
    CHECK_EQ(as.palette[2 * 4 + 0], 255);
    CHECK_EQ(as.sheet_count, 1);
    CHECK_EQ(as.sprite_count, 2);
    CHECK_EQ(as.sprites[0].w, 8);
    CHECK_EQ(as.sprites[0].ox, 4);
    CHECK_EQ(as.sprites[0].fw, 16);
    CHECK_EQ(as.sprites[1].ox, 6);

    const orb_sheet_desc* sh = &as.sheets[0];
    const orb_sprite_desc* sp = &as.sprites[0];
    CHECK_EQ(as.pixels[sh->pixels + sp->y * sh->w + sp->x], 2);

    CHECK_EQ(m.sound_count, 1);
    CHECK_EQ(m.song_count, 1);
    CHECK_EQ(r.sample_count, 2);
    CHECK_EQ(r.song_count, 1);

    CHECK_EQ(as.animation_count, 1);
    CHECK_EQ(as.animations[0].first_sprite, 0);
    CHECK_EQ(as.animations[0].count, 2);
    CHECK_EQ(as.durations[as.animations[0].first_duration], 6);
    CHECK_EQ(as.durations[as.animations[0].first_duration + 1], 12);

    // ids are what a game finds assets by: the file stem plus frame number or
    // tag, case-insensitive, hashed the same way at cast and at find
    CHECK(as.sprite_ids != nullptr);
    CHECK(as.sprite_ids[0] == orb_asset_id("player", "0"));
    CHECK(as.sprite_ids[1] == orb_asset_id("Player", "1"));
    CHECK(as.animation_ids != nullptr);
    CHECK(as.animation_ids[0] == orb_asset_id("player", "walk"));
    CHECK(orb_asset_id("player", "walk") != orb_asset_id("player", "0"));

    // sounds first, then each song's sample; a song's sample is never a sound
    CHECK_EQ(as.sample_count, 2);
    CHECK_EQ(as.samples[0].count, 4800);
    CHECK_EQ(as.samples[0].channels, 1);
    CHECK_EQ(as.samples[0].rate, 48000);
    CHECK_EQ(as.samples[0].loop_end, 0);
    CHECK_EQ(as.samples[1].first, 4800);
    CHECK_EQ(as.samples[1].channels, 2);
    CHECK_EQ(as.samples[1].count, 96000);
    CHECK_EQ(as.samples[1].loop_start, 0);
    CHECK_EQ(as.samples[1].loop_end, 96000);
    CHECK_EQ(as.pcm_count, 4800 + 2 * 96000);
    CHECK_EQ(as.pcm[0], -12000);
    CHECK_EQ(as.song_count, 1);
    CHECK_EQ(as.songs[0].sample, 1);
    CHECK(as.songs[0].bpm == 120);
    CHECK(as.sample_ids[0] == orb_asset_id("beep", ""));
    CHECK(as.sample_ids[1] == orb_asset_id("loop", "song"));
    CHECK(as.song_ids[0] == orb_asset_id("LOOP", ""));

    // scratch peaks at the packed PCM plus the largest file (here nearly the PCM again)
    // plus the art, where the old three copies of the audio put it past twice the PCM
    CHECK(scratch.peak < 2 * as.pcm_count * sizeof(int16_t) + (100 << 10));

    // a song that is not a wav is refused by name
    const char* tracker =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
        " \"sprites\": [], \"songs\": [{\"bpm\": 120, \"path\": \"music/title.fur\"}]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)tracker, strlen(tracker)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "title.fur") != nullptr);
    CHECK(strstr(err.text, ".wav") != nullptr);

    // a song is an object with a path and a positive bpm; a bare path and a bad bpm are refused
    const char* bare = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                       " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
                       " \"sprites\": [], \"songs\": [\"" ART "loop.wav\"]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)bare, strlen(bare)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "songs") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* slow =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
        " \"sprites\": [], \"songs\": [{\"bpm\": 0, \"path\": \"" ART "loop.wav\"}]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)slow, strlen(slow)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* fast =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
        " \"sprites\": [], \"songs\": [{\"bpm\": 1e999, \"path\": \"" ART "loop.wav\"}]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)fast, strlen(fast)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "bpm") != nullptr);

    // a sound must be mono, since pan positions it; only a song may be stereo
    const char* stereo = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                         " \"asset_headroom\": 1048576, \"palette\": \"" ART "palette.aseprite\",\n"
                         " \"sprites\": [], \"sounds\": [\"" ART "loop.wav\"]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)stereo, strlen(stereo)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "mono") != nullptr);
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

    CHECK(strcmp(orb_seal_path(&scratch, DIR, &m), DIR "/bin/fixture.orb") == 0);
    CHECK(!orb_cast_game(&scratch, &out, "build/scratch", &m, &r, &err));
    CHECK(strstr(err.text, "orb.json") != nullptr);

    static alignas(16) uint8_t tiny_mem[4096];
    orb_arena tiny;
    orb_arena_init(&tiny, "cast scratch", tiny_mem, sizeof tiny_mem);
    CHECK(!orb_cast_game(&tiny, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "cast scratch") != nullptr);
    CHECK(strstr(err.text, "asset_headroom") != nullptr);

    orb_arena_init(&tiny, "asset half B", tiny_mem, 512);

    CHECK(!orb_cast_game(&scratch, &tiny, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "asset half B") != nullptr);

    return 0;
}
