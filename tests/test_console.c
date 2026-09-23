#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/core/console_font.h"
#include "../src/orb.c"
#include "fixtures/game.c"

static int test_log(void) {
    orb_log_clear();
    CHECK_EQ(orb_log_line_count(), 0);
    CHECK(strcmp(orb_log_line(0), "") == 0);
    orb_log("one");
    orb_log("two %d", 2);
    CHECK_EQ(orb_log_line_count(), 2);
    CHECK(strcmp(orb_log_line(0), "two 2") == 0);
    CHECK(strcmp(orb_log_line(1), "one") == 0);

    // a long line is cut at the last complete UTF-8 sequence that fits
    char longline[200];
    memset(longline, 'a', 158);
    memcpy(longline + 158, "\xc3\xa9", 3);
    orb_log("%s", longline);
    CHECK_EQ(strlen(orb_log_line(0)), 158);

    // the audio thread's lines skip the ring
    orb_log_off_main = true;
    orb_log("skipped");
    orb_log_off_main = false;
    CHECK(strcmp(orb_log_line(0), "skipped") != 0);

    // the ring wraps
    for (int i = 0; i < 200; i++)
        orb_log("%d", i);

    CHECK_EQ(orb_log_line_count(), ORB_LOG_LINES);
    CHECK(strcmp(orb_log_line(0), "199") == 0);
    CHECK(strcmp(orb_log_line(ORB_LOG_LINES - 1), "72") == 0);
    orb_log_clear();
    return 0;
}

static int test_text(void) {
    orb_input in = {0};

    memcpy(in.text, "ab\xc3\xa9", 5);
    orb_os_headless_set_input(&in);

    orb_input got;

    CHECK(orb_os_pump(&got));
    CHECK(strcmp(got.text, "ab\xc3\xa9") == 0);
    CHECK(orb_os_pump(&got)); // delivered once
    CHECK_EQ(got.text[0], 0);
    return 0;
}

static int test_font(void) {
    for (int g = 1; g < 95; g++) {
        bool any = false;

        for (int row = 0; row < 6; row++)
            if (ORB_CONSOLE_FONT[g][row] & 0xe0) any = true;

        CHECK(any);
        CHECK_EQ(ORB_CONSOLE_FONT[g][0] & 0x1f, 0); // only the high 3 bits carry pixels
    }

    CHECK_EQ(ORB_CONSOLE_FONT[0][0], 0);       // space is blank
    CHECK(ORB_CONSOLE_FONT['g' - 32][5] != 0); // a descender in row 6
    CHECK_EQ(ORB_CONSOLE_FONT['A' - 32][5], 0);
    return 0;
}

static int booted;

static int boot(void) {
    orb_error err;

    debug_console_register();

    if (!booted && !orb_boot(orb_game_main(), "tests/fixtures", (orb_span) {}, &err)) {
        fprintf(stderr, "boot: %s\n", err.text);
        return 1;
    }

    booted = 1;
    return 0;
}

static const char* last(void) {
    return orb_log_line(0);
}

static int test_run(void) {
    if (boot()) return 1;

    game_state* g = (game_state*)host_state.base;

    orb_log_clear();
    orb_console_run("early");
    CHECK(strcmp(last(), "unknown: early") == 0);

    orb_console_run("speed");
    CHECK(strcmp(last(), "speed = 1") == 0);
    orb_console_run("speed 3");
    CHECK_EQ(g->speed, 3);
    CHECK(strcmp(last(), "speed = 1") == 0); // a set prints nothing
    orb_console_run("speed 0x10");
    CHECK_EQ(g->speed, 16);
    orb_console_run("speed nope");
    CHECK(strcmp(last(), "bad value for speed: nope") == 0);
    CHECK_EQ(g->speed, 16);
    orb_console_run("speed 1 2");
    CHECK(strcmp(last(), "speed takes one value") == 0);
    orb_console_run("scale 1.5");
    CHECK(g->scale == 1.5f);
    orb_console_run("scale inf");
    CHECK(strcmp(last(), "bad value for scale: inf") == 0);
    orb_console_run("scale");
    CHECK(strcmp(last(), "scale = 1.5") == 0);
    orb_console_run("god on");
    CHECK(g->god);
    orb_console_run("god");
    CHECK(strcmp(last(), "god = true") == 0);
    orb_console_run("god 0");
    CHECK(!g->god);
    orb_console_run("god maybe");
    CHECK(strcmp(last(), "bad value for god: maybe") == 0);
    orb_console_run("nothing");
    CHECK(strcmp(last(), "unknown: nothing") == 0);
    orb_console_run("   ");
    CHECK(strcmp(last(), "unknown: nothing") == 0); // an empty line prints nothing

    // commands get the split arguments; quotes group
    orb_console_run("teleport 5 \"7\"");
    CHECK(orb_entity_get(g->player)->at.x == 5);
    CHECK(orb_entity_get(g->player)->at.y == 7);
    orb_console_run("teleport \"1 2");
    CHECK(orb_entity_get(g->player)->at.x == 5); // one argument "1 2": teleport wants two

    // built-ins
    orb_log_clear();
    orb_console_run("help");
    CHECK(strstr(orb_log_line(0), "variables") != nullptr);
    CHECK(strncmp(orb_log_line(orb_log_line_count() - 1), "help", 4) == 0);
    orb_console_run("help sp");
    CHECK(strncmp(last(), "speed", 5) == 0);
    orb_console_run("dump");
    CHECK(strcmp(orb_log_line(3), "speed 16") == 0 || strcmp(orb_log_line(4), "speed 16") == 0);
    orb_console_run("clear");
    CHECK_EQ(orb_log_line_count(), 0);

    // registration outside reload logs and refuses (a step closes the reload window)
    orb_input none = {0};

    orb_console_step(&none);
    orb_console_var_int("late", &g->speed, nullptr);
    CHECK(strcmp(last(), "console: \"late\" registered outside reload") == 0);
    orb_console_run("late");
    CHECK(strcmp(last(), "unknown: late") == 0);

    // a bad name and a taken name refuse inside reload
    orb_console_clear();
    orb_console_var_int("Speed", &g->speed, nullptr);
    CHECK(strcmp(last(), "console: bad or taken name \"Speed\"") == 0);
    orb_console_var_int("speed", &g->speed, nullptr);
    orb_console_var_int("speed", &g->speed, nullptr);
    CHECK(strcmp(last(), "console: bad or taken name \"speed\"") == 0);

    // the fixture registers again on reload, and quit ends the loop
    orb_console_clear();
    reload(g, orb_api_table());
    orb_console_run("speed 2");
    CHECK_EQ(g->speed, 2);
    return 0;
}

static void tick(void);

static int test_entities(void) {
    if (boot()) return 1;

    game_state* g = (game_state*)host_state.base;

    // the fixture room spawned at boot: two crates, a marker, the player
    orb_log_clear();
    orb_console_run("entities");
    CHECK(strcmp(last(), "4 entities") == 0);
    orb_console_run("entities crate");
    CHECK(strcmp(last(), "2 entities") == 0);
    CHECK(strncmp(orb_log_line(1), "1 type 0 (32, 8) 8x8 v---", 25) == 0);
    orb_console_run("entities ghost");
    CHECK(strcmp(last(), "no entity type \"ghost\"") == 0);
    orb_console_run("entities a b");
    CHECK(strcmp(last(), "entities [type]") == 0);

    // spawn prints the slot; a bad type logs; despawn frees at the next update
    orb_console_run("spawn crate 20 10");
    CHECK(strcmp(last(), "entity 4") == 0);
    orb_console_run("spawn ghost 0 0");
    CHECK(strcmp(last(), "no entity type \"ghost\"") == 0);
    orb_console_run("spawn crate");
    CHECK(strcmp(last(), "spawn <type> <x> <y>") == 0);
    orb_console_run("despawn 4");
    CHECK(orb_entity_get(ORB_ENTITY(4 | 1u << 24))->flags & ORB_ENTITY_DESPAWNING);
    tick();
    CHECK(orb_entity_get(ORB_ENTITY(4 | 1u << 24)) == nullptr);
    orb_console_run("despawn 4");
    CHECK(strcmp(last(), "no entity 4") == 0);
    orb_console_run("despawn x");
    CHECK(strcmp(last(), "no entity x") == 0);

    // entity: the record, the placement's fields, the type's defaults, the components
    orb_log_clear();
    orb_console_run("entity 0");
    CHECK(
        strncmp(
            orb_log_line(orb_log_line_count() - 1),
            "entity 0: type 0 level 0 placement 0 at (16, 8) 8x8", 51
        ) == 0
    );
    CHECK(strstr(orb_log_line(orb_log_line_count() - 2), "int[1]: 3"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 3), "bool[1]: true"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 4), "int[3]: 1 2 3"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 5), "string[1]: \"Wood\""));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 6), "point[1]: (40, 16)"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 7), "ref[1]: #1"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 8), "int[1]: 10"));
    CHECK(strstr(orb_log_line(orb_log_line_count() - 9), "string[1]: \"box\""));
    CHECK_EQ(orb_log_line_count(), 9); // the crate has no components
    orb_log_clear();
    orb_console_run("entity 3");
    CHECK(strstr(orb_log_line(orb_log_line_count() - 1), "entity 3: type 2"));
    CHECK(strncmp(orb_log_line(orb_log_line_count() - 2), "component 0, ", 13) == 0);
    CHECK(
        strstr(orb_log_line(0), "component 1, 64 bytes") || strncmp(orb_log_line(0), "  ", 2) == 0
    );
    orb_console_run("entity");
    CHECK(strcmp(last(), "entity <index>") == 0);

    // the debug variable is orb's own and survives a clear
    orb_console_run("entities.debug on");
    CHECK(orb_entity_debug);
    orb_console_clear();
    reload(g, orb_api_table());
    orb_console_run("entities.debug");
    CHECK(strcmp(last(), "entities.debug = true") == 0);
    orb_console_run("entities.debug off");
    CHECK(!orb_entity_debug);
    (void)g;
    return 0;
}

static orb_input keys;

static void tick(void) {
    orb_os_headless_set_input(&keys);
    keys.text[0] = 0;
    host_tick();
}

static void press(int key) {
    keys.keys[key] = true;
    tick();
    keys.keys[key] = false;
    tick();
}

static void type(const char* s) {
    snprintf(keys.text, sizeof keys.text, "%s", s);
    tick();
}

static void clear_line(void) {
    keys.keys[ORB_KEY_LEFT_CTRL] = true;
    press(ORB_KEY_U);
    keys.keys[ORB_KEY_LEFT_CTRL] = false;
}

static int test_editor(void) {
    if (boot()) return 1;

    game_state* g = (game_state*)host_state.base;

    memset(&keys, 0, sizeof keys);
    keys.keys[ORB_KEY_GRAVE] = true;
    keys.keys[ORB_KEY_RIGHT] = true;
    snprintf(keys.text, sizeof keys.text, "`");
    float x = orb_entity_get(g->player)->at.x;
    tick(); // opens; the game sees nothing this tick
    CHECK(orb_console_open());
    CHECK(orb_entity_get(g->player)->at.x == x);
    CHECK(!orb_key_down(ORB_KEY_GRAVE));
    CHECK(!orb_key_down(ORB_KEY_RIGHT));
    snprintf(keys.text, sizeof keys.text, "`");
    tick(); // still held: autorepeat's text is dropped too
    CHECK_EQ(console_len, 0);
    keys.keys[ORB_KEY_GRAVE] = false;
    keys.keys[ORB_KEY_RIGHT] = false;
    tick();
    CHECK_EQ(console_len, 0); // the toggling tick's text was dropped

    orb_log_clear();
    type("speed 4");
    CHECK_EQ(console_len, 7);
    press(ORB_KEY_RETURN);
    CHECK_EQ(g->speed, 4);
    CHECK_EQ(console_len, 0);

    // cursor movement and deletion
    type("abcd");
    press(ORB_KEY_LEFT);
    press(ORB_KEY_BACKSPACE);
    CHECK(strcmp(console_line, "abd") == 0);
    press(ORB_KEY_HOME);
    press(ORB_KEY_DELETE);
    CHECK(strcmp(console_line, "bd") == 0);
    press(ORB_KEY_END);
    type("e");
    CHECK(strcmp(console_line, "bde") == 0);
    keys.keys[ORB_KEY_LEFT_CTRL] = true;
    press(ORB_KEY_W);
    CHECK_EQ(console_len, 0);
    keys.keys[ORB_KEY_LEFT_CTRL] = false;
    type("one two");
    keys.keys[ORB_KEY_LEFT_CTRL] = true;
    press(ORB_KEY_W);
    CHECK(strcmp(console_line, "one ") == 0);
    press(ORB_KEY_U);
    CHECK_EQ(console_len, 0);
    keys.keys[ORB_KEY_LEFT_CTRL] = false;

    // editing keys keep a codepoint whole
    type("a\xc3\xa9");
    press(ORB_KEY_BACKSPACE);
    CHECK(strcmp(console_line, "a") == 0);
    type("\xc3\xa9");
    press(ORB_KEY_LEFT);
    type("x");
    CHECK(strcmp(console_line, "ax\xc3\xa9") == 0);
    press(ORB_KEY_DELETE);
    CHECK(strcmp(console_line, "ax") == 0);
    clear_line();

    // ctrl+alt is AltGr: text still inserts
    keys.keys[ORB_KEY_LEFT_CTRL] = true;
    keys.keys[ORB_KEY_RIGHT_ALT] = true;
    type("@");
    CHECK(strcmp(console_line, "@") == 0);
    keys.keys[ORB_KEY_LEFT_CTRL] = false;
    keys.keys[ORB_KEY_RIGHT_ALT] = false;
    clear_line();

    // history, newest first, the live line kept
    type("speed 5");
    press(ORB_KEY_RETURN);
    type("live");
    press(ORB_KEY_UP);
    CHECK(strcmp(console_line, "speed 5") == 0);
    press(ORB_KEY_UP);
    CHECK(strcmp(console_line, "speed 4") == 0);
    press(ORB_KEY_DOWN);
    press(ORB_KEY_DOWN);
    CHECK(strcmp(console_line, "live") == 0);
    clear_line();

    // repeat: a held key acts on the press, then after 30 ticks every 4
    type("xxxxxxxxxx");
    keys.keys[ORB_KEY_BACKSPACE] = true;
    tick();
    CHECK_EQ(console_len, 9);
    for (int i = 0; i < 29; i++)
        tick();
    CHECK_EQ(console_len, 9);
    tick();
    CHECK_EQ(console_len, 8);
    for (int i = 0; i < 4; i++)
        tick();
    CHECK_EQ(console_len, 7);
    keys.keys[ORB_KEY_BACKSPACE] = false;
    tick();
    clear_line();

    // a codepoint that would split at the cap is dropped whole, not truncated
    char chunk[32];

    memset(chunk, 'a', 31);
    chunk[31] = 0;
    for (int i = 0; i < 4; i++)
        type(chunk);
    CHECK_EQ(console_len, 124);
    type("aa");
    CHECK_EQ(console_len, 126);
    type("\xc3\xa9");
    CHECK_EQ(console_len, 126);
    type("b");
    CHECK_EQ(console_len, 127);
    clear_line();

    type("\xc3");
    CHECK_EQ(console_len, 1);
    clear_line();

    // tab: a unique match completes with a space, several fill the common prefix
    type("tel");
    press(ORB_KEY_TAB);
    CHECK(strcmp(console_line, "teleport ") == 0);
    clear_line();
    orb_log_clear();
    type("s");
    press(ORB_KEY_TAB);
    CHECK(strcmp(console_line, "s") == 0); // scale, speed: no common prefix past s
    CHECK(orb_log_line_count() >= 2);
    clear_line();

    // the closing tick's text does not reach the game
    orb_input edge = {0};

    orb_console_step(&edge); // grave up, so the closing press below is a clean edge

    orb_input close = {0};

    close.keys[ORB_KEY_GRAVE] = true;
    memcpy(close.text, "`", 2);
    orb_console_step(&close);
    CHECK(!orb_console_open());
    CHECK_EQ(close.text[0], 0);
    CHECK(!close.keys[ORB_KEY_GRAVE]);

    edge = (orb_input) {0};
    orb_console_step(&edge); // grave back up, so the next real press is a clean edge
    press(ORB_KEY_GRAVE);    // reopen: the rest of the test expects the console open
    CHECK(orb_console_open());

    // escape closes, and the game sees the rest of that same tick's snapshot
    keys.keys[ORB_KEY_RIGHT] = true;
    keys.keys[ORB_KEY_ESCAPE] = true;
    tick();
    CHECK(!orb_console_open());
    CHECK(orb_key_down(ORB_KEY_RIGHT));
    CHECK(!orb_key_down(ORB_KEY_ESCAPE));
    keys.keys[ORB_KEY_RIGHT] = false;
    keys.keys[ORB_KEY_ESCAPE] = false;
    tick();
    return 0;
}

static int test_binds(void) {
    if (boot()) return 1;

    game_state* g = (game_state*)host_state.base;

    memset(&keys, 0, sizeof keys);
    orb_log_clear();
    orb_console_run("bind f5 \"speed 7\"");
    orb_console_run("bind f6 teleport 3 4");
    orb_console_run("binds");
    CHECK(strcmp(orb_log_line(1), "bind f5 \"speed 7\"") == 0);
    CHECK(strcmp(orb_log_line(0), "bind f6 \"teleport 3 4\"") == 0);
    g->speed = 0;
    press(ORB_KEY_F5);
    CHECK_EQ(g->speed, 7);
    CHECK(!orb_key_down(ORB_KEY_F5)); // hidden from the game
    press(ORB_KEY_F6);
    CHECK(orb_entity_get(g->player)->at.x == 3);

    // opening the console hides a bound key from the game and skips the bind
    press(ORB_KEY_GRAVE);
    CHECK(orb_console_open());
    g->speed = 0;
    keys.keys[ORB_KEY_F5] = keys.keys[ORB_KEY_ESCAPE] = true;
    tick();
    CHECK(!orb_console_open());
    CHECK(!orb_key_down(ORB_KEY_F5));
    CHECK_EQ(g->speed, 0);
    keys.keys[ORB_KEY_F5] = false;
    keys.keys[ORB_KEY_ESCAPE] = false;
    tick();

    orb_console_run("unbind f5");
    g->speed = 0;
    press(ORB_KEY_F5);
    CHECK_EQ(g->speed, 0);
    orb_console_run("bind nosuchkey speed 1");
    CHECK(strcmp(last(), "no key \"nosuchkey\"") == 0);
    orb_console_run("bind ` dump");
    CHECK(strcmp(last(), "` is the console's key") == 0);

    static const char* const sixteen[ORB_CONSOLE_BINDS] = {
        "f1", "f2",  "f3",  "f4",  "f5",   "f6",  "f7",     "f8",
        "f9", "f10", "f11", "f12", "home", "end", "insert", "print_screen",
    };

    for (int i = 0; i < ORB_CONSOLE_BINDS; i++) { // f6 is already bound: the table ends full
        char line[64];

        snprintf(line, sizeof line, "bind %s dump", sixteen[i]);
        orb_console_run(line);
    }

    orb_console_run("bind delete dump");
    CHECK(strcmp(last(), "binds are full") == 0);
    orb_console_clear(); // binds survive
    reload(g, orb_api_table());
    orb_console_run("binds");
    CHECK(orb_log_line_count() > 10);
    return 0;
}

static uint32_t frame_pixel(int x, int y) {
    return orb_os_headless_frame()[y * 64 + x];
}

// Draws into a buffer with 16 sentinel words on each side and checks nothing past the
// frame itself changed, and that the fill still ran.
static int draw_stays_inside(orb_size size, uint32_t dark) {
    uint32_t buf[16 + 65 * 13 + 16];
    int n = size.width * size.height;

    for (int i = 0; i < 16 + n + 16; i++)
        buf[i] = 0x12345678;

    orb_console_draw(buf + 16, size);

    for (int i = 0; i < 16; i++) {
        CHECK_EQ(buf[i], 0x12345678u);
        CHECK_EQ(buf[16 + n + i], 0x12345678u);
    }

    bool filled = false;

    for (int i = 0; i < n; i++)
        if (buf[16 + i] == dark) filled = true;

    CHECK(filled);
    return 0;
}

static int test_draw(void) {
    if (boot()) return 1;

    memset(&keys, 0, sizeof keys);
    orb_log_clear();
    orb_log("%s", "0123456789abcdef0123456789abcdef0123456789"); // 42 bytes: three rows of 16
    press(ORB_KEY_GRAVE);
    CHECK(orb_console_open());
    host_render();

    uint32_t dark = 0, bright = 0;

    orb_pal_extremes(orb_api_pal_base(), &dark, &bright);
    CHECK(dark != bright);
    CHECK_EQ(frame_pixel(63, 0), dark);  // the panel's top-right corner is blank
    CHECK_EQ(frame_pixel(0, 6), bright); // the prompt '>' has a pixel at its top-left
    CHECK(frame_pixel(0, 12) != dark || frame_pixel(20, 20) != dark); // the game is below

    bool bright_in_log_row = false;

    for (int x = 0; x < 64; x++)
        for (int y = 0; y < 6; y++)
            if (frame_pixel(x, y) == bright) bright_in_log_row = true;

    CHECK(bright_in_log_row); // the log line's last wrapped piece "89" sits above the editor
    CHECK_EQ(console_columns, 16);
    CHECK_EQ(console_rows, 2);

    static const orb_size guard_sizes[] = {{3, 4}, {65, 13}, {8, 5}, {64, 1}};

    for (int i = 0; i < 4; i++)
        if (draw_stays_inside(guard_sizes[i], dark)) return 1;

    host_render(); // back to the fixture's own 64 by 32, for the rest of the test
    press(ORB_KEY_ESCAPE);
    host_render();
    CHECK(frame_pixel(0, 6) != bright || frame_pixel(63, 0) != dark);
    return 0;
}

// Renders s through ORB_CONSOLE_FONT and compares its 3 by 5 body to the frame's row.
static bool row_text_matches(int row, const char* s) {
    uint32_t dark, bright;

    orb_pal_extremes(orb_api_pal_base(), &dark, &bright);

    for (int i = 0; s[i]; i++) {
        const uint8_t* g = ORB_CONSOLE_FONT[(unsigned char)s[i] - 32];

        for (int r = 0; r < 5; r++)
            for (int col = 0; col < 3; col++) {
                uint32_t want = g[r] & 0x80 >> col ? bright : dark;

                if (frame_pixel(i * 4 + col, row * 6 + r) != want) return false;
            }
    }

    return true;
}

static int test_scroll(void) {
    if (boot()) return 1;

    memset(&keys, 0, sizeof keys);
    press(ORB_KEY_GRAVE);
    CHECK(orb_console_open());

    orb_log_clear();

    for (int i = 0; i < 12; i++)
        orb_log("L%d", i);

    host_render();
    CHECK(row_text_matches(0, "L11"));

    press(ORB_KEY_PAGE_UP);
    host_render();
    CHECK(row_text_matches(0, "L10"));

    press(ORB_KEY_PAGE_UP);
    host_render();
    CHECK(row_text_matches(0, "L9"));

    press(ORB_KEY_PAGE_DOWN);
    host_render();
    CHECK(row_text_matches(0, "L10"));

    for (int i = 0; i < 30; i++)
        press(ORB_KEY_PAGE_UP);

    host_render();
    CHECK(row_text_matches(0, "L0")); // clamped at the oldest line

    type("help");
    press(ORB_KEY_RETURN); // a run scrolls back to the bottom
    orb_log_clear();
    orb_log("Z");
    host_render();
    CHECK(row_text_matches(0, "Z"));

    press(ORB_KEY_ESCAPE);
    CHECK(!orb_console_open());
    return 0;
}

static int test_clock(void) {
    if (boot()) return 1;

    game_state* g = (game_state*)host_state.base;

    memset(&keys, 0, sizeof keys);
    orb_os_headless_set_input(&keys);
    orb_clock_get()->paused = true;

    int ticks = g->ticks;

    for (int i = 0; i < 3; i++)
        CHECK(orb_frame());

    CHECK_EQ(g->ticks, ticks);
    orb_clock_get()->step = true;
    CHECK(orb_frame());
    CHECK_EQ(g->ticks, ticks + 1);
    CHECK(orb_frame());
    CHECK_EQ(g->ticks, ticks + 1);

    // a paused frame still pumps input, so the console opens and can unpause
    keys.keys[ORB_KEY_GRAVE] = true;
    orb_os_headless_set_input(&keys);
    CHECK(orb_frame());
    CHECK(orb_console_open());
    CHECK_EQ(g->ticks, ticks + 1);
    keys.keys[ORB_KEY_GRAVE] = false;
    snprintf(keys.text, sizeof keys.text, "pause 0");
    orb_os_headless_set_input(&keys);
    keys.text[0] = 0;
    CHECK(orb_frame());
    keys.keys[ORB_KEY_RETURN] = true;
    orb_os_headless_set_input(&keys);
    CHECK(orb_frame());
    CHECK(!orb_clock_get()->paused);
    keys.keys[ORB_KEY_RETURN] = false;
    keys.keys[ORB_KEY_ESCAPE] = true;
    orb_os_headless_set_input(&keys);
    CHECK(orb_frame());
    CHECK(!orb_console_open());
    keys.keys[ORB_KEY_ESCAPE] = false;
    orb_os_headless_set_input(&keys);
    CHECK(orb_frame());
    ticks = g->ticks;
    orb_clock_get()->timescale = 0;
    CHECK(orb_frame());
    CHECK_EQ(g->ticks, ticks); // no time passes at timescale 0
    orb_clock_get()->timescale = 1;

    orb_stats s = orb_stats_get();

    CHECK(s.state > 0 && s.pool > 0);
    CHECK(s.assets > 0 && s.cast_peak > 0);
    CHECK(s.release > s.state + s.pool);

    orb_console_run("stats");
    CHECK(strncmp(orb_log_line(0), "state ", 6) == 0);
    CHECK(strstr(orb_log_line(0), " sealed; frame ") != nullptr);
    CHECK(strstr(orb_log_line(0), " B,") || strstr(orb_log_line(0), " KB,"));
    CHECK_EQ(s.frame_ticks, 0);

    orb_clock_get()->step = true; // outside a pause a step is dropped, not saved up
    CHECK(orb_frame());
    CHECK(!orb_clock_get()->step);
    orb_log_clear();
    orb_clock_get()->timescale = 8; // the 4-tick clamp must not log "dropped 0 ticks"
    for (int i = 0; i < 3; i++)
        CHECK(orb_frame());
    CHECK(strncmp(orb_log_line(0), "dropped", 7) != 0);
    orb_clock_get()->timescale = 1;

    orb_console_run("quit");
    CHECK(!orb_frame());
    return 0;
}

int main(void) {
    if (test_log()) return 1;
    if (test_text()) return 1;
    if (test_font()) return 1;
    if (test_run()) return 1;
    if (test_entities()) return 1;
    if (test_editor()) return 1;
    if (test_binds()) return 1;
    if (test_draw()) return 1;
    if (test_scroll()) return 1;
    if (test_clock()) return 1;

    return 0;
}
