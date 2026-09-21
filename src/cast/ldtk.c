#include "ldtk.h"

#include "file.h"
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct ldtk {
    orb_arena* a;
    const char* name;   // the file, for messages
    const char* level;  // identifier of the level being read, or nullptr
    const char* layer;  // identifier of the layer being read, or nullptr
    const char* entity; // identifier of the entity being read, or nullptr
    const char* field;  // identifier of the field being read, or nullptr
    orb_error* err;
} ldtk;

// Every failure names the file, then the level, layer, entity, and field being read.
static bool ldtk_fail(ldtk* l, const char* fmt, ...) {
    char text[sizeof l->err->text]; // orb_error is { char text[256]; }
    char where[sizeof l->err->text] = "";
    va_list args;
    int n = 0;

    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);

    if (l->level) n += snprintf(where + n, sizeof where - (size_t)n, ": level %s", l->level);
    if (l->layer) n += snprintf(where + n, sizeof where - (size_t)n, ": layer %s", l->layer);
    if (l->entity) n += snprintf(where + n, sizeof where - (size_t)n, ": entity %s", l->entity);
    if (l->field) n += snprintf(where + n, sizeof where - (size_t)n, ": field %s", l->field);

    return orb_error_set(l->err, "%s%s: %s", l->name, where, text);
}

// Typed getters: a missing or mistyped key is an error naming it.
static bool ldtk_number(ldtk* l, const orb_json* obj, const char* key, double* out) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v || v->kind != ORB_JSON_NUMBER) return ldtk_fail(l, "%s is not a number", key);

    *out = v->num;
    return true;
}

static bool ldtk_int(ldtk* l, const orb_json* obj, const char* key, int* out) {
    double v;

    if (!ldtk_number(l, obj, key, &v)) return false;

    *out = (int)v;
    return true;
}

// null JSON -> nullptr
static bool ldtk_string(ldtk* l, const orb_json* obj, const char* key, const char** out) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v) return ldtk_fail(l, "missing %s", key);
    if (v->kind == ORB_JSON_NULL) {
        *out = nullptr;
        return true;
    }
    if (v->kind != ORB_JSON_STRING) return ldtk_fail(l, "%s is not a string", key);

    *out = v->str;
    return true;
}

// null JSON -> nullptr
static bool ldtk_array(ldtk* l, const orb_json* obj, const char* key, const orb_json** out) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v) return ldtk_fail(l, "missing %s", key);
    if (v->kind == ORB_JSON_NULL) {
        *out = nullptr;
        return true;
    }
    if (v->kind != ORB_JSON_ARRAY) return ldtk_fail(l, "%s is not an array", key);

    *out = v;
    return true;
}

static bool ldtk_bool(ldtk* l, const orb_json* obj, const char* key, bool* out) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v || v->kind != ORB_JSON_BOOL) return ldtk_fail(l, "%s is not a bool", key);

    *out = v->boolean;
    return true;
}

// null JSON -> -1
static bool ldtk_optional_int(ldtk* l, const orb_json* obj, const char* key, int* out) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v) return ldtk_fail(l, "missing %s", key);
    if (v->kind == ORB_JSON_NULL) {
        *out = -1;
        return true;
    }
    if (v->kind != ORB_JSON_NUMBER) return ldtk_fail(l, "%s is not a number or null", key);

    *out = (int)v->num;
    return true;
}

// "Int", "Array<Int>", "LocalEnum.X"... to a kind. Color, Tile, and FilePath set skip.
static bool ldtk_field_kind(
    ldtk* l,
    const char* type,
    orb_field_kind* out,
    bool* is_array,
    bool* skip
) {
    constexpr char prefix[] = "Array<";
    size_t n = strlen(type);
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
        return ldtk_fail(l, "field type %s is not supported", type);

    return true;
}

// Where a layer's cells sit in the world, for point fields.
typedef struct ldtk_geometry {
    int world_x, world_y, offset_x, offset_y, grid;
} ldtk_geometry;

// One element: a point's cell becomes world pixels; a ref keeps its iid for the cast.
static bool ldtk_value(
    ldtk* l,
    const orb_json* v,
    orb_field_kind kind,
    const ldtk_geometry* g,
    orb_ldtk_value* out
) {
    switch (kind) {
    case ORB_FIELD_INT:
        if (v->kind != ORB_JSON_NUMBER) return ldtk_fail(l, "is not a number");
        out->i = (int32_t)v->num;
        return true;
    case ORB_FIELD_FLOAT:
        if (v->kind != ORB_JSON_NUMBER) return ldtk_fail(l, "is not a number");
        out->f = (float)v->num;
        return true;
    case ORB_FIELD_BOOL:
        if (v->kind != ORB_JSON_BOOL) return ldtk_fail(l, "is not a bool");
        out->b = v->boolean;
        return true;
    case ORB_FIELD_STRING:
        if (v->kind != ORB_JSON_STRING) return ldtk_fail(l, "is not a string");
        out->s = v->str;
        return true;
    case ORB_FIELD_POINT: {
        int cx, cy;

        if (v->kind != ORB_JSON_OBJECT || !g) return ldtk_fail(l, "is not a point");
        if (!ldtk_int(l, v, "cx", &cx) || !ldtk_int(l, v, "cy", &cy)) return false;

        out->point = (orb_vec2) {
            g->world_x + g->offset_x + cx * g->grid, g->world_y + g->offset_y + cy * g->grid
        };
        return true;
    }
    case ORB_FIELD_REF:
        if (v->kind != ORB_JSON_OBJECT) return ldtk_fail(l, "is not an entity ref");
        return ldtk_string(l, v, "entityIid", &out->s) && out->s;
    }

    return ldtk_fail(l, "has an unknown kind");
}

// fieldInstances (__value) or fieldDefs (defaultOverride, an {id, params} object whose
// params[0] is the value). A null value, a skipped kind, and an instance field the
// definition does not declare write nothing. names, when given, collects every identifier.
static bool ldtk_fields(
    ldtk* l,
    const orb_json* arr,
    bool defaults,
    const ldtk_geometry* g,
    const orb_ldtk_entity_def* def,
    orb_ldtk_field** out,
    int* out_count,
    const char*** names,
    int* name_count_out
) {
    int total = arr ? arr->count : 0, count = 0;
    orb_ldtk_field* fields = orb_arena_push_array(l->a, orb_ldtk_field, total);
    const char** name_list = names ? orb_arena_push_array(l->a, const char*, total) : nullptr;
    int name_count = 0;

    for (const orb_json* f = arr ? arr->first : nullptr; f; f = f->next) {
        const char* identifier;
        if (!ldtk_string(l, f, defaults ? "identifier" : "__identifier", &identifier) ||
            !identifier)
            return ldtk_fail(l, "a field has no identifier");

        l->field = identifier;

        if (name_list) name_list[name_count++] = identifier;

        const char* type;
        if (!ldtk_string(l, f, "__type", &type) || !type) return ldtk_fail(l, "has no type");

        orb_field_kind kind = ORB_FIELD_INT;
        bool is_array, skip;
        if (!ldtk_field_kind(l, type, &kind, &is_array, &skip)) return false;
        if (skip) continue;

        if (def) {
            bool declared = false;

            for (int i = 0; i < def->field_name_count && !declared; i++)
                declared = strcmp(def->field_names[i], identifier) == 0;

            if (!declared) continue;
        }

        const orb_json* v = orb_json_get(f, defaults ? "defaultOverride" : "__value");
        if (!v) return ldtk_fail(l, "has no value");
        if (v->kind == ORB_JSON_NULL) continue;

        if (defaults) {
            const orb_json* params;

            if (v->kind != ORB_JSON_OBJECT) return ldtk_fail(l, "default is not an object");
            if (!ldtk_array(l, v, "params", &params)) return false;
            if (!params || params->count == 0) continue;

            v = params->first;
            is_array = false;
        }

        orb_ldtk_field* field = &fields[count];
        int n = 1;

        if (is_array) {
            if (v->kind != ORB_JSON_ARRAY) return ldtk_fail(l, "is not an array");

            n = v->count;
        }

        field->name = identifier;
        field->kind = kind;
        field->count = n;
        field->values = orb_arena_push_array(l->a, orb_ldtk_value, n);

        if (is_array) {
            int k = 0;

            for (const orb_json* e = v->first; e; e = e->next, k++) {
                if (e->kind == ORB_JSON_NULL) return ldtk_fail(l, "null element");
                if (!ldtk_value(l, e, kind, g, &field->values[k])) return false;
            }
        } else if (!ldtk_value(l, v, kind, g, &field->values[0]))
            return false;

        count++;
    }

    l->field = nullptr;
    *out = fields;
    *out_count = count;

    if (names) {
        *names = name_list;
        *name_count_out = name_count;
    }

    return true;
}

// "n" "s" "e" "w" "ne" "nw" "se" "sw" "<" ">" "o" to the compass, ordering, and overlap values.
static bool ldtk_dir(ldtk* l, const char* s, orb_level_dir* out) {
    static const struct {
        const char* s;
        orb_level_dir dir;
    } table[] = {
        {"n", ORB_LEVEL_N},      {"s", ORB_LEVEL_S},       {"e", ORB_LEVEL_E},
        {"w", ORB_LEVEL_W},      {"ne", ORB_LEVEL_NE},     {"nw", ORB_LEVEL_NW},
        {"se", ORB_LEVEL_SE},    {"sw", ORB_LEVEL_SW},     {"<", ORB_LEVEL_LOWER},
        {">", ORB_LEVEL_HIGHER}, {"o", ORB_LEVEL_OVERLAP},
    };

    for (size_t i = 0; i < sizeof table / sizeof *table; i++) {
        if (strcmp(s, table[i].s) != 0) continue;

        *out = table[i].dir;
        return true;
    }

    return ldtk_fail(l, "unknown neighbour dir %s", s);
}

// defs.tilesets: one orb_ldtk_tileset per definition with an .aseprite relPath. A
// definition with no relPath, or one that does not end in .aseprite, is kept in
// skipped instead of failing outright, so only a layer that actually uses it fails.
static bool ldtk_defs_tilesets(ldtk* l, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(l, defs, "tilesets", &arr)) return false;

    int total = arr ? arr->count : 0;
    orb_ldtk_tileset* tilesets = orb_arena_push_array(l->a, orb_ldtk_tileset, total);
    orb_ldtk_skipped* skipped = orb_arena_push_array(l->a, orb_ldtk_skipped, total);
    int count = 0, skipped_count = 0;

    for (const orb_json* t = arr ? arr->first : nullptr; t; t = t->next) {
        const char* path;
        if (!ldtk_string(l, t, "relPath", &path)) return false;

        int uid;
        if (!ldtk_int(l, t, "uid", &uid)) return false;

        constexpr char suffix[] = ".aseprite";
        constexpr size_t suffix_len = sizeof suffix - 1;
        size_t plen = path ? strlen(path) : 0;
        bool usable = path && plen >= suffix_len && strcmp(path + plen - suffix_len, suffix) == 0;

        if (!usable) {
            skipped[skipped_count++] = (orb_ldtk_skipped) {.uid = uid, .path = path};
            continue;
        }

        int grid, spacing, padding, columns, rows, width, height;
        if (!ldtk_int(l, t, "tileGridSize", &grid)) return false;
        if (!ldtk_int(l, t, "spacing", &spacing)) return false;
        if (!ldtk_int(l, t, "padding", &padding)) return false;
        if (!ldtk_int(l, t, "__cWid", &columns)) return false;
        if (!ldtk_int(l, t, "__cHei", &rows)) return false;
        if (!ldtk_int(l, t, "pxWid", &width)) return false;
        if (!ldtk_int(l, t, "pxHei", &height)) return false;

        tilesets[count++] = (orb_ldtk_tileset) {
            .path = path,
            .uid = uid,
            .grid = grid,
            .spacing = spacing,
            .padding = padding,
            .columns = columns,
            .rows = rows,
            .width = width,
            .height = height
        };
    }

    out->tilesets = tilesets;
    out->tileset_count = count;
    out->skipped = skipped;
    out->skipped_count = skipped_count;
    return true;
}

// defs.layers: one orb_ldtk_layer_def per definition.
static bool ldtk_defs_layers(ldtk* l, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(l, defs, "layers", &arr)) return false;

    int count = arr ? arr->count : 0;
    orb_ldtk_layer_def* layer_defs = orb_arena_push_array(l->a, orb_ldtk_layer_def, count);
    int i = 0;

    for (const orb_json* d = arr ? arr->first : nullptr; d; d = d->next, i++) {
        const char* identifier;
        if (!ldtk_string(l, d, "identifier", &identifier)) return false;

        int uid;
        if (!ldtk_int(l, d, "uid", &uid)) return false;

        int tileset_uid;
        if (!ldtk_optional_int(l, d, "tilesetDefUid", &tileset_uid)) return false;

        double px, py;
        if (!ldtk_number(l, d, "parallaxFactorX", &px)) return false;
        if (!ldtk_number(l, d, "parallaxFactorY", &py)) return false;

        bool scaling = false;
        if (!ldtk_bool(l, d, "parallaxScaling", &scaling)) return false;

        if (scaling && (px != 0 || py != 0))
            return ldtk_fail(
                l, "layer %s: parallaxScaling is not supported with a parallax factor", identifier
            );

        layer_defs[i] = (orb_ldtk_layer_def) {
            .name = identifier,
            .uid = uid,
            .tileset_uid = tileset_uid,
            .parallax_x = (float)px,
            .parallax_y = (float)py,
            .scaling = scaling
        };
    }

    out->layer_defs = layer_defs;
    out->layer_def_count = count;
    return true;
}

// defs.entities: one orb_ldtk_entity_def per definition, with its non-null defaults.
static bool ldtk_defs_entities(ldtk* l, const orb_json* defs, orb_ldtk* out) {
    const orb_json* arr;

    if (!ldtk_array(l, defs, "entities", &arr)) return false;

    int count = arr ? arr->count : 0;
    orb_ldtk_entity_def* entity_defs = orb_arena_push_array(l->a, orb_ldtk_entity_def, count);
    int i = 0;

    for (const orb_json* d = arr ? arr->first : nullptr; d; d = d->next, i++) {
        const char* identifier;
        if (!ldtk_string(l, d, "identifier", &identifier) || !identifier)
            return ldtk_fail(l, "an entity definition has no identifier");

        l->entity = identifier;

        int uid, width, height;
        if (!ldtk_int(l, d, "uid", &uid)) return false;
        if (!ldtk_int(l, d, "width", &width)) return false;
        if (!ldtk_int(l, d, "height", &height)) return false;

        const orb_json* field_defs;
        if (!ldtk_array(l, d, "fieldDefs", &field_defs)) return false;

        orb_ldtk_entity_def* def = &entity_defs[i];

        def->name = identifier;
        def->uid = uid;
        def->width = width;
        def->height = height;

        if (!ldtk_fields(
                l, field_defs, true, nullptr, nullptr, &def->fields, &def->field_count,
                &def->field_names, &def->field_name_count
            ))
            return false;

        l->entity = nullptr;
    }

    out->entity_defs = entity_defs;
    out->entity_def_count = count;
    return true;
}

// Index of the entity definition with this uid, or -1.
static int ldtk_entity_def_by_uid(const orb_ldtk* p, int uid) {
    for (int i = 0; i < p->entity_def_count; i++)
        if (p->entity_defs[i].uid == uid) return i;

    return -1;
}

// gridTiles then autoLayerTiles, in that order, into one array.
static bool ldtk_tiles(
    ldtk* l,
    const orb_json* inst,
    int grid,
    int columns,
    int rows,
    int tileset,
    int tileset_slots,
    orb_ldtk_tile** out,
    int* out_count
) {
    const orb_json *grid_tiles, *auto_tiles;

    if (!ldtk_array(l, inst, "gridTiles", &grid_tiles)) return false;
    if (!ldtk_array(l, inst, "autoLayerTiles", &auto_tiles)) return false;

    int gt_count = grid_tiles ? grid_tiles->count : 0;
    int at_count = auto_tiles ? auto_tiles->count : 0;
    int count = gt_count + at_count;

    if (count > 0 && tileset < 0) return ldtk_fail(l, "tiles on a layer with no tileset");

    orb_ldtk_tile* tiles = orb_arena_push_array(l->a, orb_ldtk_tile, count);
    int ti = 0;

    for (int pass = 0; pass < 2; pass++) {
        const orb_json* arr = pass == 0 ? grid_tiles : auto_tiles;

        for (const orb_json* t = arr ? arr->first : nullptr; t; t = t->next, ti++) {
            const orb_json* px;
            if (!ldtk_array(l, t, "px", &px)) return false;
            if (!px || px->count != 2) return ldtk_fail(l, "px does not hold two values");

            const orb_json* px0 = px->first;
            const orb_json* px1 = px0->next;

            if (px0->kind != ORB_JSON_NUMBER || px1->kind != ORB_JSON_NUMBER)
                return ldtk_fail(l, "px does not hold two numbers");

            int px_x = (int)px0->num, px_y = (int)px1->num;
            int cell_x = px_x / grid, cell_y = px_y / grid;

            if (px_x < 0 || px_y < 0 || cell_x >= columns || cell_y >= rows)
                return ldtk_fail(l, "tile at px (%d, %d) is outside the layer", px_x, px_y);

            int id;
            if (!ldtk_int(l, t, "t", &id)) return false;
            if (id < 0 || id > (int)ORB_MAX_TILE_ID)
                return ldtk_fail(l, "tile id %d does not fit", id);
            if (id >= tileset_slots) return ldtk_fail(l, "tile id %d is past the tileset", id);

            int flip;
            if (!ldtk_int(l, t, "f", &flip)) return false;

            double alpha;
            if (!ldtk_number(l, t, "a", &alpha)) return false;
            if (alpha != 1) return ldtk_fail(l, "tile alpha %g is not 1", alpha);

            tiles[ti] = (orb_ldtk_tile) {
                .cell_x = (uint16_t)cell_x,
                .cell_y = (uint16_t)cell_y,
                .id = (uint16_t)id,
                .flip = (uint8_t)flip
            };
        }
    }

    *out = tiles;
    *out_count = count;
    return true;
}

// intGridCsv: columns * rows values, each 0..255.
static bool ldtk_cells(ldtk* l, const orb_json* inst, int columns, int rows, const uint8_t** out) {
    const orb_json* csv;

    if (!ldtk_array(l, inst, "intGridCsv", &csv)) return false;

    int cell_count = columns * rows;
    int csv_count = csv ? csv->count : 0;

    if (csv_count != cell_count)
        return ldtk_fail(l, "intGridCsv holds %d values for %d cells", csv_count, cell_count);

    uint8_t* cells = orb_arena_push_array(l->a, uint8_t, cell_count);
    int i = 0;

    for (const orb_json* c = csv ? csv->first : nullptr; c; c = c->next, i++) {
        if (c->kind != ORB_JSON_NUMBER) return ldtk_fail(l, "intGridCsv value is not a number");
        if (c->num < 0 || c->num > 255) return ldtk_fail(l, "intGrid value %g exceeds 255", c->num);

        cells[i] = (uint8_t)c->num;
    }

    *out = cells;
    return true;
}

// The layer definition with this uid, or nullptr.
static const orb_ldtk_layer_def* ldtk_layer_def_by_uid(const orb_ldtk* p, int uid) {
    for (int i = 0; i < p->layer_def_count; i++)
        if (p->layer_defs[i].uid == uid) return &p->layer_defs[i];

    return nullptr;
}

// Index of the tileset with this uid, or -1.
static int ldtk_tileset_by_uid(const orb_ldtk* p, int uid) {
    for (int i = 0; i < p->tileset_count; i++)
        if (p->tilesets[i].uid == uid) return i;

    return -1;
}

// The skipped tileset definition with this uid, or nullptr.
static const orb_ldtk_skipped* ldtk_skipped_by_uid(const orb_ldtk* p, int uid) {
    for (int i = 0; i < p->skipped_count; i++)
        if (p->skipped[i].uid == uid) return &p->skipped[i];

    return nullptr;
}

// One layer instance: identifier, type, its definition, geometry, cells (IntGrid), and tiles.
static bool ldtk_layer(
    ldtk* l,
    const orb_ldtk* project,
    const orb_json* inst,
    orb_ldtk_layer* out
) {
    const char* identifier;
    if (!ldtk_string(l, inst, "__identifier", &identifier)) return false;

    l->layer = identifier;

    const char* type;
    if (!ldtk_string(l, inst, "__type", &type)) return false;

    int def_uid;
    if (!ldtk_int(l, inst, "layerDefUid", &def_uid)) return false;

    const orb_ldtk_layer_def* def = ldtk_layer_def_by_uid(project, def_uid);
    if (!def) return ldtk_fail(l, "layerDefUid %d has no definition", def_uid);

    int grid, columns, rows, offset_x, offset_y;
    if (!ldtk_int(l, inst, "__gridSize", &grid)) return false;
    if (!ldtk_int(l, inst, "__cWid", &columns)) return false;
    if (!ldtk_int(l, inst, "__cHei", &rows)) return false;
    if (grid < 1) return ldtk_fail(l, "__gridSize %d is not positive", grid);
    if (columns < 1 || rows < 1) return ldtk_fail(l, "__cWid/__cHei %dx%d is empty", columns, rows);
    if (!ldtk_int(l, inst, "__pxTotalOffsetX", &offset_x)) return false;
    if (!ldtk_int(l, inst, "__pxTotalOffsetY", &offset_y)) return false;

    double opacity;
    if (!ldtk_number(l, inst, "__opacity", &opacity)) return false;
    if (opacity != 1) return ldtk_fail(l, "opacity %g is not 1", opacity);

    int tuid;
    if (!ldtk_optional_int(l, inst, "__tilesetDefUid", &tuid)) return false;

    int tileset = -1, tileset_slots = 0;

    if (tuid >= 0) {
        tileset = ldtk_tileset_by_uid(project, tuid);

        if (tileset < 0) {
            const orb_ldtk_skipped* skip = ldtk_skipped_by_uid(project, tuid);

            if (skip && skip->path)
                return ldtk_fail(l, "tileset %s must end in .aseprite", skip->path);
            if (skip) return ldtk_fail(l, "tileset uid %d has no image path", tuid);
            return ldtk_fail(l, "tileset uid %d has no definition", tuid);
        }

        tileset_slots = project->tilesets[tileset].columns * project->tilesets[tileset].rows;
    }

    const uint8_t* cells = nullptr;
    if (strcmp(type, "IntGrid") == 0 && !ldtk_cells(l, inst, columns, rows, &cells)) return false;

    orb_ldtk_tile* tiles = nullptr;
    int tile_count = 0;
    if (!ldtk_tiles(l, inst, grid, columns, rows, tileset, tileset_slots, &tiles, &tile_count))
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
    out->tile_count = tile_count;

    l->layer = nullptr;
    return true;
}

// One Entities layer instance: its entityInstances into out, in array order.
static bool ldtk_entities(
    ldtk* l,
    const orb_ldtk* project,
    const orb_ldtk_level* level,
    const orb_json* inst,
    orb_ldtk_instance* out
) {
    const char* identifier;
    if (!ldtk_string(l, inst, "__identifier", &identifier)) return false;

    l->layer = identifier;

    int grid, offset_x, offset_y;
    if (!ldtk_int(l, inst, "__gridSize", &grid)) return false;
    if (!ldtk_int(l, inst, "__pxTotalOffsetX", &offset_x)) return false;
    if (!ldtk_int(l, inst, "__pxTotalOffsetY", &offset_y)) return false;

    const orb_json* arr;
    if (!ldtk_array(l, inst, "entityInstances", &arr)) return false;

    ldtk_geometry g = {level->world_x, level->world_y, offset_x, offset_y, grid};
    int i = 0;

    for (const orb_json* e = arr ? arr->first : nullptr; e; e = e->next, i++) {
        const char* type_name;
        if (!ldtk_string(l, e, "__identifier", &type_name) || !type_name)
            return ldtk_fail(l, "an entity has no identifier");

        l->entity = type_name;

        const char* iid;
        if (!ldtk_string(l, e, "iid", &iid) || !iid) return ldtk_fail(l, "has no iid");

        int def_uid;
        if (!ldtk_int(l, e, "defUid", &def_uid)) return false;

        int def = ldtk_entity_def_by_uid(project, def_uid);
        if (def < 0) return ldtk_fail(l, "defUid %d has no definition", def_uid);

        const orb_json* px;
        if (!ldtk_array(l, e, "px", &px)) return false;
        if (!px || px->count != 2 || px->first->kind != ORB_JSON_NUMBER ||
            px->first->next->kind != ORB_JSON_NUMBER)
            return ldtk_fail(l, "px does not hold two numbers");

        int width, height;
        if (!ldtk_int(l, e, "width", &width)) return false;
        if (!ldtk_int(l, e, "height", &height)) return false;

        const orb_json* field_instances;
        if (!ldtk_array(l, e, "fieldInstances", &field_instances)) return false;

        orb_ldtk_instance* o = &out[i];

        o->iid = iid;
        o->def = def;
        o->x = level->world_x + (int)px->first->num;
        o->y = level->world_y + (int)px->first->next->num;
        o->width = width;
        o->height = height;

        if (!ldtk_fields(
                l, field_instances, false, &g, &project->entity_defs[def], &o->fields,
                &o->field_count, nullptr, nullptr
            ))
            return false;

        l->entity = nullptr;
    }

    l->layer = nullptr;
    return true;
}

// layerInstances, top first in LDtk: non-Entities instances fill the layers bottom to top,
// and Entities instances fill the level's instances the same way.
static bool ldtk_layers(
    ldtk* l,
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

    orb_ldtk_layer* layers = orb_arena_push_array(l->a, orb_ldtk_layer, count);
    orb_ldtk_instance* entities = orb_arena_push_array(l->a, orb_ldtk_instance, instance_count);
    int i = count, e = instance_count;

    for (const orb_json* inst = instances->first; inst; inst = inst->next) {
        const char* type;
        if (!ldtk_string(l, inst, "__type", &type)) return false;

        if (strcmp(type, "Entities") == 0) {
            const orb_json* arr = orb_json_get(inst, "entityInstances");
            int n = arr && arr->kind == ORB_JSON_ARRAY ? arr->count : 0;

            e -= n;
            if (!ldtk_entities(l, project, level, inst, &entities[e])) return false;
            continue;
        }

        i--;
        if (!ldtk_layer(l, project, inst, &layers[i])) return false;
    }

    level->layers = layers;
    level->layer_count = count;
    level->instances = entities;
    level->instance_count = instance_count;
    return true;
}

// identifier, iid, world placement, background (unsupported), and neighbours.
static bool ldtk_level_head(ldtk* l, const orb_json* lv, orb_ldtk_level* out) {
    const char* identifier;
    if (!ldtk_string(l, lv, "identifier", &identifier)) return false;

    l->level = identifier;

    const char* iid;
    if (!ldtk_string(l, lv, "iid", &iid)) return false;

    int world_x, world_y, depth, width, height;
    if (!ldtk_int(l, lv, "worldX", &world_x)) return false;
    if (!ldtk_int(l, lv, "worldY", &world_y)) return false;
    if (!ldtk_int(l, lv, "worldDepth", &depth)) return false;
    if (!ldtk_int(l, lv, "pxWid", &width)) return false;
    if (!ldtk_int(l, lv, "pxHei", &height)) return false;

    const char* bg;
    if (!ldtk_string(l, lv, "bgRelPath", &bg)) return false;
    if (bg) return ldtk_fail(l, "background images are not supported");

    const orb_json* neighbours;
    if (!ldtk_array(l, lv, "__neighbours", &neighbours)) return false;

    int neighbor_count = neighbours ? neighbours->count : 0;
    orb_ldtk_neighbor* neighbors = orb_arena_push_array(l->a, orb_ldtk_neighbor, neighbor_count);
    int i = 0;

    for (const orb_json* n = neighbours ? neighbours->first : nullptr; n; n = n->next, i++) {
        const char* level_iid;
        if (!ldtk_string(l, n, "levelIid", &level_iid)) return false;

        const char* dir_s;
        if (!ldtk_string(l, n, "dir", &dir_s)) return false;

        orb_level_dir dir = ORB_LEVEL_N;
        if (!ldtk_dir(l, dir_s, &dir)) return false;

        neighbors[i] = (orb_ldtk_neighbor) {.level_iid = level_iid, .dir = dir};
    }

    out->name = identifier;
    out->iid = iid;
    out->world_x = world_x;
    out->world_y = world_y;
    out->depth = depth;
    out->width = width;
    out->height = height;
    out->neighbors = neighbors;
    out->neighbor_count = neighbor_count;

    return true;
}

bool orb_ldtk_parse(orb_arena* a, orb_span text, const char* name, orb_ldtk* out, orb_error* err) {
    ldtk l = {.a = a, .name = name, .err = err};
    orb_json* root = orb_json_parse(a, (const char*)text.ptr, text.len, err);

    if (!root) return ldtk_fail(&l, "%s", err->text);

    const char* version;
    if (!ldtk_string(&l, root, "jsonVersion", &version)) return false;
    if (strncmp(version, "1.", 2) != 0) return ldtk_fail(&l, "jsonVersion %s is not 1.x", version);

    const orb_json* worlds;
    if (!ldtk_array(&l, root, "worlds", &worlds)) return false;
    if (worlds && worlds->count != 0)
        return ldtk_fail(&l, "multi-world projects (worlds) are not supported");

    const orb_json* defs = orb_json_get(root, "defs");
    if (!defs) return ldtk_fail(&l, "missing defs");

    if (!ldtk_defs_tilesets(&l, defs, out)) return false;
    if (!ldtk_defs_layers(&l, defs, out)) return false;
    if (!ldtk_defs_entities(&l, defs, out)) return false;

    const orb_json* levels;
    if (!ldtk_array(&l, root, "levels", &levels)) return false;

    int level_count = levels ? levels->count : 0;
    orb_ldtk_level* levels_out = orb_arena_push_array(a, orb_ldtk_level, level_count);
    int i = 0;

    for (const orb_json* lv = levels ? levels->first : nullptr; lv; lv = lv->next, i++) {
        if (!ldtk_level_head(&l, lv, &levels_out[i])) return false;

        const orb_json* instances;
        if (!ldtk_array(&l, lv, "layerInstances", &instances)) return false;

        if (instances) {
            if (!ldtk_layers(&l, out, &levels_out[i], instances)) return false;
        } else {
            const char* external;
            if (!ldtk_string(&l, lv, "externalRelPath", &external)) return false;
            if (!external)
                return ldtk_fail(&l, "level has neither layerInstances nor externalRelPath");

            levels_out[i].external_path = external;
            levels_out[i].layer_count = 0;
        }

        l.level = nullptr;
    }

    out->levels = levels_out;
    out->level_count = level_count;
    return true;
}

bool orb_ldtk_parse_level(
    orb_arena* a,
    orb_span text,
    const char* name,
    const orb_ldtk* project,
    orb_ldtk_level* level,
    orb_error* err
) {
    ldtk l = {.a = a, .name = name, .level = level->name, .err = err};
    orb_json* root = orb_json_parse(a, (const char*)text.ptr, text.len, err);

    if (!root) return ldtk_fail(&l, "%s", err->text);

    const orb_json* instances;
    if (!ldtk_array(&l, root, "layerInstances", &instances)) return false;
    if (!instances) return ldtk_fail(&l, "external level has no layerInstances");

    return ldtk_layers(&l, project, level, instances);
}
