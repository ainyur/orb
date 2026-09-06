#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// A game directory under scratch that borrows the fixture art, so the header
// the cast writes lands in scratch too.
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
    CHECK(strcmp(r.sprite_names[0], "PLAYER_0") == 0);
    CHECK(strcmp(r.sprite_names[1], "PLAYER_1") == 0);
    CHECK_EQ(r.animation_count, 1);
    CHECK(strcmp(r.animation_names[0], "PLAYER_WALK") == 0);

    orb_assets as;

    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.palette[1 * 4 + 2], 64);  // master index 1 is (32,32,64)
    CHECK_EQ(as.palette[2 * 4 + 0], 255); // master index 2 is red
    CHECK_EQ(as.sheet_count, 1);
    CHECK_EQ(as.sprite_count, 2);
    CHECK_EQ(as.sprites[0].w, 8);
    CHECK_EQ(as.sprites[0].ox, 4);
    CHECK_EQ(as.sprites[0].fw, 16);
    CHECK_EQ(as.sprites[1].ox, 6);

    // the player's own red (index 1) was remapped to master red (index 2)
    const orb_sheet_desc* sh = &as.sheets[0];
    const orb_sprite_desc* sp = &as.sprites[0];

    CHECK_EQ(as.pixels[sh->pixels + sp->y * sh->w + sp->x], 2);
    CHECK_EQ(as.animation_count, 1);
    CHECK_EQ(as.animations[0].first_sprite, 0);
    CHECK_EQ(as.animations[0].count, 2);
    CHECK_EQ(as.durations[as.animations[0].first_duration], 6);      // 100 ms
    CHECK_EQ(as.durations[as.animations[0].first_duration + 1], 12); // 200 ms
    // every entry carries an id derived from its source name, so a recast can tell
    // when a different source landed at the same index
    CHECK(as.sprite_ids != NULL);
    CHECK(as.sprite_ids[0] != as.sprite_ids[1]);
    CHECK(as.animation_ids != NULL);
    CHECK(as.animation_ids[0] != 0);

    // the header was written beside the manifest, and a second cast leaves it alone
    orb_span h;

    CHECK(orb_os_read_file(DIR "/game_assets.h", &scratch, &h));
    CHECK(strstr((char*)h.ptr, "#define ORB_SPRITE_PLAYER_1 ORB_SPRITE(1)") != NULL);
    CHECK(strstr((char*)h.ptr, "#define ORB_ANIMATION_PLAYER_WALK ORB_ANIMATION(0)") != NULL);

    uint64_t written = orb_os_file_mtime(DIR "/game_assets.h");

    orb_os_sleep(10000000);
    orb_arena_reset(&out);
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK_EQ(orb_os_file_mtime(DIR "/game_assets.h"), written);

    // seal without an output path writes bin/<id>.orb beside the manifest
    CHECK(strcmp(orb_seal_path(&scratch, DIR, &m), DIR "/bin/fixture.orb") == 0);

    // a directory without a manifest
    CHECK(!orb_cast_game(&scratch, &out, "build/scratch", &m, &r, &err));
    CHECK(strstr(err.text, "game.json") != NULL);

    // running out of cast scratch or output is a cast error, not a fatal: scry keeps
    // the previous assets and tells the author what to raise
    static alignas(16) uint8_t tiny_mem[4096];
    orb_arena tiny;

    orb_arena_init(&tiny, "cast scratch", tiny_mem, sizeof tiny_mem);
    CHECK(!orb_cast_game(&tiny, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "cast scratch") != NULL);
    CHECK(strstr(err.text, "asset_headroom") != NULL);
    orb_arena_init(&tiny, "asset half B", tiny_mem, 512); // the palette alone is 1 KB
    CHECK(!orb_cast_game(&scratch, &tiny, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "asset half B") != NULL);
    return 0;
}
