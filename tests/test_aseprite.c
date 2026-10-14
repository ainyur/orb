#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static const u8 player_ase[] = {
#embed "fixtures/art/player.aseprite"
};
static const u8 pal_ase[] = {
#embed "fixtures/art/palette.aseprite"
};

int main(void) {
    static u8 mem[1 << 20];
    orb_arena scratch;
    orb_arena_init(&scratch, "test", mem, sizeof mem);
    orb_error err;

    u8_span file = {(u8*)player_ase, sizeof player_ase};
    orb_ase ase;

    CHECK(orb_ase_parse(&scratch, file, &ase, &err));
    CHECK_EQ(ase.width, 16);
    CHECK_EQ(ase.height, 16);
    CHECK_EQ(ase.frame_count, 2);
    CHECK_EQ(ase.color_count, 4);
    CHECK_EQ(ase.transparent, 0);
    CHECK_EQ(ase.rgb[1][0], 255);
    CHECK_EQ(ase.rgb[2][1], 255);
    CHECK_EQ(ase.durations[0], 100);
    CHECK_EQ(ase.durations[1], 200);

    // frame 0: red square at 4..11, frame 1: green square at x 6..13
    CHECK_EQ(ase.frames[4 * 16 + 4], 1);
    CHECK_EQ(ase.frames[11 * 16 + 11], 1);
    CHECK_EQ(ase.frames[3 * 16 + 3], 0);
    CHECK_EQ(ase.frames[12 * 16 + 12], 0);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 6], 2);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 13], 2);
    CHECK_EQ(ase.frames[256 + 4 * 16 + 5], 0);

    CHECK_EQ(ase.tags.len, 1);
    CHECK(strcmp(ase.tags.elems[0].name, "walk") == 0);
    CHECK_EQ(ase.tags.elems[0].from, 0);
    CHECK_EQ(ase.tags.elems[0].to, 1);
    CHECK_EQ(ase.tags.elems[0].direction, 0);

    // Aseprite writes a 16x16 grid at the origin unless the file sets one.
    CHECK_EQ(ase.grid_x, 0);
    CHECK_EQ(ase.grid_y, 0);
    CHECK_EQ(ase.grid_width, 16);
    CHECK_EQ(ase.grid_height, 16);

    u8_span pal = {(u8*)pal_ase, sizeof pal_ase};
    orb_ase palette;

    CHECK(orb_ase_parse(&scratch, pal, &palette, &err));
    CHECK_EQ(palette.color_count, 8);
    CHECK_EQ(palette.rgb[1][2], 64);
    CHECK_EQ(palette.rgb[5][0], 255);

    u8 junk[200] = {0};

    CHECK(!orb_ase_parse(&scratch, (u8_span) {junk, sizeof junk}, &ase, &err));
    CHECK(strstr(err.text, "aseprite") != nullptr);

    // A cel chunk (0x2005) with only the 6-byte generic header: the fixed cel
    // fields it reads at d+0..d+19 are not there.
    static const u8 cel_truncated[150] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 22,                 // frame size: 16-byte frame header + 6-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 6,                  // chunk size: the generic header, no cel payload
        [148] = 0x05, [149] = 0x20, // chunk type: cel
    };

    CHECK(!orb_ase_parse(&scratch, (u8_span) {cel_truncated, sizeof cel_truncated}, &ase, &err));
    CHECK(strstr(err.text, "truncated chunk") != nullptr);

    // A tags chunk (0x2018) claiming one tag but ending right where its record would start.
    static const u8 tag_truncated[160] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 32,                 // frame size: 16-byte frame header + 16-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 16,                 // chunk size: header and reserved bytes, no tag record
        [148] = 0x18, [149] = 0x20, // chunk type: tags
        [150] = 1,                  // tag_count
    };

    CHECK(!orb_ase_parse(&scratch, (u8_span) {tag_truncated, sizeof tag_truncated}, &ase, &err));
    CHECK(strstr(err.text, "truncated chunk") != nullptr);

    // A tags chunk (0x2018) with one full tag record whose "to" reaches past frame_count.
    static const u8 tag_out_of_range[180] = {
        [4] = 0xE0,   [5] = 0xA5,   // magic
        [6] = 1,                    // frame_count
        [8] = 1,      [10] = 1,     // width, height
        [12] = 8,                   // color depth: indexed
        [128] = 51,                 // frame size: 16-byte frame header + 35-byte chunk
        [132] = 0xFA, [133] = 0xF1, // frame magic
        [134] = 1,                  // chunk_count
        [144] = 35,                 // chunk size: header, reserved, and one tag record
        [148] = 0x18, [149] = 0x20, // chunk type: tags
        [150] = 1,                  // tag_count
        [162] = 1,                  // tag[0].to = 1, but frame_count is 1
    };

    CHECK(
        !orb_ase_parse(&scratch, (u8_span) {tag_out_of_range, sizeof tag_out_of_range}, &ase, &err)
    );
    CHECK(strstr(err.text, "out of bounds") != nullptr);

    // A header claiming more than 256 colors.
    static const u8 too_many_colors[128] = {
        [4] = 0xE0, [5] = 0xA5, // magic
        [12] = 8,               // color depth: indexed
        [32] = 44,  [33] = 1,   // color_count: 300
    };

    CHECK(
        !orb_ase_parse(&scratch, (u8_span) {too_many_colors, sizeof too_many_colors}, &ase, &err)
    );
    CHECK(strstr(err.text, "more than 256 colors") != nullptr);

    // A 6x2 tilemap layer over a tileset of three 2x2 tiles (tile 0 empty), its row of tiles 1, 0,
    // and 2 flattened as LDtk flattens it; each zlib stream is one stored block.
    static const u8 tilemap[316] = {
        [0] = 60,     [1] = 1,                                  // file size: 316
        [4] = 0xE0,   [5] = 0xA5,                               // magic
        [6] = 1,                                                // frame_count
        [8] = 6,      [10] = 2,                                 // width, height
        [12] = 8,                                               // color depth: indexed
        [128] = 188,                                            // frame size
        [132] = 0xFA, [133] = 0xF1,                             // frame magic
        [134] = 3,                                              // chunk_count
        [144] = 28,                                             // layer chunk: size
        [148] = 0x04, [149] = 0x20,                             // type
        [150] = 1,                                              // flags: visible
        [152] = 2,                                              // layer type: tilemap
        [162] = 255,                                            // opacity; no name; tileset 0
        [172] = 67,                                             // tileset chunk: size
        [176] = 0x23, [177] = 0x20,                             // type
        [182] = 2,                                              // flags: its tiles in this file
        [186] = 3,                                              // three tiles
        [190] = 2,    [192] = 2,                                // each 2x2
        [194] = 1,                                              // base index; no name
        [212] = 23,                                             // the tiles' stream: its size
        [216] = 0x78, [217] = 0x01,                             // zlib's header
        [218] = 0x01, [219] = 12,   [221] = 0xF3, [222] = 0xFF, // a last stored block of 12
        [227] = 1,    [228] = 2,    [229] = 3,    [230] = 4,    // tile 1
        [231] = 5,    [232] = 5,    [233] = 5,                  // tile 2
        [239] = 77,                                             // cel chunk: size
        [243] = 0x05, [244] = 0x20,                             // type
        [251] = 255,                                            // opacity
        [252] = 3,                                              // cel type: compressed tilemap
        [261] = 3,    [263] = 1,                                // three tiles by one
        [265] = 32,                                             // bits a tile
        [267] = 0xFF, [268] = 0xFF, [269] = 0xFF, [270] = 0x1F, // the id's bits
        [274] = 0x80, [278] = 0x40, [282] = 0x20,               // the flips' bits
        [293] = 0x78, [294] = 0x01,                             // zlib's header
        [295] = 0x01, [296] = 12,   [298] = 0xF3, [299] = 0xFF, // a last stored block of 12
        [300] = 1,    [308] = 2,                                // tiles 1, 0, and 2
    };
    static u8 changed[sizeof tilemap];
    static const u8 flat[12] = {1, 2, 0, 0, 5, 5, 3, 4, 0, 0, 5, 0};

    CHECK(orb_ase_parse(&scratch, (u8_span) {(u8*)tilemap, sizeof tilemap}, &ase, &err));
    CHECK_EQ(ase.width, 6);
    CHECK(memcmp(ase.frames, flat, sizeof flat) == 0);

    // a flipped tile, a tile past the tileset, and a tileset kept in another file are refused
    memcpy(changed, tilemap, sizeof tilemap);
    changed[311] = 0x80;
    CHECK(!orb_ase_parse(&scratch, (u8_span) {changed, sizeof changed}, &ase, &err));
    CHECK(strstr(err.text, "a tilemap tile is flipped, which LDtk does not draw") != nullptr);

    memcpy(changed, tilemap, sizeof tilemap);
    changed[308] = 3;
    CHECK(!orb_ase_parse(&scratch, (u8_span) {changed, sizeof changed}, &ase, &err));
    CHECK(strstr(err.text, "a tilemap tile is past its tileset") != nullptr);

    memcpy(changed, tilemap, sizeof tilemap);
    changed[182] = 1;
    CHECK(!orb_ase_parse(&scratch, (u8_span) {changed, sizeof changed}, &ase, &err));
    CHECK(strstr(err.text, "a tileset's tiles are not in the file") != nullptr);
    return 0;
}
