// A font's cells come from its own Aseprite grid; a glyph's rect is the inked span of
// its cell and its advance that width plus one.
static bool font_fail(cast* c, const char* path, const char* fmt, ...);

static bool font_cast(cast* c, const cast_files* fonts, uint32_t first_sheet, orb_assets* as) {
    if ((uint32_t)fonts->count > ORB_MAX_FONTS)
        return orb_error_set(c->err, "%d fonts, the most is %u", fonts->count, ORB_MAX_FONTS);

    orb_font_desc* descs = orb_arena_push_array(c->out, orb_font_desc, fonts->count);
    orb_glyph_desc* glyphs =
        orb_arena_push_array(c->out, orb_glyph_desc, (size_t)fonts->count * ORB_FONT_GLYPHS);
    uint64_t* ids = orb_arena_push_array(c->out, uint64_t, fonts->count);

    for (int i = 0; i < fonts->count; i++) {
        const char* path = fonts->paths[i];
        const orb_sheet_desc* sheet = &as->sheets[first_sheet + i];
        const uint8_t* px = as->pixels + sheet->pixels;
        int cw = fonts->grid_width[i], ch = fonts->grid_height[i];

        if (cw == 0 || ch == 0)
            return font_fail(c, path, "the grid is %dx%d, set one in Sprite Properties", cw, ch);

        if (fonts->grid_x[i] || fonts->grid_y[i])
            return font_fail(c, path, "the grid origin is not 0,0");

        if (sheet->width % cw || sheet->height % ch)
            return font_fail(
                c, path, "the %dx%d grid does not divide the %ux%u sheet", cw, ch, sheet->width,
                sheet->height
            );

        if ((uint32_t)cw > ORB_MAX_CELL)
            return font_fail(c, path, "the cell is %d wide, the most is %u", cw, ORB_MAX_CELL);

        int columns = sheet->width / cw, cells = columns * (sheet->height / ch);

        if ((uint32_t)cells < ORB_FONT_GLYPHS)
            return font_fail(
                c, path, "%d cells, %u are needed for codepoints 32 to 126", cells, ORB_FONT_GLYPHS
            );

        for (uint32_t n = 0; n < ORB_FONT_GLYPHS; n++) {
            int ox = (int)(n % (uint32_t)columns) * cw, oy = (int)(n / (uint32_t)columns) * ch;
            int first = -1, last = -1;

            for (int x = 0; x < cw; x++)
                for (int y = 0; y < ch; y++)
                    if (px[(oy + y) * sheet->width + ox + x]) {
                        if (first < 0) first = x;
                        last = x;
                        break;
                    }

            if (first < 0 && n)
                return font_fail(
                    c, path, "the cell for codepoint %u '%c' has no ink", ORB_FONT_FIRST + n,
                    (char)(ORB_FONT_FIRST + n)
                );

            int width = first < 0 ? 0 : last - first + 1;

            glyphs[i * ORB_FONT_GLYPHS + n] = (orb_glyph_desc) {
                .x = (uint16_t)(ox + (first < 0 ? 0 : first)),
                .y = (uint16_t)oy,
                .width = (uint8_t)width,
                .advance = (uint8_t)(first < 0 ? (cw + 2) / 3 : width + 1)
            };
        }

        descs[i] = (orb_font_desc) {
            .sheet = (uint16_t)(first_sheet + i),
            .first_glyph = (uint16_t)(i * (int)ORB_FONT_GLYPHS),
            .line_height = (uint16_t)ch
        };
        ids[i] = orb_asset_id(fonts->stems[i], "font");
    }

    as->fonts = descs;
    as->font_count = (uint32_t)fonts->count;
    as->glyphs = glyphs;
    as->glyph_count = (uint32_t)fonts->count * ORB_FONT_GLYPHS;
    as->font_ids = ids;
    return true;
}

static bool font_fail(cast* c, const char* path, const char* fmt, ...) {
    va_list args;

    va_start(args, fmt);

    char detail[160];

    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    return orb_error_set(c->err, "%s: %s", path, detail);
}
