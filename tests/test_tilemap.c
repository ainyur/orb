#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#define W 8
#define H 6

static u8 at(const orb_fb* fb, int x, int y) {
    return fb->px[y * W + x];
}

int main(void) {
    static alignas(16) u8 mem[1 << 20];
    orb_arena out;
    orb_arena_init(&out, "test", mem, sizeof mem);

    orb_fb fb;
    orb_fb_init(&fb, &out, (orb_size) {W, H});

    u8 pixels[8] = {1, 1, 2, 3, 1, 1, 4, 5}; // 4x2 sheet: tile 0 at x 0, tile 1 at x 2
    orb_sheet_desc sheets[1] = {{.width = 4, .height = 2, .pixels = 0}};
    orb_tileset_desc tilesets[1] = {{.sheet = 0, .grid = 2, .columns = 2, .count = 2}};
    // layer 0: 3x2 cells, one sub-layer; layer 1: 1x1 with two sub-layers, offset and parallax
    u16 tiles[6 + 2] = {1, 2, 0, 2 | ORB_TILE_FLIP_X, 2 | ORB_TILE_FLIP_Y, 1, 1, 2};
    u8 cells[6] = {0, 7, 0, 0, 0, 9};
    orb_layer_desc layers[2] = {
        {.tiles = 0, .cells = 0, .tileset = 0, .grid = 2, .columns = 3, .rows = 2, .sublayers = 1},
        {.tiles = 6,
         .cells = ORB_NO_INDEX,
         .tileset = 0,
         .grid = 2,
         .columns = 1,
         .rows = 1,
         .offset_x = 1,
         .offset_y = 1,
         .parallax_x = 0.5f,
         .parallax_y = 0,
         .sublayers = 2},
    };
    orb_level_desc levels[1] = {
        {.world_x = 10, .world_y = -4, .width = 6, .height = 4, .first_layer = 0, .layer_count = 2}
    };
    orb_assets assets = {
        .sheets = {sheets, 1},
        .pixels = {pixels, 8},
        .tilesets = {tilesets, 1},
        .levels = {levels, 1},
        .layers = {layers, 2},
        .tiles = {tiles, 8},
        .cells = {cells, 6}
    };

    // camera at the level's origin: the layer fills the top-left 6x4
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {10, -4}, ORB_LEVEL(0), 0);
    CHECK_EQ(at(&fb, 0, 0), 1); // tile 0
    CHECK_EQ(at(&fb, 2, 0), 2); // tile 1 top-left
    CHECK_EQ(at(&fb, 3, 1), 5); // tile 1 bottom-right
    CHECK_EQ(at(&fb, 4, 0), 0); // empty cell
    CHECK_EQ(at(&fb, 0, 2), 3); // tile 1 flipped X: top-left shows 3
    CHECK_EQ(at(&fb, 2, 2), 4); // tile 1 flipped Y: top-left shows 4
    CHECK_EQ(at(&fb, 4, 3), 1); // tile 0
    CHECK_EQ(at(&fb, 6, 0), 0); // outside the layer

    // camera offset by a fraction: floors, so 10.9 draws like 10
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {10.9f, -4}, ORB_LEVEL(0), 0);
    CHECK_EQ(at(&fb, 2, 0), 2);
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {-0.5f, -4}, ORB_LEVEL(0), 0); // floor(-0.5) = -1
    CHECK_EQ(at(&fb, 0, 0), 0); // the level starts at screen x 11, off an 8-wide screen
    CHECK_EQ(at(&fb, 7, 0), 0);

    // camera past the level: clipped, and only visible cells are drawn (no crash on huge offsets)
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(
        &fb, &assets, (orb_vec2f) {13, -3}, ORB_LEVEL(0), 0
    );                          // level origin lands at (-3, -1)
    CHECK_EQ(at(&fb, 0, 0), 5); // tile 1's bottom-right at world (13, -3)
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {1e6f, -1e6f}, ORB_LEVEL(0), 0);

    // layer 1: two sub-layers stack, the second (tile 1) over the first (tile 0); offset (1,1)
    // parallax anchors on the camera's center relative to the level's center (LDtk's rule)
    // origin_x = 11 - floor(0.5 * cam.x + 4.25) for parallax_x 0.5
    layers[1].offset_y = 5; // origin_y = 1 - floor(cam.y) at parallax_y 0
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {0, 0}, ORB_LEVEL(0), 1); // origin (7,1)
    CHECK_EQ(at(&fb, 7, 1), 2);                                          // tile 1 top-left
    CHECK_EQ(at(&fb, 7, 2), 4); // tile 1 bottom-left; column 8 is off screen
    layers[1].offset_y = 1;     // origin_y = -3 - floor(cam.y) at parallax_y 0
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(
        &fb, &assets, (orb_vec2f) {12, 0}, ORB_LEVEL(0), 1
    ); // origin (1,-3): rows off screen
    CHECK_EQ(at(&fb, 1, 0), 0);
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {12, -6}, ORB_LEVEL(0), 1); // origin (1,3)
    CHECK_EQ(at(&fb, 1, 3), 2);                                            // tile 1 over tile 0
    CHECK_EQ(at(&fb, 2, 4), 5);
    // parallax 1 fixes the layer on screen relative to the camera's center: origin_y is 3
    // regardless of the camera
    layers[1].parallax_y = 1;
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {12, 0}, ORB_LEVEL(0), 1); // origin (1,3)
    CHECK_EQ(at(&fb, 1, 3), 2);
    CHECK_EQ(at(&fb, 2, 4), 5);
    CHECK_EQ(at(&fb, 1, 1), 0);
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(
        &fb, &assets, (orb_vec2f) {12, -6}, ORB_LEVEL(0), 1
    ); // origin (1,3): unchanged
    CHECK_EQ(at(&fb, 1, 3), 2);
    CHECK_EQ(at(&fb, 2, 4), 5);

    // the level containing a pixel: the fixture level spans x 10..15, y -4..-1
    CHECK_EQ(orb_tilemap_level_at(&assets, (orb_vec2) {10, -4}), 0);
    CHECK_EQ(orb_tilemap_level_at(&assets, (orb_vec2) {16, -4}), ORB_NO_INDEX);

    // bad handle or layer index: nothing
    orb_fb_clear(&fb, 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {10, -4}, ORB_LEVEL(3), 0);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {10, -4}, ORB_LEVEL(0), 2);
    orb_tilemap_draw(&fb, &assets, (orb_vec2f) {10, -4}, ORB_LEVEL(0), -1);
    CHECK_EQ(at(&fb, 0, 0), 0);

    // cells by world pixel
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {10, -4}), 0);
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {12, -4}), 7);
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {13, -3}), 7);
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {15, -1}), 9);
    CHECK_EQ(
        orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {9, -4}), 0
    ); // left of the level
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {16, -4}), 0); // right of it
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(0), 0, (orb_vec2) {10, -5}), 0); // above
    CHECK_EQ(
        orb_tilemap_cell(&assets, ORB_LEVEL(0), 1, (orb_vec2) {11, -3}), 0
    ); // a layer with no cells
    CHECK_EQ(orb_tilemap_cell(&assets, ORB_LEVEL(1), 0, (orb_vec2) {10, -4}), 0); // bad handle

    // camera: snap, lerp, clamp, center when the bounds are smaller than the screen, shake
    orb_camera cam = {.target = {100, 50}, .lerp = 0};
    cam = orb_camera_update(cam, (orb_size) {W, H});
    CHECK(cam.at.x == 100 - W / 2.0f && cam.at.y == 50 - H / 2.0f);
    orb_vec2f shown = {cam.at.x + cam.shake.x, cam.at.y + cam.shake.y};
    CHECK(shown.x == cam.at.x && shown.y == cam.at.y);

    cam.lerp = 0.5f;
    cam.target = (orb_vec2f) {200, 50};
    cam = orb_camera_update(cam, (orb_size) {W, H});
    CHECK(cam.at.x > 100 - W / 2.0f + 49 && cam.at.x < 100 - W / 2.0f + 51); // halfway

    cam.lerp = 0;
    cam.bounds = (orb_rect) {{0, 0}, {20, 10}};
    cam.target = (orb_vec2f) {-50, -50};
    cam = orb_camera_update(cam, (orb_size) {W, H});
    CHECK(cam.at.x == 0 && cam.at.y == 0);
    cam.target = (orb_vec2f) {500, 500};
    cam = orb_camera_update(cam, (orb_size) {W, H});
    CHECK(cam.at.x == 20 - W && cam.at.y == 10 - H);

    cam.bounds = (orb_rect) {{4, 2}, {4, 2}}; // smaller than the 8x6 screen: centered
    cam = orb_camera_update(cam, (orb_size) {W, H});
    CHECK(cam.at.x == 4 + 2 - W / 2.0f && cam.at.y == 2 + 1 - H / 2.0f);

    cam.bounds = (orb_rect) {};
    cam.shake = (orb_vec2f) {1, -2};
    cam.target = (orb_vec2f) {0, 0};
    cam = orb_camera_update(cam, (orb_size) {W, H});
    shown = (orb_vec2f) {cam.at.x + cam.shake.x, cam.at.y + cam.shake.y};
    CHECK(cam.at.x == -W / 2.0f && shown.x == -W / 2.0f + 1 && shown.y == -H / 2.0f - 2);

    // through the API: names, bounds, neighbors, draw, cells, camera
    u64 level_ids[1] = {orb_asset_id("Cave", "")};
    u64 layer_ids[2] = {orb_asset_id("floor", ""), orb_asset_id("Deco", "")};
    orb_neighbor_desc neighbors[1] = {{.level = 0, .dir = ORB_LEVEL_OVERLAP}};
    u8 pal[256 * 4] = {0}; // orb_api_set_assets always loads one
    assets.level_ids = level_ids;
    assets.layer_ids = layer_ids;
    assets.neighbors = (orb_neighbor_desc_span) {neighbors, 1};
    assets.pal = pal;
    levels[0].neighbor_count = 1;
    layers[1].parallax_y = 0;

    orb_api_boot(&out, (orb_size) {W, H}, &assets);

    static alignas(16) u8 frame_mem[256];

    orb_arena_init(orb_api_frame(), "frame", frame_mem, sizeof frame_mem);

    const orb_api* api = orb_api_table();
    orb_level cave = api->level_find("cave");
    CHECK_EQ(ORB_HANDLE_INDEX(cave), 0);
    CHECK_EQ(api->level_find("attic").v, ORB_NO_LEVEL.v);
    CHECK_EQ(api->layer_find(cave, "FLOOR"), 0);
    CHECK_EQ(api->layer_find(cave, "deco"), 1);
    CHECK_EQ(api->layer_find(cave, "sky"), ORB_NO_INDEX);
    CHECK_EQ(api->layer_find(ORB_NO_LEVEL, "floor"), ORB_NO_INDEX);

    orb_rect bounds = api->level_bounds(cave);
    CHECK_EQ(bounds.at.x, 10);
    CHECK_EQ(bounds.at.y, -4);
    CHECK_EQ(bounds.size.width, 6);
    CHECK_EQ(api->level_bounds(ORB_NO_LEVEL).size.width, 0);

    orb_layer_info info = api->layer_info(cave, 0);
    CHECK_EQ(info.grid, 2);
    CHECK_EQ(info.columns, 3);
    CHECK(info.has_tiles && info.has_cells);
    CHECK(!api->layer_info(cave, 1).has_cells);
    CHECK_EQ(api->layer_info(cave, 5).grid, 0);

    orb_level_neighbor_list links = api->level_neighbors(cave);

    CHECK_EQ(links.len, 1);
    CHECK_EQ(links.elems[0].dir, ORB_LEVEL_OVERLAP);
    CHECK_EQ(links.elems[0].level.v, cave.v);
    CHECK_EQ(api->level_neighbors(ORB_NO_LEVEL).len, 0);

    api->camera_set((orb_vec2f) {10, -4});
    api->clear(0);
    api->layer_draw(cave, 0);
    CHECK_EQ(orb_api_fb()->px[0], 1);
    CHECK_EQ(orb_api_fb()->px[2], 2);
    CHECK_EQ(api->cell_get(cave, 0, (orb_vec2) {12, -4}), 7);

    orb_camera follow = {.target = {13, -1}, .lerp = 0, .bounds = bounds};
    follow = api->camera_update(follow);
    CHECK(
        follow.at.x == 9 && follow.at.y == -5
    ); // 6x4 bounds inside an 8x6 screen: centered on them
    api->clear(0);
    api->layer_draw(cave, 0); // camera_update set the shown origin from its result: (1,1)
    CHECK_EQ(orb_api_fb()->px[1 * W + 1], 1);

    return 0;
}
