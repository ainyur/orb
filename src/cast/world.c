// World casting: the LDtk project into the level sections. Textually included by
// cast.c, so it sees cast, cast_path, cast_dir_of, cast_read, and cast_note.

// The LDtk project and its external level files, parsed but not yet cast. A
// missing project is an empty world; its directory (or "." when the path names
// no directory) is watched so creating the file recasts.
static bool world_parse(cast* context, orb_ldtk* world) {
    const char* rel = context->manifest->world;
    const char* path = cast_path(context->scratch, context->game_dir, rel);
    orb_os_info info;

    memset(world, 0, sizeof *world);

    if (!orb_os_stat(path, &info)) {
        const char* dir = cast_dir_of(context->scratch, rel);
        return cast_note(context, *dir ? dir : ".");
    }

    orb_span text;

    if (!cast_read(context, rel, &text)) return false;

    orb_error inner;

    if (!orb_ldtk_parse(context->scratch, text, rel, world, &inner))
        return orb_error_set(context->err, "%s", inner.text);

    const char* dir = cast_dir_of(context->scratch, rel);

    for (int i = 0; i < world->level_count; i++) {
        orb_ldtk_level* level = &world->levels[i];

        if (!level->external_path) continue;

        const char* level_rel = cast_path(context->scratch, dir, level->external_path);

        if (!cast_read(context, level_rel, &text)) return false;
        if (!orb_ldtk_parse_level(context->scratch, text, level_rel, world, level, &inner))
            return orb_error_set(context->err, "%s", inner.text);
    }

    return true;
}

// The maximum number of tiles any one cell of the layer receives, counted with
// a per-cell counter pushed and popped from scratch. A cell over ORB_MAX_SUBLAYERS
// is an error naming the level, the layer, and the cell.
static bool world_sublayers(
    cast* context,
    const orb_ldtk_level* level,
    const orb_ldtk_layer* layer,
    uint32_t* out
) {
    orb_arena* scratch = context->scratch;
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
                context->err,
                "level %s: layer %s: cell (%d, %d) stacks more than %u tiles (sub-layers)",
                level->name, layer->name, tile->cell_x, tile->cell_y, ORB_MAX_SUBLAYERS
            );
        }
    }

    scratch->used = mark;
    *out = max_depth;
    return true;
}

// Bytes the field data section needs for these fields: elements at 4-byte alignment, then
// the strings, then the next field aligned again.
static uint32_t world_field_bytes(const orb_ldtk_field* fields, int count) {
    uint32_t total = 0;

    for (int i = 0; i < count; i++) {
        const orb_ldtk_field* field = &fields[i];

        total += (uint32_t)field->count * orb_field_width(field->kind);

        if (field->kind == ORB_FIELD_STRING)
            for (int k = 0; k < field->count; k++)
                total += (uint32_t)strlen(field->values[k].string) + 1;

        total = (total + 3) & ~3u;
    }

    return total;
}

typedef struct world_iid {
    uint64_t id;
    uint32_t index;
} world_iid;

static int world_iid_compare(const void* a, const void* b) {
    uint64_t x = ((const world_iid*)a)->id, y = ((const world_iid*)b)->id;

    return x < y ? -1 : x > y;
}

// Every placement's iid and index, sorted by iid.
static world_iid* world_iid_index(orb_arena* scratch, const orb_ldtk* world, uint32_t total) {
    world_iid* iids = orb_arena_push_array(scratch, world_iid, total);
    uint32_t n = 0;

    for (int i = 0; i < world->level_count; i++)
        for (int j = 0; j < world->levels[i].instance_count; j++, n++)
            iids[n] = (world_iid) {orb_asset_id(world->levels[i].instances[j].iid, ""), n};

    qsort(iids, total, sizeof *iids, world_iid_compare);
    return iids;
}

// The placement index of the instance with this iid, or -1.
static int world_placement_of(const world_iid* iids, uint32_t count, const char* iid) {
    world_iid key = {orb_asset_id(iid, ""), 0};
    const world_iid* hit = bsearch(&key, iids, count, sizeof *iids, world_iid_compare);

    return hit ? (int)hit->index : -1;
}

// Field rows and their data for one type or placement, refs resolved to placement indices.
static bool world_write_fields(
    cast* context,
    const world_iid* iids,
    uint32_t iid_count,
    const char* level_name,
    const char* entity_name,
    const orb_ldtk_field* src,
    int count,
    orb_field_desc* fields,
    uint8_t* data,
    uint32_t* field_offset,
    uint32_t* data_offset
) {
    char prefix[160];

    if (level_name)
        snprintf(prefix, sizeof prefix, "level %s: entity %s", level_name, entity_name);
    else
        snprintf(prefix, sizeof prefix, "entity %s", entity_name);

    for (int i = 0; i < count; i++) {
        const orb_ldtk_field* field = &src[i];
        uint32_t width = orb_field_width(field->kind);
        uint8_t* dest = data + *data_offset;
        uint32_t string_at = *data_offset + (((uint32_t)field->count * width + 3) & ~3u);

        if (field->count > 65535)
            return orb_error_set(
                context->err, "%s: field %s: %d elements is more than 65535", prefix, field->name,
                field->count
            );

        fields[(*field_offset)++] = (orb_field_desc) {
            .name = orb_asset_id(field->name, ""),
            .data = *data_offset,
            .count = (uint16_t)field->count,
            .kind = (uint8_t)field->kind
        };

        for (int k = 0; k < field->count; k++) {
            const orb_ldtk_value* value = &field->values[k];

            switch (field->kind) {
            case ORB_FIELD_INT:
                memcpy(dest + k * 4, &value->integer, 4);
                break;
            case ORB_FIELD_FLOAT:
                memcpy(dest + k * 4, &value->number, 4);
                break;
            case ORB_FIELD_BOOL:
                dest[k] = value->boolean;
                break;
            case ORB_FIELD_STRING: {
                size_t n = strlen(value->string) + 1;

                memcpy(dest + k * 4, &string_at, 4);
                memcpy(data + string_at, value->string, n);
                string_at += (uint32_t)n;
                break;
            }
            case ORB_FIELD_POINT:
                memcpy(dest + k * 8, &value->point.x, 4);
                memcpy(dest + k * 8 + 4, &value->point.y, 4);
                break;
            case ORB_FIELD_REF: {
                int target = world_placement_of(iids, iid_count, value->string);

                if (target < 0)
                    return orb_error_set(
                        context->err, "%s: field %s: ref %s is not an entity in this project",
                        prefix, field->name, value->string
                    );

                uint32_t index = (uint32_t)target;

                memcpy(dest + k * 4, &index, 4);
                break;
            }
            }
        }

        *data_offset = (string_at + 3) & ~3u;
    }

    return true;
}

// The type, placement, field, and field data sections, and each level's placement run.
// Sized by one counting pass, then filled.
static bool world_cast_entities(
    cast* context,
    const orb_ldtk* world,
    orb_level_desc* levels,
    orb_assets* assets
) {
    orb_arena* scratch = context->scratch;

    if ((uint32_t)world->entity_def_count > ORB_MAX_TYPES)
        return orb_error_set(
            context->err, "%s: more than %u entity types", context->manifest->world, ORB_MAX_TYPES
        );

    uint32_t placement_total = 0, field_total = 0, data_total = 0;

    for (int i = 0; i < world->entity_def_count; i++) {
        const orb_ldtk_entity_def* def = &world->entity_defs[i];

        field_total += (uint32_t)def->field_count;
        data_total += world_field_bytes(def->fields, def->field_count);
    }

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        placement_total += (uint32_t)level->instance_count;

        for (int j = 0; j < level->instance_count; j++) {
            const orb_ldtk_instance* inst = &level->instances[j];

            field_total += (uint32_t)inst->field_count;
            data_total += world_field_bytes(inst->fields, inst->field_count);
        }
    }

    if (placement_total > ORB_MAX_PLACEMENTS)
        return orb_error_set(
            context->err, "%s: more than %u entities", context->manifest->world, ORB_MAX_PLACEMENTS
        );
    if (field_total > ORB_MAX_FIELDS)
        return orb_error_set(
            context->err, "%s: more than %u field values", context->manifest->world, ORB_MAX_FIELDS
        );

    orb_type_desc* types = orb_arena_push_array(scratch, orb_type_desc, world->entity_def_count);
    uint64_t* type_ids = orb_arena_push_array(scratch, uint64_t, world->entity_def_count);
    orb_placement_desc* placements =
        orb_arena_push_array(scratch, orb_placement_desc, placement_total);
    orb_field_desc* fields = orb_arena_push_array(scratch, orb_field_desc, field_total);
    uint8_t* data = orb_arena_push_array(scratch, uint8_t, data_total);
    world_iid* iids = world_iid_index(scratch, world, placement_total);
    uint32_t field_offset = 0, data_offset = 0, placement_offset = 0;

    for (int i = 0; i < world->entity_def_count; i++) {
        const orb_ldtk_entity_def* def = &world->entity_defs[i];

        type_ids[i] = orb_asset_id(def->name, "");

        int dup = cast_dup_id(type_ids, i);

        if (dup >= 0)
            return orb_error_set(
                context->err, "entity types %s and %s share a name", world->entity_defs[dup].name,
                def->name
            );

        if (def->width > 65535 || def->height > 65535 || def->width < 0 || def->height < 0)
            return orb_error_set(
                context->err, "entity %s: %dx%d pixels does not fit in 16 bits", def->name,
                def->width, def->height
            );

        types[i] = (orb_type_desc) {
            .width = (uint16_t)def->width,
            .height = (uint16_t)def->height,
            .first_field = field_offset,
            .field_count = (uint32_t)def->field_count
        };

        if (!world_write_fields(
                context, iids, placement_total, nullptr, def->name, def->fields, def->field_count,
                fields, data, &field_offset, &data_offset
            ))
            return false;
    }

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        levels[i].first_placement = placement_offset;
        levels[i].placement_count = (uint32_t)level->instance_count;

        for (int j = 0; j < level->instance_count; j++) {
            const orb_ldtk_instance* inst = &level->instances[j];
            const char* name = world->entity_defs[inst->def].name;

            if (inst->width > 65535 || inst->height > 65535 || inst->width < 0 || inst->height < 0)
                return orb_error_set(
                    context->err, "level %s: entity %s: %dx%d pixels does not fit in 16 bits",
                    level->name, name, inst->width, inst->height
                );

            placements[placement_offset++] = (orb_placement_desc) {
                .iid = orb_asset_id(inst->iid, ""),
                .type = (uint16_t)inst->def,
                .level = (uint16_t)i,
                .x = inst->x,
                .y = inst->y,
                .width = (uint16_t)inst->width,
                .height = (uint16_t)inst->height,
                .first_field = field_offset,
                .field_count = (uint32_t)inst->field_count
            };

            if (!world_write_fields(
                    context, iids, placement_total, level->name, name, inst->fields,
                    inst->field_count, fields, data, &field_offset, &data_offset
                ))
                return false;
        }
    }

    assets->types = types;
    assets->type_count = (uint32_t)world->entity_def_count;
    assets->type_ids = type_ids;
    assets->placements = placements;
    assets->placement_count = placement_total;
    assets->fields = fields;
    assets->field_count = field_total;
    assets->field_data = data;
    assets->field_data_count = data_total;
    return true;
}

typedef struct world_sizes {
    uint8_t* sublayers; // one depth per layer
    uint32_t layer_total, neighbor_total, tile_total, cell_total;
} world_sizes;

// Level and layer counts against their limits, and each layer's sublayer depth
// (from world_sublayers), which sizes the tiles section.
static bool world_size(cast* context, const orb_ldtk* world, world_sizes* out) {
    if ((uint32_t)world->level_count > ORB_MAX_LEVELS)
        return orb_error_set(
            context->err, "%s: more than %u levels", context->manifest->world, ORB_MAX_LEVELS
        );

    uint32_t layer_total = 0;

    for (int i = 0; i < world->level_count; i++)
        layer_total += (uint32_t)world->levels[i].layer_count;

    if (layer_total > ORB_MAX_LAYERS)
        return orb_error_set(
            context->err, "%s: more than %u layers", context->manifest->world, ORB_MAX_LAYERS
        );

    uint8_t* sublayers = orb_arena_push_array(context->scratch, uint8_t, layer_total);
    uint32_t neighbor_total = 0, tile_total = 0, cell_total = 0, k = 0;

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        if (level->width > 65535 || level->height > 65535)
            return orb_error_set(
                context->err, "level %s: %dx%d pixels is larger than 65535x65535", level->name,
                level->width, level->height
            );

        neighbor_total += (uint32_t)level->neighbor_count;

        for (int j = 0; j < level->layer_count; j++, k++) {
            const orb_ldtk_layer* layer = &level->layers[j];

            if (layer->offset_x < -32768 || layer->offset_x > 32767 || layer->offset_y < -32768 ||
                layer->offset_y > 32767)
                return orb_error_set(
                    context->err, "level %s: layer %s: offset (%d, %d) does not fit in 16 bits",
                    level->name, layer->name, layer->offset_x, layer->offset_y
                );

            uint32_t area = (uint32_t)layer->columns * layer->rows;
            uint32_t depth;

            if (!world_sublayers(context, level, layer, &depth)) return false;

            sublayers[k] = (uint8_t)depth;
            tile_total += area * depth; // 0 for a layer with no tiles
            cell_total += layer->cells ? area : 0;
        }
    }

    if (neighbor_total > 65535)
        return orb_error_set(
            context->err, "%s: more than 65535 neighbors", context->manifest->world
        );

    *out = (world_sizes) {
        .sublayers = sublayers,
        .layer_total = layer_total,
        .neighbor_total = neighbor_total,
        .tile_total = tile_total,
        .cell_total = cell_total
    };
    return true;
}

// The tileset table: sheet index, grid metrics, and the tile-id bound each layer checks against.
static bool world_tilesets(
    cast* context,
    const orb_ldtk* world,
    uint32_t first_tileset_sheet,
    orb_tileset_desc* tilesets
) {
    for (int i = 0; i < world->tileset_count; i++) {
        const orb_ldtk_tileset* tileset = &world->tilesets[i];
        uint32_t slots = (uint32_t)tileset->columns * tileset->rows;

        if (slots > ORB_MAX_TILE_ID + 1)
            return orb_error_set(
                context->err, "tileset %s: %u tiles is more than %u", tileset->path, slots,
                ORB_MAX_TILE_ID + 1
            );

        tilesets[i] = (orb_tileset_desc) {
            .sheet = (uint16_t)(first_tileset_sheet + i),
            .grid = (uint16_t)tileset->grid,
            .spacing = (uint16_t)tileset->spacing,
            .padding = (uint16_t)tileset->padding,
            .columns = (uint16_t)tileset->columns,
            .count = (uint16_t)slots
        };
    }

    return true;
}

// One level's neighbors, each resolved from its iid to a level index.
static bool world_neighbors(
    cast* context,
    const orb_ldtk* world,
    const orb_ldtk_level* level,
    uint32_t neighbor_offset,
    orb_neighbor_desc* neighbors
) {
    for (int n = 0; n < level->neighbor_count; n++) {
        const orb_ldtk_neighbor* neighbor = &level->neighbors[n];
        int target = -1;

        for (int level_index = 0; level_index < world->level_count; level_index++) {
            if (strcmp(world->levels[level_index].iid, neighbor->level_iid) != 0) continue;

            target = level_index;
            break;
        }

        if (target < 0)
            return orb_error_set(
                context->err, "neighbour %s is not a level in this project", neighbor->level_iid
            );

        neighbors[neighbor_offset + (uint32_t)n] =
            (orb_neighbor_desc) {.level = (uint16_t)target, .dir = (uint8_t)neighbor->dir};
    }

    return true;
}

// The level sections: tilesets, levels, layers, neighbors, tiles, and cells.
// Sized by one counting pass, then filled by a second.
static bool world_cast(
    cast* context,
    const orb_ldtk* world,
    uint32_t first_tileset_sheet,
    orb_assets* assets
) {
    orb_arena* scratch = context->scratch;
    world_sizes sizes;

    if (!world_size(context, world, &sizes)) return false;

    orb_tileset_desc* tilesets =
        orb_arena_push_array(scratch, orb_tileset_desc, world->tileset_count);
    orb_level_desc* levels = orb_arena_push_array(scratch, orb_level_desc, world->level_count);
    orb_layer_desc* layers = orb_arena_push_array(scratch, orb_layer_desc, sizes.layer_total);
    orb_neighbor_desc* neighbors =
        orb_arena_push_array(scratch, orb_neighbor_desc, sizes.neighbor_total);
    uint16_t* tiles =
        orb_arena_push_array(scratch, uint16_t, sizes.tile_total); // zeroed by the arena
    uint8_t* cells = orb_arena_push_array(scratch, uint8_t, sizes.cell_total);
    uint64_t* level_ids = orb_arena_push_array(scratch, uint64_t, world->level_count);
    uint64_t* layer_ids = orb_arena_push_array(scratch, uint64_t, sizes.layer_total);

    if (!world_tilesets(context, world, first_tileset_sheet, tilesets)) return false;

    uint32_t layer_offset = 0, neighbor_offset = 0, tile_offset = 0, cell_offset = 0;

    for (int i = 0; i < world->level_count; i++) {
        const orb_ldtk_level* level = &world->levels[i];

        level_ids[i] = orb_asset_id(level->name, "");

        int dup = cast_dup_id(level_ids, i);

        if (dup >= 0)
            return orb_error_set(
                context->err, "levels %s and %s share a name", world->levels[dup].name, level->name
            );

        levels[i] = (orb_level_desc) {
            .world_x = level->world_x,
            .world_y = level->world_y,
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
            uint32_t depth = sizes.sublayers[idx];

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

                for (int tile_index = 0; tile_index < layer->tile_count; tile_index++) {
                    const orb_ldtk_tile* tile = &layer->tiles[tile_index];
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

        if (!world_neighbors(context, world, level, neighbor_offset, neighbors)) return false;

        neighbor_offset += (uint32_t)level->neighbor_count;
    }

    if (!world_cast_entities(context, world, levels, assets)) return false;

    assets->tilesets = tilesets;
    assets->tileset_count = (uint32_t)world->tileset_count;
    assets->levels = levels;
    assets->level_count = (uint32_t)world->level_count;
    assets->layers = layers;
    assets->layer_count = sizes.layer_total;
    assets->neighbors = neighbors;
    assets->neighbor_count = sizes.neighbor_total;
    assets->tiles = tiles;
    assets->tile_count = sizes.tile_total;
    assets->cells = cells;
    assets->cell_count = sizes.cell_total;
    assets->level_ids = level_ids;
    assets->layer_ids = layer_ids;
    return true;
}
