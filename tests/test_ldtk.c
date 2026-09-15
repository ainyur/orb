#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

// A one-tileset, two-layer-def project; %s slots take the levels array and extra defs
#define PROJECT_HEAD                                                                               \
    "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": %s,"              \
    " \"worlds\": [%s],"                                                                           \
    " \"defs\": {\"tilesets\": [{\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": \"%s\","      \
    "   \"pxWid\": 32, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"        \
    "   \"__cWid\": 4, \"__cHei\": 2}, {\"uid\": 9, \"identifier\": \"Icons\", \"relPath\": null," \
    "   \"pxWid\": 8, \"pxHei\": 8, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"          \
    "   \"__cWid\": 1, \"__cHei\": 1}],"                                                           \
    "  \"layers\": [{\"uid\": 1, \"identifier\": \"Floor\", \"__type\": \"Tiles\", \"gridSize\": " \
    "8,"                                                                                           \
    "   \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": true, "               \
    "\"tilesetDefUid\": 7},"                                                                       \
    "   {\"uid\": 2, \"identifier\": \"Collision\", \"__type\": \"IntGrid\", \"gridSize\": 8,"     \
    "   \"parallaxFactorX\": %s, \"parallaxFactorY\": %s, \"parallaxScaling\": %s, "               \
    "\"tilesetDefUid\": 7},"                                                                       \
    "   {\"uid\": 3, \"identifier\": \"Things\", \"__type\": \"Entities\", \"gridSize\": 8,"       \
    "   \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": true, "               \
    "\"tilesetDefUid\": null}]},"                                                                  \
    " \"levels\": [%s]}"

#define LEVEL_A                                                                                    \
    "{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": -8, \"worldDepth\": " \
    "1,"                                                                                           \
    " \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": %s, \"externalRelPath\": null,"                  \
    " \"__neighbours\": [{\"levelIid\": \"bbb\", \"dir\": \"e\"}, {\"levelIid\": \"bbb\", "        \
    "\"dir\": \"%s\"}],"                                                                           \
    " \"layerInstances\": ["                                                                       \
    "  {\"__identifier\": \"Things\", \"__type\": \"Entities\", \"layerDefUid\": 3, \"__cWid\": "  \
    "2, \"__cHei\": 1,"                                                                            \
    "   \"__gridSize\": 8, \"__opacity\": 1, \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0,"    \
    "   \"__tilesetDefUid\": null, \"intGridCsv\": [], \"autoLayerTiles\": [], \"gridTiles\": "    \
    "[], \"entityInstances\": [{}]},"                                                              \
    "  {\"__identifier\": \"Collision\", \"__type\": \"IntGrid\", \"layerDefUid\": 2, "            \
    "\"__cWid\": 2, \"__cHei\": 1,"                                                                \
    "   \"__gridSize\": 8, \"__opacity\": %s, \"__pxTotalOffsetX\": 3, \"__pxTotalOffsetY\": -1,"  \
    "   \"__tilesetDefUid\": %s, \"intGridCsv\": [%s], \"gridTiles\": [],"                         \
    "   \"autoLayerTiles\": [{\"px\": [8, 0], \"src\": [0, 0], \"f\": 3, \"t\": %s, \"a\": %s},"   \
    "                       {\"px\": [8, 0], \"src\": [8, 0], \"f\": 0, \"t\": 1, \"a\": 1}], "    \
    "\"entityInstances\": []},"                                                                    \
    "  {\"__identifier\": \"Floor\", \"__type\": \"Tiles\", \"layerDefUid\": 1, \"__cWid\": 2, "   \
    "\"__cHei\": 1,"                                                                               \
    "   \"__gridSize\": 8, \"__opacity\": 1, \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0,"    \
    "   \"__tilesetDefUid\": 7, \"intGridCsv\": [], \"autoLayerTiles\": [],"                       \
    "   \"gridTiles\": [{\"px\": [0, 0], \"src\": [0, 0], \"f\": 1, \"t\": 2, \"a\": 1}], "        \
    "\"entityInstances\": []}]}"

#define LEVEL_B                                                                                    \
    "{\"identifier\": \"Annex\", \"iid\": \"bbb\", \"worldX\": 16, \"worldY\": 0, "                \
    "\"worldDepth\": 0,"                                                                           \
    " \"pxWid\": 8, \"pxHei\": 8, \"bgRelPath\": null, \"externalRelPath\": "                      \
    "\"world/Annex.ldtkl\","                                                                       \
    " \"__neighbours\": [], \"layerInstances\": null}"

static char text[16384];

static orb_span project(
    const char* external,
    const char* worlds,
    const char* tileset_path,
    const char* parallax_x,
    const char* parallax_y,
    const char* scaling,
    const char* levels
) {
    snprintf(
        text, sizeof text, PROJECT_HEAD, external, worlds, tileset_path, parallax_x, parallax_y,
        scaling, levels
    );
    return (orb_span) {(const uint8_t*)text, strlen(text)};
}

static char level_text[8192];

static const char* level_a(
    const char* bg,
    const char* opacity,
    const char* tileset,
    const char* csv,
    const char* tile,
    const char* alpha,
    const char* dir
) {
    snprintf(level_text, sizeof level_text, LEVEL_A, bg, dir, opacity, tileset, csv, tile, alpha);
    return level_text;
}

#define GOOD_A level_a("null", "1", "7", "0, 1", "5", "1", "<")

static bool fails_with(orb_arena* a, orb_span text, const char* needle) {
    orb_ldtk out;
    orb_error err;

    if (orb_ldtk_parse(a, text, "world.ldtk", &out, &err)) return false;

    return strstr(err.text, needle) != nullptr && strncmp(err.text, "world.ldtk", 10) == 0;
}

int main(void) {
    static alignas(16) uint8_t mem[4 << 20];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    orb_ldtk p;
    orb_error err;

    char both[12288];
    snprintf(both, sizeof both, "%s, %s", GOOD_A, LEVEL_B);
    CHECK(orb_ldtk_parse(
        &a, project("true", "", "tiles.aseprite", "0.5", "0.25", "false", both), "world.ldtk", &p,
        &err
    ));

    // the tileset with a path is kept, the internal one dropped
    CHECK_EQ(p.tileset_count, 1);
    CHECK(strcmp(p.tilesets[0].path, "tiles.aseprite") == 0);
    CHECK_EQ(p.tilesets[0].uid, 7);
    CHECK_EQ(p.tilesets[0].columns, 4);
    CHECK_EQ(p.tilesets[0].rows, 2);
    CHECK_EQ(p.tilesets[0].width, 32);
    CHECK_EQ(p.layer_def_count, 3);
    CHECK_EQ(p.level_count, 2);

    // level A: world placement, layers bottom to top with Entities skipped
    const orb_ldtk_level* room = &p.levels[0];
    CHECK(strcmp(room->name, "Room") == 0);
    CHECK_EQ(room->world_y, -8);
    CHECK_EQ(room->depth, 1);
    CHECK_EQ(room->width, 16);
    CHECK(room->external_path == nullptr);
    CHECK_EQ(room->layer_count, 2);
    CHECK(strcmp(room->layers[0].name, "Floor") == 0);
    CHECK(strcmp(room->layers[1].name, "Collision") == 0);
    CHECK_EQ(room->neighbor_count, 2);
    CHECK(strcmp(room->neighbors[0].level_iid, "bbb") == 0);
    CHECK_EQ(room->neighbors[0].dir, ORB_NEIGHBOR_E);
    CHECK_EQ(room->neighbors[1].dir, ORB_NEIGHBOR_LOWER);

    // the tiles layer: one flipped tile, no cells
    const orb_ldtk_layer* floor = &room->layers[0];
    CHECK_EQ(floor->tileset, 0);
    CHECK_EQ(floor->grid, 8);
    CHECK_EQ(floor->columns, 2);
    CHECK(floor->cells == nullptr);
    CHECK_EQ(floor->tile_count, 1);
    CHECK_EQ(floor->tiles[0].cell_x, 0);
    CHECK_EQ(floor->tiles[0].id, 2);
    CHECK_EQ(floor->tiles[0].flip, 1);

    // the intgrid layer: cells, offset, parallax from its definition, two tiles in one cell
    const orb_ldtk_layer* collision = &room->layers[1];
    CHECK_EQ(collision->offset_x, 3);
    CHECK_EQ(collision->offset_y, -1);
    CHECK(collision->parallax_x == 0.5f);
    CHECK(collision->parallax_y == 0.25f);
    CHECK(collision->cells != nullptr);
    CHECK_EQ(collision->cells[0], 0);
    CHECK_EQ(collision->cells[1], 1);
    CHECK_EQ(collision->tile_count, 2);
    CHECK_EQ(collision->tiles[0].cell_x, 1);
    CHECK_EQ(collision->tiles[0].id, 5);
    CHECK_EQ(collision->tiles[0].flip, 3);
    CHECK_EQ(collision->tiles[1].id, 1);

    // level B is external: placement known, layers to come from its own file
    CHECK(strcmp(p.levels[1].external_path, "world/Annex.ldtkl") == 0);
    CHECK_EQ(p.levels[1].layer_count, 0);
    CHECK_EQ(p.levels[1].world_x, 16);

    // an external level file is one level object
    char annex[8192];
    snprintf(annex, sizeof annex, "%s", GOOD_A);
    CHECK(orb_ldtk_parse_level(
        &a, (orb_span) {(const uint8_t*)annex, strlen(annex)}, "world/Annex.ldtkl", &p,
        &p.levels[1], &err
    ));
    CHECK_EQ(p.levels[1].layer_count, 2);
    CHECK(strcmp(p.levels[1].name, "Annex") == 0); // identity comes from the project, not the file
    CHECK_EQ(p.levels[1].world_x, 16);

    // rejections, each naming the file
    CHECK(
        fails_with(&a, project("false", "{}", "tiles.aseprite", "0", "0", "true", GOOD_A), "worlds")
    );
    CHECK(fails_with(&a, project("false", "", "tiles.png", "0", "0", "true", GOOD_A), ".aseprite"));
    CHECK(fails_with(
        &a, project("false", "", "tiles.png", "0", "0", "true", GOOD_A), "layer Collision"
    )); // a non-.aseprite relPath is only an error for the layer that actually uses it

    // a tileset definition no layer uses is skipped silently, non-.aseprite path included
    const char* unused_png_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [],"
        " \"defs\": {\"tilesets\": ["
        "   {\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": \"tiles.aseprite\","
        "    \"pxWid\": 32, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "    \"__cWid\": 4, \"__cHei\": 2},"
        "   {\"uid\": 8, \"identifier\": \"Unused\", \"relPath\": \"unused.png\","
        "    \"pxWid\": 16, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "    \"__cWid\": 2, \"__cHei\": 2}],"
        "  \"layers\": ["
        "   {\"uid\": 1, \"identifier\": \"Floor\", \"__type\": \"Tiles\", \"gridSize\": 8,"
        "    \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": false,"
        "    \"tilesetDefUid\": 7},"
        "   {\"uid\": 2, \"identifier\": \"Collision\", \"__type\": \"IntGrid\", \"gridSize\": 8,"
        "    \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": false,"
        "    \"tilesetDefUid\": 7}]},"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": 0,"
        "   \"worldDepth\": 0, \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": null,"
        "   \"externalRelPath\": null, \"__neighbours\": [],"
        "   \"layerInstances\": ["
        "    {\"__identifier\": \"Collision\", \"__type\": \"IntGrid\", \"layerDefUid\": 2,"
        "     \"__cWid\": 2, \"__cHei\": 1, \"__gridSize\": 8, \"__opacity\": 1,"
        "     \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0, \"__tilesetDefUid\": 7,"
        "     \"intGridCsv\": [0, 0], \"gridTiles\": [], \"autoLayerTiles\": [],"
        "     \"entityInstances\": []},"
        "    {\"__identifier\": \"Floor\", \"__type\": \"Tiles\", \"layerDefUid\": 1,"
        "     \"__cWid\": 2, \"__cHei\": 1, \"__gridSize\": 8, \"__opacity\": 1,"
        "     \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0, \"__tilesetDefUid\": 7,"
        "     \"intGridCsv\": [], \"gridTiles\": [], \"autoLayerTiles\": [],"
        "     \"entityInstances\": []}]}]}\n";
    orb_ldtk unused_p;
    CHECK(orb_ldtk_parse(
        &a, (orb_span) {(const uint8_t*)unused_png_project, strlen(unused_png_project)},
        "world.ldtk", &unused_p, &err
    ));
    CHECK_EQ(unused_p.tileset_count, 1);

    // a tile whose px lands outside its layer: cell (8, 0) on a 2-column layer
    const char* outside_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [],"
        " \"defs\": {\"tilesets\": ["
        "   {\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": \"tiles.aseprite\","
        "    \"pxWid\": 32, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "    \"__cWid\": 4, \"__cHei\": 2}],"
        "  \"layers\": [{\"uid\": 1, \"identifier\": \"Floor\", \"__type\": \"Tiles\","
        "    \"gridSize\": 8, \"parallaxFactorX\": 0, \"parallaxFactorY\": 0,"
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}]},"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": 0,"
        "   \"worldDepth\": 0, \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": null,"
        "   \"externalRelPath\": null, \"__neighbours\": [],"
        "   \"layerInstances\": [{\"__identifier\": \"Floor\", \"__type\": \"Tiles\","
        "     \"layerDefUid\": 1, \"__cWid\": 2, \"__cHei\": 1, \"__gridSize\": 8,"
        "     \"__opacity\": 1, \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0,"
        "     \"__tilesetDefUid\": 7, \"intGridCsv\": [],"
        "     \"gridTiles\": [{\"px\": [64, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"a\": 1}],"
        "     \"autoLayerTiles\": [], \"entityInstances\": []}]}]}\n";
    CHECK(fails_with(
        &a, (orb_span) {(const uint8_t*)outside_project, strlen(outside_project)}, "outside"
    ));

    // __gridSize 0 with a tile present: would divide by zero in ldtk_tiles if not caught first
    const char* zero_grid_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [],"
        " \"defs\": {\"tilesets\": ["
        "   {\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": \"tiles.aseprite\","
        "    \"pxWid\": 32, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "    \"__cWid\": 4, \"__cHei\": 2}],"
        "  \"layers\": [{\"uid\": 1, \"identifier\": \"Floor\", \"__type\": \"Tiles\","
        "    \"gridSize\": 8, \"parallaxFactorX\": 0, \"parallaxFactorY\": 0,"
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}]},"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": 0,"
        "   \"worldDepth\": 0, \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": null,"
        "   \"externalRelPath\": null, \"__neighbours\": [],"
        "   \"layerInstances\": [{\"__identifier\": \"Floor\", \"__type\": \"Tiles\","
        "     \"layerDefUid\": 1, \"__cWid\": 2, \"__cHei\": 1, \"__gridSize\": 0,"
        "     \"__opacity\": 1, \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0,"
        "     \"__tilesetDefUid\": 7, \"intGridCsv\": [],"
        "     \"gridTiles\": [{\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"a\": 1}],"
        "     \"autoLayerTiles\": [], \"entityInstances\": []}]}]}\n";
    CHECK(fails_with(
        &a, (orb_span) {(const uint8_t*)zero_grid_project, strlen(zero_grid_project)}, "positive"
    ));

    // __cWid 0 with a tile present: an empty layer
    const char* zero_cells_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [],"
        " \"defs\": {\"tilesets\": ["
        "   {\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": \"tiles.aseprite\","
        "    \"pxWid\": 32, \"pxHei\": 16, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "    \"__cWid\": 4, \"__cHei\": 2}],"
        "  \"layers\": [{\"uid\": 1, \"identifier\": \"Floor\", \"__type\": \"Tiles\","
        "    \"gridSize\": 8, \"parallaxFactorX\": 0, \"parallaxFactorY\": 0,"
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}]},"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": 0,"
        "   \"worldDepth\": 0, \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": null,"
        "   \"externalRelPath\": null, \"__neighbours\": [],"
        "   \"layerInstances\": [{\"__identifier\": \"Floor\", \"__type\": \"Tiles\","
        "     \"layerDefUid\": 1, \"__cWid\": 0, \"__cHei\": 1, \"__gridSize\": 8,"
        "     \"__opacity\": 1, \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0,"
        "     \"__tilesetDefUid\": 7, \"intGridCsv\": [],"
        "     \"gridTiles\": [{\"px\": [0, 0], \"src\": [0, 0], \"f\": 0, \"t\": 0, \"a\": 1}],"
        "     \"autoLayerTiles\": [], \"entityInstances\": []}]}]}\n";
    CHECK(fails_with(
        &a, (orb_span) {(const uint8_t*)zero_cells_project, strlen(zero_cells_project)}, "empty"
    ));

    CHECK(fails_with(
        &a, project("false", "", "tiles.aseprite", "0.5", "0", "true", GOOD_A), "parallaxScaling"
    ));
    CHECK(orb_ldtk_parse(
        &a, project("false", "", "tiles.aseprite", "0", "0", "true", GOOD_A), "world.ldtk", &p,
        &err
    )); // both factors zero, scaling true: fine
    CHECK(fails_with(
        &a, project("false", "", "tiles.aseprite", "0", "0.25", "true", GOOD_A), "parallaxScaling"
    )); // the other axis alone triggers it
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("\"bg.png\"", "1", "7", "0, 1", "5", "1", "<")
        ),
        "background"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "0.5", "7", "0, 1", "5", "1", "<")
        ),
        "opacity"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0, 1", "5", "0.5", "<")
        ),
        "alpha"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "9", "0, 1", "5", "1", "<")
        ),
        "tileset"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0, 300", "5", "1", "<")
        ),
        "255"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0, 1", "20000", "1", "<")
        ),
        "tile id"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0, 1", "-1", "1", "<")
        ),
        "does not fit"
    ));
    CHECK(fails_with(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0", "5", "1", "<")
        ),
        "intGridCsv"
    ));

    // the error names the level and layer
    orb_error e;
    CHECK(!orb_ldtk_parse(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "0.5", "7", "0, 1", "5", "1", "<")
        ),
        "world.ldtk", &p, &e
    ));
    CHECK(strstr(e.text, "level Room") != nullptr);
    CHECK(strstr(e.text, "layer Collision") != nullptr);

    // an unknown neighbour direction is rejected, naming the level
    CHECK(!orb_ldtk_parse(
        &a,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", "1", "7", "0, 1", "5", "1", "x")
        ),
        "world.ldtk", &p, &e
    ));
    CHECK(strstr(e.text, "dir") != nullptr);
    CHECK(strstr(e.text, "level Room") != nullptr);

    // a version outside 1.x and malformed JSON
    static const char v2[] = "{\"jsonVersion\": \"2.0.0\"}";
    CHECK(
        !orb_ldtk_parse(&a, (orb_span) {(const uint8_t*)v2, sizeof v2 - 1}, "world.ldtk", &p, &e)
    );
    CHECK(strstr(e.text, "jsonVersion") != nullptr);
    static const char broken[] = "{\"jsonVersion\": ";
    CHECK(!orb_ldtk_parse(
        &a, (orb_span) {(const uint8_t*)broken, sizeof broken - 1}, "world.ldtk", &p, &e
    ));

    // the generated fixture parses, external levels included
    orb_span project_text, room_text;
    CHECK(orb_os_read_file("tests/fixtures/levels/world.ldtk", &a, &project_text));
    CHECK(orb_ldtk_parse(&a, project_text, "world.ldtk", &p, &err));
    CHECK_EQ(p.level_count, 2);
    CHECK(strcmp(p.levels[0].external_path, "world/Room.ldtkl") == 0);
    CHECK(orb_os_read_file("tests/fixtures/levels/world/Room.ldtkl", &a, &room_text));
    CHECK(orb_ldtk_parse_level(&a, room_text, "world/Room.ldtkl", &p, &p.levels[0], &err));
    CHECK_EQ(p.levels[0].layer_count, 3);
    CHECK(strcmp(p.levels[0].layers[0].name, "floor") == 0);
    CHECK(strcmp(p.levels[0].layers[2].name, "deco") == 0);
    CHECK_EQ(p.levels[0].layers[1].tile_count, 23); // 20 border tiles + 2 seconds + 1 third
    CHECK_EQ(p.levels[0].layers[2].offset_x, 2);

    return 0;
}
