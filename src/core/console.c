#include "console.h"
#include "../graphics/pal.h"
#include "api.h"
#include "bytes.h"
#include "console_font.h"
#include "host.h"
#include "input.h"
#include "log.h"
#include "macros.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum console_kind { CONSOLE_INT, CONSOLE_FLOAT, CONSOLE_BOOL } console_kind;

typedef struct console_var {
    char name[ORB_CONSOLE_NAME];
    char help[ORB_CONSOLE_HELP];
    console_kind kind;
    void* at;
} console_var;

typedef struct console_command {
    char name[ORB_CONSOLE_NAME];
    char help[ORB_CONSOLE_HELP];
    orb_command_fn fn;
} console_command;

typedef struct console_builtin {
    const char* name;
    void (*fn)(int argc, const char* const* argv);
    const char* help;
} console_builtin;

typedef struct console_bind {
    int key;
    char line[ORB_CONSOLE_LINE];
} console_bind;

static void* console_state;
static const orb_api* console_api;
static console_var console_vars[ORB_CONSOLE_VARS];
static console_command console_commands[ORB_CONSOLE_COMMANDS];
static console_bind console_binds[ORB_CONSOLE_BINDS];
static int console_var_count, console_command_count;
static int console_bind_count;
static int console_fixed_vars = -1, console_fixed_commands = -1; // orb's own; -1 before boot
static bool console_registering = true; // until boot, then between a clear and the next step
static bool console_is_open;
static orb_input console_before; // the previous tick's raw snapshot, for edges
static char console_line[ORB_CONSOLE_LINE];
static int console_len, console_cursor;
static char console_history[ORB_CONSOLE_HISTORY][ORB_CONSOLE_LINE];
static int console_history_count, console_history_at = -1; // -1: the live line
static char console_live[ORB_CONSOLE_LINE];
static int console_scroll, console_columns, console_rows; // rows and columns from the last draw
static int console_height; // frame height from the last draw, for the bottom-row clip
static int console_held, console_held_ticks;

static bool console_name_ok(const char* name) {
    size_t n = strlen(name);

    if (n == 0 || n >= ORB_CONSOLE_NAME || name[0] < 'a' || name[0] > 'z') return false;

    for (size_t i = 0; i < n; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
            return false;
    }

    return true;
}

static console_var* console_var_find(const char* name) {
    for (int i = 0; i < console_var_count; i++)
        if (strcmp(console_vars[i].name, name) == 0) return &console_vars[i];

    return nullptr;
}

static console_command* console_command_find(const char* name) {
    for (int i = 0; i < console_command_count; i++)
        if (strcmp(console_commands[i].name, name) == 0) return &console_commands[i];

    return nullptr;
}

static void console_var_write(const console_var* var, const char* separator) {
    switch (var->kind) {
    case CONSOLE_INT:
        orb_log("%s%s%d", var->name, separator, *(int32_t*)var->at);
        break;
    case CONSOLE_FLOAT:
        orb_log("%s%s%g", var->name, separator, (double)*(float*)var->at);
        break;
    case CONSOLE_BOOL:
        orb_log("%s%s%s", var->name, separator, *(bool*)var->at ? "true" : "false");
        break;
    }
}

static console_bind* console_bind_find(int key) {
    for (int i = 0; i < console_bind_count; i++)
        if (console_binds[i].key == key) return &console_binds[i];

    return nullptr;
}

static void console_bind_add(int argc, const char* const* argv) {
    if (argc < 3) {
        orb_log("bind <key> <command>");
        return;
    }

    int key = orb_input_symbol_position(argv[1]);

    if (key == ORB_KEY_NONE) {
        orb_log("no key \"%s\"", argv[1]);
        return;
    }

    if (key == ORB_KEY_GRAVE) {
        orb_log("%s is the console's key", argv[1]);
        return;
    }

    console_bind* bind = console_bind_find(key);

    if (!bind && console_bind_count == ORB_CONSOLE_BINDS) {
        orb_log("binds are full");
        return;
    }

    if (!bind) bind = &console_binds[console_bind_count++];

    bind->key = key;
    bind->line[0] = 0;

    for (int i = 2; i < argc; i++) {
        size_t n = strlen(bind->line);

        snprintf(bind->line + n, sizeof bind->line - n, "%s%s", i > 2 ? " " : "", argv[i]);
    }
}

static void console_bind_remove(int argc, const char* const* argv) {
    console_bind* bind = argc > 1 ? console_bind_find(orb_input_symbol_position(argv[1])) : nullptr;

    if (!bind) return;

    *bind = console_binds[--console_bind_count];
}

static void console_bind_list(int, const char* const*) {
    for (int i = 0; i < console_bind_count; i++)
        orb_log("bind %s \"%s\"", orb_key_name(console_binds[i].key), console_binds[i].line);
}

static void console_dump(int, const char* const*) {
    for (int i = 0; i < console_var_count; i++)
        console_var_write(&console_vars[i], " ");
}

static void console_clear_log(int, const char* const*) {
    orb_log_clear();
}

static void console_quit(int, const char* const*) {
    orb_quit_request();
}

static void console_help(int argc, const char* const* argv);

static const console_builtin console_builtins[] = {
    {"help", console_help, "help [prefix]: the commands, or everything named like prefix"},
    {"dump", console_dump, "every variable as name value"},
    {"clear", console_clear_log, "empty the log"},
    {"quit", console_quit, "close the game"},
    {"bind", console_bind_add, "bind <key> <command>: run it when the key is pressed"},
    {"unbind", console_bind_remove, "unbind <key>"},
    {"binds", console_bind_list, "the binds"},
    {nullptr, nullptr, nullptr},
};

static const console_builtin* console_builtin_find(const char* name) {
    for (const console_builtin* builtin = console_builtins; builtin->name; builtin++)
        if (strcmp(builtin->name, name) == 0) return builtin;

    return nullptr;
}

static bool console_may_register(const char* name, int count, int cap) {
    if (!console_registering) {
        orb_log("console: \"%s\" registered outside reload", name);
        return false;
    }

    if (!console_name_ok(name) || console_var_find(name) || console_command_find(name) ||
        console_builtin_find(name)) {
        orb_log("console: bad or taken name \"%s\"", name);
        return false;
    }

    if (count == cap) {
        orb_log("console: no room for \"%s\"", name);
        return false;
    }

    return true;
}

static void console_var_add(const char* name, console_kind kind, void* at, const char* help) {
    if (!console_may_register(name, console_var_count, ORB_CONSOLE_VARS)) return;

    console_var* var = &console_vars[console_var_count++];

    snprintf(var->name, sizeof var->name, "%s", name);
    snprintf(var->help, sizeof var->help, "%s", help ? help : "");
    var->kind = kind;
    var->at = at;
}

static bool console_var_set(console_var* var, const char* text) {
    char* end;

    switch (var->kind) {
    case CONSOLE_INT: {
        errno = 0;

        long value = strtol(text, &end, 0);

        if (!*text || *end || errno || value < INT32_MIN || value > INT32_MAX) return false;

        *(int32_t*)var->at = (int32_t)value;
        return true;
    }
    case CONSOLE_FLOAT: {
        float value = strtof(text, &end);

        if (!*text || *end || !(value - value == 0)) return false; // NaN and infinity fail the test

        *(float*)var->at = value;
        return true;
    }
    case CONSOLE_BOOL: {
        bool on = !strcmp(text, "1") || !strcmp(text, "true") || !strcmp(text, "on");
        bool off = !strcmp(text, "0") || !strcmp(text, "false") || !strcmp(text, "off");

        if (!on && !off) return false;

        *(bool*)var->at = on;
        return true;
    }
    }

    return false;
}

// Splits in place: runs of spaces separate, a quoted span is one argument, an
// unterminated quote runs to the end.
static int console_split(char* line, const char* argv[ORB_CONSOLE_ARGS]) {
    int argc = 0;

    for (char* scan = line; *scan && argc < ORB_CONSOLE_ARGS;) {
        while (*scan == ' ')
            scan++;

        if (!*scan) break;

        if (*scan == '"') {
            argv[argc++] = ++scan;

            while (*scan && *scan != '"')
                scan++;
        } else {
            argv[argc++] = scan;

            while (*scan && *scan != ' ')
                scan++;
        }

        if (*scan) *scan++ = 0;
    }

    return argc;
}

static bool console_pressed(const orb_input* raw, int key) {
    return raw->keys[key] && !console_before.keys[key];
}

// A press acts at once; a hold acts again after 30 ticks, then every 4.
static bool console_repeats(const orb_input* raw, int key) {
    if (console_pressed(raw, key)) {
        console_held = key;
        console_held_ticks = 0;
        return true;
    }

    if (console_held != key || !raw->keys[key]) return false;

    console_held_ticks++;
    return console_held_ticks >= 30 && (console_held_ticks - 30) % 4 == 0;
}

static void console_set_line(const char* text) {
    snprintf(console_line, sizeof console_line, "%s", text);
    console_len = (int)strlen(console_line);
    console_cursor = console_len;
}

// A codepoint is inserted whole or, at the cap, dropped whole; a malformed byte goes in
// as itself.
static void console_insert(const char* text) {
    while (*text) {
        uint32_t codepoint;
        int len = orb_bytes_utf8_decode(text, &codepoint);

        if (len == 0) len = 1;
        if (console_len + len > ORB_CONSOLE_LINE - 1) return;

        memmove(
            console_line + console_cursor + len, console_line + console_cursor,
            (size_t)(console_len - console_cursor + 1)
        );
        memcpy(console_line + console_cursor, text, (size_t)len);
        console_cursor += len;
        console_len += len;
        text += len;
    }
}

static void console_erase(int from, int to) {
    memmove(console_line + from, console_line + to, (size_t)(console_len - to + 1));
    console_len -= to - from;
    console_cursor = from;
}

// The byte offsets of the codepoints before and after at, so editing keeps sequences whole.
static int console_previous(int at) {
    while (at > 0 && ((unsigned char)console_line[--at] & 0xc0) == 0x80)
        continue;

    return at;
}

static int console_next(int at) {
    if (at < console_len) at++;

    while (at < console_len && ((unsigned char)console_line[at] & 0xc0) == 0x80)
        at++;

    return at;
}

static void console_history_push(const char* line) {
    if (console_history_count == ORB_CONSOLE_HISTORY) {
        memmove(
            console_history[0], console_history[1],
            sizeof console_history[0] * (ORB_CONSOLE_HISTORY - 1)
        );
        console_history_count--;
    }

    snprintf(console_history[console_history_count++], ORB_CONSOLE_LINE, "%s", line);
    console_history_at = -1;
}

static void console_history_walk(int direction) {
    int next = console_history_at + direction;

    if (next < -1 || next >= console_history_count) return;
    if (console_history_at == -1) snprintf(console_live, sizeof console_live, "%s", console_line);

    console_history_at = next;
    console_set_line(next == -1 ? console_live : console_history[console_history_count - 1 - next]);
}

// The longest common prefix of every name starting with the line, filled in; a unique
// match gets a trailing space; several are listed.
static void console_complete(void) {
    if (memchr(console_line, ' ', (size_t)console_len)) return;

    const char* matches[ORB_CONSOLE_VARS + ORB_CONSOLE_COMMANDS + 8];
    int n = 0;
    size_t len = (size_t)console_len;

    for (const console_builtin* builtin = console_builtins; builtin->name; builtin++)
        if (strncmp(builtin->name, console_line, len) == 0) matches[n++] = builtin->name;

    for (int i = 0; i < console_command_count; i++)
        if (strncmp(console_commands[i].name, console_line, len) == 0)
            matches[n++] = console_commands[i].name;

    for (int i = 0; i < console_var_count; i++)
        if (strncmp(console_vars[i].name, console_line, len) == 0)
            matches[n++] = console_vars[i].name;

    if (n == 0) return;

    if (n == 1) {
        char full[ORB_CONSOLE_LINE];

        snprintf(full, sizeof full, "%s ", matches[0]);
        console_set_line(full);
        return;
    }

    size_t common = strlen(matches[0]);

    for (int i = 1; i < n; i++) {
        size_t k = 0;

        while (k < common && matches[i][k] == matches[0][k])
            k++;

        common = k;
    }

    char prefix[ORB_CONSOLE_LINE];

    snprintf(prefix, sizeof prefix, "%.*s", (int)common, matches[0]);
    console_set_line(prefix);

    for (int i = 0; i < n; i++)
        orb_log("%s", matches[i]);
}

static void console_page(int direction) {
    int total = 0;

    for (int back = 0; back < orb_log_line_count(); back++) {
        int len = (int)strlen(orb_log_line(back));

        total += console_columns ? orb_max(1, (len + console_columns - 1) / console_columns) : 1;
    }

    int page = orb_max(1, console_rows - 1);

    console_scroll = orb_clamp(console_scroll + direction * page, 0, orb_max(0, total - page));
}

static void console_edit(const orb_input* raw) {
    // Windows reports AltGr as Ctrl+Alt, and both backends already drop the control
    // characters ctrl+letter produces.
    bool ctrl = (raw->keys[ORB_KEY_LEFT_CTRL] || raw->keys[ORB_KEY_RIGHT_CTRL]) &&
                !(raw->keys[ORB_KEY_LEFT_ALT] || raw->keys[ORB_KEY_RIGHT_ALT]);

    if (console_repeats(raw, ORB_KEY_LEFT)) console_cursor = console_previous(console_cursor);
    if (console_repeats(raw, ORB_KEY_RIGHT)) console_cursor = console_next(console_cursor);
    if (console_repeats(raw, ORB_KEY_HOME)) console_cursor = 0;
    if (console_repeats(raw, ORB_KEY_END)) console_cursor = console_len;
    if (console_repeats(raw, ORB_KEY_BACKSPACE))
        console_erase(console_previous(console_cursor), console_cursor);
    if (console_repeats(raw, ORB_KEY_DELETE))
        console_erase(console_cursor, console_next(console_cursor));
    if (console_repeats(raw, ORB_KEY_UP)) console_history_walk(1);
    if (console_repeats(raw, ORB_KEY_DOWN)) console_history_walk(-1);
    if (console_pressed(raw, ORB_KEY_PAGE_UP)) console_page(1);
    if (console_pressed(raw, ORB_KEY_PAGE_DOWN)) console_page(-1);
    if (console_pressed(raw, ORB_KEY_TAB)) console_complete();
    if (console_pressed(raw, ORB_KEY_ESCAPE)) console_is_open = false;

    if (ctrl && console_pressed(raw, ORB_KEY_U)) console_erase(0, console_len);

    if (ctrl && console_pressed(raw, ORB_KEY_W)) {
        int from = console_cursor;

        while (from > 0 && console_line[from - 1] == ' ')
            from--;

        while (from > 0 && console_line[from - 1] != ' ')
            from--;

        console_erase(from, console_cursor);
    }

    console_insert(raw->text);

    if (console_pressed(raw, ORB_KEY_RETURN)) {
        if (console_len) console_history_push(console_line);

        char line[ORB_CONSOLE_LINE];

        snprintf(line, sizeof line, "%s", console_line);
        console_set_line("");
        console_scroll = 0;
        orb_console_run(line);
    }
}

static void console_help(int argc, const char* const* argv) {
    const char* prefix = argc > 1 ? argv[1] : nullptr;
    size_t n = prefix ? strlen(prefix) : 0;

    for (const console_builtin* builtin = console_builtins; builtin->name; builtin++)
        if (!prefix || strncmp(builtin->name, prefix, n) == 0)
            orb_log("%s  %s", builtin->name, builtin->help);

    for (int i = 0; i < console_command_count; i++)
        if (!prefix || strncmp(console_commands[i].name, prefix, n) == 0)
            orb_log("%s  %s", console_commands[i].name, console_commands[i].help);

    if (prefix) {
        for (int i = 0; i < console_var_count; i++)
            if (strncmp(console_vars[i].name, prefix, n) == 0)
                orb_log("%s  %s", console_vars[i].name, console_vars[i].help);
    } else
        orb_log("%d variables", console_var_count);
}

void orb_console_run(const char* line) {
    char copy[ORB_CONSOLE_LINE];
    const char* argv[ORB_CONSOLE_ARGS];

    snprintf(copy, sizeof copy, "%s", line);

    int argc = console_split(copy, argv);

    if (!argc) return;

    const console_builtin* builtin = console_builtin_find(argv[0]);

    if (builtin) {
        builtin->fn(argc, argv);
        return;
    }

    console_command* command = console_command_find(argv[0]);

    if (command) {
        command->fn(console_state, console_api, argc, argv);
        return;
    }

    console_var* var = console_var_find(argv[0]);

    if (!var)
        orb_log("unknown: %s", argv[0]);
    else if (argc == 1)
        console_var_write(var, " = ");
    else if (argc > 2)
        orb_log("%s takes one value", var->name);
    else if (!console_var_set(var, argv[1]))
        orb_log("bad value for %s: %s", var->name, argv[1]);
}

bool orb_console_open(void) {
    return console_is_open;
}

void orb_console_var_int(const char* name, int32_t* at, const char* help) {
    console_var_add(name, CONSOLE_INT, at, help);
}

void orb_console_var_float(const char* name, float* at, const char* help) {
    console_var_add(name, CONSOLE_FLOAT, at, help);
}

void orb_console_var_bool(const char* name, bool* at, const char* help) {
    console_var_add(name, CONSOLE_BOOL, at, help);
}

void orb_console_command(const char* name, orb_command_fn fn, const char* help) {
    if (!console_may_register(name, console_command_count, ORB_CONSOLE_COMMANDS)) return;

    console_command* command = &console_commands[console_command_count++];

    snprintf(command->name, sizeof command->name, "%s", name);
    snprintf(command->help, sizeof command->help, "%s", help ? help : "");
    command->fn = fn;
}

void orb_console_boot(void* state, const orb_api* api) {
    console_state = state;
    console_api = api;
    console_is_open = false;

    if (console_fixed_vars < 0) {
        console_fixed_vars = console_var_count;
        console_fixed_commands = console_command_count;
    }

    console_registering = false;
}

void orb_console_step(orb_input* input) {
    orb_input raw = *input;

    console_registering = false;

    if (console_pressed(&raw, ORB_KEY_GRAVE)) {
        console_is_open = !console_is_open;
        console_held = 0;
    }

    // The grave key's text is dropped while it is held, since autorepeat keeps sending it.
    if (raw.keys[ORB_KEY_GRAVE]) {
        raw.text[0] = 0;
        input->text[0] = 0;
    }

    input->keys[ORB_KEY_GRAVE] = false;

    // A press while the console is closed runs the bind; a bind may rebind, so the keys
    // are hidden from the game in a second pass over the table as it stands after.
    if (!console_is_open)
        for (int i = 0; i < console_bind_count; i++)
            if (console_pressed(&raw, console_binds[i].key)) orb_console_run(console_binds[i].line);

    for (int i = 0; i < console_bind_count; i++)
        input->keys[console_binds[i].key] = false;

    if (console_is_open) {
        console_edit(&raw);

        if (console_is_open)
            *input = (orb_input) {};
        else
            input->keys[ORB_KEY_ESCAPE] = false;
    }

    console_before = raw;
}

static void console_glyph(
    uint32_t* rgb,
    int width,
    int x,
    int y,
    unsigned char character,
    uint32_t color
) {
    const uint8_t* glyph =
        ORB_CONSOLE_FONT[character < 32 || character > 126 ? '?' - 32 : character - 32];

    for (int row = 0; row < 6 && y + row < console_height; row++)
        for (int col = 0; col < 3; col++)
            if (glyph[row] & 0x80 >> col) rgb[(y + row) * width + x + col] = color;
}

static void console_text(
    uint32_t* rgb,
    int width,
    int row,
    const char* text,
    int n,
    uint32_t color
) {
    for (int i = 0; i < n && text[i]; i++)
        console_glyph(rgb, width, i * 4, row * 6, (unsigned char)text[i], color);
}

// The prompt, the part of the line around the cursor that fits after it, and a filled
// cursor cell.
static void console_editor_row(uint32_t* rgb, int width, int row, uint32_t color) {
    int fit = console_columns - 1;
    int first = orb_max(0, console_cursor - (fit - 1));

    console_glyph(rgb, width, 0, row * 6, '>', color);
    console_text(rgb + 4, width, row, console_line + first, fit, color);

    int x = (1 + console_cursor - first) * 4;

    for (int y = 0; y < 6 && row * 6 + y < console_height; y++)
        for (int col = 0; col < 4; col++)
            rgb[(row * 6 + y) * width + x + col] = color;
}

void orb_console_draw(uint32_t* rgb, orb_size size) {
    if (!console_is_open) return;

    console_columns = size.width / 4;
    console_rows = orb_max(1, size.height / 2 / 6);
    console_height = size.height;

    uint32_t dark = 0, bright = 0;

    orb_pal_extremes(orb_api_pal_base(), &dark, &bright);

    for (int i = 0; i < orb_min(console_rows * 6, size.height) * size.width; i++)
        rgb[i] = dark;

    if (console_columns < 2) return; // no room for a prompt cell plus a cursor cell

    console_editor_row(rgb, size.width, console_rows - 1, bright);

    int row = console_rows - 2, skip = console_scroll;

    for (int back = 0; row >= 0 && back < orb_log_line_count(); back++) {
        const char* line = orb_log_line(back);
        int len = (int)strlen(line),
            pieces = orb_max(1, (len + console_columns - 1) / console_columns);

        for (int piece = pieces - 1; piece >= 0 && row >= 0; piece--) {
            if (skip > 0) {
                skip--;
                continue;
            }

            console_text(
                rgb, size.width, row, line + piece * console_columns, console_columns, bright
            );
            row--;
        }
    }
}

void orb_console_clear(void) {
    console_var_count = orb_max(console_fixed_vars, 0);
    console_command_count = orb_max(console_fixed_commands, 0);
    console_registering = true;
}
