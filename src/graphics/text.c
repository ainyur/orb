#include "text.h"

static const orb_font_desc* text_font(const orb_assets* assets, orb_font font) {
    uint32_t index = orb_asset_index_of(assets, font);

    return index == ORB_NO_INDEX ? nullptr : &assets->fonts[index];
}

static const orb_glyph_desc* text_glyph(
    const orb_assets* assets,
    const orb_font_desc* desc,
    unsigned char c
) {
    if (c < ORB_FONT_FIRST || c >= ORB_FONT_FIRST + ORB_FONT_GLYPHS) return nullptr;

    return &assets->glyphs[desc->first_glyph + c - ORB_FONT_FIRST];
}

orb_size orb_text_measure(const orb_assets* assets, orb_font font, const char* text) {
    const orb_font_desc* desc = text_font(assets, font);

    if (!desc || !text || !text[0]) return (orb_size) {0, 0};

    int width = 0;

    for (const unsigned char* c = (const unsigned char*)text; *c; c++) {
        const orb_glyph_desc* glyph = text_glyph(assets, desc, *c);

        if (glyph) width += glyph->advance;
    }

    return (orb_size) {width, desc->line_height};
}

void orb_text_draw(
    orb_fb* fb,
    const orb_assets* assets,
    orb_font font,
    const char* text,
    orb_vec2 at,
    const uint8_t* remap
) {
    const orb_font_desc* desc = text_font(assets, font);

    if (!desc || !text) return;

    const orb_sheet_desc* sheet = &assets->sheets[desc->sheet];

    for (const unsigned char* c = (const unsigned char*)text; *c; c++) {
        const orb_glyph_desc* glyph = text_glyph(assets, desc, *c);

        if (!glyph) continue;

        if (glyph->width) {
            const uint8_t* src =
                assets->pixels + sheet->pixels + glyph->y * sheet->width + glyph->x;

            orb_fb_blit(
                fb, src, sheet->width, (orb_size) {glyph->width, desc->line_height}, at, 0, remap
            );
        }

        at.x += glyph->advance;
    }
}
