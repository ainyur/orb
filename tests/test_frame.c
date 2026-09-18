#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"
#include "fixtures/game.c"

#define W 64
#define H 32
#define BACKGROUND 0x202040u // master index 1, what the fixture clears to
#define RED 0xff0000u        // walk frame 0: an 8x8 body at (4,4) of the 16x16 sprite
#define GREEN 0x00ff00u      // walk frame 1: an 8x8 body at (6,4)
#define WHITE 0xffffffu      // tile 7's body, master index 5
#define YELLOW 0xffff00u     // tile 7's marker pixel, master index 6

static uint32_t pixel(int x, int y) {
    return orb_os_headless_frame()[y * W + x];
}

static bool find(uint32_t color, int* x, int* y) {
    for (*y = 0; *y < H; (*y)++)
        for (*x = 0; *x < W; (*x)++)
            if (pixel(*x, *y) == color) return true;

    return false;
}

int main(void) {
    orb_error err;

    if (!orb_boot(orb_game_main(), "tests/fixtures", (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    orb_input in = {0};
    orb_os_headless_set_input(&in);

    CHECK(host_tick());

    host_render();

    // "AB" in the fixture font: 'A' is cell 33, inked at columns 0 and 2, so column 1
    // shows the background beneath it.
    CHECK_EQ(pixel(0, 0), WHITE);
    CHECK_EQ(pixel(1, 0), BACKGROUND);

    // the floor layer: tile 7 at cell (4,2) flipped Y puts its marker at the cell's bottom-left.
    // Cell (3,2) is under the sprite's body at boot (body x 24..31, y 12..19), so it is
    // checked after the walk below moves the body away.
    CHECK_EQ(pixel(4 * 8, 2 * 8 + 7), YELLOW);
    CHECK_EQ(pixel(4 * 8, 2 * 8), WHITE);

    // the manifest binds select to M, resolved through the headless US layout
    in.keys[ORB_KEY_M] = true;
    orb_os_headless_set_input(&in);
    CHECK(host_tick());
    CHECK_EQ(orb_api_table()->key_pressed_any(), ORB_KEY_M);
    CHECK(orb_api_table()->key_down(ORB_KEY_M));
    CHECK(
        strcmp(orb_api_table()->key_name(orb_api_table()->button_source(ORB_BTN_SELECT)), "m") == 0
    );
    CHECK(orb_api_table()->button_down(ORB_BTN_SELECT));
    in.keys[ORB_KEY_M] = false;
    in.keys[ORB_KEY_TAB] = true;
    orb_os_headless_set_input(&in);
    CHECK(host_tick());
    CHECK(!orb_api_table()->button_down(ORB_BTN_SELECT));
    in = (orb_input) {0};
    orb_os_headless_set_input(&in);

    // sounds and the song play through the API table into the headless render
    const orb_api* api = orb_api_table();
    orb_sample beep = api->sample_find("beep");
    orb_song loop = api->song_find("loop");
    CHECK(beep.v != ORB_NO_SAMPLE.v);
    CHECK(loop.v != ORB_NO_SONG.v);
    CHECK_EQ(api->sample_find("loop").v, ORB_NO_SAMPLE.v); // a song's sample is not a sound
    CHECK_EQ(api->song_find("beep").v, ORB_NO_SONG.v);

    CHECK(api->song_position().seconds == -1);
    CHECK(api->song_position().beats == -1);
    api->song_play(loop, true);

    orb_voice voice = api->sound_play(beep, (orb_sound_params) {.volume = 1}, 0);
    CHECK(voice.v != ORB_NO_VOICE.v);

    static int16_t audio[8192 * 2];
    orb_audio_render(audio, 1024);

    bool left = false, right = false;

    for (int i = 0; i < 1024; i++) {
        if (audio[i * 2]) left = true;
        if (audio[i * 2 + 1]) right = true;
    }

    CHECK(left && right);

    // the position is the render head: 1024 output frames into a 48 kHz file at 120 BPM
    orb_song_position at = api->song_position();
    CHECK(at.seconds > 1023.0f / 48000 && at.seconds < 1025.0f / 48000);
    CHECK(at.beats > 1023.0f / 48000 * 2 && at.beats < 1025.0f / 48000 * 2);

    // idle rendering covers the elapsed time
    uint64_t rendered = 0;
    orb_audio_idle(1000000000u, &rendered);
    CHECK_EQ(rendered, 48000 / ORB_MIXER_CHUNK * ORB_MIXER_CHUNK);
    orb_audio_idle(1000000000u, &rendered); // nothing more is owed for the same second
    CHECK_EQ(rendered, 48000 / ORB_MIXER_CHUNK * ORB_MIXER_CHUNK);
    at = api->song_position();
    CHECK(at.seconds > 1 && at.seconds < 1 + 2000.0f / 48000);

    // the beep ends on its own inside 4800 frames and the song fades out in 100 ms
    api->song_stop(100);
    orb_audio_render(audio, 8192);

    for (int i = 8192 - 512; i < 8192; i++) {
        CHECK_EQ(audio[i * 2], 0);
        CHECK_EQ(audio[i * 2 + 1], 0);
    }

    int bx, by;
    CHECK(find(RED, &bx, &by));

    CHECK_EQ(pixel(bx + 7, by + 7), RED);
    CHECK_EQ(pixel(bx - 1, by), BACKGROUND);
    CHECK_EQ(pixel(bx + 8, by), BACKGROUND);

    in.keys[ORB_KEY_RIGHT] = true;
    orb_os_headless_set_input(&in);

    for (int i = 0; i < 10; i++)
        CHECK(host_tick());

    host_render();

    CHECK_EQ(pixel(bx + 10, by), BACKGROUND);
    CHECK_EQ(pixel(bx + 12, by), GREEN);
    CHECK_EQ(pixel(bx + 19, by), GREEN);
    CHECK_EQ(pixel(bx + 20, by), BACKGROUND);

    in.keys[ORB_KEY_RIGHT] = false;
    in.keys[ORB_KEY_LEFT] = true;
    orb_os_headless_set_input(&in);

    CHECK(host_tick());

    host_render();

    CHECK_EQ(pixel(bx + 7, by), GREEN);
    CHECK_EQ(pixel(bx + 14, by), GREEN);
    CHECK_EQ(pixel(bx + 15, by), BACKGROUND);

    api->button_bind(ORB_BTN_SELECT, ORB_KEY_TAB);
    CHECK(orb_recast(&err));
    CHECK_EQ(api->button_source(ORB_BTN_SELECT), ORB_KEY_M); // the manifest's, again

    host_render();

    CHECK_EQ(pixel(bx + 7, by), GREEN);

    // handles survive a recast that changes nothing, so a game need not find again
    CHECK(api->sound_play(beep, (orb_sound_params) {.volume = 1}, 0).v != ORB_NO_VOICE.v);

    in = (orb_input) {0};
    in.keys[ORB_KEY_LEFT] = true;
    orb_os_headless_set_input(&in);

    for (int i = 0; i < 40; i++)
        CHECK(host_tick());

    host_render();

    int x, y;
    bool is_red = find(RED, &x, &y);
    CHECK(is_red || find(GREEN, &x, &y));
    // the body's left edge stops against the wall cells; flipped, walk frame 0's body sits
    // symmetric in its 16-wide sprite (edge at x+4) but frame 1's does not (edge at x+2)
    CHECK_EQ(x, is_red ? 8 : 6);
    // cell (3,2) is uncovered now: tile 7 flipped X has its marker at the cell's top-right
    CHECK_EQ(pixel(3 * 8 + 7, 2 * 8), YELLOW);
    CHECK_EQ(pixel(3 * 8 + 0, 2 * 8), WHITE);

    orb_quit();

    static alignas(16) uint8_t scratch_mem[4 << 20], out_mem[1 << 20];
    orb_arena scratch, out;
    orb_manifest m;
    orb_cast_result r;

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    CHECK(orb_cast_game(&scratch, &out, "tests/fixtures", &m, &r, &err));
    CHECK(orb_boot(orb_game_main(), nullptr, r.file, &err));
    CHECK(host_tick());

    host_render();

    CHECK_EQ(pixel(0, 0), WHITE);
    CHECK_EQ(pixel(1, 0), BACKGROUND);
    CHECK(find(RED, &bx, &by));

    orb_quit();

    return 0;
}
