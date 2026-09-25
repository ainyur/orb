#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t v;
} orb_anim;

typedef struct orb_anim_state {
    orb_anim anim;
    uint16_t frame, ticks;
} orb_anim_state;

typedef struct {
    uint32_t v;
} orb_font;

typedef struct orb_layer_info {
    int grid, columns, rows;
    bool has_tiles, has_cells;
} orb_layer_info;

typedef struct {
    uint32_t v;
} orb_level;

typedef struct {
    uint32_t v;
} orb_sample;

typedef struct orb_size {
    int width, height;
} orb_size;

typedef struct {
    uint32_t v;
} orb_song;

typedef struct orb_song_position {
    int32_t ms;         // into the song's file; both -1 when no song plays
    int32_t millibeats; // beats * 1000, from the manifest's bpm
} orb_song_position;

typedef struct orb_sound_params {
    float volume, pan; // 0..1, -1..1
    int pitch_cents;   // -2400..2400; 0 is the sample's own rate
} orb_sound_params;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} orb_span;

typedef struct {
    uint32_t v;
} orb_sprite;

typedef struct orb_vec2 {
    int x, y;
} orb_vec2;

typedef struct orb_vec2f {
    float x, y;
} orb_vec2f;

typedef struct orb_rect {
    orb_vec2 at;
    orb_size size;
} orb_rect;

typedef struct orb_camera {
    orb_vec2f at;     // world position of the framebuffer's top-left
    orb_vec2f target; // world point to center
    float lerp;       // 0 snaps to the target, 1 never moves
    orb_rect bounds;  // zero size means no clamp
    orb_vec2f shake;  // added after the clamp
} orb_camera;

// Opaque: a game only passes it back. Not an ORB_HANDLE_INDEX handle.
typedef struct {
    uint32_t v;
} orb_voice;

typedef struct orb_volumes {
    float master, song, sound; // 0..1 each, 1 by default
} orb_volumes;

typedef enum orb_button {
    ORB_BTN_UP,
    ORB_BTN_DOWN,
    ORB_BTN_LEFT,
    ORB_BTN_RIGHT,
    ORB_BTN_A,
    ORB_BTN_B,
    ORB_BTN_X,
    ORB_BTN_Y,
    ORB_BTN_L,
    ORB_BTN_R,
    ORB_BTN_START,
    ORB_BTN_SELECT,
    ORB_BTN_COUNT
} orb_button;

// Physical positions, as USB HID keyboard usage IDs. A position without an
// enumerator is still a position.
typedef enum orb_key {
    ORB_KEY_NONE = 0,
    ORB_KEY_A = 4,
    ORB_KEY_B,
    ORB_KEY_C,
    ORB_KEY_D,
    ORB_KEY_E,
    ORB_KEY_F,
    ORB_KEY_G,
    ORB_KEY_H,
    ORB_KEY_I,
    ORB_KEY_J,
    ORB_KEY_K,
    ORB_KEY_L,
    ORB_KEY_M,
    ORB_KEY_N,
    ORB_KEY_O,
    ORB_KEY_P,
    ORB_KEY_Q,
    ORB_KEY_R,
    ORB_KEY_S,
    ORB_KEY_T,
    ORB_KEY_U,
    ORB_KEY_V,
    ORB_KEY_W,
    ORB_KEY_X,
    ORB_KEY_Y,
    ORB_KEY_Z,
    ORB_KEY_1 = 30,
    ORB_KEY_2,
    ORB_KEY_3,
    ORB_KEY_4,
    ORB_KEY_5,
    ORB_KEY_6,
    ORB_KEY_7,
    ORB_KEY_8,
    ORB_KEY_9,
    ORB_KEY_0,
    ORB_KEY_RETURN = 40,
    ORB_KEY_ESCAPE,
    ORB_KEY_BACKSPACE,
    ORB_KEY_TAB,
    ORB_KEY_SPACE,
    ORB_KEY_MINUS = 45,
    ORB_KEY_EQUALS,
    ORB_KEY_LEFT_BRACKET,
    ORB_KEY_RIGHT_BRACKET,
    ORB_KEY_BACKSLASH,
    ORB_KEY_NON_US_HASH,
    ORB_KEY_SEMICOLON,
    ORB_KEY_APOSTROPHE,
    ORB_KEY_GRAVE,
    ORB_KEY_COMMA,
    ORB_KEY_PERIOD,
    ORB_KEY_SLASH,
    ORB_KEY_CAPS_LOCK = 57,
    ORB_KEY_F1 = 58,
    ORB_KEY_F2,
    ORB_KEY_F3,
    ORB_KEY_F4,
    ORB_KEY_F5,
    ORB_KEY_F6,
    ORB_KEY_F7,
    ORB_KEY_F8,
    ORB_KEY_F9,
    ORB_KEY_F10,
    ORB_KEY_F11,
    ORB_KEY_F12,
    ORB_KEY_PRINT_SCREEN = 70,
    ORB_KEY_SCROLL_LOCK,
    ORB_KEY_PAUSE,
    ORB_KEY_INSERT,
    ORB_KEY_HOME,
    ORB_KEY_PAGE_UP,
    ORB_KEY_DELETE,
    ORB_KEY_END,
    ORB_KEY_PAGE_DOWN,
    ORB_KEY_RIGHT,
    ORB_KEY_LEFT,
    ORB_KEY_DOWN,
    ORB_KEY_UP,
    ORB_KEY_NUM_LOCK = 83,
    ORB_KEY_KP_DIVIDE,
    ORB_KEY_KP_MULTIPLY,
    ORB_KEY_KP_MINUS,
    ORB_KEY_KP_PLUS,
    ORB_KEY_KP_ENTER,
    ORB_KEY_KP_1,
    ORB_KEY_KP_2,
    ORB_KEY_KP_3,
    ORB_KEY_KP_4,
    ORB_KEY_KP_5,
    ORB_KEY_KP_6,
    ORB_KEY_KP_7,
    ORB_KEY_KP_8,
    ORB_KEY_KP_9,
    ORB_KEY_KP_0,
    ORB_KEY_KP_PERIOD,
    ORB_KEY_NON_US_BACKSLASH = 100,
    ORB_KEY_APPLICATION,
    ORB_KEY_LEFT_CTRL = 224,
    ORB_KEY_LEFT_SHIFT,
    ORB_KEY_LEFT_ALT,
    ORB_KEY_LEFT_GUI,
    ORB_KEY_RIGHT_CTRL,
    ORB_KEY_RIGHT_SHIFT,
    ORB_KEY_RIGHT_ALT,
    ORB_KEY_RIGHT_GUI,
    ORB_KEY_COUNT = 256
} orb_key;

// Pad buttons by position on an Xbox pad. The values continue above the keys, so a
// binding's source is a key or a pad button by value.
typedef enum orb_pad {
    ORB_PAD_NONE = 512,
    ORB_PAD_SOUTH, // Xbox A
    ORB_PAD_EAST,  // Xbox B
    ORB_PAD_WEST,  // Xbox X
    ORB_PAD_NORTH, // Xbox Y
    ORB_PAD_LEFT_SHOULDER,
    ORB_PAD_RIGHT_SHOULDER,
    ORB_PAD_LEFT_TRIGGER, // down from half its travel
    ORB_PAD_RIGHT_TRIGGER,
    ORB_PAD_BACK,
    ORB_PAD_START,
    ORB_PAD_LEFT_STICK, // the stick's click
    ORB_PAD_RIGHT_STICK,
    ORB_PAD_UP, // the d-pad
    ORB_PAD_DOWN,
    ORB_PAD_LEFT,
    ORB_PAD_RIGHT,
    ORB_PAD_END
} orb_pad;

// The family whose labels a pad carries.
typedef enum orb_pad_make : uint8_t {
    ORB_PAD_MAKE_NONE, // no pad
    ORB_PAD_MAKE_OTHER,
    ORB_PAD_MAKE_XBOX,
    ORB_PAD_MAKE_PLAYSTATION,
    ORB_PAD_MAKE_NINTENDO,
} orb_pad_make;

constexpr int ORB_INPUT_TEXT = 32;
constexpr int ORB_PAD_BUTTONS = ORB_PAD_END - ORB_PAD_NONE; // index 0 unused

// The pad as the OS layer reports it: raw axes, and every button but the triggers.
typedef struct orb_pad_input {
    orb_pad_make make;
    bool buttons[ORB_PAD_BUTTONS]; // by pad - ORB_PAD_NONE
    struct {
        int16_t x, y;
    } left_stick, right_stick;           // -32767..32767 each, y down
    int16_t left_trigger, right_trigger; // 0..32767
} orb_pad_input;

// The keyboard and the pad as the OS layer reports them each tick.
typedef struct orb_input {
    bool keys[ORB_KEY_COUNT];  // by position
    char text[ORB_INPUT_TEXT]; // UTF-8 the layout produced this tick, NUL-terminated
    orb_pad_input pad;
} orb_input;

typedef enum orb_level_dir : uint8_t {
    ORB_LEVEL_N,
    ORB_LEVEL_S,
    ORB_LEVEL_E,
    ORB_LEVEL_W,
    ORB_LEVEL_NE,
    ORB_LEVEL_NW,
    ORB_LEVEL_SE,
    ORB_LEVEL_SW,
    ORB_LEVEL_LOWER,
    ORB_LEVEL_HIGHER,
    ORB_LEVEL_OVERLAP
} orb_level_dir;

typedef struct orb_level_neighbor {
    orb_level level;
    orb_level_dir dir;
} orb_level_neighbor;

#define ORB_ANIM(index) ((orb_anim) {(uint32_t)(index)})
#define ORB_FONT(index) ((orb_font) {(uint32_t)(index)})
#define ORB_LEVEL(index) ((orb_level) {(uint32_t)(index)})
#define ORB_SAMPLE(index) ((orb_sample) {(uint32_t)(index)})
#define ORB_SONG(index) ((orb_song) {(uint32_t)(index)})
#define ORB_SPRITE(index) ((orb_sprite) {(uint32_t)(index)})
#define ORB_HANDLE_INDEX(handle)                                                                   \
    ((handle).v & ORB_NO_INDEX) // v = handle index (24 bits) | generation << 24
#define ORB_HANDLE_GEN(handle) ((handle).v >> 24)

constexpr uint32_t ORB_NO_INDEX = 0xffffffu;
constexpr orb_anim ORB_NO_ANIM = {ORB_NO_INDEX};
constexpr orb_font ORB_NO_FONT = {ORB_NO_INDEX};
constexpr orb_level ORB_NO_LEVEL = {ORB_NO_INDEX};
constexpr orb_sample ORB_NO_SAMPLE = {ORB_NO_INDEX};
constexpr orb_song ORB_NO_SONG = {ORB_NO_INDEX};
constexpr orb_sprite ORB_NO_SPRITE = {ORB_NO_INDEX};
constexpr orb_voice ORB_NO_VOICE = {0xffffu};

typedef struct {
    uint32_t v;
} orb_entity_id;

typedef struct {
    uint32_t v;
} orb_type;

#define ORB_ENTITY(index) ((orb_entity_id) {(uint32_t)(index)})
#define ORB_TYPE(index) ((orb_type) {(uint32_t)(index)})

constexpr orb_entity_id ORB_NO_ENTITY = {ORB_NO_INDEX};
constexpr orb_type ORB_NO_TYPE = {ORB_NO_INDEX};

constexpr int ORB_COMPONENT_SPRITE = 0;
constexpr int ORB_COMPONENT_BODY = 1;
constexpr int ORB_COMPONENT_TAG = 2;
// the first of the game's kinds, in orb_config.components order
constexpr int ORB_COMPONENT_GAME = 3;
constexpr int ORB_MAX_COMPONENTS = 32;
constexpr uint32_t ORB_MAX_ENTITIES = 1 << 16;
constexpr int ORB_MAX_REMAPS = 8;
constexpr int ORB_MAX_SPRITE_LAYERS = 8;

typedef struct orb_entity {
    uint64_t iid; // the placement's iid hash, or 0 for a runtime spawn
    orb_entity_id self;
    orb_type type;
    orb_level level; // the level it was placed or spawned in, or ORB_NO_LEVEL
    orb_entity_id parent;
    uint32_t placement;  // its placement index, or ORB_NO_INDEX
    uint32_t components; // bit per live kind
    uint16_t flags;      // ORB_ENTITY_*
    uint8_t pad[2];
    orb_vec2f at; // world pixels; relative to the parent when set
    orb_size size;
    uint8_t pad2[4];
} orb_entity;

static_assert(sizeof(orb_entity) == 56, "orb_entity layout");

constexpr uint16_t ORB_ENTITY_VISIBLE = 1;
constexpr uint16_t ORB_ENTITY_PAUSED = 2;
constexpr uint16_t ORB_ENTITY_PERSISTENT = 4;
constexpr uint16_t ORB_ENTITY_DESPAWNING = 8;
constexpr uint16_t ORB_ENTITY_NEW = 16; // orb's: spawned inside the update in progress

typedef struct orb_sprite_component {
    orb_sprite sprite;   // drawn when anim is ORB_NO_ANIM
    orb_anim_state anim; // stepped by world_update; its frame is drawn when set
    uint32_t flags;      // ORB_FLIP_X, ORB_FLIP_Y
    orb_vec2 offset;     // draw position relative to the world position
    int8_t remap;        // remap table index, -1 for none
    int8_t sort_bias;    // added to the sort key
    uint8_t layer;       // 0 to ORB_MAX_SPRITE_LAYERS - 1; world_draw takes one
    uint8_t pad;
} orb_sprite_component;

typedef struct orb_body {
    orb_vec2f velocity; // pixels per tick
    orb_vec2f impact;   // the velocity a blocked axis had before the block, else 0
    orb_vec2f moved;    // how far the last update moved it, world pixels
    orb_vec2f last_at;  // world position at the end of the last update, kept by orb; setting
                        // parent after entity_add moves the body without moving this, so a
                        // solid reports that jump as moved on the next update
    float gravity;      // scale on the world gravity, 0 for none
    orb_rect box;       // relative to the world position; entity_add sets it to the entity's size
    uint16_t flags;     // ORB_BODY_*
    uint8_t pad[2];
    orb_entity_id standing_on; // the solid under it after the last update, or ORB_NO_ENTITY
    orb_entity_id carrier;     // a solid the game says moves it, or ORB_NO_ENTITY
} orb_body;

static_assert(sizeof(orb_body) == 64, "orb_body layout");

constexpr uint16_t ORB_BODY_SOLID = 1;
// with SOLID: solid only to a body arriving from the north
constexpr uint16_t ORB_BODY_ONEWAY_N = 2;
constexpr uint16_t ORB_BODY_ONEWAY_S = 4;
constexpr uint16_t ORB_BODY_ONEWAY_E = 8;
constexpr uint16_t ORB_BODY_ONEWAY_W = 16;
constexpr uint16_t ORB_BODY_DROP = 32; // this update treats every one-way as open; cleared after
constexpr uint16_t ORB_BODY_GROUNDED = 64; // set by the last update
constexpr uint16_t ORB_BODY_CEILING = 128;
constexpr uint16_t ORB_BODY_WALL_LEFT = 256;
constexpr uint16_t ORB_BODY_WALL_RIGHT = 512;
constexpr uint16_t ORB_BODY_CRUSHED = 1024;

typedef struct orb_tag {
    uint32_t bits;
} orb_tag;

constexpr uint32_t ORB_TAG_ANY = 0xffffffffu;

typedef enum orb_cell_kind : uint8_t {
    ORB_CELL_OPEN,
    ORB_CELL_SOLID,
    ORB_CELL_ONEWAY_N, // solid only to a body arriving from the north
    ORB_CELL_ONEWAY_S,
    ORB_CELL_ONEWAY_E,
    ORB_CELL_ONEWAY_W
} orb_cell_kind;

typedef struct orb_hit {
    orb_entity_id entity; // ORB_NO_ENTITY for a cell
    orb_vec2 at;          // the hit point; for a cell, the last open pixel before it
    orb_vec2 normal;      // the face hit: one axis -1 or 1, the other 0
    float fraction;       // 0..1 along the segment
} orb_hit;

constexpr uint32_t ORB_RAY_CELLS = 1;  // test collision cells
constexpr uint32_t ORB_RAY_SOLIDS = 2; // test solid bodies whatever their tags
// one-way cells and bodies block from their solid side; else open
constexpr uint32_t ORB_RAY_ONEWAY = 4;

constexpr uint32_t ORB_FLIP_X = 1;
constexpr uint32_t ORB_FLIP_Y = 2;

constexpr uint64_t ORB_NS_PER_SECOND = 1000000000;
constexpr int ORB_TICK_RATE = 60; // update calls per second
constexpr float ORB_TICK_SECONDS = 1.0f / ORB_TICK_RATE;

constexpr int ORB_AUDIO_CHANNELS = 2;
constexpr int ORB_AUDIO_RATE = 48000;

// Audio. sample_find("jump") is jump.wav from the manifest's sounds; song_find("title")
// is title.wav from its songs. sound_play returns a voice handle; when all 16 game voices
// are busy it steals the oldest voice whose priority is at or below the new sound's, or
// returns ORB_NO_VOICE. sound_set and sound_stop on a voice that ended or was stolen do
// nothing. song_play loops on the WAV's own loop points when it has them, else over the
// whole file. song_position is where the mixer's render head is in the song's file, as
// milliseconds and as thousandths of a beat; it wraps with the loop and freezes on pause.
// The render head runs ahead of the speaker by one device buffer, a few tens of
// milliseconds. Volumes are 0..1 and start at 1. orb_sound_params.volume 0 is silence, so
// a zero-initialized orb_sound_params plays nothing; set .volume explicitly.
//
// Levels. level_find("cave") is the LDtk level Cave; layer_find(level, "floor") is a layer
// index within it, ORB_NO_INDEX when missing. layer_draw draws one layer in world space under
// the camera; cell_get reads an IntGrid value at a world pixel, 0 outside. camera_update moves
// an orb_camera toward its target, clamps it to its bounds, draws under at plus shake, and
// returns the moved camera: g->camera = orb->camera_update(g->camera).
//
// Text. font_find("body") is body.aseprite from the manifest's fonts. text_draw puts a
// string on the framebuffer in screen space, ignoring the camera; bytes outside 32 to 126
// draw nothing and do not advance. text_measure is the string's pixel width and the font's
// line height, for centring.
//
// Input. A key is a physical position (ORB_KEY_*, HID usage IDs) and a pad button a
// position on an Xbox pad (ORB_PAD_SOUTH is A). A button is bound to one key and one pad
// button and is down while either is. Bindings start at orb's defaults and change only
// through button_bind, which sets the slot the source's kind belongs to (ORB_KEY_NONE or
// ORB_PAD_NONE empties it); button_key and button_pad read them, and a game that saves
// bindings calls button_bind after loading. key_find("m") is the position of the key
// labelled m on the player's layout. key_* and pad_* down, pressed and released read one
// source, exact per tick; key_pressed_any and pad_pressed_any are the lowest source that
// went down this tick, for capturing a binding. The triggers also read as pad buttons past
// half. pad_stick_dpad(true), off at boot, makes the left stick also hold the d-pad
// buttons: from half its travel until it falls under 0.35, the directions of its 45°
// sector, two on a diagonal. pad_stick is -1..1 each way with y down and a dead zone,
// pad_trigger 0..1, pad_make ORB_PAD_MAKE_NONE with no pad, else the family whose labels
// to draw. key_name is a named key's name ("tab", "page_up") or the layout's symbol
// ("m", ","), "" for neither; the string is valid until the next call.
//
// Entities. type_find("crate") is the LDtk entity definition Crate; type_bind attaches init
// and update functions to it and belongs in reload, since the table is cleared before every
// reload. level_spawn places a level's entities and belongs after type_bind, in update or
// reload; entity_spawn makes one from a type at runtime. Fields are the editor's values and
// read-only: entity_field_int(id, "hp", 0) reads the placement, else the type's default,
// else 0. Components are the game's: sprite, body, and tag are built in, and the sizes in
// orb_config.components declare the game's kinds, numbered from ORB_COMPONENT_GAME in that
// order (enum { HEALTH = ORB_COMPONENT_GAME, BRAIN } beside the list); a change to the list
// resets the state. entity_add zeroes a new slot; a sprite starts with no sprite, no
// animation, and no remap, and a body's box starts at the entity's size. Every pointer into
// the pool is valid for the current tick; store handles. world_collision names the IntGrid
// layer, in every level, whose values the kinds table maps to open, solid, or one-way, and
// world_update runs the type updates, moves solids and bodies, and frees despawns; both are
// yours to call from update. world_draw draws the sprites of one layer y-sorted; queries
// take a tag mask where ORB_TAG_ANY matches every tagged body and 0 none, and a handle to
// leave out. Setting parent after entity_add moves the body's world position without moving
// last_at, so a solid reports that jump as moved on the next update; a game that parents a
// solid sets last_at to the new world position.
//
typedef struct orb_api orb_api;

typedef void (*orb_entity_fn)(void* state, const orb_api* orb, orb_entity_id id);

// Console. The console opens on the grave key. var_int, var_float, var_bool, and command
// register a name the console reads, sets, or runs; call them in reload, since the console
// clears its tables before every reload. A variable points into your state; help is one
// line or nullptr. A name is 1 to 31 bytes of lowercase letters, digits, '.', and '_',
// starting with a letter, and unique across both tables, or the registration is refused
// with a log line. A command gets the line split on spaces, quotes grouping, argv[0] its
// name; argv is valid only for that call. console_open is true while the console is down
// and your input is empty.
typedef void (*orb_command_fn)(void* state, const orb_api* orb, int argc, const char* const* argv);
typedef struct orb_api {
    void (*pal_reset)(void);
    void (*pal_set)(int index, uint8_t r, uint8_t g, uint8_t b);
    uint32_t (*pal_get)(int index);

    void (*clear)(uint8_t index);
    void (*camera_set)(orb_vec2f at);
    orb_camera (*camera_update)(orb_camera camera);

    orb_sprite (*sprite_find)(const char* stem, int frame);
    void (*sprite_draw)(orb_sprite sprite, orb_vec2 at, uint32_t flags, const uint8_t* remap);
    orb_anim (*anim_find)(const char* stem, const char* tag);
    void (*anim_start)(orb_anim_state* state, orb_anim anim);
    orb_sprite (*anim_step)(orb_anim_state* state);

    int (*layer_find)(orb_level level, const char* name);
    orb_layer_info (*layer_info)(orb_level level, int layer);
    void (*layer_draw)(orb_level level, int layer);
    orb_level (*level_find)(const char* stem);
    orb_rect (*level_bounds)(orb_level level);
    int (*level_neighbors)(orb_level level, orb_level_neighbor* out, int max);
    int (*cell_get)(orb_level level, int layer, orb_vec2 at);

    orb_type (*type_find)(const char* stem);
    void (*type_bind)(orb_type type, orb_entity_fn init, orb_entity_fn update);
    orb_entity_id (*entity_spawn)(orb_type type, orb_vec2f at);
    void (*entity_despawn)(orb_entity_id id);
    orb_entity* (*entity_get)(orb_entity_id id);
    void* (*entity_add)(orb_entity_id id, int kind);
    void (*entity_remove)(orb_entity_id id, int kind);
    void* (*entity_component)(orb_entity_id id, int kind);
    orb_vec2f (*entity_world_at)(orb_entity_id id);
    int (*entity_all)(orb_entity_id* out, int max);
    int (*entity_of_type)(orb_type type, orb_entity_id* out, int max);
    int (*entity_field_count)(orb_entity_id id, const char* name);
    int32_t (*entity_field_int)(orb_entity_id id, const char* name, int index);
    float (*entity_field_float)(orb_entity_id id, const char* name, int index);
    bool (*entity_field_bool)(orb_entity_id id, const char* name, int index);
    const char* (*entity_field_string)(orb_entity_id id, const char* name, int index);
    orb_vec2 (*entity_field_point)(orb_entity_id id, const char* name, int index);
    orb_entity_id (*entity_field_ref)(orb_entity_id id, const char* name, int index);
    void (*level_spawn)(orb_level level);
    void (*level_despawn)(orb_level level);
    void (*world_collision)(const char* layer, const uint8_t kinds[256]);
    orb_cell_kind (*cell_kind)(orb_vec2 at);
    void (*world_gravity)(orb_vec2f gravity);
    void (*world_update)(void);
    void (*world_draw)(int layer);
    void (*remap_set)(int index, const uint8_t table[256]);
    int (*query_rect)(
        orb_rect rect,
        uint32_t mask,
        orb_entity_id except,
        orb_entity_id* out,
        int max
    );
    int (*query_circle)(
        orb_vec2 center,
        int radius,
        uint32_t mask,
        orb_entity_id except,
        orb_entity_id* out,
        int max
    );
    int (*query_point)(
        orb_vec2 at,
        uint32_t mask,
        orb_entity_id except,
        orb_entity_id* out,
        int max
    );
    bool (*query_ray)(
        orb_vec2 from,
        orb_vec2 to,
        uint32_t mask,
        uint32_t flags,
        orb_entity_id except,
        orb_hit* hit
    );

    orb_font (*font_find)(const char* stem);
    orb_size (*text_measure)(orb_font font, const char* text);
    void (*text_draw)(orb_font font, const char* text, orb_vec2 at, const uint8_t* remap);

    orb_sample (*sample_find)(const char* stem);
    orb_voice (*sound_play)(orb_sample sample, orb_sound_params params, int priority);
    void (*sound_set)(orb_voice voice, orb_sound_params params);
    void (*sound_stop)(orb_voice voice);
    orb_song (*song_find)(const char* stem);
    void (*song_play)(orb_song song, bool loop);
    orb_song_position (*song_position)(void);
    void (*song_pause)(void);
    void (*song_resume)(void);
    void (*song_stop)(int fade_ms);
    void (*volume_set)(orb_volumes volumes);

    void (*button_bind)(orb_button button, int source);
    bool (*button_down)(orb_button button);
    bool (*button_pressed)(orb_button button);
    bool (*button_released)(orb_button button);
    int (*button_key)(orb_button button);
    orb_pad (*button_pad)(orb_button button);
    bool (*key_down)(int key);
    bool (*key_pressed)(int key);
    bool (*key_released)(int key);
    int (*key_pressed_any)(void);
    int (*key_find)(const char* symbol);
    const char* (*key_name)(int key);
    bool (*pad_down)(orb_pad pad);
    bool (*pad_pressed)(orb_pad pad);
    bool (*pad_released)(orb_pad pad);
    orb_pad (*pad_pressed_any)(void);
    orb_vec2f (*pad_stick)(orb_pad stick);
    void (*pad_stick_dpad)(bool on);
    float (*pad_trigger)(orb_pad trigger);
    orb_pad_make (*pad_make)(void);

    [[gnu::format(gnu_printf, 1, 2)]] void (*log)(const char* fmt, ...);

    void (*var_int)(const char* name, int32_t* at, const char* help);
    void (*var_float)(const char* name, float* at, const char* help);
    void (*var_bool)(const char* name, bool* at, const char* help);
    void (*command)(const char* name, orb_command_fn fn, const char* help);
    bool (*console_open)(void);
} orb_api;

// Typed wrappers over entity_component for the three built-in kinds; a game's own
// kinds follow the same pattern in their own code.
static inline orb_sprite_component* orb_sprite_component_of(const orb_api* orb, orb_entity_id id) {
    return orb->entity_component(id, ORB_COMPONENT_SPRITE);
}

static inline orb_body* orb_body_of(const orb_api* orb, orb_entity_id id) {
    return orb->entity_component(id, ORB_COMPONENT_BODY);
}

static inline orb_tag* orb_tag_of(const orb_api* orb, orb_entity_id id) {
    return orb->entity_component(id, ORB_COMPONENT_TAG);
}

// Reload rules. orb reloads game code and recasts art while the game runs, and
// three rules keep that safe:
//  1. State lives in the struct orb hands you. config() is read once at boot;
//     if state_version or state_size differs after a code reload, orb zeroes
//     the state and calls init instead of continuing on stale bytes.
//  2. No pointer into your library survives a reload: not function pointers,
//     not string literals, not static const tables. Store handles and indices,
//     and re-bind behavior in reload.
//  3. Assets are found by name: sprite_find("player", 0) is frame 0 of
//     player.aseprite, anim_find("player", "walk") its "walk" tag. reload
//     runs at boot after init, after every code reload, and after every art
//     recast, so it is the one place to find things and store the handles.
// A handle is an index plus a generation. A recast that puts different art at
// an index bumps its generation, so a handle found before it draws and plays
// nothing until reload finds it again. A find that misses logs the name and
// returns ORB_NO_SPRITE, ORB_NO_ANIM, ORB_NO_SAMPLE, or ORB_NO_SONG.
typedef struct orb_config {
    size_t state_size;
    uint32_t state_version;
    uint32_t save_version;
    uint32_t max_entities;                                        // 0 defaults to 256
    uint16_t components[ORB_MAX_COMPONENTS - ORB_COMPONENT_GAME]; // byte sizes of the game's kinds;
                                                                  // 0 ends the list
} orb_config;

typedef struct orb_game {
    orb_config (*config)(void);
    void (*init)(void* state, const orb_api* orb);
    void (*reload)(void* state, const orb_api* orb);
    void (*update)(void* state, const orb_api* orb);
    void (*draw)(void* state, const orb_api* orb);
} orb_game;

const orb_game* orb_game_main(void);
