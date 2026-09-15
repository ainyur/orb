// World casting: the LDtk project into the level sections. Textually included by
// cast.c, so it sees cast, cast_path, cast_dir_of, cast_read, and cast_note.

static bool world_sublayers(
    cast* c,
    const orb_ldtk_level* level,
    const orb_ldtk_layer* layer,
    uint32_t* out
);

// The level sections: tilesets, levels, layers, neighbors, tiles, and cells.
// Sized by one counting pass, then filled by a second.
static bool world_cast(
    cast* c,
    const orb_ldtk* world,
    uint32_t first_tileset_sheet,
    orb_assets* as
) {
    orb_arena* scratch = c->scratch;

    if ((uint32_t)world->level_count > ORB_MAX_LEVELS)
        return orb_error_set(c->err, "%s: more than %u levels", c->m->world, ORB_MAX_LEVELS);

    uint32_t layer_total = 0;

    for (int i = 0; i < world->level_count; i++)
        layer_total += (uint32_t)world->levels[i].layer_count;

    if (layer_total > ORB_MAX_LAYERS)
        return orb_error_set(c->err, "%s: more than %u layers", c->m->world, ORB_MAX_LAYERS);

    uint8_t* sublayers = orb_arena_push_array(scratch, uint8_t, layer_total);
    uint32_t neighbor_total = 0, tile_total = 0, cell_total = 0, k = 0;

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        if (level->width > 65535 || level->height > 65535)
            return orb_error_set(
                c->err, "level %s: %dx%d pixels is larger than 65535x65535", level->name,
                level->width, level->height
            );

        neighbor_total += (uint32_t)level->neighbor_count;

        for (int j = 0; j < level->layer_count; j++, k++) {
            const orb_ldtk_layer* layer = &level->layers[j];

            if (layer->columns > 65535 || layer->rows > 65535)
                return orb_error_set(
                    c->err, "level %s: layer %s: %dx%d cells is larger than 65535x65535",
                    level->name, layer->name, layer->columns, layer->rows
                );

            if (layer->offset_x < -32768 || layer->offset_x > 32767 || layer->offset_y < -32768 ||
                layer->offset_y > 32767)
                return orb_error_set(
                    c->err, "level %s: layer %s: offset (%d, %d) does not fit in 16 bits",
                    level->name, layer->name, layer->offset_x, layer->offset_y
                );

            uint32_t area = (uint32_t)layer->columns * layer->rows;
            uint32_t depth;

            if (!world_sublayers(c, level, layer, &depth)) return false;

            sublayers[k] = (uint8_t)depth;
            tile_total += area * depth; // 0 for a layer with no tiles
            cell_total += layer->cells ? area : 0;
        }
    }

    if (neighbor_total > 65535)
        return orb_error_set(c->err, "%s: more than 65535 neighbors", c->m->world);

    orb_tileset_desc* tilesets =
        orb_arena_push_array(scratch, orb_tileset_desc, world->tileset_count);
    orb_level_desc* levels = orb_arena_push_array(scratch, orb_level_desc, world->level_count);
    orb_layer_desc* layers = orb_arena_push_array(scratch, orb_layer_desc, layer_total);
    orb_neighbor_desc* neighbors = orb_arena_push_array(scratch, orb_neighbor_desc, neighbor_total);
    uint16_t* tiles = orb_arena_push_array(scratch, uint16_t, tile_total); // zeroed by the arena
    uint8_t* cells = orb_arena_push_array(scratch, uint8_t, cell_total);
    uint64_t* level_ids = orb_arena_push_array(scratch, uint64_t, world->level_count);
    uint64_t* layer_ids = orb_arena_push_array(scratch, uint64_t, layer_total);

    for (int i = 0; i < world->tileset_count; i++) {
        const orb_ldtk_tileset* t = &world->tilesets[i];
        uint32_t slots = (uint32_t)t->columns * t->rows;

        if (slots > ORB_MAX_TILE_ID + 1)
            return orb_error_set(
                c->err, "tileset %s: %u tiles is more than %u", t->path, slots, ORB_MAX_TILE_ID + 1
            );

        tilesets[i] = (orb_tileset_desc) {
            .sheet = (uint16_t)(first_tileset_sheet + i),
            .grid = (uint16_t)t->grid,
            .spacing = (uint16_t)t->spacing,
            .padding = (uint16_t)t->padding,
            .columns = (uint16_t)t->columns,
            .count = (uint16_t)slots
        };
    }

    uint32_t layer_offset = 0, neighbor_offset = 0, tile_offset = 0, cell_offset = 0;

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        level_ids[i] = orb_asset_id(level->name, "");

        for (int p = 0; p < i; p++) {
            if (level_ids[p] != level_ids[i]) continue;

            return orb_error_set(
                c->err, "levels %s and %s share a name", world->levels[p].name, level->name
            );
        }

        levels[i] = (orb_level_desc) {
            .world_x = level->world_x,
            .world_y = level->world_y,
            .depth = level->depth,
            .width = (uint16_t)level->width,
            .height = (uint16_t)level->height,
            .first_layer = (uint16_t)layer_offset,
            .layer_count = (uint16_t)level->layer_count,
            .first_neighbor = (uint16_t)neighbor_offset,
            .neighbor_count = (uint16_t)level->neighbor_count
        };

        for (int j = 0; j < level->layer_count; j++) {
            const orb_ldtk_layer* layer = &level->layers[j];
            uint32_t idx = layer_offset + (uint32_t)j;
            uint32_t area = (uint32_t)layer->columns * layer->rows;
            uint32_t depth = sublayers[idx];

            layer_ids[idx] = orb_asset_id(layer->name, "");

            layers[idx] = (orb_layer_desc) {
                .tiles = tile_offset,
                .cells = layer->cells ? cell_offset : ORB_NO_INDEX,
                .parallax_x = layer->parallax_x,
                .parallax_y = layer->parallax_y,
                .tileset = layer->tileset >= 0 ? (uint16_t)layer->tileset : 0,
                .grid = (uint16_t)layer->grid,
                .columns = (uint16_t)layer->columns,
                .rows = (uint16_t)layer->rows,
                .offset_x = (int16_t)layer->offset_x,
                .offset_y = (int16_t)layer->offset_y,
                .sublayers = (uint8_t)depth
            };

            if (depth) {
                size_t mark = scratch->used;
                uint8_t* plane_of = orb_arena_push_array(scratch, uint8_t, area); // zeroed

                for (int t = 0; t < layer->tile_count; t++) {
                    const orb_ldtk_tile* tile = &layer->tiles[t];
                    uint32_t cell =
                        (uint32_t)tile->cell_y * (uint32_t)layer->columns + tile->cell_x;
                    uint32_t plane = plane_of[cell]++;
                    uint16_t flags = (uint16_t)((tile->flip & 1 ? ORB_TILE_FLIP_X : 0) |
                                                (tile->flip & 2 ? ORB_TILE_FLIP_Y : 0));

                    tiles[tile_offset + plane * area + cell] = (uint16_t)((tile->id + 1) | flags);
                }

                scratch->used = mark;
            }

            if (layer->cells) memcpy(cells + cell_offset, layer->cells, area);

            tile_offset += area * depth;
            cell_offset += layer->cells ? area : 0;
        }

        layer_offset += (uint32_t)level->layer_count;

        for (int n = 0; n < level->neighbor_count; n++) {
            const orb_ldtk_neighbor* nb = &level->neighbors[n];
            int target = -1;

            for (int q = 0; q < world->level_count; q++) {
                if (strcmp(world->levels[q].iid, nb->level_iid) != 0) continue;

                target = q;
                break;
            }

            if (target < 0)
                return orb_error_set(
                    c->err, "neighbour %s is not a level in this project", nb->level_iid
                );

            neighbors[neighbor_offset + (uint32_t)n] =
                (orb_neighbor_desc) {.level = (uint16_t)target, .dir = (uint8_t)nb->dir};
        }

        neighbor_offset += (uint32_t)level->neighbor_count;
    }

    as->tilesets = tilesets;
    as->tileset_count = (uint32_t)world->tileset_count;
    as->levels = levels;
    as->level_count = (uint32_t)world->level_count;
    as->layers = layers;
    as->layer_count = layer_total;
    as->neighbors = neighbors;
    as->neighbor_count = neighbor_total;
    as->tiles = tiles;
    as->tile_count = tile_total;
    as->cells = cells;
    as->cell_count = cell_total;
    as->level_ids = level_ids;
    as->layer_ids = layer_ids;
    return true;
}

// The LDtk project and its external level files, parsed but not yet cast. A
// missing project is an empty world; its directory (or "." when the path names
// no directory) is watched so creating the file recasts.
static bool world_parse(cast* c, orb_ldtk* world) {
    const char* rel = c->m->world;
    const char* path = cast_path(c->scratch, c->game_dir, rel);
    orb_os_info info;

    memset(world, 0, sizeof *world);

    if (!orb_os_stat(path, &info)) {
        const char* dir = cast_dir_of(c->scratch, rel);
        return cast_note(c, *dir ? dir : ".");
    }

    orb_span text;

    if (!cast_read(c, rel, &text)) return false;

    orb_error inner;

    if (!orb_ldtk_parse(c->scratch, text, rel, world, &inner))
        return orb_error_set(c->err, "%s", inner.text);

    const char* dir = cast_dir_of(c->scratch, rel);

    for (int i = 0; i < world->level_count; i++) {
        orb_ldtk_level* level = &world->levels[i];

        if (!level->external_path) continue;

        const char* level_rel = cast_path(c->scratch, dir, level->external_path);

        if (!cast_read(c, level_rel, &text)) return false;
        if (!orb_ldtk_parse_level(c->scratch, text, level_rel, world, level, &inner))
            return orb_error_set(c->err, "%s", inner.text);
    }

    return true;
}

// The maximum number of tiles any one cell of the layer receives, counted with
// a per-cell counter pushed and popped from scratch. A cell over ORB_MAX_SUBLAYERS
// is an error naming the level, the layer, and the cell.
static bool world_sublayers(
    cast* c,
    const orb_ldtk_level* level,
    const orb_ldtk_layer* layer,
    uint32_t* out
) {
    orb_arena* scratch = c->scratch;
    uint32_t area = (uint32_t)layer->columns * layer->rows;
    size_t mark = scratch->used;
    uint8_t* counts = orb_arena_push_array(scratch, uint8_t, area);
    uint32_t max_depth = 0;

    for (int i = 0; i < layer->tile_count; i++) {
        const orb_ldtk_tile* tile = &layer->tiles[i];
        uint32_t cell = (uint32_t)tile->cell_y * (uint32_t)layer->columns + tile->cell_x;
        uint32_t depth = ++counts[cell];

        if (depth > max_depth) max_depth = depth;

        if (depth > ORB_MAX_SUBLAYERS) {
            scratch->used = mark;
            return orb_error_set(
                c->err, "level %s: layer %s: cell (%d, %d) stacks more than %u tiles (sub-layers)",
                level->name, layer->name, tile->cell_x, tile->cell_y, ORB_MAX_SUBLAYERS
            );
        }
    }

    scratch->used = mark;
    *out = max_depth;
    return true;
}
