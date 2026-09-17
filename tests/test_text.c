#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

#define DIR "build/scratch/text"
#define ART "../../../tests/fixtures/"

// Advances follow the fixture's ink pattern: cell n inks columns fixed by n % 4.
static int expect_advance(int n) {
    return (int[]) {2, 4, 5, 2}[n % 4];
}

static orb_arena scratch, out;

// dir names the fonts directory, relative to DIR (as orb.json's "fonts" key is).
static bool cast_fonts(const char* dir, orb_assets* as, orb_error* err) {
    orb_arena_reset(&scratch);
    orb_arena_reset(&out);

    static char json[512];

    snprintf(
        json, sizeof json,
        "{\"id\": \"t\", \"name\": \"T\", \"size\": [64, 64], \"asset_headroom\": 1048576,\n"
        " \"palette\": \"" ART "art/palette.aseprite\", \"art\": \"" ART "art\",\n"
        " \"fonts\": \"%s\", \"world\": \"nothing.ldtk\"}\n",
        dir
    );

    if (!orb_os_write_file(DIR "/orb.json", (orb_span) {(const uint8_t*)json, strlen(json)}))
        return orb_error_set(err, "cannot write " DIR "/orb.json");

    orb_manifest m;
    orb_cast_result r;

    return orb_cast_game(&scratch, &out, DIR, &m, &r, err) && orb_file_load(r.file, as, err);
}

int main(void) {
    static alignas(16) uint8_t scratch_mem[8 << 20], out_mem[1 << 20];

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    orb_assets as = {};
    orb_error err;

    CHECK(orb_os_make_dir(DIR));

    CHECK(cast_fonts(ART "fonts", &as, &err));
    CHECK_EQ(as.font_count, 1);
    CHECK_EQ(as.glyph_count, ORB_FONT_GLYPHS);
    CHECK_EQ(as.fonts[0].line_height, 6);

    // Cell 0 is the space: no ink, and a third of the 4-wide cell, rounded up.
    CHECK_EQ(as.glyphs[0].width, 0);
    CHECK_EQ(as.glyphs[0].advance, 2);

    for (int n = 1; n < (int)ORB_FONT_GLYPHS; n++)
        CHECK_EQ(as.glyphs[n].advance, expect_advance(n));

    CHECK_EQ(as.glyphs[2].x, 8); // cell 2 sits at column 2 of a 4-wide grid
    CHECK_EQ(as.glyphs[2].width, 4);
    CHECK_EQ(as.glyphs[1].x, 4);
    CHECK_EQ(as.glyphs[1].width, 3); // columns 0 and 2 inked, the rect spans the gap

    // nogrid is not a fixture, since Aseprite clamps a zero grid to 1x1 on save;
    // patch a copy of the real fixture's header instead.
    orb_span original;
    CHECK(orb_os_read_file("tests/fixtures/fonts/body.aseprite", &scratch, &original));

    uint8_t* patched = orb_arena_push(&scratch, original.len, 1);

    memcpy(patched, original.ptr, original.len);
    memset(patched + 40, 0, 4); // grid width and height, header bytes 40 and 42

    CHECK(orb_os_make_dir(DIR "/nogrid"));
    CHECK(orb_os_write_file(DIR "/nogrid/body.aseprite", (orb_span) {patched, original.len}));

    CHECK(!cast_fonts("nogrid", &as, &err));
    CHECK(strstr(err.text, "grid is 0x0") != nullptr);

    const char* bad[] = {"uneven", "origin", "wide", "few", "frames", "blank"};
    const char* fragment[] = {
        "does not divide",
        "origin is not 0,0",
        "wide, the most is",
        "cells",
        "has one frame, this file has",
        "no ink"
    };

    for (int i = 0; i < 6; i++) {
        char dir[128];

        snprintf(dir, sizeof dir, ART "fonts-bad/%s", bad[i]);
        CHECK(!cast_fonts(dir, &as, &err));
        CHECK(strstr(err.text, fragment[i]) != nullptr);
    }

    CHECK(cast_fonts(ART "fonts", &as, &err));

    orb_asset_table table = {};

    orb_asset_set(&table, &as);

    orb_font f = ORB_FONT(orb_asset_find(&table, ORB_ASSET_FONT, orb_asset_id("body", "font")));

    // '!' is codepoint 33, cell 1, advance 4; '"' is 34, cell 2, advance 5.
    CHECK_EQ(orb_text_measure(&table.assets, f, "!\"").width, 9);
    CHECK_EQ(orb_text_measure(&table.assets, f, "!\"").height, 6);
    CHECK_EQ(orb_text_measure(&table.assets, f, "").width, 0);
    CHECK_EQ(orb_text_measure(&table.assets, f, "").height, 0);
    CHECK_EQ(orb_text_measure(&table.assets, ORB_NO_FONT, "!").width, 0);
    CHECK_EQ(orb_text_measure(&table.assets, f, nullptr).width, 0);
    // neither byte is drawable
    CHECK_EQ(orb_text_measure(&table.assets, f, "\n\t").width, 0);
    CHECK_EQ(orb_text_measure(&table.assets, f, "\n\t").height, 6);

    orb_framebuffer fb;

    orb_framebuffer_init(&fb, &out, (orb_size) {16, 8});
    orb_framebuffer_clear(&fb, 0);
    orb_text_draw(&fb, &table.assets, f, "\"", (orb_vec2) {0, 0}, nullptr);

    // Cell 2 inks all four columns for the cell's full height.
    CHECK(fb.px[0] != 0);
    CHECK(fb.px[3] != 0);
    CHECK_EQ(fb.px[4], 0);
    CHECK(fb.px[5 * 16] != 0); // row 5 is the last of a 6-tall cell
    CHECK_EQ(fb.px[6 * 16], 0);

    // Clipped at the top-left: columns 2..3 and rows 3..5 land at 0..1, 0..2.
    orb_framebuffer_clear(&fb, 0);
    orb_text_draw(&fb, &table.assets, f, "\"", (orb_vec2) {-2, -3}, nullptr);
    CHECK(fb.px[0] != 0);

    // Clipped at the bottom-right: no wrap into the next row.
    orb_framebuffer_clear(&fb, 0);
    orb_text_draw(&fb, &table.assets, f, "\"", (orb_vec2) {14, 5}, nullptr);
    CHECK(fb.px[5 * 16 + 15] != 0);
    CHECK_EQ(fb.px[6 * 16], 0);

    // Wholly off the left edge: the blitter clips, the pen still advances.
    orb_framebuffer_clear(&fb, 0);
    orb_text_draw(&fb, &table.assets, f, "\"\"", (orb_vec2) {-5, 0}, nullptr);
    CHECK(fb.px[0] != 0); // the second glyph starts at -5 + 5 = 0

    // Wholly outside: nothing drawn, no crash.
    orb_framebuffer_clear(&fb, 0);
    orb_text_draw(&fb, &table.assets, f, "\"", (orb_vec2) {100, 100}, nullptr);
    CHECK_EQ(fb.px[0], 0);

    orb_text_draw(&fb, &table.assets, ORB_NO_FONT, "\"", (orb_vec2) {0, 0}, nullptr);
    orb_text_draw(&fb, &table.assets, f, nullptr, (orb_vec2) {0, 0}, nullptr);
    CHECK_EQ(fb.px[0], 0);

    return 0;
}
