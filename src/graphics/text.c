#include "text.h"

static const orb_font_desc* text_font(const orb_assets* assets, orb_font f) {
    uint32_t index = orb_asset_index_of(assets, f);

    return index == ORB_NO_INDEX ? nullptr : &assets->fonts[index];
}

static const orb_glyph_desc* text_glyph(
    const orb_assets* assets,
    const orb_font_desc* d,
    unsigned char c
) {
    if (c < ORB_FONT_FIRST || c >= ORB_FONT_FIRST + ORB_FONT_GLYPHS) return nullptr;

    return &assets->glyphs[d->first_glyph + c - ORB_FONT_FIRST];
}

void orb_text_draw(
    orb_framebuffer* fb,
    const orb_assets* assets,
    orb_font f,
    const char* s,
    orb_vec2 at,
    const uint8_t* remap
) {
    const orb_font_desc* d = text_font(assets, f);

    if (!d || !s) return;

    const orb_sheet_desc* sheet = &assets->sheets[d->sheet];

    for (const unsigned char* c = (const unsigned char*)s; *c; c++) {
        const orb_glyph_desc* g = text_glyph(assets, d, *c);

        if (!g) continue;

        if (g->width) {
            const uint8_t* src = assets->pixels + sheet->pixels + g->y * sheet->width + g->x;

            orb_framebuffer_blit(
                fb, src, sheet->width, (orb_size) {g->width, d->line_height}, at, 0, remap
            );
        }

        at.x += g->advance;
    }
}

orb_size orb_text_measure(const orb_assets* assets, orb_font f, const char* s) {
    const orb_font_desc* d = text_font(assets, f);

    if (!d || !s || !s[0]) return (orb_size) {0, 0};

    int width = 0;

    for (const unsigned char* c = (const unsigned char*)s; *c; c++) {
        const orb_glyph_desc* g = text_glyph(assets, d, *c);

        if (g) width += g->advance;
    }

    return (orb_size) {width, d->line_height};
}
