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
    "\"tilesetDefUid\": null}],"                                                                   \
    "  \"entities\": [{\"identifier\": \"Crate\", \"uid\": 20, \"width\": 8, \"height\": 8,"       \
    "   \"fieldDefs\": ["                                                                          \
    "    {\"identifier\": \"hp\", \"__type\": \"Int\", \"defaultOverride\": {\"id\": \"V_Int\", "  \
    "\"params\": [10]}},"                                                                          \
    "    {\"identifier\": \"locked\", \"__type\": \"Bool\", \"defaultOverride\": null},"           \
    "    {\"identifier\": \"loot\", \"__type\": \"Array<Int>\", \"defaultOverride\": null},"       \
    "    {\"identifier\": \"label\", \"__type\": \"String\", \"defaultOverride\": {\"id\": "       \
    "\"V_String\", \"params\": [\"box\"]}},"                                                       \
    "    {\"identifier\": \"kind\", \"__type\": \"LocalEnum.Kind\", \"defaultOverride\": null},"   \
    "    {\"identifier\": \"exit\", \"__type\": \"Point\", \"defaultOverride\": null},"            \
    "    {\"identifier\": \"link\", \"__type\": \"EntityRef\", \"defaultOverride\": null},"        \
    "    {\"identifier\": \"tint\", \"__type\": \"Color\", \"defaultOverride\": {\"id\": "         \
    "\"V_Int\", \"params\": [255]}}]},"                                                            \
    "   {\"identifier\": \"Marker\", \"uid\": 30, \"width\": 4, \"height\": 4, \"fieldDefs\": "    \
    "[]}]},"                                                                                       \
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
    "[], \"entityInstances\": [%s]},"                                                              \
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
    const char* background,
    const char* entities,
    const char* opacity,
    const char* tileset,
    const char* csv,
    const char* tile,
    const char* alpha,
    const char* dir
) {
    snprintf(
        level_text, sizeof level_text, LEVEL_A, background, dir, entities, opacity, tileset, csv,
        tile, alpha
    );
    return level_text;
}

#define INSTANCES                                                                                  \
    "{\"__identifier\": \"Crate\", \"iid\": \"crate-a\", \"defUid\": 20, \"px\": [8, 0],"          \
    " \"width\": 8, \"height\": 8, \"fieldInstances\": ["                                          \
    "  {\"__identifier\": \"hp\", \"__type\": \"Int\", \"__value\": 3},"                           \
    "  {\"__identifier\": \"locked\", \"__type\": \"Bool\", \"__value\": true},"                   \
    "  {\"__identifier\": \"loot\", \"__type\": \"Array<Int>\", \"__value\": [1, 2, 3]},"          \
    "  {\"__identifier\": \"label\", \"__type\": \"String\", \"__value\": null},"                  \
    "  {\"__identifier\": \"kind\", \"__type\": \"LocalEnum.Kind\", \"__value\": \"Wood\"},"       \
    "  {\"__identifier\": \"exit\", \"__type\": \"Point\", \"__value\": {\"cx\": 1, \"cy\": 0}},"  \
    "  {\"__identifier\": \"link\", \"__type\": \"EntityRef\", \"__value\": {\"entityIid\": "      \
    "\"marker-a\"}},"                                                                              \
    "  {\"__identifier\": \"tint\", \"__type\": \"Color\", \"__value\": \"#ff0000\"}]},"           \
    "{\"__identifier\": \"Marker\", \"iid\": \"marker-a\", \"defUid\": 30, \"px\": [0, 0],"        \
    " \"width\": 4, \"height\": 4, \"fieldInstances\": []}"

#define GOOD_A level_a("null", INSTANCES, "1", "7", "0, 1", "5", "1", "<")

static bool fails_with(orb_arena* arena, orb_span json, const char* needle) {
    orb_ldtk parsed;
    orb_error err;

    if (orb_ldtk_parse(arena, json, "world.ldtk", &parsed, &err)) return false;

    return strstr(err.text, needle) != nullptr && strncmp(err.text, "world.ldtk", 10) == 0;
}

int main(void) {
    static alignas(16) uint8_t mem[4 << 20];
    orb_arena arena;
    orb_arena_init(&arena, "test", mem, sizeof mem);
    orb_ldtk world;
    orb_error err;

    char both[12288];
    snprintf(both, sizeof both, "%s, %s", GOOD_A, LEVEL_B);
    CHECK(orb_ldtk_parse(
        &arena, project("true", "", "tiles.aseprite", "0.5", "0.25", "false", both), "world.ldtk",
        &world, &err
    ));

    // the tileset with a path is kept, the internal one dropped
    CHECK_EQ(world.tileset_count, 1);
    CHECK(strcmp(world.tilesets[0].path, "tiles.aseprite") == 0);
    CHECK_EQ(world.tilesets[0].uid, 7);
    CHECK_EQ(world.tilesets[0].columns, 4);
    CHECK_EQ(world.tilesets[0].rows, 2);
    CHECK_EQ(world.tilesets[0].width, 32);
    CHECK_EQ(world.layer_def_count, 3);
    CHECK_EQ(world.level_count, 2);

    // level A: world placement, layers bottom to top with Entities skipped
    const orb_ldtk_level* room = &world.levels[0];
    CHECK(strcmp(room->name, "Room") == 0);
    CHECK_EQ(room->world_y, -8);
    CHECK_EQ(room->width, 16);
    CHECK(room->external_path == nullptr);
    CHECK_EQ(room->layer_count, 2);
    CHECK(strcmp(room->layers[0].name, "Floor") == 0);
    CHECK(strcmp(room->layers[1].name, "Collision") == 0);
    CHECK_EQ(room->neighbor_count, 2);
    CHECK(strcmp(room->neighbors[0].level_iid, "bbb") == 0);
    CHECK_EQ(room->neighbors[0].dir, ORB_LEVEL_E);
    CHECK_EQ(room->neighbors[1].dir, ORB_LEVEL_LOWER);

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
    CHECK(strcmp(world.levels[1].external_path, "world/Annex.ldtkl") == 0);
    CHECK_EQ(world.levels[1].layer_count, 0);
    CHECK_EQ(world.levels[1].world_x, 16);

    // entity definitions: two types; the crate's defaults are the two non-null ones, the color
    // definition is skipped, and every declared name is kept for the instance check
    CHECK_EQ(world.entity_def_count, 2);
    const orb_ldtk_entity_def* crate = &world.entity_defs[0];
    CHECK(strcmp(crate->name, "Crate") == 0);
    CHECK_EQ(crate->uid, 20);
    CHECK_EQ(crate->width, 8);
    CHECK_EQ(crate->field_count, 2);
    CHECK(strcmp(crate->fields[0].name, "hp") == 0);
    CHECK_EQ(crate->fields[0].kind, ORB_FIELD_INT);
    CHECK_EQ(crate->fields[0].count, 1);
    CHECK_EQ(crate->fields[0].values[0].integer, 10);
    CHECK_EQ(crate->fields[1].kind, ORB_FIELD_STRING);
    CHECK(strcmp(crate->fields[1].values[0].string, "box") == 0);
    CHECK_EQ(crate->field_name_count, 8);
    CHECK_EQ(world.entity_defs[1].field_count, 0);

    // instances: world position is the level's plus px; the point is the layer's grid cell in
    // world pixels; the null label and the color are not read; the ref keeps its iid
    CHECK_EQ(room->instance_count, 2);
    const orb_ldtk_instance* a_crate = &room->instances[0];
    CHECK(strcmp(a_crate->iid, "crate-a") == 0);
    CHECK_EQ(a_crate->def, 0);
    CHECK_EQ(a_crate->x, 8);
    CHECK_EQ(a_crate->y, -8);
    CHECK_EQ(a_crate->width, 8);
    CHECK_EQ(a_crate->field_count, 6);
    CHECK_EQ(a_crate->fields[0].values[0].integer, 3);
    CHECK_EQ(a_crate->fields[1].kind, ORB_FIELD_BOOL);
    CHECK(a_crate->fields[1].values[0].boolean);
    CHECK_EQ(a_crate->fields[2].count, 3);
    CHECK_EQ(a_crate->fields[2].values[2].integer, 3);
    CHECK(strcmp(a_crate->fields[3].name, "kind") == 0);
    CHECK(strcmp(a_crate->fields[3].values[0].string, "Wood") == 0);
    CHECK_EQ(a_crate->fields[4].kind, ORB_FIELD_POINT);
    CHECK_EQ(a_crate->fields[4].values[0].point.x, 8);
    CHECK_EQ(a_crate->fields[4].values[0].point.y, -8);
    CHECK_EQ(a_crate->fields[5].kind, ORB_FIELD_REF);
    CHECK(strcmp(a_crate->fields[5].values[0].string, "marker-a") == 0);
    CHECK_EQ(room->instances[1].def, 1);
    CHECK_EQ(room->instances[1].field_count, 0);

    // an external level file is one level object
    char annex[8192];
    snprintf(annex, sizeof annex, "%s", GOOD_A);
    CHECK(orb_ldtk_parse_level(
        &arena, (orb_span) {(const uint8_t*)annex, strlen(annex)}, "world/Annex.ldtkl", &world,
        &world.levels[1], &err
    ));
    CHECK_EQ(world.levels[1].layer_count, 2);
    CHECK(
        strcmp(world.levels[1].name, "Annex") == 0
    ); // identity comes from the project, not the file
    CHECK_EQ(world.levels[1].world_x, 16);

    // entity rejections name the level, the entity, and the field
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a(
                "null",
                "{\"__identifier\": \"Ghost\", \"iid\": \"g\", \"defUid\": 99, \"px\": [0, 0],"
                " \"width\": 8, \"height\": 8, \"fieldInstances\": []}",
                "1", "7", "0, 1", "5", "1", "<"
            )
        ),
        "level Room: layer Things: entity Ghost: defUid 99 has no definition"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a(
                "null",
                "{\"__identifier\": \"Crate\", \"iid\": \"c\", \"defUid\": 20, \"px\": [0, 0],"
                " \"width\": 8, \"height\": 8, \"fieldInstances\": [{\"__identifier\": \"loot\","
                " \"__type\": \"Array<Int>\", \"__value\": [1, null]}]}",
                "1", "7", "0, 1", "5", "1", "<"
            )
        ),
        "entity Crate: field loot: null element"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a(
                "null",
                "{\"__identifier\": \"Crate\", \"iid\": \"c\", \"defUid\": 20, \"px\": [0, 0],"
                " \"width\": 8, \"height\": 8, \"fieldInstances\": [{\"__identifier\": \"hp\","
                " \"__type\": \"Int\", \"__value\": \"ten\"}]}",
                "1", "7", "0, 1", "5", "1", "<"
            )
        ),
        "field hp: is not a number"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a(
                "null",
                "{\"__identifier\": \"Crate\", \"iid\": \"c\", \"defUid\": 20, \"px\": [0, 0],"
                " \"width\": 8, \"height\": 8, \"fieldInstances\": [{\"__identifier\": \"hp\","
                " \"__type\": \"Array<Int>\", \"__value\": 4}]}",
                "1", "7", "0, 1", "5", "1", "<"
            )
        ),
        "field hp: is not an array"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a(
                "null",
                "{\"__identifier\": \"Crate\", \"iid\": \"c\", \"defUid\": 20, \"px\": [0, 0],"
                " \"width\": 8, \"height\": 8, \"fieldInstances\": [{\"__identifier\": \"hp\","
                " \"__type\": \"Matrix\", \"__value\": 4}]}",
                "1", "7", "0, 1", "5", "1", "<"
            )
        ),
        "field hp: field type Matrix is not supported"
    ));

    const char* bad_default_project =
        "{\"jsonVersion\": \"1.5.3\", \"worldLayout\": \"Free\", \"externalLevels\": false,"
        " \"worlds\": [], \"defs\": {\"tilesets\": [], \"layers\": [],"
        "  \"entities\": [{\"identifier\": \"Crate\", \"uid\": 1, \"width\": 8, \"height\": 8,"
        "   \"fieldDefs\": [{\"identifier\": \"label\", \"__type\": \"String\","
        "    \"defaultOverride\": {\"id\": \"V_Int\", \"params\": [4]}}]}]},"
        " \"levels\": []}";
    CHECK(fails_with(
        &arena, (orb_span) {(const uint8_t*)bad_default_project, strlen(bad_default_project)},
        "entity Crate: field label: is not a string"
    ));

    // rejections, each naming the file
    CHECK(fails_with(
        &arena, project("false", "{}", "tiles.aseprite", "0", "0", "true", GOOD_A), "worlds"
    ));
    CHECK(
        fails_with(&arena, project("false", "", "tiles.png", "0", "0", "true", GOOD_A), ".aseprite")
    );
    CHECK(fails_with(
        &arena, project("false", "", "tiles.png", "0", "0", "true", GOOD_A), "layer Collision"
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
        "    \"tilesetDefUid\": 7}], \"entities\": []},"
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
    orb_ldtk unused_ldtk;
    CHECK(orb_ldtk_parse(
        &arena, (orb_span) {(const uint8_t*)unused_png_project, strlen(unused_png_project)},
        "world.ldtk", &unused_ldtk, &err
    ));
    CHECK_EQ(unused_ldtk.tileset_count, 1);

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
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}], \"entities\": []},"
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
        &arena, (orb_span) {(const uint8_t*)outside_project, strlen(outside_project)}, "outside"
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
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}], \"entities\": []},"
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
        &arena, (orb_span) {(const uint8_t*)zero_grid_project, strlen(zero_grid_project)},
        "positive"
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
        "    \"parallaxScaling\": false, \"tilesetDefUid\": 7}], \"entities\": []},"
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
        &arena, (orb_span) {(const uint8_t*)zero_cells_project, strlen(zero_cells_project)}, "empty"
    ));

    // __cWid/__cHei of 65536 each overflows a signed 32-bit int product; refused before the
    // multiply
    const char* huge_grid_project =
        "{\"jsonVersion\": \"1.5.3\", \"worlds\": [],"
        " \"defs\": {\"tilesets\": [],"
        "  \"layers\": [{\"uid\": 2, \"identifier\": \"Collision\", \"__type\": \"IntGrid\","
        "   \"parallaxFactorX\": 0, \"parallaxFactorY\": 0, \"parallaxScaling\": false,"
        "   \"tilesetDefUid\": null}],"
        "  \"entities\": []},"
        " \"levels\": [{\"identifier\": \"Room\", \"iid\": \"aaa\", \"worldX\": 0, \"worldY\": 0,"
        "  \"worldDepth\": 0, \"pxWid\": 16, \"pxHei\": 8, \"bgRelPath\": null,"
        "  \"externalRelPath\": null, \"__neighbours\": [],"
        "  \"layerInstances\": ["
        "   {\"__identifier\": \"Collision\", \"__type\": \"IntGrid\", \"layerDefUid\": 2,"
        "    \"__cWid\": 65536, \"__cHei\": 65536, \"__gridSize\": 8, \"__opacity\": 1,"
        "    \"__pxTotalOffsetX\": 0, \"__pxTotalOffsetY\": 0, \"__tilesetDefUid\": null,"
        "    \"intGridCsv\": [], \"gridTiles\": [], \"autoLayerTiles\": [],"
        "    \"entityInstances\": []}]}]}";
    CHECK(fails_with(
        &arena, (orb_span) {(const uint8_t*)huge_grid_project, strlen(huge_grid_project)},
        "larger than 65535x65535"
    ));

    // the same bound on a tileset definition's own grid, ahead of the slot count product
    const char* huge_tileset_project =
        "{\"jsonVersion\": \"1.5.3\", \"worlds\": [],"
        " \"defs\": {\"tilesets\": [{\"uid\": 7, \"identifier\": \"Tiles\", \"relPath\": "
        "\"t.aseprite\","
        "   \"pxWid\": 8, \"pxHei\": 8, \"tileGridSize\": 8, \"spacing\": 0, \"padding\": 0,"
        "   \"__cWid\": 65536, \"__cHei\": 1}],"
        "  \"layers\": [], \"entities\": []},"
        " \"levels\": []}";
    CHECK(fails_with(
        &arena, (orb_span) {(const uint8_t*)huge_tileset_project, strlen(huge_tileset_project)},
        "larger than 65535x65535"
    ));

    CHECK(fails_with(
        &arena, project("false", "", "tiles.aseprite", "0.5", "0", "true", GOOD_A),
        "parallaxScaling"
    ));
    CHECK(orb_ldtk_parse(
        &arena, project("false", "", "tiles.aseprite", "0", "0", "true", GOOD_A), "world.ldtk",
        &world,
        &err
    )); // both factors zero, scaling true: fine
    CHECK(fails_with(
        &arena, project("false", "", "tiles.aseprite", "0", "0.25", "true", GOOD_A),
        "parallaxScaling"
    )); // the other axis alone triggers it
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("\"bg.png\"", INSTANCES, "1", "7", "0, 1", "5", "1", "<")
        ),
        "background"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "0.5", "7", "0, 1", "5", "1", "<")
        ),
        "opacity"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0, 1", "5", "0.5", "<")
        ),
        "alpha"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "9", "0, 1", "5", "1", "<")
        ),
        "tileset"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0, 300", "5", "1", "<")
        ),
        "255"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0, 1", "20000", "1", "<")
        ),
        "tile id"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0, 1", "-1", "1", "<")
        ),
        "does not fit"
    ));
    CHECK(fails_with(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0", "5", "1", "<")
        ),
        "intGridCsv"
    ));

    // the error names the level and layer
    orb_error refusal;
    CHECK(!orb_ldtk_parse(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "0.5", "7", "0, 1", "5", "1", "<")
        ),
        "world.ldtk", &world, &refusal
    ));
    CHECK(strstr(refusal.text, "level Room") != nullptr);
    CHECK(strstr(refusal.text, "layer Collision") != nullptr);

    // an unknown neighbour direction is rejected, naming the level
    CHECK(!orb_ldtk_parse(
        &arena,
        project(
            "false", "", "tiles.aseprite", "0", "0", "true",
            level_a("null", INSTANCES, "1", "7", "0, 1", "5", "1", "x")
        ),
        "world.ldtk", &world, &refusal
    ));
    CHECK(strstr(refusal.text, "dir") != nullptr);
    CHECK(strstr(refusal.text, "level Room") != nullptr);

    // a version outside 1.x and malformed JSON
    static const char v2[] = "{\"jsonVersion\": \"2.0.0\"}";
    CHECK(!orb_ldtk_parse(
        &arena, (orb_span) {(const uint8_t*)v2, sizeof v2 - 1}, "world.ldtk", &world, &refusal
    ));
    CHECK(strstr(refusal.text, "jsonVersion") != nullptr);
    static const char broken[] = "{\"jsonVersion\": ";
    CHECK(!orb_ldtk_parse(
        &arena, (orb_span) {(const uint8_t*)broken, sizeof broken - 1}, "world.ldtk", &world,
        &refusal
    ));

    // the generated fixture parses, external levels included
    orb_span project_text, room_text;
    CHECK(orb_os_read_file("tests/fixtures/levels/world.ldtk", &arena, &project_text));
    CHECK(orb_ldtk_parse(&arena, project_text, "world.ldtk", &world, &err));
    CHECK_EQ(world.level_count, 2);
    CHECK(strcmp(world.levels[0].external_path, "world/Room.ldtkl") == 0);
    CHECK(orb_os_read_file("tests/fixtures/levels/world/Room.ldtkl", &arena, &room_text));
    CHECK(
        orb_ldtk_parse_level(&arena, room_text, "world/Room.ldtkl", &world, &world.levels[0], &err)
    );
    CHECK_EQ(world.levels[0].layer_count, 3);
    CHECK(strcmp(world.levels[0].layers[0].name, "floor") == 0);
    CHECK(strcmp(world.levels[0].layers[2].name, "deco") == 0);
    CHECK_EQ(world.levels[0].layers[1].tile_count, 23); // 20 border tiles + 2 seconds + 1 third
    CHECK_EQ(world.levels[0].layers[2].offset_x, 2);

    return 0;
}
