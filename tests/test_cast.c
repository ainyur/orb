#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#define DIR "build/scratch/fixture"
#define ART "../../../tests/fixtures/"

int main(void) {
    static alignas(16) u8 scratch_mem[4 << 20], out_mem[1 << 20];
    arena scratch, out;

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
    const char* manifest_json =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"art\": \"" ART "art\", \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\",\n"
        " \"fonts\": \"" ART "fonts\",\n"
        " \"world\": \"" ART "levels/world.ldtk\",\n"
        " \"songs\": {\"loop\": 120}}\n";

    CHECK(
        orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)manifest_json, strlen(manifest_json)})
    );

    orb_manifest manifest;
    orb_cast_result result;
    CHECK(orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strcmp(manifest.id, "fixture") == 0);
    CHECK_EQ(manifest.size.width, 64);
    CHECK_EQ(manifest.size.height, 32);
    CHECK_EQ(manifest.songs.len, 1);
    CHECK(result.file.elems >= out_mem && result.file.elems < out_mem + sizeof out_mem);

    // every file and directory the cast read, once each: what scry watches, so a
    // file added to a directory recasts without touching orb.json. The world
    // casts before the fonts and the art, so the project and its two level
    // files come first; the font directory and its file follow, ahead of the
    // art walk and the tileset it pulls in.
    CHECK_EQ(result.reads.len, 14);
    CHECK(strcmp(result.reads.elems[0], "orb.json") == 0);
    CHECK(strcmp(result.reads.elems[1], ART "art/palette.aseprite") == 0);
    CHECK(strcmp(result.reads.elems[2], ART "levels/world.ldtk") == 0);
    CHECK(strcmp(result.reads.elems[3], ART "levels/world/Room.ldtkl") == 0);
    CHECK(strcmp(result.reads.elems[4], ART "levels/world/Annex.ldtkl") == 0);
    CHECK(strcmp(result.reads.elems[5], ART "fonts") == 0);
    CHECK(strcmp(result.reads.elems[6], ART "fonts/body.aseprite") == 0);
    CHECK(strcmp(result.reads.elems[7], ART "art") == 0);
    CHECK(strcmp(result.reads.elems[8], ART "art/player.aseprite") == 0);
    CHECK(strcmp(result.reads.elems[9], ART "levels/tiles.aseprite") == 0);
    CHECK(strcmp(result.reads.elems[13], ART "music/loop.wav") == 0);

    orb_assets assets;
    CHECK(orb_file_load(result.file, &assets, &err));
    CHECK_EQ(assets.pal[1 * 4 + 2], 64);
    CHECK_EQ(assets.pal[2 * 4 + 0], 255);
    // sprites, then the font sheet, then the world's tileset sheet
    CHECK_EQ(assets.sheets.len, 3);
    CHECK_EQ(assets.sprites.len, 2);
    CHECK_EQ(assets.sprites.elems[0].width, 8);
    CHECK_EQ(assets.sprites.elems[0].ox, 4);
    CHECK_EQ(assets.sprites.elems[0].frame_width, 16);
    CHECK_EQ(assets.sprites.elems[1].ox, 6);

    const orb_sheet_desc* sheet = &assets.sheets.elems[0];
    const orb_sprite_desc* sprite = &assets.sprites.elems[0];
    CHECK_EQ(assets.pixels.elems[sheet->pixels + sprite->y * sheet->width + sprite->x], 2);

    CHECK_EQ(assets.samples.len, 2);
    CHECK_EQ(assets.songs.len, 1);

    CHECK_EQ(assets.anims.len, 1);
    CHECK_EQ(assets.anims.elems[0].first_sprite, 0);
    CHECK_EQ(assets.anims.elems[0].count, 2);
    CHECK_EQ(assets.durations.elems[assets.anims.elems[0].first_duration], 6);
    CHECK_EQ(assets.durations.elems[assets.anims.elems[0].first_duration + 1], 12);

    // ids are what a game finds assets by: the file stem plus frame number or
    // tag, case-insensitive, hashed the same way at cast and at find
    CHECK(assets.sprite_ids != nullptr);
    CHECK(assets.sprite_ids[0] == orb_asset_id("player", "0"));
    CHECK(assets.sprite_ids[1] == orb_asset_id("Player", "1"));
    CHECK(assets.anim_ids != nullptr);
    CHECK(assets.anim_ids[0] == orb_asset_id("player", "walk"));
    CHECK(orb_asset_id("player", "walk") != orb_asset_id("player", "0"));

    // sounds first, then each song's sample; a song's sample is never a sound
    CHECK_EQ(assets.samples.len, 2);
    CHECK_EQ(assets.samples.elems[0].count, 4800);
    CHECK_EQ(assets.samples.elems[0].channels, 1);
    CHECK_EQ(assets.samples.elems[0].rate, 48000);
    CHECK_EQ(assets.samples.elems[0].loop_end, 0);
    CHECK_EQ(assets.samples.elems[1].first, 4800);
    CHECK_EQ(assets.samples.elems[1].channels, 2);
    CHECK_EQ(assets.samples.elems[1].count, 96000);
    CHECK_EQ(assets.samples.elems[1].loop_start, 0);
    CHECK_EQ(assets.samples.elems[1].loop_end, 96000);
    CHECK_EQ(assets.pcm.len, 4800 + 2 * 96000);
    CHECK_EQ(assets.pcm.elems[0], -12000);
    CHECK_EQ(assets.songs.len, 1);
    CHECK_EQ(assets.songs.elems[0].sample, 1);
    CHECK_EQ(assets.songs.elems[0].millibpm, 120000);
    CHECK(assets.sample_ids[0] == orb_asset_id("beep", ""));
    CHECK(assets.sample_ids[1] == orb_asset_id("loop", "song"));
    CHECK(assets.song_ids[0] == orb_asset_id("LOOP", ""));

    // the world: one tileset sheet after the art and font sheets, two levels, six layers
    CHECK_EQ(assets.sheets.len, 3);
    CHECK_EQ(assets.tilesets.len, 1);
    CHECK_EQ(assets.tilesets.elems[0].sheet, 2);
    CHECK_EQ(assets.tilesets.elems[0].grid, 8);
    CHECK_EQ(assets.tilesets.elems[0].columns, 4);
    CHECK_EQ(assets.tilesets.elems[0].count, 8);
    CHECK_EQ(assets.sheets.elems[2].width, 32);
    CHECK_EQ(
        assets.pixels.elems[assets.sheets.elems[2].pixels + 8 * 32 + 24], 6
    ); // tile 7's marker at its (0,0)
    CHECK_EQ(assets.pixels.elems[assets.sheets.elems[2].pixels + 8 * 32 + 25], 5); // tile 7's body
    CHECK_EQ(assets.levels.len, 2);
    CHECK_EQ(assets.level_ids[0], orb_asset_id("room", ""));
    CHECK_EQ(assets.level_ids[1], orb_asset_id("Annex", ""));
    CHECK_EQ(assets.levels.elems[0].width, 64);
    CHECK_EQ(assets.levels.elems[0].layer_count, 3);
    CHECK_EQ(assets.levels.elems[1].world_x, 64);
    CHECK_EQ(assets.levels.elems[1].first_layer, 3);
    CHECK_EQ(
        assets.levels.elems[1].layer_count, 3
    ); // deco and collision are empty in Annex but still exist
    CHECK_EQ(assets.layers.len, 6);
    CHECK_EQ(assets.layer_ids[0], orb_asset_id("floor", ""));
    CHECK_EQ(assets.layer_ids[2], orb_asset_id("deco", ""));

    // floor: one sub-layer, tile 0 stored as 1, the two flipped tile 7s
    const orb_layer_desc* floor = &assets.layers.elems[0];
    CHECK_EQ(floor->sublayers, 1);
    CHECK_EQ(floor->columns, 8);
    CHECK_EQ(floor->rows, 4);
    CHECK_EQ(floor->cells, ORB_NO_INDEX);
    CHECK_EQ(assets.tiles.elems[floor->tiles + 0], 1);
    CHECK_EQ(assets.tiles.elems[floor->tiles + 2 * 8 + 3], 8 | ORB_TILE_FLIP_X);
    CHECK_EQ(assets.tiles.elems[floor->tiles + 2 * 8 + 4], 8 | ORB_TILE_FLIP_Y);

    // collision: cells on the border, three sub-layers from the stacked cells
    const orb_layer_desc* collision = &assets.layers.elems[1];
    CHECK(collision->cells != ORB_NO_INDEX);
    CHECK_EQ(assets.cells.elems[collision->cells + 0], 1);
    CHECK_EQ(assets.cells.elems[collision->cells + 1 * 8 + 1], 0);
    CHECK_EQ(collision->sublayers, 3);
    CHECK_EQ(assets.tiles.elems[collision->tiles + 0], 2);              // tile 1 at (0,0), depth 0
    CHECK_EQ(assets.tiles.elems[collision->tiles + 32 + 0], 5);         // tile 4 at (0,0), depth 1
    CHECK_EQ(assets.tiles.elems[collision->tiles + 64 + 0], 3);         // tile 2 at (0,0), depth 2
    CHECK_EQ(assets.tiles.elems[collision->tiles + 32 + 3 * 8 + 7], 5); // tile 4 at (7,3), depth 1
    CHECK_EQ(assets.tiles.elems[collision->tiles + 64 + 3 * 8 + 7], 0); // nothing at (7,3), depth 2
    CHECK_EQ(assets.tiles.elems[collision->tiles + 1 * 8 + 1], 0);      // interior is empty

    // deco: offset, parallax, one flipped tile
    const orb_layer_desc* deco = &assets.layers.elems[2];
    CHECK_EQ(deco->offset_x, 2);
    CHECK_EQ(deco->offset_y, 3);
    CHECK(deco->parallax_x == 0.5f);
    CHECK(deco->parallax_y == 0.25f);
    CHECK_EQ(assets.tiles.elems[deco->tiles + 1 * 8 + 2], 4 | ORB_TILE_FLIP_X | ORB_TILE_FLIP_Y);

    // neighbors resolve to level indices
    CHECK_EQ(assets.neighbors.len, 2);
    CHECK_EQ(assets.levels.elems[0].neighbor_count, 2);
    CHECK_EQ(assets.neighbors.elems[0].level, 1);
    CHECK_EQ(assets.neighbors.elems[0].dir, ORB_LEVEL_E);
    CHECK_EQ(assets.neighbors.elems[1].dir, ORB_LEVEL_HIGHER);

    // entities: three types, four placements in Room and none in Annex, the crate's two
    // defaults, crate-a's six values with its ref resolved to crate-b, and no field for the
    // color or the null values
    CHECK_EQ(assets.types.len, 3);
    CHECK_EQ(assets.type_ids[0], orb_asset_id("crate", ""));
    CHECK_EQ(assets.type_ids[2], orb_asset_id("Player", ""));
    CHECK_EQ(assets.types.elems[0].width, 8);
    CHECK_EQ(assets.types.elems[0].field_count, 2);
    CHECK_EQ(assets.types.elems[1].field_count, 0);
    CHECK_EQ(assets.placements.len, 4);
    CHECK_EQ(assets.levels.elems[0].first_placement, 0);
    CHECK_EQ(assets.levels.elems[0].placement_count, 4);
    CHECK_EQ(assets.levels.elems[1].placement_count, 0);
    CHECK_EQ(assets.placements.elems[0].type, 0);
    CHECK_EQ(assets.placements.elems[0].level, 0);
    CHECK_EQ(assets.placements.elems[0].x, 16);
    CHECK_EQ(assets.placements.elems[0].y, 8);
    CHECK(assets.placements.elems[0].iid == orb_asset_id("crate-a", ""));
    CHECK_EQ(assets.placements.elems[0].field_count, 6);
    CHECK_EQ(assets.placements.elems[1].field_count, 0);
    CHECK_EQ(assets.placements.elems[3].type, 2);
    CHECK_EQ(assets.placements.elems[3].x, 24);
    CHECK_EQ(assets.fields.len, 8);

    const orb_field_desc* hp_default = &assets.fields.elems[assets.types.elems[0].first_field];
    CHECK(hp_default->name == orb_asset_id("hp", ""));
    CHECK_EQ(hp_default->kind, ORB_FIELD_INT);
    CHECK_EQ(orb_bytes_i32(assets.field_data.elems + hp_default->data), 10);

    const orb_field_desc* label_default = hp_default + 1;
    CHECK_EQ(label_default->kind, ORB_FIELD_STRING);
    CHECK(
        strcmp(
            (const char*)assets.field_data.elems +
                orb_bytes_u32(assets.field_data.elems + label_default->data),
            "box"
        ) == 0
    );

    const orb_field_desc* crate_a = &assets.fields.elems[assets.placements.elems[0].first_field];
    CHECK_EQ(orb_bytes_i32(assets.field_data.elems + crate_a[0].data), 3);
    CHECK_EQ(crate_a[1].kind, ORB_FIELD_BOOL);
    CHECK_EQ(assets.field_data.elems[crate_a[1].data], 1);
    CHECK_EQ(crate_a[2].count, 3);
    CHECK_EQ(orb_bytes_i32(assets.field_data.elems + crate_a[2].data + 8), 3);
    CHECK(crate_a[3].name == orb_asset_id("kind", ""));
    CHECK(
        strcmp(
            (const char*)assets.field_data.elems +
                orb_bytes_u32(assets.field_data.elems + crate_a[3].data),
            "Wood"
        ) == 0
    );
    CHECK_EQ(crate_a[4].kind, ORB_FIELD_POINT);
    CHECK_EQ(orb_bytes_i32(assets.field_data.elems + crate_a[4].data), 40);
    CHECK_EQ(orb_bytes_i32(assets.field_data.elems + crate_a[4].data + 4), 16);
    CHECK_EQ(crate_a[5].kind, ORB_FIELD_REF);
    CHECK_EQ(orb_bytes_u32(assets.field_data.elems + crate_a[5].data), 1);
    CHECK_EQ(assets.fields.elems[0].data & 3, 0);
    CHECK_EQ(crate_a[5].data & 3, 0);

    // scratch peaks at the packed PCM plus the largest file plus the art, the font, and the
    // world, whose entity definitions and instances now add to its parsed size
    CHECK(scratch.peak < 2 * assets.pcm.len * sizeof(i16) + (150 << 10));

    // no project: no levels, and the directory is watched so its creation recasts
    const char* no_project =
        "{\"id\": \"bare\", \"name\": \"Bare\", \"size\": [64, 32],\n"
        " \"palette\": \"" ART "art/palette.aseprite\", \"art\": \"" ART "art\",\n"
        " \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\", \"songs\": {\"loop\": 120}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)no_project, strlen(no_project)}));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(orb_file_load(result.file, &assets, &err));
    CHECK_EQ(assets.levels.len, 0);
    CHECK_EQ(assets.sheets.len, 1);
    bool watched = false;
    for (u32 i = 0; i < result.reads.len; i++)
        if (strcmp(result.reads.elems[i], "levels") == 0) watched = true;
    CHECK(watched);

    // "world" naming a bare filename has no directory part (cast_dir_of("w.ldtk")
    // is ""), so "." is watched instead, and the game directory's own creation
    // of the file still recasts
    const char* bare_world =
        "{\"id\": \"bare\", \"name\": \"Bare\", \"size\": [64, 32],\n"
        " \"palette\": \"" ART "art/palette.aseprite\", \"world\": \"w.ldtk\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)bare_world, strlen(bare_world)}));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(orb_file_load(result.file, &assets, &err));
    CHECK_EQ(assets.levels.len, 0);
    bool dot_watched = false;
    for (u32 i = 0; i < result.reads.len; i++)
        if (strcmp(result.reads.elems[i], ".") == 0) dot_watched = true;
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
    CHECK(stack_len > 0 && (usize)stack_len < sizeof stack_project);
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk", (u8_span) {(u8*)stack_project, (usize)stack_len}
    ));

    const char* stack_manifest =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
        " \"palette\": \"" ART "art/palette.aseprite\"}\n";
    CHECK(
        orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)stack_manifest, strlen(stack_manifest)})
    );
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "sub-layers") != nullptr);

    int mismatch_len = snprintf(stack_project, sizeof stack_project, stack_project_template, 16);
    CHECK(mismatch_len > 0 && (usize)mismatch_len < sizeof stack_project);
    CHECK(orb_os_write_file(
        DIR "/levels/world.ldtk", (u8_span) {(u8*)stack_project, (usize)mismatch_len}
    ));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
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
        (u8_span) {(u8*)duplicate_names_project, strlen(duplicate_names_project)}
    ));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
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
        (u8_span) {(u8*)bad_neighbor_project, strlen(bad_neighbor_project)}
    ));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
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
        static u8 big_scratch[48 << 20], big_out[8 << 20];
        arena sample_scratch, sample_out;
        orb_arena_init(&sample_scratch, "scratch", big_scratch, sizeof big_scratch);
        orb_arena_init(&sample_out, "out", big_out, sizeof big_out);
        // the tileset file doubles as the palette, so every color matches
        const char* sample_manifest =
            "{\"id\": \"sample\", \"name\": \"Sample\", \"size\": [320, 180],\n"
            " \"palette\": "
            "\"/usr/share/ldtk/extraFiles/samples/atlas/NuclearBlaze_by_deepnight.aseprite\",\n"
            " \"art\": \"none\", \"sfx\": \"none\", \"music\": \"none\",\n"
            " \"world\": \"/usr/share/ldtk/extraFiles/samples/WorldMap_Free_layout.ldtk\"}\n";
        CHECK(orb_os_write_file(
            DIR "/orb.json", (u8_span) {(u8*)sample_manifest, strlen(sample_manifest)}
        ));

        CHECK(orb_cast_game(&sample_scratch, &sample_out, DIR, &manifest, &result, &err));
        CHECK(orb_file_load(result.file, &assets, &err));
        CHECK_EQ(assets.levels.len, 12);
        CHECK_EQ(assets.tilesets.len, 1);
        CHECK_EQ(assets.tilesets.elems[0].columns, 36);
        CHECK(assets.layers.len >= 24);
        printf(
            "sample: %u levels, %u layers, %u tiles, %u cells\n", assets.levels.len,
            assets.layers.len, assets.tiles.len, assets.cells.len
        );
    } else
        printf("sample: LDtk samples not installed, skipped\n");

    // the art directory is walked recursively; a stem must be unique within a kind
    u8_span player;
    CHECK(orb_os_read_file("tests/fixtures/art/player.aseprite", &scratch, &player));
    CHECK(orb_os_make_dir(DIR "/art"));
    CHECK(orb_os_make_dir(DIR "/art/sub"));
    CHECK(orb_os_write_file(DIR "/art/sub/hero.aseprite", player));
    remove(DIR "/art/hero.aseprite");

    const char* nested = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                         " \"palette\": \"" ART "art/palette.aseprite\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)nested, strlen(nested)}));
    CHECK(orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(orb_file_load(result.file, &assets, &err));
    CHECK_EQ(assets.sprites.len, 2);
    CHECK_EQ(assets.samples.len, 0); // no sfx or music directory is no error, and both are watched
    CHECK_EQ(result.reads.len, 9);   // "levels" and "fonts" too: missing, but both are watched
    CHECK(strcmp(result.reads.elems[2], "levels") == 0);
    CHECK(strcmp(result.reads.elems[3], "fonts") == 0);
    CHECK(strcmp(result.reads.elems[4], "art") == 0);
    CHECK(strcmp(result.reads.elems[5], "art/sub") == 0);
    CHECK(strcmp(result.reads.elems[6], "art/sub/hero.aseprite") == 0);
    CHECK(strcmp(result.reads.elems[7], "sfx") == 0);
    CHECK(strcmp(result.reads.elems[8], "music") == 0);
    CHECK(orb_os_write_file(DIR "/art/hero.aseprite", player));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "art/hero.aseprite") != nullptr);
    CHECK(strstr(err.text, "art/sub/hero.aseprite") != nullptr);
    remove(DIR "/art/hero.aseprite");

    // every key but id, name, and size has a default
    const char* plain = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)plain, strlen(plain)}));
    CHECK(
        !orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err)
    ); // no art/palette.aseprite here
    CHECK(strstr(err.text, "art/palette.aseprite") != nullptr);

    // a leftover asset_headroom is ignored like any key the manifest does not know
    const char* leftover =
        "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32], \"asset_headroom\": 1}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)leftover, strlen(leftover)}));
    CHECK(orb_manifest_load(&scratch, DIR, &manifest, &err));
    CHECK(strcmp(manifest.art, "art") == 0);
    CHECK(strcmp(manifest.sfx, "sfx") == 0);
    CHECK(strcmp(manifest.music, "music") == 0);

    // a song names its tempo in orb.json, since a rendered file carries none; a
    // song without one and a tempo without a song are both refused
    const char* silent = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                         " \"palette\": \"" ART "art/palette.aseprite\",\n"
                         " \"music\": \"" ART "music\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)silent, strlen(silent)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* phantom = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                          " \"palette\": \"" ART "art/palette.aseprite\",\n"
                          " \"songs\": {\"title\": 120}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)phantom, strlen(phantom)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "title") != nullptr);
    CHECK(strstr(err.text, "music") != nullptr);

    // songs is an object of stem to bpm within 0..1000
    const char* bare = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                       " \"palette\": \"" ART "art/palette.aseprite\",\n"
                       " \"songs\": [\"loop\"]}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)bare, strlen(bare)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "songs") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* slow = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                       " \"palette\": \"" ART "art/palette.aseprite\",\n"
                       " \"songs\": {\"loop\": 0}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)slow, strlen(slow)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "loop") != nullptr);
    CHECK(strstr(err.text, "bpm") != nullptr);

    const char* fast = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                       " \"palette\": \"" ART "art/palette.aseprite\",\n"
                       " \"songs\": {\"loop\": 1e999}}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)fast, strlen(fast)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "bpm") != nullptr);

    // a sound must be mono, since pan positions it; only a song may be stereo
    const char* stereo = "{\"id\": \"fixture\", \"name\": \"Fixture\", \"size\": [64, 32],\n"
                         " \"palette\": \"" ART "art/palette.aseprite\",\n"
                         " \"sfx\": \"" ART "music\"}\n";
    CHECK(orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)stereo, strlen(stereo)}));
    CHECK(!orb_cast_game(&scratch, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "loop.wav") != nullptr);
    CHECK(strstr(err.text, "mono") != nullptr);

    CHECK(
        orb_os_write_file(DIR "/orb.json", (u8_span) {(u8*)manifest_json, strlen(manifest_json)})
    );

    CHECK(!orb_manifest_load(&scratch, "build/scratch", &manifest, &err));
    CHECK(strstr(err.text, "orb.json") != nullptr);

    static alignas(16) u8 tiny_mem[4096];
    arena tiny;
    orb_arena_init(&tiny, "cast scratch", tiny_mem, sizeof tiny_mem);
    CHECK(!orb_cast_game(&tiny, &out, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "cast scratch") != nullptr);
    CHECK(strstr(err.text, "exhausted:") != nullptr);
    CHECK(strstr(err.text, "over its 4.0 KB size") != nullptr);

    orb_arena_init(&tiny, "asset half B", tiny_mem, 512);

    CHECK(!orb_cast_game(&scratch, &tiny, DIR, &manifest, &result, &err));
    CHECK(strstr(err.text, "asset half B") != nullptr);

    // an unresolved ref is a cast error naming the level, the entity, and the field
    const char* badref_manifest =
        "{\"id\": \"badref\", \"name\": \"b\", \"size\": [8, 8],\n"
        " \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"art\": \"" ART "art\", \"sfx\": \"" ART "sfx\", \"music\": \"" ART "music\",\n"
        " \"fonts\": \"" ART "fonts\", \"world\": \"world.ldtk\"}\n";
    const char* badref_world =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [], \"defs\": {\"tilesets\": [], \"layers\": [{\"uid\": 1,"
        "  \"identifier\": \"e\", \"__type\": \"Entities\", \"gridSize\": 8,"
        "  \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": false,"
        "  \"tilesetDefUid\": null}],"
        "  \"entities\": [{\"identifier\": \"Door\", \"uid\": 1, \"width\": 8, \"height\": 8,"
        "   \"fieldDefs\": [{\"identifier\": \"to\", \"__type\": \"EntityRef\","
        "    \"defaultOverride\": null}]}]},"
        " \"levels\": [{\"identifier\": \"Hall\", \"iid\": \"h\", \"worldX\": 0, \"worldY\": 0,"
        "  \"worldDepth\": 0, \"pxWid\": 8, \"pxHei\": 8, \"bgRelPath\": null,"
        "  \"externalRelPath\": null, \"__neighbours\": [], \"layerInstances\": ["
        "   {\"__identifier\": \"e\", \"__type\": \"Entities\", \"layerDefUid\": 1,"
        "    \"__cWid\": 1, \"__cHei\": 1, \"__gridSize\": 8, \"__opacity\": 1,"
        "    \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0, \"__tilesetDefUid\": null,"
        "    \"intGridCsv\": [], \"gridTiles\": [], \"autoLayerTiles\": [],"
        "    \"entityInstances\": [{\"__identifier\": \"Door\", \"iid\": \"d\", \"defUid\": 1,"
        "     \"px\": [0, 0], \"width\": 8, \"height\": 8, \"fieldInstances\": ["
        "      {\"__identifier\": \"to\", \"__type\": \"EntityRef\","
        "       \"__value\": {\"entityIid\": \"nowhere\"}}]}]}]}]}";

    CHECK(orb_os_make_dir("build/scratch/badref"));
    CHECK(orb_os_write_file(
        "build/scratch/badref/orb.json",
        (u8_span) {(const u8*)badref_manifest, strlen(badref_manifest)}
    ));
    CHECK(orb_os_write_file(
        "build/scratch/badref/world.ldtk", (u8_span) {(const u8*)badref_world, strlen(badref_world)}
    ));
    arena_clear(&scratch);
    arena_clear(&out);
    CHECK(!orb_cast_game(&scratch, &out, "build/scratch/badref", &manifest, &result, &err));
    CHECK(strstr(err.text, "level Hall: entity Door: field to: ref nowhere"));

    // a WAV loop whose start is not before its end is dropped, not a cast error: the
    // caster logs the file and zeroes both fields, rather than wav_parse refusing it
    u8 bad_loop_wav[128] = {0};

    memcpy(bad_loop_wav, "RIFF", 4);
    bad_loop_wav[4] = 120; // file size - 8
    memcpy(bad_loop_wav + 8, "WAVE", 4);
    memcpy(bad_loop_wav + 12, "fmt ", 4);
    bad_loop_wav[16] = 16;                                                     // fmt chunk size
    bad_loop_wav[20] = 1;                                                      // PCM
    bad_loop_wav[22] = 1;                                                      // mono
    bad_loop_wav[24] = 0x80, bad_loop_wav[25] = 0xBB;                          // rate 48000
    bad_loop_wav[28] = 0x00, bad_loop_wav[29] = 0x77, bad_loop_wav[30] = 0x01; // byte rate
    bad_loop_wav[32] = 2;                                                      // block align
    bad_loop_wav[34] = 16;                                                     // bits
    memcpy(bad_loop_wav + 36, "smpl", 4);
    bad_loop_wav[40] = 60; // smpl chunk size
    bad_loop_wav[72] = 1;  // loop count
    bad_loop_wav[88] = 5;  // loop start
    bad_loop_wav[92] = 4;  // loop end_inclusive: +1 equals the start
    memcpy(bad_loop_wav + 104, "data", 4);
    bad_loop_wav[108] = 16; // 8 samples * 2 bytes

    CHECK(orb_os_make_dir("build/scratch/badloop"));
    CHECK(orb_os_make_dir("build/scratch/badloop/sfx"));
    CHECK(orb_os_write_file(
        "build/scratch/badloop/sfx/bad.wav", (u8_span) {bad_loop_wav, sizeof bad_loop_wav}
    ));

    const char* badloop_manifest =
        "{\"id\": \"badloop\", \"name\": \"b\", \"size\": [8, 8],\n"
        " \"palette\": \"" ART "art/palette.aseprite\",\n"
        " \"art\": \"" ART "art\", \"sfx\": \"sfx\",\n"
        " \"fonts\": \"" ART "fonts\", \"world\": \"" ART "levels/world.ldtk\"}\n";
    CHECK(orb_os_write_file(
        "build/scratch/badloop/orb.json",
        (u8_span) {(const u8*)badloop_manifest, strlen(badloop_manifest)}
    ));
    arena_clear(&scratch);
    arena_clear(&out);
    orb_log_clear();
    CHECK(orb_cast_game(&scratch, &out, "build/scratch/badloop", &manifest, &result, &err));
    CHECK(strstr(orb_log_line(0), "bad.wav") != nullptr);
    CHECK(strstr(orb_log_line(0), "dropped") != nullptr);

    orb_assets badloop_as;
    CHECK(orb_file_load(result.file, &badloop_as, &err));
    CHECK_EQ(badloop_as.samples.len, 1);
    CHECK_EQ(badloop_as.samples.elems[0].loop_start, 0);
    CHECK_EQ(badloop_as.samples.elems[0].loop_end, 0);

    return 0;
}
