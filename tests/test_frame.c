#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"
#include "fixtures/game.c"

#define W 64
#define H 32
#define BACKGROUND 0x202040u // master index 1, what the fixture clears to
#define RED 0xff0000u        // walk frame 0: an 8x8 body at (4,4) of the 16x16 sprite
#define GREEN 0x00ff00u      // walk frame 1: an 8x8 body at (6,4)

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

    if (!orb_run_boot(orb_game_main(), "tests/fixtures", (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    orb_input in = {0};
    orb_os_headless_set_input(&in);

    CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(0, 0), BACKGROUND);

    // sounds and the song play through the API table into the headless render
    const orb_api* api = orb_api_table();
    orb_sample beep = api->sample_find("beep");
    orb_song loop = api->song_find("loop");
    CHECK(beep.v != ORB_NO_SAMPLE.v);
    CHECK(loop.v != ORB_NO_SONG.v);
    CHECK_EQ(api->sample_find("loop").v, ORB_NO_SAMPLE.v); // a song's sample is not a sound
    CHECK_EQ(api->song_find("beep").v, ORB_NO_SONG.v);

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

    in.down[ORB_BTN_RIGHT] = true;
    orb_os_headless_set_input(&in);

    for (int i = 0; i < 10; i++)
        CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(bx + 10, by), BACKGROUND);
    CHECK_EQ(pixel(bx + 12, by), GREEN);
    CHECK_EQ(pixel(bx + 19, by), GREEN);
    CHECK_EQ(pixel(bx + 20, by), BACKGROUND);

    in.down[ORB_BTN_RIGHT] = false;
    in.down[ORB_BTN_LEFT] = true;
    orb_os_headless_set_input(&in);

    CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(bx + 7, by), GREEN);
    CHECK_EQ(pixel(bx + 14, by), GREEN);
    CHECK_EQ(pixel(bx + 15, by), BACKGROUND);

    CHECK(orb_run_recast(&err));

    orb_run_draw();

    CHECK_EQ(pixel(bx + 7, by), GREEN);

    // handles survive a recast that changes nothing, so a game need not find again
    CHECK(api->sound_play(beep, (orb_sound_params) {.volume = 1}, 0).v != ORB_NO_VOICE.v);

    orb_os_close();

    static alignas(16) uint8_t scratch_mem[4 << 20], out_mem[1 << 20];
    orb_arena scratch, out;
    orb_manifest m;
    orb_cast_result r;

    orb_arena_init(&scratch, "scratch", scratch_mem, sizeof scratch_mem);
    orb_arena_init(&out, "out", out_mem, sizeof out_mem);

    CHECK(orb_cast_game(&scratch, &out, "tests/fixtures", &m, &r, &err));
    CHECK(orb_run_boot(orb_game_main(), nullptr, r.file, &err));
    CHECK(orb_run_tick());

    orb_run_draw();

    CHECK_EQ(pixel(0, 0), BACKGROUND);
    CHECK(find(RED, &bx, &by));

    orb_os_close();

    return 0;
}
