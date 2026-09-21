#include "tilemap.h"
#include "../core/macros.h"

// The level desc, or nullptr when the handle is stale.
static const orb_level_desc* tilemap_level(const orb_assets* assets, orb_level level) {
    uint32_t index = orb_asset_index_of(assets, level);

    return index == ORB_NO_INDEX ? nullptr : &assets->levels[index];
}

// The layer desc, or nullptr when the handle is stale or the index is not the level's.
static const orb_layer_desc* tilemap_layer(const orb_assets* assets, orb_level level, int layer) {
    const orb_level_desc* d = tilemap_level(assets, level);

    if (!d || layer < 0 || layer >= d->layer_count) return nullptr;

    return &assets->layers[d->first_layer + layer];
}

int orb_tilemap_cell(const orb_assets* assets, orb_level level, int layer, orb_vec2 at) {
    const orb_layer_desc* l = tilemap_layer(assets, level, layer);

    if (!l || l->cells == ORB_NO_INDEX) return 0;

    const orb_level_desc* d = &assets->levels[ORB_HANDLE_INDEX(level)];
    int x = at.x - d->world_x - l->offset_x, y = at.y - d->world_y - l->offset_y;

    if (x < 0 || y < 0) return 0;

    int cell_x = x / l->grid, cell_y = y / l->grid;

    if (cell_x >= l->columns || cell_y >= l->rows) return 0;

    return assets->cells[l->cells + cell_y * l->columns + cell_x];
}

uint32_t orb_tilemap_level_at(const orb_assets* assets, orb_vec2 at) {
    for (uint32_t i = 0; i < assets->level_count; i++) {
        const orb_level_desc* d = &assets->levels[i];

        if (at.x >= d->world_x && at.x < d->world_x + d->width && at.y >= d->world_y &&
            at.y < d->world_y + d->height)
            return i;
    }

    return ORB_NO_INDEX;
}

int orb_tilemap_layer_find(const orb_assets* assets, orb_level level, const char* name) {
    const orb_level_desc* d = tilemap_level(assets, level);
    uint64_t id = orb_asset_id(name, "");

    for (int i = 0; d && i < d->layer_count; i++)
        if (assets->layer_ids[d->first_layer + i] == id) return i;

    return (int)ORB_NO_INDEX;
}

orb_layer_info orb_tilemap_layer_info(const orb_assets* assets, orb_level level, int layer) {
    const orb_layer_desc* l = tilemap_layer(assets, level, layer);

    if (!l) return (orb_layer_info) {};

    return (orb_layer_info) {
        .grid = l->grid,
        .columns = l->columns,
        .rows = l->rows,
        .has_tiles = l->sublayers > 0,
        .has_cells = l->cells != ORB_NO_INDEX
    };
}

orb_rect orb_tilemap_level_bounds(const orb_assets* assets, orb_level level) {
    const orb_level_desc* d = tilemap_level(assets, level);

    if (!d) return (orb_rect) {};

    return (orb_rect) {{d->world_x, d->world_y}, {d->width, d->height}};
}

int orb_tilemap_level_neighbors(
    const orb_assets* assets,
    orb_level level,
    orb_level_neighbor* out,
    int max
) {
    const orb_level_desc* d = tilemap_level(assets, level);
    int n = 0;

    for (int i = 0; d && i < d->neighbor_count && n < max; i++) {
        const orb_neighbor_desc* link = &assets->neighbors[d->first_neighbor + i];
        uint32_t gen = assets->level_gens[link->level];

        out[n++] = (orb_level_neighbor) {ORB_LEVEL(link->level | gen << 24), link->dir};
    }

    return n;
}

void orb_tilemap_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_vec2f cam,
    orb_level level,
    int layer
) {
    const orb_layer_desc* l = tilemap_layer(assets, level, layer);

    if (!l || l->sublayers == 0) return;

    const orb_level_desc* d = &assets->levels[ORB_HANDLE_INDEX(level)];
    const orb_tileset_desc* t = &assets->tilesets[l->tileset];
    const orb_sheet_desc* sheet = &assets->sheets[t->sheet];
    const uint8_t* pixels = assets->pixels + sheet->pixels;
    int grid = l->grid, area = l->columns * l->rows;
    // LDtk's parallax: 0 scrolls the layer with the camera like a sprite, 1 fixes it on screen
    // relative to the camera's center, anchored on the level's center.
    float center_x = cam.x + fb->width / 2.0f;
    float anchor_x = d->world_x + (d->width - l->offset_x) / 2.0f;
    int origin_x =
        d->world_x + l->offset_x - orb_floor(cam.x - (center_x - anchor_x) * l->parallax_x);
    float center_y = cam.y + fb->height / 2.0f;
    float anchor_y = d->world_y + (d->height - l->offset_y) / 2.0f;
    int origin_y =
        d->world_y + l->offset_y - orb_floor(cam.y - (center_y - anchor_y) * l->parallax_y);
    // Only the cells the framebuffer can see.
    int first_x = orb_max(0, -origin_x / grid);
    int last_x = orb_min(l->columns, (fb->width - origin_x + grid - 1) / grid);
    int first_y = orb_max(0, -origin_y / grid);
    int last_y = orb_min(l->rows, (fb->height - origin_y + grid - 1) / grid);

    for (int s = 0; s < l->sublayers; s++) {
        const uint16_t* tiles = assets->tiles + l->tiles + s * area;

        for (int y = first_y; y < last_y; y++) {
            for (int x = first_x; x < last_x; x++) {
                uint16_t tile = tiles[y * l->columns + x];

                if (!tile) continue;

                int id = (tile & ORB_TILE_ID_MASK) - 1;
                int src_x = t->padding + (id % t->columns) * (t->grid + t->spacing);
                int src_y = t->padding + (id / t->columns) * (t->grid + t->spacing);
                uint32_t flags = (tile & ORB_TILE_FLIP_X ? ORB_FLIP_X : 0) |
                                 (tile & ORB_TILE_FLIP_Y ? ORB_FLIP_Y : 0);

                orb_fb_blit(
                    fb, pixels + src_y * sheet->width + src_x, sheet->width,
                    (orb_size) {t->grid, t->grid},
                    (orb_vec2) {origin_x + x * grid, origin_y + y * grid}, flags, nullptr
                );
            }
        }
    }
}
