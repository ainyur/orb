#include "ldtk.h"

#include "file.h"
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct ldtk {
    arena* scratch;
    const char* name;   // the file, for messages
    const char* level;  // identifier of the level being read, or nullptr
    const char* layer;  // identifier of the layer being read, or nullptr
    const char* entity; // identifier of the entity being read, or nullptr
    const char* field;  // identifier of the field being read, or nullptr
    orb_error* err;
} ldtk;

// Every failure names the file, then the level, layer, entity, and field being read.
static bool ldtk_fail(ldtk* reader, const char* fmt, ...) {
    char text[sizeof reader->err->text]; // orb_error is { char text[256]; }
    char where[sizeof reader->err->text] = "";
    va_list args;
    int n = 0;

    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    if (reader->level)
        n += snprintf(where + n, sizeof where - (usize)n, ": level %s", reader->level);
    if (reader->layer)
        n += snprintf(where + n, sizeof where - (usize)n, ": layer %s", reader->layer);
    if (reader->entity)
        n += snprintf(where + n, sizeof where - (usize)n, ": entity %s", reader->entity);
    if (reader->field)
        n += snprintf(where + n, sizeof where - (usize)n, ": field %s", reader->field);

    return orb_error_set(reader->err, "%s%s: %s", reader->name, where, text);
}

// Typed getters: a missing or mistyped key is an error naming it.
static bool ldtk_number(ldtk* reader, const orb_json* obj, const char* key, f64* out) {
    const orb_json* value = orb_json_get(obj, key);

    if (!value || value->kind != ORB_JSON_NUMBER)
        return ldtk_fail(reader, "%s is not a number", key);

    *out = value->num;
    return true;
}

static bool ldtk_int(ldtk* reader, const orb_json* obj, const char* key, int* out) {
    f64 value;

    if (!ldtk_number(reader, obj, key, &value)) return false;

    *out = (int)value;
    return true;
}

// null JSON -> nullptr
static bool ldtk_string(ldtk* reader, const orb_json* obj, const char* key, const char** out) {
    const orb_json* value = orb_json_get(obj, key);

    if (!value) return ldtk_fail(reader, "missing %s", key);
    if (value->kind == ORB_JSON_NULL) {
        *out = nullptr;
        return true;
    }
    if (value->kind != ORB_JSON_STRING) return ldtk_fail(reader, "%s is not a string", key);

    *out = value->str;
    return true;
}

// null JSON -> nullptr
static bool ldtk_array(ldtk* reader, const orb_json* obj, const char* key, const orb_json** out) {
    const orb_json* value = orb_json_get(obj, key);

    if (!value) return ldtk_fail(reader, "missing %s", key);
    if (value->kind == ORB_JSON_NULL) {
        *out = nullptr;
        return true;
    }
    if (value->kind != ORB_JSON_ARRAY) return ldtk_fail(reader, "%s is not an array", key);

    *out = value;
    return true;
}

static bool ldtk_bool(ldtk* reader, const orb_json* obj, const char* key, bool* out) {
    const orb_json* value = orb_json_get(obj, key);

    if (!value || value->kind != ORB_JSON_BOOL) return ldtk_fail(reader, "%s is not a bool", key);

    *out = value->boolean;
    return true;
}

// null JSON -> -1
static bool ldtk_optional_int(ldtk* reader, const orb_json* obj, const char* key, int* out) {
    const orb_json* value = orb_json_get(obj, key);

    if (!value) return ldtk_fail(reader, "missing %s", key);
    if (value->kind == ORB_JSON_NULL) {
        *out = -1;
        return true;
    }
    if (value->kind != ORB_JSON_NUMBER) return ldtk_fail(reader, "%s is not a number or null", key);

    *out = (int)value->num;
    return true;
}

// "Int", "Array<Int>", "LocalEnum.X"... to a kind. Color, Tile, and FilePath set skip.
static bool ldtk_field_kind(
    ldtk* reader,
    const char* type,
    orb_field_kind* out,
    bool* is_array,
    bool* skip
) {
    constexpr char prefix[] = "Array<";
    usize n = strlen(type);
    char inner[64];

    *is_array =
        strncmp(type, prefix, sizeof prefix - 1) == 0 && n > sizeof prefix && type[n - 1] == '>';
    *skip = false;

    if (*is_array)
        snprintf(inner, sizeof inner, "%.*s", (int)(n - sizeof prefix), type + sizeof prefix - 1);
    else
        snprintf(inner, sizeof inner, "%s", type);

    if (strcmp(inner, "Int") == 0)
        *out = ORB_FIELD_INT;
    else if (strcmp(inner, "Float") == 0)
        *out = ORB_FIELD_FLOAT;
    else if (strcmp(inner, "Bool") == 0)
        *out = ORB_FIELD_BOOL;
    else if (
        strcmp(inner, "String") == 0 || strcmp(inner, "Multilines") == 0 ||
        strncmp(inner, "LocalEnum.", 10) == 0 || strncmp(inner, "ExternEnum.", 11) == 0
    )
        *out = ORB_FIELD_STRING;
    else if (strcmp(inner, "Point") == 0)
        *out = ORB_FIELD_POINT;
    else if (strcmp(inner, "EntityRef") == 0)
        *out = ORB_FIELD_REF;
    else if (
        strcmp(inner, "Color") == 0 || strcmp(inner, "Tile") == 0 || strcmp(inner, "FilePath") == 0
    )
        *skip = true;
    else
        return ldtk_fail(reader, "field type %s is not supported", type);

    return true;
}

// Where a layer's cells sit in the world, for point fields.
typedef struct ldtk_geometry {
    int world_x, world_y, offset_x, offset_y, grid;
} ldtk_geometry;

// One element: a point's cell becomes world pixels; a ref keeps its iid for the cast.
static bool ldtk_value(
    ldtk* reader,
    const orb_json* value,
    orb_field_kind kind,
    const ldtk_geometry* geometry,
    orb_ldtk_value* out
) {
    switch (kind) {
    case ORB_FIELD_INT:
        if (value->kind != ORB_JSON_NUMBER) return ldtk_fail(reader, "is not a number");
        out->integer = (i32)value->num;
        return true;
    case ORB_FIELD_FLOAT:
        if (value->kind != ORB_JSON_NUMBER) return ldtk_fail(reader, "is not a number");
        out->number = (f32)value->num;
        return true;
    case ORB_FIELD_BOOL:
        if (value->kind != ORB_JSON_BOOL) return ldtk_fail(reader, "is not a bool");
        out->boolean = value->boolean;
        return true;
    case ORB_FIELD_STRING:
        if (value->kind != ORB_JSON_STRING) return ldtk_fail(reader, "is not a string");
        out->string = value->str;
        return true;
    case ORB_FIELD_POINT: {
        int cx, cy;

        if (value->kind != ORB_JSON_OBJECT || !geometry) return ldtk_fail(reader, "is not a point");
        if (!ldtk_int(reader, value, "cx", &cx) || !ldtk_int(reader, value, "cy", &cy))
            return false;

        out->point = (orb_vec2) {
            geometry->world_x + geometry->offset_x + cx * geometry->grid,
            geometry->world_y + geometry->offset_y + cy * geometry->grid
        };
        return true;
    }
    case ORB_FIELD_REF:
        if (value->kind != ORB_JSON_OBJECT) return ldtk_fail(reader, "is not an entity ref");
        return ldtk_string(reader, value, "entityIid", &out->string) && out->string;
    }

    return ldtk_fail(reader, "has an unknown kind");
}

// fieldInstances (__value) or fieldDefs (defaultOverride, an {id, params} object whose
// params[0] is the value). A null value, a skipped kind, and an instance field the
// definition does not declare write nothing. names, when given, collects every identifier.
static bool ldtk_fields(
    ldtk* reader,
    const orb_json* arr,
    bool defaults,
    const ldtk_geometry* geometry,
    const orb_ldtk_entity_def* def,
    orb_ldtk_field_list* out,
    field_names_list* names
) {
    u32 total = arr ? (u32)arr->count : 0;
    orb_ldtk_field_list fields = {
        .elems = orb_arena_push_array(reader->scratch, orb_ldtk_field, total), .cap = total
    };
    field_names_list name_list = {};

    if (names)
        name_list = (field_names_list) {
            .elems = orb_arena_push_array(reader->scratch, const char*, total), .cap = total
        };

    for (const orb_json* item = arr ? arr->first : nullptr; item; item = item->next) {
        const char* identifier;
        if (!ldtk_string(reader, item, defaults ? "identifier" : "__identifier", &identifier) ||
            !identifier)
            return ldtk_fail(reader, "a field has no identifier");

        reader->field = identifier;

        if (names) (void)push(reader->scratch, &name_list, identifier);

        const char* type;
        if (!ldtk_string(reader, item, "__type", &type) || !type)
            return ldtk_fail(reader, "has no type");

        orb_field_kind kind = ORB_FIELD_INT;
        bool is_array, skip;
        if (!ldtk_field_kind(reader, type, &kind, &is_array, &skip)) return false;
        if (skip) continue;

        if (def) {
            bool declared = false;

            for (u32 i = 0; i < def->field_names.len && !declared; i++)
                declared = strcmp(def->field_names.elems[i], identifier) == 0;

            if (!declared) continue;
        }

        const orb_json* value = orb_json_get(item, defaults ? "defaultOverride" : "__value");
        if (!value) return ldtk_fail(reader, "has no value");
        if (value->kind == ORB_JSON_NULL) continue;

        if (defaults) {
            const orb_json* params;

            if (value->kind != ORB_JSON_OBJECT)
                return ldtk_fail(reader, "default is not an object");
            if (!ldtk_array(reader, value, "params", &params)) return false;
            if (!params || params->count == 0) continue;

            value = params->first;
            is_array = false;
        }

        orb_ldtk_field field = {};
        int n = 1;

        if (is_array) {
            if (value->kind != ORB_JSON_ARRAY) return ldtk_fail(reader, "is not an array");

            n = value->count;
        }

        field.name = identifier;
        field.kind = kind;
        field.values = (orb_ldtk_value_slice) {
            .elems = orb_arena_push_array(reader->scratch, orb_ldtk_value, n), .len = (u32)n
        };

        if (is_array) {
            int k = 0;

            for (const orb_json* element = value->first; element; element = element->next, k++) {
                if (element->kind == ORB_JSON_NULL) return ldtk_fail(reader, "null element");
                if (!ldtk_value(reader, element, kind, geometry, &field.values.elems[k]))
                    return false;
            }
        } else if (!ldtk_value(reader, value, kind, geometry, &field.values.elems[0]))
            return false;

        (void)push(reader->scratch, &fields, field);
    }

    reader->field = nullptr;
    *out = fields;

    if (names) *names = name_list;

    return true;
}

// "n" "s" "e" "w" "ne" "nw" "se" "sw" "<" ">" "o" to the compass, ordering, and overlap values.
static bool ldtk_dir(ldtk* reader, const char* code, orb_level_dir* out) {
    static const struct {
        const char* code;
        orb_level_dir dir;
    } table[] = {
        {"n", ORB_LEVEL_N},      {"s", ORB_LEVEL_S},       {"e", ORB_LEVEL_E},
        {"w", ORB_LEVEL_W},      {"ne", ORB_LEVEL_NE},     {"nw", ORB_LEVEL_NW},
        {"se", ORB_LEVEL_SE},    {"sw", ORB_LEVEL_SW},     {"<", ORB_LEVEL_LOWER},
        {">", ORB_LEVEL_HIGHER}, {"o", ORB_LEVEL_OVERLAP},
    };

    for (usize i = 0; i < sizeof table / sizeof *table; i++) {
        if (strcmp(code, table[i].code) != 0) continue;

        *out = table[i].dir;
        return true;
    }

    return ldtk_fail(reader, "unknown neighbour dir %s", code);
}

// defs.tilesets: one orb_ldtk_tileset per definition with an .aseprite relPath. A
// definition with no relPath, or one that does not end in .aseprite, is kept in
// skipped instead of failing outright, so only a layer that actually uses it fails.
static bool ldtk_defs_tilesets(ldtk* reader, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(reader, defs, "tilesets", &arr)) return false;

    u32 total = arr ? (u32)arr->count : 0;
    orb_ldtk_tileset_list tilesets = {
        .elems = orb_arena_push_array(reader->scratch, orb_ldtk_tileset, total), .cap = total
    };
    orb_ldtk_skipped_list skipped = {
        .elems = orb_arena_push_array(reader->scratch, orb_ldtk_skipped, total), .cap = total
    };

    for (const orb_json* entry = arr ? arr->first : nullptr; entry; entry = entry->next) {
        const char* path;
        if (!ldtk_string(reader, entry, "relPath", &path)) return false;

        int uid;
        if (!ldtk_int(reader, entry, "uid", &uid)) return false;

        constexpr char suffix[] = ".aseprite";
        constexpr usize suffix_len = sizeof suffix - 1;
        usize plen = path ? strlen(path) : 0;
        bool usable = path && plen >= suffix_len && strcmp(path + plen - suffix_len, suffix) == 0;

        if (!usable) {
            (void)push(reader->scratch, &skipped, ((orb_ldtk_skipped) {.uid = uid, .path = path}));
            continue;
        }

        int grid, spacing, padding, columns, rows, width, height;
        if (!ldtk_int(reader, entry, "tileGridSize", &grid)) return false;
        if (!ldtk_int(reader, entry, "spacing", &spacing)) return false;
        if (!ldtk_int(reader, entry, "padding", &padding)) return false;
        if (!ldtk_int(reader, entry, "__cWid", &columns)) return false;
        if (!ldtk_int(reader, entry, "__cHei", &rows)) return false;
        if (columns > 65535 || rows > 65535)
            return ldtk_fail(
                reader, "tileset %s: __cWid/__cHei %dx%d is larger than 65535x65535", path, columns,
                rows
            );
        if (!ldtk_int(reader, entry, "pxWid", &width)) return false;
        if (!ldtk_int(reader, entry, "pxHei", &height)) return false;

        (void)push(
            reader->scratch, &tilesets,
            ((orb_ldtk_tileset) {
                .path = path,
                .uid = uid,
                .grid = grid,
                .spacing = spacing,
                .padding = padding,
                .columns = columns,
                .rows = rows,
                .width = width,
                .height = height
            })
        );
    }

    out->tilesets = tilesets;
    out->skipped = skipped;
    return true;
}

// defs.layers: one orb_ldtk_layer_def per definition.
static bool ldtk_defs_layers(ldtk* reader, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(reader, defs, "layers", &arr)) return false;

    int count = arr ? arr->count : 0;
    orb_ldtk_layer_def* layer_defs =
        orb_arena_push_array(reader->scratch, orb_ldtk_layer_def, count);
    int i = 0;

    for (const orb_json* entry = arr ? arr->first : nullptr; entry; entry = entry->next, i++) {
        const char* identifier;
        if (!ldtk_string(reader, entry, "identifier", &identifier)) return false;

        int uid;
        if (!ldtk_int(reader, entry, "uid", &uid)) return false;

        int tileset_uid;
        if (!ldtk_optional_int(reader, entry, "tilesetDefUid", &tileset_uid)) return false;

        f64 px, py;
        if (!ldtk_number(reader, entry, "parallaxFactorX", &px)) return false;
        if (!ldtk_number(reader, entry, "parallaxFactorY", &py)) return false;

        bool scaling = false;
        if (!ldtk_bool(reader, entry, "parallaxScaling", &scaling)) return false;

        if (scaling && (px != 0 || py != 0))
            return ldtk_fail(
                reader, "layer %s: parallaxScaling is not supported with a parallax factor",
                identifier
            );

        layer_defs[i] = (orb_ldtk_layer_def) {
            .name = identifier,
            .uid = uid,
            .tileset_uid = tileset_uid,
            .parallax_x = (f32)px,
            .parallax_y = (f32)py,
            .scaling = scaling
        };
    }

    out->layer_defs = (orb_ldtk_layer_def_slice) {.elems = layer_defs, .len = (u32)count};
    return true;
}

// defs.entities: one orb_ldtk_entity_def per definition, with its non-null defaults.
static bool ldtk_defs_entities(ldtk* reader, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(reader, defs, "entities", &arr)) return false;

    int count = arr ? arr->count : 0;
    orb_ldtk_entity_def* entity_defs =
        orb_arena_push_array(reader->scratch, orb_ldtk_entity_def, count);
    int i = 0;

    for (const orb_json* entry = arr ? arr->first : nullptr; entry; entry = entry->next, i++) {
        const char* identifier;
        if (!ldtk_string(reader, entry, "identifier", &identifier) || !identifier)
            return ldtk_fail(reader, "an entity definition has no identifier");

        reader->entity = identifier;

        int uid, width, height;
        if (!ldtk_int(reader, entry, "uid", &uid)) return false;
        if (!ldtk_int(reader, entry, "width", &width)) return false;
        if (!ldtk_int(reader, entry, "height", &height)) return false;

        const orb_json* field_defs;
        if (!ldtk_array(reader, entry, "fieldDefs", &field_defs)) return false;

        orb_ldtk_entity_def* def = &entity_defs[i];

        def->name = identifier;
        def->uid = uid;
        def->width = width;
        def->height = height;

        if (!ldtk_fields(
                reader, field_defs, true, nullptr, nullptr, &def->fields, &def->field_names
            ))
            return false;

        reader->entity = nullptr;
    }

    out->entity_defs = (orb_ldtk_entity_def_slice) {.elems = entity_defs, .len = (u32)count};
    return true;
}

// Index of the entity definition with this uid, or -1.
static int ldtk_entity_def_by_uid(const orb_ldtk* project, int uid) {
    for (u32 i = 0; i < project->entity_defs.len; i++)
        if (project->entity_defs.elems[i].uid == uid) return (int)i;

    return -1;
}

// gridTiles then autoLayerTiles, in that order, into one array.
static bool ldtk_tiles(
    ldtk* reader,
    const orb_json* inst,
    int grid,
    int columns,
    int rows,
    int tileset,
    u64 tileset_slots,
    orb_ldtk_tile_span* out
) {
    const orb_json *grid_tiles, *auto_tiles;

    if (!ldtk_array(reader, inst, "gridTiles", &grid_tiles)) return false;
    if (!ldtk_array(reader, inst, "autoLayerTiles", &auto_tiles)) return false;

    int gt_count = grid_tiles ? grid_tiles->count : 0;
    int at_count = auto_tiles ? auto_tiles->count : 0;
    int count = gt_count + at_count;

    if (count > 0 && tileset < 0) return ldtk_fail(reader, "tiles on a layer with no tileset");

    orb_ldtk_tile* tiles = orb_arena_push_array(reader->scratch, orb_ldtk_tile, count);
    int tile_index = 0;

    for (int pass = 0; pass < 2; pass++) {
        const orb_json* arr = pass == 0 ? grid_tiles : auto_tiles;

        for (const orb_json* entry = arr ? arr->first : nullptr; entry;
             entry = entry->next, tile_index++) {
            const orb_json* px;
            if (!ldtk_array(reader, entry, "px", &px)) return false;
            if (!px || px->count != 2) return ldtk_fail(reader, "px does not hold two values");

            const orb_json* px0 = px->first;
            const orb_json* px1 = px0->next;

            if (px0->kind != ORB_JSON_NUMBER || px1->kind != ORB_JSON_NUMBER)
                return ldtk_fail(reader, "px does not hold two numbers");

            int px_x = (int)px0->num, px_y = (int)px1->num;
            int cell_x = px_x / grid, cell_y = px_y / grid;

            if (px_x < 0 || px_y < 0 || cell_x >= columns || cell_y >= rows)
                return ldtk_fail(reader, "tile at px (%d, %d) is outside the layer", px_x, px_y);

            int id;
            if (!ldtk_int(reader, entry, "t", &id)) return false;
            if (id < 0 || id > (int)ORB_MAX_TILE_ID)
                return ldtk_fail(reader, "tile id %d does not fit", id);
            if ((u64)id >= tileset_slots)
                return ldtk_fail(reader, "tile id %d is past the tileset", id);

            int flip;
            if (!ldtk_int(reader, entry, "f", &flip)) return false;

            f64 alpha;
            if (!ldtk_number(reader, entry, "a", &alpha)) return false;
            if (alpha != 1) return ldtk_fail(reader, "tile alpha %g is not 1", alpha);

            tiles[tile_index] = (orb_ldtk_tile) {
                .cell_x = (u16)cell_x, .cell_y = (u16)cell_y, .id = (u16)id, .flip = (u8)flip
            };
        }
    }

    *out = (orb_ldtk_tile_span) {tiles, (u32)count};
    return true;
}

// intGridCsv: columns * rows values, each 0..255.
static bool ldtk_cells(ldtk* reader, const orb_json* inst, int columns, int rows, const u8** out) {
    const orb_json* csv;

    if (!ldtk_array(reader, inst, "intGridCsv", &csv)) return false;

    u64 cell_count = (u64)columns * rows;
    int csv_count = csv ? csv->count : 0;

    if ((u64)csv_count != cell_count)
        return ldtk_fail(
            reader, "intGridCsv holds %d values for %llu cells", csv_count,
            (unsigned long long)cell_count
        );

    u8* cells = orb_arena_push_array(reader->scratch, u8, cell_count);
    int i = 0;

    for (const orb_json* value = csv ? csv->first : nullptr; value; value = value->next, i++) {
        if (value->kind != ORB_JSON_NUMBER)
            return ldtk_fail(reader, "intGridCsv value is not a number");
        if (value->num < 0 || value->num > 255)
            return ldtk_fail(reader, "intGrid value %g exceeds 255", value->num);

        cells[i] = (u8)value->num;
    }

    *out = cells;
    return true;
}

// The layer definition with this uid, or nullptr.
static const orb_ldtk_layer_def* ldtk_layer_def_by_uid(const orb_ldtk* project, int uid) {
    for (u32 i = 0; i < project->layer_defs.len; i++)
        if (project->layer_defs.elems[i].uid == uid) return &project->layer_defs.elems[i];

    return nullptr;
}

// Index of the tileset with this uid, or -1.
static int ldtk_tileset_by_uid(const orb_ldtk* project, int uid) {
    for (u32 i = 0; i < project->tilesets.len; i++)
        if (project->tilesets.elems[i].uid == uid) return (int)i;

    return -1;
}

// The skipped tileset definition with this uid, or nullptr.
static const orb_ldtk_skipped* ldtk_skipped_by_uid(const orb_ldtk* project, int uid) {
    for (u32 i = 0; i < project->skipped.len; i++)
        if (project->skipped.elems[i].uid == uid) return &project->skipped.elems[i];

    return nullptr;
}

// One layer instance: identifier, type, its definition, geometry, cells (IntGrid), and tiles.
static bool ldtk_layer(
    ldtk* reader,
    const orb_ldtk* project,
    const orb_json* inst,
    orb_ldtk_layer* out
) {
    const char* identifier;
    if (!ldtk_string(reader, inst, "__identifier", &identifier)) return false;

    reader->layer = identifier;

    const char* type;
    if (!ldtk_string(reader, inst, "__type", &type)) return false;

    int def_uid;
    if (!ldtk_int(reader, inst, "layerDefUid", &def_uid)) return false;

    const orb_ldtk_layer_def* def = ldtk_layer_def_by_uid(project, def_uid);
    if (!def) return ldtk_fail(reader, "layerDefUid %d has no definition", def_uid);

    int grid, columns, rows, offset_x, offset_y;
    if (!ldtk_int(reader, inst, "__gridSize", &grid)) return false;
    if (!ldtk_int(reader, inst, "__cWid", &columns)) return false;
    if (!ldtk_int(reader, inst, "__cHei", &rows)) return false;
    if (grid < 1) return ldtk_fail(reader, "__gridSize %d is not positive", grid);
    if (columns < 1 || rows < 1)
        return ldtk_fail(reader, "__cWid/__cHei %dx%d is empty", columns, rows);
    if (columns > 65535 || rows > 65535)
        return ldtk_fail(reader, "__cWid/__cHei %dx%d is larger than 65535x65535", columns, rows);
    if (!ldtk_int(reader, inst, "__pxTotalOffsetX", &offset_x)) return false;
    if (!ldtk_int(reader, inst, "__pxTotalOffsetY", &offset_y)) return false;

    f64 opacity;
    if (!ldtk_number(reader, inst, "__opacity", &opacity)) return false;
    if (opacity != 1) return ldtk_fail(reader, "opacity %g is not 1", opacity);

    int tuid;
    if (!ldtk_optional_int(reader, inst, "__tilesetDefUid", &tuid)) return false;

    int tileset = -1;
    u64 tileset_slots = 0;

    if (tuid >= 0) {
        tileset = ldtk_tileset_by_uid(project, tuid);

        if (tileset < 0) {
            const orb_ldtk_skipped* skip = ldtk_skipped_by_uid(project, tuid);

            if (skip && skip->path)
                return ldtk_fail(reader, "tileset %s must end in .aseprite", skip->path);
            if (skip) return ldtk_fail(reader, "tileset uid %d has no image path", tuid);
            return ldtk_fail(reader, "tileset uid %d has no definition", tuid);
        }

        tileset_slots =
            (u64)project->tilesets.elems[tileset].columns * project->tilesets.elems[tileset].rows;
    }

    const u8* cells = nullptr;
    if (strcmp(type, "IntGrid") == 0 && !ldtk_cells(reader, inst, columns, rows, &cells))
        return false;

    orb_ldtk_tile_span tiles = {};
    if (!ldtk_tiles(reader, inst, grid, columns, rows, tileset, tileset_slots, &tiles))
        return false;

    out->name = identifier;
    out->grid = grid;
    out->columns = columns;
    out->rows = rows;
    out->offset_x = offset_x;
    out->offset_y = offset_y;
    out->parallax_x = def->parallax_x;
    out->parallax_y = def->parallax_y;
    out->tileset = tileset;
    out->cells = cells;
    out->tiles = tiles;

    reader->layer = nullptr;
    return true;
}

// One Entities layer instance: its entityInstances into out, in array order.
static bool ldtk_entities(
    ldtk* reader,
    const orb_ldtk* project,
    const orb_ldtk_level* level,
    const orb_json* inst,
    orb_ldtk_instance* out
) {
    const char* identifier;
    if (!ldtk_string(reader, inst, "__identifier", &identifier)) return false;

    reader->layer = identifier;

    int grid, offset_x, offset_y;
    if (!ldtk_int(reader, inst, "__gridSize", &grid)) return false;
    if (!ldtk_int(reader, inst, "__pxTotalOffsetX", &offset_x)) return false;
    if (!ldtk_int(reader, inst, "__pxTotalOffsetY", &offset_y)) return false;

    const orb_json* arr;
    if (!ldtk_array(reader, inst, "entityInstances", &arr)) return false;

    ldtk_geometry geometry = {level->world_x, level->world_y, offset_x, offset_y, grid};
    int i = 0;

    for (const orb_json* entry = arr ? arr->first : nullptr; entry; entry = entry->next, i++) {
        const char* type_name;
        if (!ldtk_string(reader, entry, "__identifier", &type_name) || !type_name)
            return ldtk_fail(reader, "an entity has no identifier");

        reader->entity = type_name;

        const char* iid;
        if (!ldtk_string(reader, entry, "iid", &iid) || !iid)
            return ldtk_fail(reader, "has no iid");

        int def_uid;
        if (!ldtk_int(reader, entry, "defUid", &def_uid)) return false;

        int def = ldtk_entity_def_by_uid(project, def_uid);
        if (def < 0) return ldtk_fail(reader, "defUid %d has no definition", def_uid);

        const orb_json* px;
        if (!ldtk_array(reader, entry, "px", &px)) return false;
        if (!px || px->count != 2 || px->first->kind != ORB_JSON_NUMBER ||
            px->first->next->kind != ORB_JSON_NUMBER)
            return ldtk_fail(reader, "px does not hold two numbers");

        int width, height;
        if (!ldtk_int(reader, entry, "width", &width)) return false;
        if (!ldtk_int(reader, entry, "height", &height)) return false;

        const orb_json* field_instances;
        if (!ldtk_array(reader, entry, "fieldInstances", &field_instances)) return false;

        orb_ldtk_instance* instance = &out[i];

        instance->iid = iid;
        instance->def = def;
        instance->x = level->world_x + (int)px->first->num;
        instance->y = level->world_y + (int)px->first->next->num;
        instance->width = width;
        instance->height = height;

        if (!ldtk_fields(
                reader, field_instances, false, &geometry, &project->entity_defs.elems[def],
                &instance->fields, nullptr
            ))
            return false;

        reader->entity = nullptr;
    }

    reader->layer = nullptr;
    return true;
}

// layerInstances, top first in LDtk: non-Entities instances fill the layers bottom to top,
// and Entities instances fill the level's instances the same way.
static bool ldtk_layers(
    ldtk* reader,
    const orb_ldtk* project,
    orb_ldtk_level* level,
    const orb_json* instances
) {
    int count = 0, instance_count = 0;

    for (const orb_json* inst = instances->first; inst; inst = inst->next) {
        const orb_json* type = orb_json_get(inst, "__type");
        bool entities = type && type->kind == ORB_JSON_STRING && strcmp(type->str, "Entities") == 0;

        if (!entities) {
            count++;
            continue;
        }

        const orb_json* arr = orb_json_get(inst, "entityInstances");

        if (arr && arr->kind == ORB_JSON_ARRAY) instance_count += arr->count;
    }

    orb_ldtk_layer* layers = orb_arena_push_array(reader->scratch, orb_ldtk_layer, count);
    orb_ldtk_instance* entities =
        orb_arena_push_array(reader->scratch, orb_ldtk_instance, instance_count);
    int i = count, entity_index = instance_count;

    for (const orb_json* inst = instances->first; inst; inst = inst->next) {
        const char* type;
        if (!ldtk_string(reader, inst, "__type", &type)) return false;

        if (strcmp(type, "Entities") == 0) {
            const orb_json* arr = orb_json_get(inst, "entityInstances");
            int n = arr && arr->kind == ORB_JSON_ARRAY ? arr->count : 0;

            entity_index -= n;
            if (!ldtk_entities(reader, project, level, inst, &entities[entity_index])) return false;
            continue;
        }

        i--;
        if (!ldtk_layer(reader, project, inst, &layers[i])) return false;
    }

    level->layers = (orb_ldtk_layer_slice) {.elems = layers, .len = (u32)count};
    level->instances = (orb_ldtk_instance_slice) {.elems = entities, .len = (u32)instance_count};
    return true;
}

// identifier, iid, world placement, background (unsupported), and neighbours.
static bool ldtk_level_head(ldtk* reader, const orb_json* level_json, orb_ldtk_level* out) {
    const char* identifier;
    if (!ldtk_string(reader, level_json, "identifier", &identifier)) return false;

    reader->level = identifier;

    const char* iid;
    if (!ldtk_string(reader, level_json, "iid", &iid)) return false;

    int world_x, world_y, width, height;
    if (!ldtk_int(reader, level_json, "worldX", &world_x)) return false;
    if (!ldtk_int(reader, level_json, "worldY", &world_y)) return false;
    if (!ldtk_int(reader, level_json, "pxWid", &width)) return false;
    if (!ldtk_int(reader, level_json, "pxHei", &height)) return false;

    const char* background;
    if (!ldtk_string(reader, level_json, "bgRelPath", &background)) return false;
    if (background) return ldtk_fail(reader, "background images are not supported");

    const orb_json* neighbours;
    if (!ldtk_array(reader, level_json, "__neighbours", &neighbours)) return false;

    int neighbor_count = neighbours ? neighbours->count : 0;
    orb_ldtk_neighbor* neighbors =
        orb_arena_push_array(reader->scratch, orb_ldtk_neighbor, neighbor_count);
    int i = 0;

    for (const orb_json* entry = neighbours ? neighbours->first : nullptr; entry;
         entry = entry->next, i++) {
        const char* level_iid;
        if (!ldtk_string(reader, entry, "levelIid", &level_iid)) return false;

        const char* dir_s;
        if (!ldtk_string(reader, entry, "dir", &dir_s)) return false;

        orb_level_dir dir = ORB_LEVEL_N;
        if (!ldtk_dir(reader, dir_s, &dir)) return false;

        neighbors[i] = (orb_ldtk_neighbor) {.level_iid = level_iid, .dir = dir};
    }

    out->name = identifier;
    out->iid = iid;
    out->world_x = world_x;
    out->world_y = world_y;
    out->width = width;
    out->height = height;
    out->neighbors = (orb_ldtk_neighbor_slice) {.elems = neighbors, .len = (u32)neighbor_count};

    return true;
}

bool orb_ldtk_parse(arena* scratch, u8_span text, const char* name, orb_ldtk* out, orb_error* err) {
    ldtk reader = {.scratch = scratch, .name = name, .err = err};
    orb_json* root = orb_json_parse(scratch, (const char*)text.elems, text.len, err);

    if (!root) return ldtk_fail(&reader, "%s", err->text);

    const char* version;
    if (!ldtk_string(&reader, root, "jsonVersion", &version)) return false;
    if (strncmp(version, "1.", 2) != 0)
        return ldtk_fail(&reader, "jsonVersion %s is not 1.x", version);

    const orb_json* worlds;
    if (!ldtk_array(&reader, root, "worlds", &worlds)) return false;
    if (worlds && worlds->count != 0)
        return ldtk_fail(&reader, "multi-world projects (worlds) are not supported");

    const orb_json* defs = orb_json_get(root, "defs");
    if (!defs) return ldtk_fail(&reader, "missing defs");

    if (!ldtk_defs_tilesets(&reader, defs, out)) return false;
    if (!ldtk_defs_layers(&reader, defs, out)) return false;
    if (!ldtk_defs_entities(&reader, defs, out)) return false;

    const orb_json* levels;
    if (!ldtk_array(&reader, root, "levels", &levels)) return false;

    int level_count = levels ? levels->count : 0;
    orb_ldtk_level* levels_out = orb_arena_push_array(scratch, orb_ldtk_level, level_count);
    int i = 0;

    for (const orb_json* level_json = levels ? levels->first : nullptr; level_json;
         level_json = level_json->next, i++) {
        if (!ldtk_level_head(&reader, level_json, &levels_out[i])) return false;

        const orb_json* instances;
        if (!ldtk_array(&reader, level_json, "layerInstances", &instances)) return false;

        if (instances) {
            if (!ldtk_layers(&reader, out, &levels_out[i], instances)) return false;
        } else {
            const char* external;
            if (!ldtk_string(&reader, level_json, "externalRelPath", &external)) return false;
            if (!external)
                return ldtk_fail(&reader, "level has neither layerInstances nor externalRelPath");

            levels_out[i].external_path = external;
        }

        reader.level = nullptr;
    }

    out->levels = (orb_ldtk_level_slice) {.elems = levels_out, .len = (u32)level_count};
    return true;
}

bool orb_ldtk_parse_level(
    arena* scratch,
    u8_span text,
    const char* name,
    const orb_ldtk* project,
    orb_ldtk_level* level,
    orb_error* err
) {
    ldtk reader = {.scratch = scratch, .name = name, .level = level->name, .err = err};
    orb_json* root = orb_json_parse(scratch, (const char*)text.elems, text.len, err);

    if (!root) return ldtk_fail(&reader, "%s", err->text);

    const orb_json* instances;
    if (!ldtk_array(&reader, root, "layerInstances", &instances)) return false;
    if (!instances) return ldtk_fail(&reader, "external level has no layerInstances");

    return ldtk_layers(&reader, project, level, instances);
}
