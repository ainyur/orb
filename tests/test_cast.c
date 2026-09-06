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
        " \"sprites\": [\"" ART "player.aseprite\"]}\n";

    CHECK(orb_os_write_file(DIR "/game.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

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
    CHECK_EQ(as.animation_count, 1);
    CHECK_EQ(as.animations[0].first_sprite, 0);
    CHECK_EQ(as.animations[0].count, 2);
    CHECK_EQ(as.durations[as.animations[0].first_duration], 6);
    CHECK_EQ(as.durations[as.animations[0].first_duration + 1], 12);

    // ids are what a game finds assets by: the file stem plus frame number or
    // tag, case-insensitive, hashed the same way at cast and at find
    CHECK(as.sprite_ids != NULL);
    CHECK(as.sprite_ids[0] == orb_asset_id("player", "0"));
    CHECK(as.sprite_ids[1] == orb_asset_id("Player", "1"));
    CHECK(as.animation_ids != NULL);
    CHECK(as.animation_ids[0] == orb_asset_id("player", "walk"));
    CHECK(orb_asset_id("player", "walk") != orb_asset_id("player", "0"));

    CHECK(strcmp(orb_seal_path(&scratch, DIR, &m), DIR "/bin/fixture.orb") == 0);
    CHECK(!orb_cast_game(&scratch, &out, "build/scratch", &m, &r, &err));
    CHECK(strstr(err.text, "game.json") != NULL);

    static alignas(16) uint8_t tiny_mem[4096];
    orb_arena tiny;
    orb_arena_init(&tiny, "cast scratch", tiny_mem, sizeof tiny_mem);
    CHECK(!orb_cast_game(&tiny, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "cast scratch") != NULL);
    CHECK(strstr(err.text, "asset_headroom") != NULL);

    orb_arena_init(&tiny, "asset half B", tiny_mem, 512);

    CHECK(!orb_cast_game(&scratch, &tiny, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "asset half B") != NULL);

    return 0;
}
