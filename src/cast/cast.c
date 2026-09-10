#include "cast.h"
#include "../os/os.h"
#include "aseprite.h"
#include "file.h"
#include "json.h"
#include "pack.h"
#include "wav.h"

#include <ctype.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

static const char* cast_path(orb_arena* a, const char* dir, const char* rel) {
    size_t n = strlen(dir) + 1 + strlen(rel) + 1;
    char* p = orb_arena_push(a, n, 1);

    snprintf(p, n, "%s/%s", dir, rel);
    return p;
}

static const char* cast_string(const orb_json* obj, const char* key, orb_error* err) {
    const orb_json* v = orb_json_get(obj, key);

    if (!v || v->kind != ORB_JSON_STRING) {
        orb_error_set(err, "orb.json: \"%s\" must be a string", key);
        return nullptr;
    }

    return v->str;
}

// A manifest array of paths. A required key that is missing is an error; an
// optional one is an empty list.
static bool cast_string_list(
    orb_arena* a,
    const orb_json* root,
    const char* key,
    bool required,
    const char*** out,
    int* count,
    orb_error* err
) {
    const orb_json* list = orb_json_get(root, key);

    *out = nullptr;
    *count = 0;

    if (!list && !required) return true;

    if (!list || list->kind != ORB_JSON_ARRAY) {
        orb_error_set(err, "orb.json: \"%s\" must be an array of paths", key);
        return false;
    }

    *out = orb_arena_push(a, sizeof(char*) * list->count, alignof(char*));

    for (const orb_json* s = list->first; s; s = s->next) {
        if (s->kind != ORB_JSON_STRING) {
            orb_error_set(err, "orb.json: \"%s\" entries must be strings", key);
            return false;
        }

        (*out)[(*count)++] = s->str;
    }

    return true;
}

// "songs": [{"bpm": 120, "path": "music/title.wav"}, ...]. Every song names its
// tempo, since a rendered file carries none and song_position reports beats.
static bool cast_song_list(orb_arena* a, const orb_json* root, orb_manifest* m, orb_error* err) {
    const orb_json* list = orb_json_get(root, "songs");

    m->songs = nullptr;
    m->song_count = 0;

    if (!list) return true;

    if (list->kind != ORB_JSON_ARRAY) {
        orb_error_set(err, "orb.json: \"songs\" must be an array of {\"bpm\", \"path\"} objects");
        return false;
    }

    orb_manifest_song* songs = orb_arena_push_array(a, orb_manifest_song, list->count);

    for (const orb_json* s = list->first; s; s = s->next) {
        const orb_json* bpm = orb_json_get(s, "bpm"); // nullptr when s is not an object
        const orb_json* path = orb_json_get(s, "path");

        if (!path || path->kind != ORB_JSON_STRING) {
            orb_error_set(
                err, "orb.json: \"songs\" entries are {\"bpm\": 120, \"path\": \"music/title.wav\"}"
            );
            return false;
        }

        if (!bpm || bpm->kind != ORB_JSON_NUMBER || !(bpm->num > 0 && bpm->num <= 1000)) {
            orb_error_set(err, "orb.json: %s: \"bpm\" must be a number within 0..1000", path->str);
            return false;
        }

        songs[m->song_count++] = (orb_manifest_song) {.bpm = (float)bpm->num, .path = path->str};
    }

    m->songs = songs;
    return true;
}

bool orb_manifest_load(orb_arena* a, const char* game_dir, orb_manifest* m, orb_error* err) {
    orb_span text;
    const char* path = cast_path(a, game_dir, "orb.json");

    if (!orb_os_read_file(path, a, &text)) {
        orb_error_set(err, "cannot read %s", path);
        return false;
    }

    orb_json* root = orb_json_parse(a, (const char*)text.ptr, text.len, err);

    if (!root) return false;

    memset(m, 0, sizeof *m);

    if (!(m->id = cast_string(root, "id", err))) return false;
    if (!(m->name = cast_string(root, "name", err))) return false;
    if (!(m->palette = cast_string(root, "palette", err))) return false;

    const orb_json* size = orb_json_get(root, "size");

    if (!size || size->kind != ORB_JSON_ARRAY || size->count != 2) {
        orb_error_set(err, "orb.json: \"size\" must be [width, height]");
        return false;
    }

    m->size_w = (int)size->first->num;
    m->size_h = (int)size->first->next->num;

    if (m->size_w < 1 || m->size_w > 4096 || m->size_h < 1 || m->size_h > 4096) {
        orb_error_set(err, "orb.json: \"size\" must be within 1..4096");
        return false;
    }

    const orb_json* headroom = orb_json_get(root, "asset_headroom");

    if (!headroom || headroom->kind != ORB_JSON_NUMBER) {
        orb_error_set(err, "orb.json: \"asset_headroom\" must be a number of bytes");
        return false;
    }

    m->asset_headroom = (size_t)headroom->num;

    return cast_string_list(a, root, "sprites", true, &m->sprites, &m->sprite_count, err) &&
           cast_string_list(a, root, "sounds", false, &m->sounds, &m->sound_count, err) &&
           cast_song_list(a, root, m, err);
}

// A WAV's descriptor: an empty loop is no loop.
static orb_sample_desc cast_sample_desc(const orb_wav* w, uint32_t first) {
    bool loop = w->has_loop && w->loop_end > w->loop_start;

    return (orb_sample_desc) {
        .first = first,
        .count = w->count,
        .loop_start = loop ? w->loop_start : 0,
        .loop_end = loop ? w->loop_end : 0,
        .rate = w->rate,
        .channels = w->channels
    };
}

// The i-th WAV: the sounds in manifest order, then the songs.
static const char* cast_wav_path(const orb_manifest* m, uint32_t i) {
    return i < (uint32_t)m->sound_count ? m->sounds[i] : m->songs[i - m->sound_count].path;
}

// "art/player.aseprite" -> "PLAYER"
static const char* cast_stem(orb_arena* a, const char* path) {
    const char* slash = strrchr(path, '/');
    const char* start = slash ? slash + 1 : path;
    const char* dot = strrchr(start, '.');
    size_t n = dot ? (size_t)(dot - start) : strlen(start);
    char* s = orb_arena_push(a, n + 1, 1);

    for (size_t i = 0; i < n; i++)
        s[i] = isalnum((unsigned char)start[i]) ? (char)toupper((unsigned char)start[i]) : '_';

    return s;
}

static bool cast_load_ase(
    orb_arena* scratch,
    const char* game_dir,
    const char* rel,
    orb_ase* ase,
    orb_error* err
) {
    const char* path = cast_path(scratch, game_dir, rel);
    orb_span file;

    if (!orb_os_read_file(path, scratch, &file)) {
        orb_error_set(err, "cannot read %s", path);
        return false;
    }

    orb_error inner;

    if (!orb_ase_parse(scratch, file, ase, &inner)) {
        orb_error_set(err, "%s: %s", rel, inner.text);
        return false;
    }

    return true;
}

static bool cast_has_suffix(const char* path, const char* suffix) {
    size_t n = strlen(path), m = strlen(suffix);

    return n >= m && strcmp(path + n - m, suffix) == 0;
}

static bool cast_load_wav(
    orb_arena* scratch,
    const char* game_dir,
    const char* rel,
    orb_wav* wav,
    orb_error* err
) {
    const char* path = cast_path(scratch, game_dir, rel);
    orb_span file;

    if (!orb_os_read_file(path, scratch, &file)) {
        orb_error_set(err, "cannot read %s", path);
        return false;
    }

    orb_error inner;

    if (!orb_wav_parse(file, wav, &inner)) {
        orb_error_set(err, "%s: %s", rel, inner.text);
        return false;
    }

    return true;
}

// Arm both arenas so that running out of room unwinds to the cast entry point
// as an ordinary error instead of ending the process.
static void cast_arm(orb_arena* a, jmp_buf* recover) {
    a->recover = recover;
    a->overflow = 0;
}

static void cast_disarm(orb_arena* a) {
    a->recover = nullptr;
}

static void cast_exhausted(const orb_arena* a, orb_error* err) {
    orb_error_set(
        err, "%s exhausted by %zu bytes: raise asset_headroom in orb.json", a->name, a->overflow
    );
}

static bool cast_body(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    const orb_manifest* m,
    orb_cast_result* r,
    orb_error* err
) {
    orb_ase master;

    if (!cast_load_ase(scratch, game_dir, m->palette, &master, err)) return false;

    uint8_t* palette = orb_arena_push(scratch, 256 * 4, 16);

    for (int i = 0; i < master.color_count; i++) {
        palette[i * 4 + 0] = master.rgb[i][0];
        palette[i * 4 + 1] = master.rgb[i][1];
        palette[i * 4 + 2] = master.rgb[i][2];
    }

    // Generous upper bounds so tables can be filled in one pass.
    uint32_t max_sprites = 0, max_anims = 0;
    orb_ase* files = orb_arena_push_array(scratch, orb_ase, m->sprite_count);

    for (int i = 0; i < m->sprite_count; i++) {
        if (!cast_load_ase(scratch, game_dir, m->sprites[i], &files[i], err)) return false;
        max_sprites += files[i].frame_count;
        max_anims += files[i].tag_count;
    }

    orb_sheet_desc* sheets = orb_arena_push(scratch, sizeof(orb_sheet_desc) * m->sprite_count, 16);
    orb_sprite_desc* sprites = orb_arena_push(scratch, sizeof(orb_sprite_desc) * max_sprites, 16);
    orb_animation_desc* animations =
        orb_arena_push(scratch, sizeof(orb_animation_desc) * max_anims, 16);
    uint16_t* durations = orb_arena_push(scratch, sizeof(uint16_t) * max_sprites, 16);
    uint64_t* sprite_ids = orb_arena_push(scratch, sizeof(uint64_t) * max_sprites, 16);
    uint64_t* animation_ids = orb_arena_push(scratch, sizeof(uint64_t) * max_anims, 16);
    orb_pack* packs = orb_arena_push_array(scratch, orb_pack, m->sprite_count);

    r->sprite_count = r->animation_count = r->sample_count = r->song_count = 0;

    uint32_t pixel_total = 0, duration_count = 0;

    for (int i = 0; i < m->sprite_count; i++) {
        orb_ase* ase = &files[i];
        const char* stem = cast_stem(scratch, m->sprites[i]);

        uint8_t remap[256] = {0};

        for (int c = 0; c < ase->color_count; c++) {
            if (c == ase->transparent) continue;

            int found = 0;

            for (int k = 1; k < master.color_count; k++) {
                if (memcmp(master.rgb[k], ase->rgb[c], 3) == 0) {
                    found = k;
                    break;
                }
            }

            if (!found) {
                orb_error_set(
                    err, "%s: color %d (%d,%d,%d) is not in the master palette", m->sprites[i], c,
                    ase->rgb[c][0], ase->rgb[c][1], ase->rgb[c][2]
                );
                return false;
            }

            remap[c] = (uint8_t)found;
        }

        size_t pixel_count = (size_t)ase->w * ase->h * ase->frame_count;

        for (size_t p = 0; p < pixel_count; p++)
            ase->frames[p] = remap[ase->frames[p]];

        orb_pack* pack = &packs[i];

        orb_pack_frames(scratch, ase->frames, ase->frame_count, (orb_size) {ase->w, ase->h}, pack);
        sheets[i] =
            (orb_sheet_desc) {.w = pack->sheet_w, .h = pack->sheet_h, .pixels = pixel_total};
        pixel_total += (uint32_t)pack->sheet_w * pack->sheet_h;

        uint32_t first_sprite = r->sprite_count;

        for (uint32_t f = 0; f < ase->frame_count; f++) {
            const orb_pack_rect* rect = &pack->rects[pack->frames[f].rect];

            sprites[r->sprite_count] = (orb_sprite_desc) {
                .sheet = (uint16_t)i,
                .x = rect->x,
                .y = rect->y,
                .w = rect->w,
                .h = rect->h,
                .ox = pack->frames[f].ox,
                .oy = pack->frames[f].oy,
                .fw = ase->w,
                .fh = ase->h
            };

            char suffix[16];

            snprintf(suffix, sizeof suffix, "%u", f);
            sprite_ids[r->sprite_count++] = orb_asset_id(stem, suffix);
        }

        for (int t = 0; t < ase->tag_count; t++) {
            const orb_ase_tag* tag = &ase->tags[t];

            if (tag->direction != 0) {
                orb_error_set(
                    err, "%s: tag \"%s\" uses a loop direction other than forward", m->sprites[i],
                    tag->name
                );
                return false;
            }

            animations[r->animation_count] = (orb_animation_desc) {
                .first_sprite = first_sprite + tag->from,
                .first_duration = duration_count,
                .count = (uint16_t)(tag->to - tag->from + 1)
            };

            for (int f = tag->from; f <= tag->to; f++) {
                uint32_t ticks = (ase->durations[f] * 60u + 500u) / 1000u;

                durations[duration_count++] = (uint16_t)(ticks ? ticks : 1);
            }

            animation_ids[r->animation_count++] = orb_asset_id(stem, tag->name);
        }
    }

    uint8_t* pixels = orb_arena_push(scratch, pixel_total, 16);

    for (int i = 0; i < m->sprite_count; i++) {
        memcpy(
            pixels + sheets[i].pixels, packs[i].pixels, (size_t)packs[i].sheet_w * packs[i].sheet_h
        );
    }

    // Sounds first, then each song's sample, so a song and a sound may share a stem.
    // Two passes, each file's bytes dropped after use: the first sizes the packed PCM,
    // the second decodes into it, so scratch holds one file beside the pack instead of
    // every file, its widened copy, and the pack.
    uint32_t wav_count = (uint32_t)(m->sound_count + m->song_count);
    orb_sample_desc* samples = orb_arena_push(scratch, sizeof(orb_sample_desc) * wav_count, 16);
    uint64_t* sample_ids = orb_arena_push(scratch, sizeof(uint64_t) * wav_count, 16);
    orb_song_desc* songs = orb_arena_push(scratch, sizeof(orb_song_desc) * m->song_count, 16);
    uint64_t* song_ids = orb_arena_push(scratch, sizeof(uint64_t) * m->song_count, 16);
    uint32_t pcm_total = 0;

    for (uint32_t i = 0; i < wav_count; i++) {
        bool song = i >= (uint32_t)m->sound_count;
        const char* rel = cast_wav_path(m, i);
        size_t mark = scratch->used;
        orb_wav w;

        if (song && !cast_has_suffix(rel, ".wav")) {
            orb_error_set(err, "%s: only .wav songs are accepted", rel);
            return false;
        }

        if (!cast_load_wav(scratch, game_dir, rel, &w, err)) return false;

        scratch->used = mark; // the header is all this pass needs

        bool loop = w.has_loop && w.loop_end > w.loop_start;

        if (!song && w.channels != 1) {
            orb_error_set(
                err, "%s: sounds must be mono, since pan positions them; only songs may be stereo",
                rel
            );
            return false;
        }

        if (w.has_loop && !loop) orb_log("%s: loop points ignored, the loop is empty", rel);

        samples[i] = cast_sample_desc(&w, pcm_total);
        sample_ids[i] = orb_asset_id(cast_stem(scratch, rel), song ? "song" : "");
        pcm_total += w.count * w.channels;
    }

    for (int i = 0; i < m->song_count; i++) {
        songs[i] =
            (orb_song_desc) {.sample = (uint32_t)(m->sound_count + i), .bpm = m->songs[i].bpm};
        song_ids[i] = orb_asset_id(cast_stem(scratch, m->songs[i].path), "");
    }

    int16_t* pcm = orb_arena_push(scratch, sizeof(int16_t) * pcm_total, 16);

    for (uint32_t i = 0; i < wav_count; i++) {
        const char* rel = cast_wav_path(m, i);
        size_t mark = scratch->used;
        orb_wav w;

        if (!cast_load_wav(scratch, game_dir, rel, &w, err)) return false;

        orb_sample_desc again = cast_sample_desc(&w, samples[i].first);

        if (memcmp(&again, &samples[i], sizeof again) != 0) { // the pack is sized by pass one
            orb_error_set(err, "%s changed while casting", rel);
            return false;
        }

        orb_wav_decode(&w, pcm + samples[i].first);
        scratch->used = mark;
    }

    r->sample_count = wav_count;
    r->song_count = (uint32_t)m->song_count;

    orb_info_desc info = {.w = (uint16_t)m->size_w, .h = (uint16_t)m->size_h};

    snprintf(info.name, sizeof info.name, "%s", m->name);

    orb_assets assets = {
        .info = &info,
        .palette = palette,
        .sheets = sheets,
        .sheet_count = (uint32_t)m->sprite_count,
        .pixels = pixels,
        .pixel_count = pixel_total,
        .sprites = sprites,
        .sprite_count = r->sprite_count,
        .animations = animations,
        .animation_count = r->animation_count,
        .durations = durations,
        .duration_count = duration_count,
        .sprite_ids = sprite_ids,
        .animation_ids = animation_ids,
        .samples = samples,
        .sample_count = wav_count,
        .pcm = pcm,
        .pcm_count = pcm_total,
        .songs = songs,
        .song_count = (uint32_t)m->song_count,
        .sample_ids = sample_ids,
        .song_ids = song_ids
    };

    r->file = orb_file_write(out, &assets);
    return true;
}

const char* orb_seal_path(orb_arena* a, const char* game_dir, const orb_manifest* m) {
    size_t n = strlen(game_dir) + strlen("/bin/") + strlen(m->id) + strlen(".orb") + 1;
    char* p = orb_arena_push(a, n, 1);

    snprintf(p, n, "%s/bin/%s.orb", game_dir, m->id);
    return p;
}

bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* m,
    orb_cast_result* r,
    orb_error* err
) {
    jmp_buf recover;

    cast_arm(scratch, &recover);
    cast_arm(out, &recover);

    bool ok;

    if (setjmp(recover) == 0) {
        ok = orb_manifest_load(scratch, game_dir, m, err) &&
             cast_body(scratch, out, game_dir, m, r, err);
    } else {
        cast_exhausted(scratch->overflow ? scratch : out, err);
        ok = false;
    }

    cast_disarm(scratch);
    cast_disarm(out);
    return ok;
}
