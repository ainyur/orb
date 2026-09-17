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

    // cast_path: dir/rel, unless rel is absolute or dir is empty
    CHECK(strcmp(cast_path(&scratch, "levels", "world.ldtk"), "levels/world.ldtk") == 0);
    CHECK(strcmp(cast_path(&scratch, "", "w.ldtk"), "w.ldtk") == 0);
    CHECK(strcmp(cast_path(&scratch, "levels", "/abs/w.ldtk"), "/abs/w.ldtk") == 0);

    // cast_dir_of: the directory part of a path, "" when there is none
    CHECK(strcmp(cast_dir_of(&scratch, "levels/world.ldtk"), "levels") == 0);
    CHECK(strcmp(cast_dir_of(&scratch, "w.ldtk"), "") == 0);
    CHECK(strcmp(cast_dir_of(&scratch, "/abs/dir/w.ldtk"), "/abs/dir") == 0);

#ifdef _WIN32
    CHECK(strcmp(cast_path(&scratch, "levels", "C:\\g\\w.ldtk"), "C:\\g\\w.ldtk") == 0);
    CHECK(strcmp(cast_dir_of(&scratch, "levels\\w.ldtk"), "levels") == 0);
    CHECK(strcmp(cast_dir_of(&scratch, "C:\\g\\w.ldtk"), "C:\\g") == 0);
#endif

    // the directories are overridden to point at the checked-in fixtures; a game
    // that keeps the conventional layout names none of them
    const char* manifest =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"art\": \"" ART "art\", \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\",\n"
        " \"fonts\": \"" ART "fonts\",\n"
        " \"world\": \"" ART "levels/world.ldtk\",\n"
        " \"songs\": {\"loop\": 120}}\n";

    CHECK(orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)manifest, strlen(manifest)}));

    orb_manifest m;
    orb_cast_result r;
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strcmp(m.id, "fixture") == 0);
    CHECK_EQ(m.size.width, 64);
    CHECK_EQ(m.size.height, 32);
    CHECK_EQ(m.asset_headroom, 1048576);
    CHECK_EQ(m.song_count, 1);
    CHECK(r.file.ptr >= out_mem && r.file.ptr < out_mem + sizeof out_mem);

    // every file and directory the cast read, once each: what scry watches, so a
    // file added to a directory recasts without touching orb.json. The world
    // casts before the fonts and the art, so the project and its two level
    // files come first; the font directory and its file follow, ahead of the
    // art walk and the tileset it pulls in.
    CHECK_EQ(r.read_count, 14);
    CHECK(strcmp(r.reads[0], "orb.json") == 0);
    CHECK(strcmp(r.reads[1], ART "art/palette.aseprite") == 0);
    CHECK(strcmp(r.reads[2], ART "levels/world.ldtk") == 0);
    CHECK(strcmp(r.reads[3], ART "levels/world/Room.ldtkl") == 0);
    CHECK(strcmp(r.reads[4], ART "levels/world/Annex.ldtkl") == 0);
    CHECK(strcmp(r.reads[5], ART "fonts") == 0);
    CHECK(strcmp(r.reads[6], ART "fonts/body.aseprite") == 0);
    CHECK(strcmp(r.reads[7], ART "art") == 0);
    CHECK(strcmp(r.reads[8], ART "art/player.aseprite") == 0);
    CHECK(strcmp(r.reads[9], ART "levels/tiles.aseprite") == 0);
    CHECK(strcmp(r.reads[13], ART "music/loop.wav") == 0);

    orb_assets as;
    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.palette[1 * 4 + 2], 64);
    CHECK_EQ(as.palette[2 * 4 + 0], 255);
    // sprites, then the font sheet, then the world's tileset sheet
    CHECK_EQ(as.sheet_count, 3);
    CHECK_EQ(as.sprite_count, 2);
    CHECK_EQ(as.sprites[0].width, 8);
    CHECK_EQ(as.sprites[0].ox, 4);
    CHECK_EQ(as.sprites[0].frame_width, 16);
    CHECK_EQ(as.sprites[1].ox, 6);

    const orb_sheet_desc* sh = &as.sheets[0];
    const orb_sprite_desc* sp = &as.sprites[0];
    CHECK_EQ(as.pixels[sh->pixels + sp->y * sh->width + sp->x], 2);

    CHECK_EQ(as.sample_count, 2);
    CHECK_EQ(as.song_count, 1);

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

    // the world: one tileset sheet after the art and font sheets, two levels, six layers
    CHECK_EQ(as.sheet_count, 3);
    CHECK_EQ(as.tileset_count, 1);
    CHECK_EQ(as.tilesets[0].sheet, 2);
    CHECK_EQ(as.tilesets[0].grid, 8);
    CHECK_EQ(as.tilesets[0].columns, 4);
    CHECK_EQ(as.tilesets[0].count, 8);
    CHECK_EQ(as.sheets[2].width, 32);
    CHECK_EQ(as.pixels[as.sheets[2].pixels + 8 * 32 + 24], 6); // tile 7's marker at its (0,0)
    CHECK_EQ(as.pixels[as.sheets[2].pixels + 8 * 32 + 25], 5); // tile 7's body
    CHECK_EQ(as.level_count, 2);
    CHECK_EQ(as.level_ids[0], orb_asset_id("room", ""));
    CHECK_EQ(as.level_ids[1], orb_asset_id("Annex", ""));
    CHECK_EQ(as.levels[0].width, 64);
    CHECK_EQ(as.levels[0].layer_count, 3);
    CHECK_EQ(as.levels[1].world_x, 64);
    CHECK_EQ(as.levels[1].depth, 1);
    CHECK_EQ(as.levels[1].first_layer, 3);
    CHECK_EQ(as.levels[1].layer_count, 3); // deco and collision are empty in Annex but still exist
    CHECK_EQ(as.layer_count, 6);
    CHECK_EQ(as.layer_ids[0], orb_asset_id("floor", ""));
    CHECK_EQ(as.layer_ids[2], orb_asset_id("deco", ""));

    // floor: one sub-layer, tile 0 stored as 1, the two flipped tile 7s
    const orb_layer_desc* floor = &as.layers[0];
    CHECK_EQ(floor->sublayers, 1);
    CHECK_EQ(floor->columns, 8);
    CHECK_EQ(floor->rows, 4);
    CHECK_EQ(floor->cells, ORB_NO_INDEX);
    CHECK_EQ(as.tiles[floor->tiles + 0], 1);
    CHECK_EQ(as.tiles[floor->tiles + 2 * 8 + 3], 8 | ORB_TILE_FLIP_X);
    CHECK_EQ(as.tiles[floor->tiles + 2 * 8 + 4], 8 | ORB_TILE_FLIP_Y);

    // collision: cells on the border, three sub-layers from the stacked cells
    const orb_layer_desc* collision = &as.layers[1];
    CHECK(collision->cells != ORB_NO_INDEX);
    CHECK_EQ(as.cells[collision->cells + 0], 1);
    CHECK_EQ(as.cells[collision->cells + 1 * 8 + 1], 0);
    CHECK_EQ(collision->sublayers, 3);
    CHECK_EQ(as.tiles[collision->tiles + 0], 2);              // tile 1 at (0,0), depth 0
    CHECK_EQ(as.tiles[collision->tiles + 32 + 0], 5);         // tile 4 at (0,0), depth 1
    CHECK_EQ(as.tiles[collision->tiles + 64 + 0], 3);         // tile 2 at (0,0), depth 2
    CHECK_EQ(as.tiles[collision->tiles + 32 + 3 * 8 + 7], 5); // tile 4 at (7,3), depth 1
    CHECK_EQ(as.tiles[collision->tiles + 64 + 3 * 8 + 7], 0); // nothing at (7,3), depth 2
    CHECK_EQ(as.tiles[collision->tiles + 1 * 8 + 1], 0);      // interior is empty

    // deco: offset, parallax, one flipped tile
    const orb_layer_desc* deco = &as.layers[2];
    CHECK_EQ(deco->offset_x, 2);
    CHECK_EQ(deco->offset_y, 3);
    CHECK(deco->parallax_x == 0.5f);
    CHECK(deco->parallax_y == 0.25f);
    CHECK_EQ(as.tiles[deco->tiles + 1 * 8 + 2], 4 | ORB_TILE_FLIP_X | ORB_TILE_FLIP_Y);

    // neighbors resolve to level indices
    CHECK_EQ(as.neighbor_count, 2);
    CHECK_EQ(as.levels[0].neighbor_count, 2);
    CHECK_EQ(as.neighbors[0].level, 1);
    CHECK_EQ(as.neighbors[0].dir, ORB_NEIGHBOR_E);
    CHECK_EQ(as.neighbors[1].dir, ORB_NEIGHBOR_HIGHER);

    // scratch peaks at the packed PCM plus the largest file plus the art, the font, and the world
    CHECK(scratch.peak < 2 * as.pcm_count * sizeof(int16_t) + (135 << 10));

    // no project: no levels, and the directory is watched so its creation recasts
    const char* no_project =
        "{\"id\": \"bare\", \"name\": \"Bare\", \"size\": [64, 32], \"asset_headroom\": 1048576,\n"
        " \"palette\": \"" ART "art/palette.aseprite\", \"art\": \"" ART "art\",\n"
        " \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\", \"songs\": {\"loop\": 120}}\n";
    CHECK(
        orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)no_project, strlen(no_project)})
    );
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.level_count, 0);
    CHECK_EQ(as.sheet_count, 1);
    bool watched = false;
    for (int i = 0; i < r.read_count; i++)
        if (strcmp(r.reads[i], "levels") == 0) watched = true;
    CHECK(watched);

    // "world" naming a bare filename has no directory part (cast_dir_of("w.ldtk")
    // is ""), so "." is watched instead, and the game directory's own creation
    // of the file still recasts
    const char* bare_world =
        "{\"id\": \"bare\", \"name\": \"Bare\", \"size\": [64, 32], \"asset_headroom\": 1048576,\n"
        " \"palette\": \"" ART "art/palette.aseprite\", \"world\": \"w.ldtk\"}\n";
    CHECK(
        orb_os_write_file(DIR "/orb.json", (orb_span) {(uint8_t*)bare_world, strlen(bare_world)})
    );
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.level_count, 0);
    bool dot_watched = false;
    for (int i = 0; i < r.read_count; i++)
        if (strcmp(r.reads[i], ".") == 0) dot_watched = true;
    CHECK(dot_watched);

    // a cell that nine tiles land on is more sub-layers than a layer allows,
    // and a tileset whose flattened size disagrees with the project is refused
    CHECK(orb_os_make_dir(DIR "/levels"));

    const char* stack_project_template =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,\n"
        " \"worlds\": [],\n"
        " \"defs\": {\"layers\": [{\"uid\": 2, \"identifier\": \"grid\", \"type\": \"IntGrid\",\n"
        "   \"gridSize\": 8, \"tilesetDefUid\": 7, \"parallaxFactorX\": 0, \"parallaxFactorY\": "
        "0,\n"
        "   \"parallaxScaling\": false, \"intGridValues\": [{\"value\": 1, \"identifier\": "
        "\"wall\"}]}],\n"
        "  \"tilesets\": [{\"uid\": 7, \"identifier\": \"tiles\",\n"
        "    \"relPath\": \"../../../../tests/fixtures/levels/tiles.aseprite\",\n"
        "    \"pxWid\": %d, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,\n"
        "    \"__cWid\": 4, \"__cHei\": 2}],\n"
        "  \"entities\": [], \"enums\": []},\n"
        " \"levels\": [{\"identifier\": \"Stack\", \"iid\": \"stack-iid\", \"uid\": 20,\n"
        "   \"worldX\": 0, \"worldY\": 0, \"worldDepth\": 0, \"pxWid\": 8, \"pxHei\": 8,\n"
        "   \"bgRelPath\": null, \"externalRelPath\": null, \"__neighbours\": [], "
        "\"fieldInstances\": [],\n"
        "   \"layerInstances\": [{\"__identifier\": \"grid\", \"__type\": \"IntGrid\", "
        "\"layerDefUid\": 2,\n"
        "     \"__cWid\": 1, \"__cHei\": 1, \"__gridSize\": 8, \"__opacity\": 1,\n"
        "     \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0, \"pxOffsetX\": 0, \"pxOffsetY\": "
        "0,\n"
        "     \"visible\": true, \"iid\": \"grid-iid\", \"seed\": 1, \"__tilesetDefUid\": 7,\n"
        "     \"intGridCsv\": [1], \"gridTiles\": [],\n"
        "     \"autoLayerTiles\": [\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1},\n"
        "       {\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"d\": [0], \"a\": 1}],\n"
        "     \"entityInstances\": []}]}]}\n";
    char stack_project[2048];
    int stack_len = snprintf(stack_project, sizeof stack_project, stack_project_template, 32);
    CHECK(stack_len > 0 && (size_t)stack_len < sizeof stack_project);
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk", (orb_span) {(uint8_t*)stack_project, (size_t)stack_len}
    ));

    const char* stack_manifest =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"asset_headroom\": 1048576, \"palette\": \"" ART "art/palette.aseprite\"}\n";
    CHECK(orb_os_write_file(
        DIR "/orb.json", (orb_span) {(uint8_t*)stack_manifest, strlen(stack_manifest)}
    ));
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "sub-layers") != nullptr);

    int mismatch_len = snprintf(stack_project, sizeof stack_project, stack_project_template, 16);
    CHECK(mismatch_len > 0 && (size_t)mismatch_len < sizeof stack_project);
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk", (orb_span) {(uint8_t*)stack_project, (size_t)mismatch_len}
    ));
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "32x16") != nullptr);

    // two level identifiers that fold to one name are refused
    const char* duplicate_names_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,\n"
        " \"worlds\": [],\n"
        " \"defs\": {\"layers\": [], \"tilesets\": [], \"entities\": [], \"enums\": []},\n"
        " \"levels\": [\n"
        "   {\"identifier\": \"Room\", \"iid\": \"room-iid\", \"uid\": 1, \"worldX\": 0,\n"
        "    \"worldY\": 0, \"worldDepth\": 0, \"pxWid\": 8, \"pxHei\": 8, \"bgRelPath\": null,\n"
        "    \"externalRelPath\": null, \"__neighbours\": [], \"fieldInstances\": [],\n"
        "    \"layerInstances\": []},\n"
        "   {\"identifier\": \"room\", \"iid\": \"room2-iid\", \"uid\": 2, \"worldX\": 8,\n"
        "    \"worldY\": 0, \"worldDepth\": 0, \"pxWid\": 8, \"pxHei\": 8, \"bgRelPath\": null,\n"
        "    \"externalRelPath\": null, \"__neighbours\": [], \"fieldInstances\": [],\n"
        "    \"layerInstances\": []}]}\n";
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk",
        (orb_span) {(uint8_t*)duplicate_names_project, strlen(duplicate_names_project)}
    ));
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "share a name") != nullptr);

    // a neighbour naming a level iid that does not exist is refused
    const char* bad_neighbor_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,\n"
        " \"worlds\": [],\n"
        " \"defs\": {\"layers\": [], \"tilesets\": [], \"entities\": [], \"enums\": []},\n"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"room-iid\", \"uid\": 1,\n"
        "   \"worldX\": 0, \"worldY\": 0, \"worldDepth\": 0, \"pxWid\": 8, \"pxHei\": 8,\n"
        "   \"bgRelPath\": null, \"externalRelPath\": null,\n"
        "   \"__neighbours\": [{\"levelIid\": \"nowhere\", \"dir\": \"e\"}],\n"
        "   \"fieldInstances\": [], \"layerInstances\": []}]}\n";
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk",
        (orb_span) {(uint8_t*)bad_neighbor_project, strlen(bad_neighbor_project)}
    ));
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &m, &r, &err));
    CHECK(strstr(err.text, "not a level") != nullptr);

    // clean up so later casts in this file, which use the default world path,
    // find no project again
    remove(DIR "/levels/world.ldtk");

    // an editor-saved project: LDtk's own free-layout sample, when LDtk is installed
    const char* samples = "/usr/share/ldtk/extraFiles/samples";
    orb_os_info sample_info;
    orb_path sample_project;
    orb_path_join(sample_project, samples, "WorldMap_Free_layout.ldtk");

    if (orb_os_stat(sample_project, &sample_info)) {
        static uint8_t big_scratch[48 << 20], big_out[8 << 20];
        orb_arena bs, bo;
        orb_arena_init(&bs, "scratch", big_scratch, sizeof big_scratch);
        orb_arena_init(&bo, "out", big_out, sizeof big_out);
        // the tileset file doubles as the palette, so every color matches
        const char* sample_manifest =
            "{\"id\": \"sample\", \"name\": \"Sample\", \"size\": [320, 180], \"asset_headroom\": "
            "50331648,\n"
            " \"palette\": "
            "\"/usr/share/ldtk/extraFiles/samples/atlas/NuclearBlaze_by_deepnight.aseprite\",\n"
            " \"art\": \"none\", \"sfx\": \"none\", \"music\": \"none\",\n"
            " \"world\": \"/usr/share/ldtk/extraFiles/samples/WorldMap_Free_layout.ldtk\"}\n";
        CHECK(orb_os_write_file(
            DIR "/orb.json", (orb_span) {(uint8_t*)sample_manifest, strlen(sample_manifest)}
        ));

        CHECK(orb_cast_game(&bs, &bo, DIR, &m, &r, &err));
        CHECK(orb_file_load(r.file, &as, &err));
        CHECK_EQ(as.level_count, 12);
        CHECK_EQ(as.tileset_count, 1);
        CHECK_EQ(as.tilesets[0].columns, 36);
        CHECK(as.layer_count >= 24);
        printf(
            "sample: %u levels, %u layers, %u tiles, %u cells\n", as.level_count, as.layer_count,
            as.tile_count, as.cell_count
        );
    } else
        printf("sample: LDtk samples not installed, skipped\n");

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
    CHECK(orb_file_load(r.file, &as, &err));
    CHECK_EQ(as.sprite_count, 2);
    CHECK_EQ(as.sample_count, 0); // no sfx or music directory is no error, and both are watched
    CHECK_EQ(r.read_count, 9);    // "levels" and "fonts" too: missing, but both are watched
    CHECK(strcmp(r.reads[2], "levels") == 0);
    CHECK(strcmp(r.reads[3], "fonts") == 0);
    CHECK(strcmp(r.reads[4], "art") == 0);
    CHECK(strcmp(r.reads[5], "art/sub") == 0);
    CHECK(strcmp(r.reads[6], "art/sub/hero.aseprite") == 0);
    CHECK(strcmp(r.reads[7], "sfx") == 0);
    CHECK(strcmp(r.reads[8], "music") == 0);
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
