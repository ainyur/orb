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

    // the directories are overridden to point at the checked-in fixtures; a game
    // that keeps the conventional layout names none of them
    const char* manifest =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"art\": \"" ART "art\", \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\",\n"
        " \"songs\": {\"loop\": 120}}\n";

    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

    orb_manifest m;
    orb_cast_result r;
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strcmp(m.id, "fixture") == 0);
    CHECK_EQ(m.size_w, 64);
    CHECK_EQ(m.size_h, 32);
    CHECK_EQ(m.asset_headroom, 1048576);
    CHECK_EQ(m.song_count, 1);
    CHECK(r.file.ptr >= out_mem && r.file.ptr < out_mem + sizeof out_mem);
    CHECK_EQ(r.sprite_count, 2);
    CHECK_EQ(r.animation_count, 1);

    // every file and directory the cast read, once each: what scry watches, so a
    // file added to a directory recasts without touching orb.json
    CHECK_EQ(r.read_count, 8);
    CHECK(strcmp(r.reads[0], "orb.json") == 0);
    CHECK(strcmp(r.reads[1], ART "art/palette.aseprite") == 0);
    CHECK(strcmp(r.reads[2], ART "art") == 0);
    CHECK(strcmp(r.reads[3], ART "art/player.aseprite") == 0);
    CHECK(strcmp(r.reads[7], ART "music/loop.wav") == 0);

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

    // scratch peaks at the packed PCM plus the largest file plus the art
    CHECK(scratch.peak < 2 * as.pcm_count * sizeof(int16_t) + (100 << 10));

    // the art directory is walked recursively; a stem must be unique within a kind
    orb_span player;
    CHECK(orb_os_read_file("tests/fixtures/art/player.aseprite", &scratch, &player));
    CHECK(orb_os_make_dir(DIR "/art"));
    CHECK(orb_os_make_dir(DIR "/art/sub"));
    CHECK(orb_os_write_file(DIR "/art/sub/hero.aseprite", player));
    remove(DIR "/art/hero.aseprite");

    const char* nested =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)nested, strlen(nested)}));
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK_EQ(r.sprite_count, 2);
    CHECK_EQ(r.sample_count, 0); // no sfx or music directory is no error, and both are watched
    CHECK_EQ(r.read_count, 7);
    CHECK(strcmp(r.reads[2], "art") == 0);
    CHECK(strcmp(r.reads[3], "art/sub") == 0);
    CHECK(strcmp(r.reads[4], "art/sub/hero.aseprite") == 0);
    CHECK(strcmp(r.reads[5], "sfx") == 0);
    CHECK(strcmp(r.reads[6], "music") == 0);
    CHECK(orb_os_write_file(DIR "/art/hero.aseprite", player));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "art/hero.aseprite") != nullptr);
    CHECK(strstr(err.text, "art/sub/hero.aseprite") != nullptr);
    remove(DIR "/art/hero.aseprite");

    // every key but id, name, and size has a default
    const char* plain = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)plain, strlen(plain)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err)); // no art/palette.aseprite here
    CHECK(strstr(err.text, "art/palette.aseprite") != nullptr);
    CHECK_EQ(m.asset_headroom, 16 << 20);
    CHECK(strcmp(m.art, "art") == 0);
    CHECK(strcmp(m.sfx, "sfx") == 0);
    CHECK(strcmp(m.music, "music") == 0);

    // a song names its tempo in orb.json, since a rendered file carries none; a
    // song without one and a tempo without a song are both refused
    const char* silent =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"music\": \"" ART "music\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)silent, strlen(silent)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* phantom =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"songs\": {\"title\": 120}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)phantom, strlen(phantom)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "title") != nullptr);
    CHECK(strstr(err.text, "music") != nullptr);

    // songs is an object of stem to bpm within 0..1000
    const char* bare =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"songs\": [\"loop\"]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)bare, strlen(bare)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "songs") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* slow =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"songs\": {\"loop\": 0}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)slow, strlen(slow)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "loop") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* fast =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"songs\": {\"loop\": 1e999}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)fast, strlen(fast)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "bpm") != nullptr);

    // a sound must be mono, since pan positions it; only a song may be stereo
    const char* stereo =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"sfx\": \"" ART "music\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)stereo, strlen(stereo)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "mono") != nullptr);
    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

    CHECK(!orb_manifest_load(&scratch, "build/scratch", &m, &err));
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
