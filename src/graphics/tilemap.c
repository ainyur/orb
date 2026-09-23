#include "tilemap.h"
#include "../core/macros.h"

// The level desc, or nullptr when the handle is stale.
static const orb_level_desc* tilemap_level(const orb_assets* assets, orb_level level) {
    uint32_t index = orb_asset_index_of(assets, level);

    return index == ORB_NO_INDEX ? nullptr : &assets->levels[index];
}

// The layer desc, or nullptr when the handle is stale or the index is not the level's.
static const orb_layer_desc* tilemap_layer(const orb_assets* assets, orb_level level, int layer) {
    const orb_level_desc* level_desc = tilemap_level(assets, level);

    if (!level_desc || layer < 0 || layer >= level_desc->layer_count) return nullptr;

    return &assets->layers[level_desc->first_layer + layer];
}

int orb_tilemap_cell(const orb_assets* assets, orb_level level, int layer, orb_vec2 at) {
    const orb_layer_desc* layer_desc = tilemap_layer(assets, level, layer);

    if (!layer_desc || layer_desc->cells == ORB_NO_INDEX) return 0;

    const orb_level_desc* level_desc = &assets->levels[ORB_HANDLE_INDEX(level)];
    int x = at.x - level_desc->world_x - layer_desc->offset_x,
        y = at.y - level_desc->world_y - layer_desc->offset_y;

    if (x < 0 || y < 0) return 0;

    int cell_x = x / layer_desc->grid, cell_y = y / layer_desc->grid;

    if (cell_x >= layer_desc->columns || cell_y >= layer_desc->rows) return 0;

    return assets->cells[layer_desc->cells + cell_y * layer_desc->columns + cell_x];
}

uint32_t orb_tilemap_level_at(const orb_assets* assets, orb_vec2 at) {
    for (uint32_t i = 0; i < assets->level_count; i++) {
        const orb_level_desc* level_desc = &assets->levels[i];

        if (at.x >= level_desc->world_x && at.x < level_desc->world_x + level_desc->width &&
            at.y >= level_desc->world_y && at.y < level_desc->world_y + level_desc->height)
            return i;
    }

    return ORB_NO_INDEX;
}

int orb_tilemap_layer_find(const orb_assets* assets, orb_level level, const char* name) {
    const orb_level_desc* level_desc = tilemap_level(assets, level);
    uint64_t id = orb_asset_id(name, "");

    for (int i = 0; level_desc && i < level_desc->layer_count; i++)
        if (assets->layer_ids[level_desc->first_layer + i] == id) return i;

    return (int)ORB_NO_INDEX;
}

orb_layer_info orb_tilemap_layer_info(const orb_assets* assets, orb_level level, int layer) {
    const orb_layer_desc* layer_desc = tilemap_layer(assets, level, layer);

    if (!layer_desc) return (orb_layer_info) {};

    return (orb_layer_info) {
        .grid = layer_desc->grid,
        .columns = layer_desc->columns,
        .rows = layer_desc->rows,
        .has_tiles = layer_desc->sublayers > 0,
        .has_cells = layer_desc->cells != ORB_NO_INDEX
    };
}

orb_rect orb_tilemap_level_bounds(const orb_assets* assets, orb_level level) {
    const orb_level_desc* level_desc = tilemap_level(assets, level);

    if (!level_desc) return (orb_rect) {};

    return (orb_rect) {
        {level_desc->world_x, level_desc->world_y}, {level_desc->width, level_desc->height}
    };
}

int orb_tilemap_level_neighbors(
    const orb_assets* assets,
    orb_level level,
    orb_level_neighbor* out,
    int max
) {
    const orb_level_desc* level_desc = tilemap_level(assets, level);
    int n = 0;

    for (int i = 0; level_desc && i < level_desc->neighbor_count && n < max; i++) {
        const orb_neighbor_desc* link = &assets->neighbors[level_desc->first_neighbor + i];
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
    const orb_layer_desc* layer_desc = tilemap_layer(assets, level, layer);

    if (!layer_desc || layer_desc->sublayers == 0) return;

    const orb_level_desc* level_desc = &assets->levels[ORB_HANDLE_INDEX(level)];
    const orb_tileset_desc* tileset = &assets->tilesets[layer_desc->tileset];
    const orb_sheet_desc* sheet = &assets->sheets[tileset->sheet];
    const uint8_t* pixels = assets->pixels + sheet->pixels;
    int grid = layer_desc->grid, area = layer_desc->columns * layer_desc->rows;
    // LDtk's parallax: 0 scrolls the layer with the camera like a sprite, 1 fixes it on screen
    // relative to the camera's center, anchored on the level's center.
    float center_x = cam.x + fb->width / 2.0f;
    float anchor_x = level_desc->world_x + (level_desc->width - layer_desc->offset_x) / 2.0f;
    int origin_x = level_desc->world_x + layer_desc->offset_x -
                   orb_floor(cam.x - (center_x - anchor_x) * layer_desc->parallax_x);
    float center_y = cam.y + fb->height / 2.0f;
    float anchor_y = level_desc->world_y + (level_desc->height - layer_desc->offset_y) / 2.0f;
    int origin_y = level_desc->world_y + layer_desc->offset_y -
                   orb_floor(cam.y - (center_y - anchor_y) * layer_desc->parallax_y);
    // Only the cells the framebuffer can see.
    int first_x = orb_max(0, -origin_x / grid);
    int last_x = orb_min(layer_desc->columns, (fb->width - origin_x + grid - 1) / grid);
    int first_y = orb_max(0, -origin_y / grid);
    int last_y = orb_min(layer_desc->rows, (fb->height - origin_y + grid - 1) / grid);

    for (int sublayer = 0; sublayer < layer_desc->sublayers; sublayer++) {
        const uint16_t* tiles = assets->tiles + layer_desc->tiles + sublayer * area;

        for (int y = first_y; y < last_y; y++) {
            for (int x = first_x; x < last_x; x++) {
                uint16_t tile = tiles[y * layer_desc->columns + x];

                if (!tile) continue;

                int id = (tile & ORB_TILE_ID_MASK) - 1;
                int src_x =
                    tileset->padding + (id % tileset->columns) * (tileset->grid + tileset->spacing);
                int src_y =
                    tileset->padding + (id / tileset->columns) * (tileset->grid + tileset->spacing);
                uint32_t flags = (tile & ORB_TILE_FLIP_X ? ORB_FLIP_X : 0) |
                                 (tile & ORB_TILE_FLIP_Y ? ORB_FLIP_Y : 0);

                orb_fb_blit(
                    fb, pixels + src_y * sheet->width + src_x, sheet->width,
                    (orb_size) {tileset->grid, tileset->grid},
                    (orb_vec2) {origin_x + x * grid, origin_y + y * grid}, flags, nullptr
                );
            }
        }
    }
}
