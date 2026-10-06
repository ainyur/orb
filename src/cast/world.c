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

    u8_span text;

    if (!cast_read(context, rel, &text)) return false;

    orb_error inner;

    if (!orb_ldtk_parse(context->scratch, text, rel, world, &inner))
        return orb_error_set(context->err, "%s", inner.text);

    const char* dir = cast_dir_of(context->scratch, rel);

    for (u32 i = 0; i < world->levels.len; i++) {
        orb_ldtk_level* level = &world->levels.elems[i];

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
    u32* out
) {
    arena* scratch = context->scratch;
    u32 area = (u32)layer->columns * layer->rows;
    usize mark = scratch->used;
    u8* counts = orb_arena_push_array(scratch, u8, area);
    u32 max_depth = 0;

    for (u32 i = 0; i < layer->tiles.len; i++) {
        const orb_ldtk_tile* tile = &layer->tiles.elems[i];
        u32 cell = (u32)tile->cell_y * (u32)layer->columns + tile->cell_x;
        u32 depth = ++counts[cell];

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
static u32 world_field_bytes(orb_ldtk_field_span fields) {
    u32 total = 0;

    for (u32 i = 0; i < fields.len; i++) {
        const orb_ldtk_field* field = &fields.elems[i];

        total += (u32)field->values.len * orb_field_width(field->kind);

        if (field->kind == ORB_FIELD_STRING)
            for (u32 k = 0; k < field->values.len; k++)
                total += (u32)strlen(field->values.elems[k].string) + 1;

        total = (total + 3) & ~3u;
    }

    return total;
}

typedef struct world_iid {
    u64 id;
    u32 index;
} world_iid;

static int world_iid_compare(const void* a, const void* b) {
    u64 x = ((const world_iid*)a)->id, y = ((const world_iid*)b)->id;

    return x < y ? -1 : x > y;
}

// Every placement's iid and index, sorted by iid.
static world_iid* world_iid_index(arena* scratch, const orb_ldtk* world, u32 total) {
    world_iid* iids = orb_arena_push_array(scratch, world_iid, total);
    u32 n = 0;

    for (u32 i = 0; i < world->levels.len; i++)
        for (u32 j = 0; j < world->levels.elems[i].instances.len; j++, n++)
            iids[n] =
                (world_iid) {orb_asset_id(world->levels.elems[i].instances.elems[j].iid, ""), n};

    qsort(iids, total, sizeof *iids, world_iid_compare);
    return iids;
}

// The placement index of the instance with this iid, or -1.
static int world_placement_of(const world_iid* iids, u32 count, const char* iid) {
    world_iid key = {orb_asset_id(iid, ""), 0};
    const world_iid* hit = bsearch(&key, iids, count, sizeof *iids, world_iid_compare);

    return hit ? (int)hit->index : -1;
}

// Field rows and their data for one type or placement, refs resolved to placement indices.
static bool world_write_fields(
    cast* context,
    const world_iid* iids,
    u32 iid_count,
    const char* level_name,
    const char* entity_name,
    orb_ldtk_field_span src,
    orb_field_desc* fields,
    u8* data,
    u32* field_offset,
    u32* data_offset
) {
    char prefix[160];

    if (level_name)
        snprintf(prefix, sizeof prefix, "level %s: entity %s", level_name, entity_name);
    else
        snprintf(prefix, sizeof prefix, "entity %s", entity_name);

    for (u32 i = 0; i < src.len; i++) {
        const orb_ldtk_field* field = &src.elems[i];
        u32 width = orb_field_width(field->kind);
        u8* dest = data + *data_offset;
        u32 string_at = *data_offset + (((u32)field->values.len * width + 3) & ~3u);

        if (field->values.len > 65535)
            return orb_error_set(
                context->err, "%s: field %s: %d elements is more than 65535", prefix, field->name,
                field->values.len
            );

        fields[(*field_offset)++] = (orb_field_desc) {
            .name = orb_asset_id(field->name, ""),
            .data = *data_offset,
            .count = (u16)field->values.len,
            .kind = (u8)field->kind
        };

        for (u32 k = 0; k < field->values.len; k++) {
            const orb_ldtk_value* value = &field->values.elems[k];

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
                usize n = strlen(value->string) + 1;

                memcpy(dest + k * 4, &string_at, 4);
                memcpy(data + string_at, value->string, n);
                string_at += (u32)n;
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

                u32 index = (u32)target;

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
    arena* scratch = context->scratch;

    if ((u32)world->entity_defs.len > ORB_MAX_TYPES)
        return orb_error_set(
            context->err, "%s: more than %u entity types", context->manifest->world, ORB_MAX_TYPES
        );

    u32 placement_total = 0, field_total = 0, data_total = 0;

    for (u32 i = 0; i < world->entity_defs.len; i++) {
        const orb_ldtk_entity_def* def = &world->entity_defs.elems[i];

        field_total += (u32)def->fields.len;
        data_total += world_field_bytes(def->fields.span);
    }

    for (u32 i = 0; i < world->levels.len; i++) {
        const orb_ldtk_level* level = &world->levels.elems[i];

        placement_total += (u32)level->instances.len;

        for (u32 j = 0; j < level->instances.len; j++) {
            const orb_ldtk_instance* inst = &level->instances.elems[j];

            field_total += (u32)inst->fields.len;
            data_total += world_field_bytes(inst->fields.span);
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

    orb_type_desc* types = orb_arena_push_array(scratch, orb_type_desc, world->entity_defs.len);
    u64* type_ids = orb_arena_push_array(scratch, u64, world->entity_defs.len);
    orb_placement_desc* placements =
        orb_arena_push_array(scratch, orb_placement_desc, placement_total);
    orb_field_desc* fields = orb_arena_push_array(scratch, orb_field_desc, field_total);
    u8* data = orb_arena_push_array(scratch, u8, data_total);
    world_iid* iids = world_iid_index(scratch, world, placement_total);
    u32 field_offset = 0, data_offset = 0, placement_offset = 0;

    for (u32 i = 0; i < world->entity_defs.len; i++) {
        const orb_ldtk_entity_def* def = &world->entity_defs.elems[i];

        type_ids[i] = orb_asset_id(def->name, "");

        int dup = cast_dup_id(type_ids, i);

        if (dup >= 0)
            return orb_error_set(
                context->err, "entity types %s and %s share a name",
                world->entity_defs.elems[dup].name, def->name
            );

        if (def->width > 65535 || def->height > 65535 || def->width < 0 || def->height < 0)
            return orb_error_set(
                context->err, "entity %s: %dx%d pixels does not fit in 16 bits", def->name,
                def->width, def->height
            );

        types[i] = (orb_type_desc) {
            .width = (u16)def->width,
            .height = (u16)def->height,
            .first_field = field_offset,
            .field_count = (u32)def->fields.len
        };

        if (!world_write_fields(
                context, iids, placement_total, nullptr, def->name, def->fields.span, fields, data,
                &field_offset, &data_offset
            ))
            return false;
    }

    for (u32 i = 0; i < world->levels.len; i++) {
        const orb_ldtk_level* level = &world->levels.elems[i];

        levels[i].first_placement = placement_offset;
        levels[i].placement_count = (u32)level->instances.len;

        for (u32 j = 0; j < level->instances.len; j++) {
            const orb_ldtk_instance* inst = &level->instances.elems[j];
            const char* name = world->entity_defs.elems[inst->def].name;

            if (inst->width > 65535 || inst->height > 65535 || inst->width < 0 || inst->height < 0)
                return orb_error_set(
                    context->err, "level %s: entity %s: %dx%d pixels does not fit in 16 bits",
                    level->name, name, inst->width, inst->height
                );

            placements[placement_offset++] = (orb_placement_desc) {
                .iid = orb_asset_id(inst->iid, ""),
                .type = (u16)inst->def,
                .level = (u16)i,
                .x = inst->x,
                .y = inst->y,
                .width = (u16)inst->width,
                .height = (u16)inst->height,
                .first_field = field_offset,
                .field_count = (u32)inst->fields.len
            };

            if (!world_write_fields(
                    context, iids, placement_total, level->name, name, inst->fields.span, fields,
                    data, &field_offset, &data_offset
                ))
                return false;
        }
    }

    assets->types = (orb_type_desc_span) {types, (u32)world->entity_defs.len};
    assets->type_ids = type_ids;
    assets->placements = (orb_placement_desc_span) {placements, placement_total};
    assets->fields = (orb_field_desc_span) {fields, field_total};
    assets->field_data = (u8_span) {data, data_total};
    return true;
}

typedef struct world_sizes {
    u8* sublayers; // one depth per layer
    u32 layer_total, neighbor_total, tile_total, cell_total;
} world_sizes;

// Level and layer counts against their limits, and each layer's sublayer depth
// (from world_sublayers), which sizes the tiles section.
static bool world_size(cast* context, const orb_ldtk* world, world_sizes* out) {
    if ((u32)world->levels.len > ORB_MAX_LEVELS)
        return orb_error_set(
            context->err, "%s: more than %u levels", context->manifest->world, ORB_MAX_LEVELS
        );

    u32 layer_total = 0;

    for (u32 i = 0; i < world->levels.len; i++)
        layer_total += (u32)world->levels.elems[i].layers.len;

    if (layer_total > ORB_MAX_LAYERS)
        return orb_error_set(
            context->err, "%s: more than %u layers", context->manifest->world, ORB_MAX_LAYERS
        );

    u8* sublayers = orb_arena_push_array(context->scratch, u8, layer_total);
    u32 neighbor_total = 0, tile_total = 0, cell_total = 0, k = 0;

    for (u32 i = 0; i < world->levels.len; i++) {
        const orb_ldtk_level* level = &world->levels.elems[i];

        if (level->width > 65535 || level->height > 65535)
            return orb_error_set(
                context->err, "level %s: %dx%d pixels is larger than 65535x65535", level->name,
                level->width, level->height
            );

        neighbor_total += (u32)level->neighbors.len;

        for (u32 j = 0; j < level->layers.len; j++, k++) {
            const orb_ldtk_layer* layer = &level->layers.elems[j];

            if (layer->offset_x < -32768 || layer->offset_x > 32767 || layer->offset_y < -32768 ||
                layer->offset_y > 32767)
                return orb_error_set(
                    context->err, "level %s: layer %s: offset (%d, %d) does not fit in 16 bits",
                    level->name, layer->name, layer->offset_x, layer->offset_y
                );

            u32 area = (u32)layer->columns * layer->rows;
            u32 depth;

            if (!world_sublayers(context, level, layer, &depth)) return false;

            sublayers[k] = (u8)depth;
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
    u32 first_tileset_sheet,
    orb_tileset_desc* tilesets
) {
    for (u32 i = 0; i < world->tilesets.len; i++) {
        const orb_ldtk_tileset* tileset = &world->tilesets.elems[i];
        u32 slots = (u32)tileset->columns * tileset->rows;

        if (slots > ORB_MAX_TILE_ID + 1)
            return orb_error_set(
                context->err, "tileset %s: %u tiles is more than %u", tileset->path, slots,
                ORB_MAX_TILE_ID + 1
            );

        tilesets[i] = (orb_tileset_desc) {
            .sheet = (u16)(first_tileset_sheet + i),
            .grid = (u16)tileset->grid,
            .spacing = (u16)tileset->spacing,
            .padding = (u16)tileset->padding,
            .columns = (u16)tileset->columns,
            .count = (u16)slots
        };
    }

    return true;
}

// One level's neighbors, each resolved from its iid to a level index.
static bool world_neighbors(
    cast* context,
    const orb_ldtk* world,
    const orb_ldtk_level* level,
    u32 neighbor_offset,
    orb_neighbor_desc* neighbors
) {
    for (u32 n = 0; n < level->neighbors.len; n++) {
        const orb_ldtk_neighbor* neighbor = &level->neighbors.elems[n];
        int target = -1;

        for (u32 level_index = 0; level_index < world->levels.len; level_index++) {
            if (strcmp(world->levels.elems[level_index].iid, neighbor->level_iid) != 0) continue;

            target = (int)level_index;
            break;
        }

        if (target < 0)
            return orb_error_set(
                context->err, "neighbour %s is not a level in this project", neighbor->level_iid
            );

        neighbors[neighbor_offset + (u32)n] =
            (orb_neighbor_desc) {.level = (u16)target, .dir = (u8)neighbor->dir};
    }

    return true;
}

// The level sections: tilesets, levels, layers, neighbors, tiles, and cells.
// Sized by one counting pass, then filled by a second.
static bool world_cast(
    cast* context,
    const orb_ldtk* world,
    u32 first_tileset_sheet,
    orb_assets* assets
) {
    arena* scratch = context->scratch;
    world_sizes sizes;

    if (!world_size(context, world, &sizes)) return false;

    orb_tileset_desc* tilesets =
        orb_arena_push_array(scratch, orb_tileset_desc, world->tilesets.len);
    orb_level_desc* levels = orb_arena_push_array(scratch, orb_level_desc, world->levels.len);
    orb_layer_desc* layers = orb_arena_push_array(scratch, orb_layer_desc, sizes.layer_total);
    orb_neighbor_desc* neighbors =
        orb_arena_push_array(scratch, orb_neighbor_desc, sizes.neighbor_total);
    u16* tiles = orb_arena_push_array(scratch, u16, sizes.tile_total); // zeroed by the arena
    u8* cells = orb_arena_push_array(scratch, u8, sizes.cell_total);
    u64* level_ids = orb_arena_push_array(scratch, u64, world->levels.len);
    u64* layer_ids = orb_arena_push_array(scratch, u64, sizes.layer_total);

    if (!world_tilesets(context, world, first_tileset_sheet, tilesets)) return false;

    u32 layer_offset = 0, neighbor_offset = 0, tile_offset = 0, cell_offset = 0;

    for (u32 i = 0; i < world->levels.len; i++) {
        const orb_ldtk_level* level = &world->levels.elems[i];

        level_ids[i] = orb_asset_id(level->name, "");

        int dup = cast_dup_id(level_ids, i);

        if (dup >= 0)
            return orb_error_set(
                context->err, "levels %s and %s share a name", world->levels.elems[dup].name,
                level->name
            );

        levels[i] = (orb_level_desc) {
            .world_x = level->world_x,
            .world_y = level->world_y,
            .width = (u16)level->width,
            .height = (u16)level->height,
            .first_layer = (u16)layer_offset,
            .layer_count = (u16)level->layers.len,
            .first_neighbor = (u16)neighbor_offset,
            .neighbor_count = (u16)level->neighbors.len
        };

        for (u32 j = 0; j < level->layers.len; j++) {
            const orb_ldtk_layer* layer = &level->layers.elems[j];
            u32 idx = layer_offset + (u32)j;
            u32 area = (u32)layer->columns * layer->rows;
            u32 depth = sizes.sublayers[idx];

            layer_ids[idx] = orb_asset_id(layer->name, "");

            layers[idx] = (orb_layer_desc) {
                .tiles = tile_offset,
                .cells = layer->cells ? cell_offset : ORB_NO_INDEX,
                .parallax_x = layer->parallax_x,
                .parallax_y = layer->parallax_y,
                .tileset = layer->tileset >= 0 ? (u16)layer->tileset : 0,
                .grid = (u16)layer->grid,
                .columns = (u16)layer->columns,
                .rows = (u16)layer->rows,
                .offset_x = (i16)layer->offset_x,
                .offset_y = (i16)layer->offset_y,
                .sublayers = (u8)depth
            };

            if (depth) {
                usize mark = scratch->used;
                u8* plane_of = orb_arena_push_array(scratch, u8, area); // zeroed

                for (u32 tile_index = 0; tile_index < layer->tiles.len; tile_index++) {
                    const orb_ldtk_tile* tile = &layer->tiles.elems[tile_index];
                    u32 cell = (u32)tile->cell_y * (u32)layer->columns + tile->cell_x;
                    u32 plane = plane_of[cell]++;
                    u16 flags = (u16)((tile->flip & 1 ? ORB_TILE_FLIP_X : 0) |
                                      (tile->flip & 2 ? ORB_TILE_FLIP_Y : 0));

                    tiles[tile_offset + plane * area + cell] = (u16)((tile->id + 1) | flags);
                }

                scratch->used = mark;
            }

            if (layer->cells) memcpy(cells + cell_offset, layer->cells, area);

            tile_offset += area * depth;
            cell_offset += layer->cells ? area : 0;
        }

        layer_offset += (u32)level->layers.len;

        if (!world_neighbors(context, world, level, neighbor_offset, neighbors)) return false;

        neighbor_offset += (u32)level->neighbors.len;
    }

    if (!world_cast_entities(context, world, levels, assets)) return false;

    assets->tilesets = (orb_tileset_desc_span) {tilesets, (u32)world->tilesets.len};
    assets->levels = (orb_level_desc_span) {levels, (u32)world->levels.len};
    assets->layers = (orb_layer_desc_span) {layers, sizes.layer_total};
    assets->neighbors = (orb_neighbor_desc_span) {neighbors, sizes.neighbor_total};
    assets->tiles = (u16_span) {tiles, sizes.tile_total};
    assets->cells = (u8_span) {cells, sizes.cell_total};
    assets->level_ids = level_ids;
    assets->layer_ids = layer_ids;
    return true;
}
