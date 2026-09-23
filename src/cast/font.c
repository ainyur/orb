// A font's cells come from its own Aseprite grid; a glyph's rect is the inked span of
// its cell and its advance that width plus one.
static bool font_fail(cast* context, const char* path, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);

    char detail[160];

    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    return orb_error_set(context->err, "%s: %s", path, detail);
}

static bool font_cast(
    cast* context,
    const cast_files* fonts,
    uint32_t first_sheet,
    orb_assets* assets
) {
    if ((uint32_t)fonts->count > ORB_MAX_FONTS)
        return orb_error_set(context->err, "%d fonts, the most is %u", fonts->count, ORB_MAX_FONTS);

    orb_font_desc* descs = orb_arena_push_array(context->out, orb_font_desc, fonts->count);
    orb_glyph_desc* glyphs =
        orb_arena_push_array(context->out, orb_glyph_desc, (size_t)fonts->count * ORB_FONT_GLYPHS);
    uint64_t* ids = orb_arena_push_array(context->out, uint64_t, fonts->count);

    for (int i = 0; i < fonts->count; i++) {
        const char* path = fonts->paths[i];
        const orb_sheet_desc* sheet = &assets->sheets[first_sheet + i];
        const uint8_t* pixels = assets->pixels + sheet->pixels;
        int cell_width = fonts->grid_width[i], cell_height = fonts->grid_height[i];

        if (cell_width == 0 || cell_height == 0)
            return font_fail(
                context, path, "the grid is %dx%d, set one in Sprite Properties", cell_width,
                cell_height
            );

        if (fonts->grid_x[i] || fonts->grid_y[i])
            return font_fail(context, path, "the grid origin is not 0,0");

        if (sheet->width % cell_width || sheet->height % cell_height)
            return font_fail(
                context, path, "the %dx%d grid does not divide the %ux%u sheet", cell_width,
                cell_height, sheet->width, sheet->height
            );

        if ((uint32_t)cell_width > ORB_MAX_CELL)
            return font_fail(
                context, path, "the cell is %d wide, the most is %u", cell_width, ORB_MAX_CELL
            );

        int columns = sheet->width / cell_width, cells = columns * (sheet->height / cell_height);

        if ((uint32_t)cells < ORB_FONT_GLYPHS)
            return font_fail(
                context, path, "%d cells, %u are needed for codepoints 32 to 126", cells,
                ORB_FONT_GLYPHS
            );

        for (uint32_t n = 0; n < ORB_FONT_GLYPHS; n++) {
            int ox = (int)(n % (uint32_t)columns) * cell_width,
                oy = (int)(n / (uint32_t)columns) * cell_height;
            int first = -1, last = -1;

            for (int x = 0; x < cell_width; x++)
                for (int y = 0; y < cell_height; y++)
                    if (pixels[(oy + y) * sheet->width + ox + x]) {
                        if (first < 0) first = x;
                        last = x;
                        break;
                    }

            if (first < 0 && n)
                return font_fail(
                    context, path, "the cell for codepoint %u '%c' has no ink", ORB_FONT_FIRST + n,
                    (char)(ORB_FONT_FIRST + n)
                );

            int width = first < 0 ? 0 : last - first + 1;

            glyphs[i * ORB_FONT_GLYPHS + n] = (orb_glyph_desc) {
                .x = (uint16_t)(ox + (first < 0 ? 0 : first)),
                .y = (uint16_t)oy,
                .width = (uint8_t)width,
                .advance = (uint8_t)(first < 0 ? (cell_width + 2) / 3 : width + 1)
            };
        }

        descs[i] = (orb_font_desc) {
            .sheet = (uint16_t)(first_sheet + i),
            .first_glyph = (uint16_t)(i * (int)ORB_FONT_GLYPHS),
            .line_height = (uint16_t)cell_height
        };
        ids[i] = orb_asset_id(fonts->stems[i], "font");
    }

    assets->fonts = descs;
    assets->font_count = (uint32_t)fonts->count;
    assets->glyphs = glyphs;
    assets->glyph_count = (uint32_t)fonts->count * ORB_FONT_GLYPHS;
    assets->font_ids = ids;
    return true;
}
