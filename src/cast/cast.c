#include "cast.h"
#include "../core/asset.h"
#include "../core/input.h"
#include "../graphics/sprite.h"
#include "../os/os.h"
#include "aseprite.h"
#include "file.h"
#include "json.h"
#include "ldtk.h"
#include "pack.h"
#include "wav.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct cast {
    orb_arena* scratch;
    orb_arena* out;
    const char* game_dir;
    const orb_manifest* m;
    orb_cast_result* r;
    orb_error* err;
} cast;

typedef struct cast_files {
    const char** paths; // relative to game_dir, in walk order: sorted at each level
    const char** stems; // one per path, what the asset is found by
    int count;
    int16_t* grid_x; // a font's Aseprite grid, filled by cast_art; null otherwise
    int16_t* grid_y;
    uint16_t* grid_width;
    uint16_t* grid_height;
} cast_files;

// dir/rel, unless rel is absolute or dir is empty, in which case rel is returned unchanged.
static const char* cast_path(orb_arena* a, const char* dir, const char* rel) {
    if (orb_path_absolute(rel) || !*dir) {
        size_t n = strlen(rel) + 1;
        return memcpy(orb_arena_push(a, n, 1), rel, n);
    }

    size_t n = strlen(dir) + 1 + strlen(rel) + 1;
    char* p = orb_arena_push(a, n, 1);

    snprintf(p, n, "%s/%s", dir, rel);
    return p;
}

// The directory part of a path, "" when there is none: cast_dir_of("levels/world.ldtk")
// is "levels", cast_dir_of("w.ldtk") is "", cast_dir_of("/abs/dir/w.ldtk") is "/abs/dir".
static const char* cast_dir_of(orb_arena* a, const char* rel) {
    const char* slash = strrchr(rel, '/');

#ifdef _WIN32
    const char* backslash = strrchr(rel, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif

    if (!slash) return "";

    size_t n = (size_t)(slash - rel);
    char* p = orb_arena_push(a, n + 1, 1);

    memcpy(p, rel, n);
    p[n] = 0;
    return p;
}

// Record a path under game_dir as read, once: the list scry watches.
static bool cast_note(cast* c, const char* rel) {
    orb_cast_result* r = c->r;

    for (int i = 0; i < r->read_count; i++)
        if (strcmp(r->reads[i], rel) == 0) return true;

    if (r->read_count == ORB_CAST_MAX_READS)
        return orb_error_set(
            c->err, "more than %d files and directories in one cast", ORB_CAST_MAX_READS
        );

    r->reads[r->read_count++] = rel;
    return true;
}

static bool cast_read(cast* c, const char* rel, orb_span* out) {
    if (!cast_note(c, rel)) return false;

    const char* path = cast_path(c->scratch, c->game_dir, rel);

    if (!orb_os_read_file(path, c->scratch, out))
        return orb_error_set(c->err, "cannot read %s", path);

    return true;
}

// "art/player.aseprite" -> "player"; orb_asset_id folds the case.
static const char* cast_stem(orb_arena* a, const char* path) {
    const char* slash = strrchr(path, '/');
    const char* start = slash ? slash + 1 : path;
    const char* dot = strrchr(start, '.');
    size_t n = dot ? (size_t)(dot - start) : strlen(start);

    return memcpy(orb_arena_push(a, n + 1, 1), start, n);
}

// Every file with the suffix under rel, recursing, but the one path to skip. A
// missing directory is empty. The listing buffer is one static reused by every
// level, so a level copies its names out before it recurses. Each file is noted
// as it is found, so the reads cap bounds this list too.
static bool cast_walk_into(
    cast* c,
    const char* rel,
    const char* suffix,
    const char* skip,
    cast_files* files
) {
    static orb_os_entry entries[ORB_CAST_MAX_READS];

    if (!cast_note(c, rel)) return false;

    const char* dir = cast_path(c->scratch, c->game_dir, rel);
    int n = orb_os_list_dir(dir, entries, ORB_CAST_MAX_READS);

    if (n == ORB_CAST_MAX_READS)
        return orb_error_set(c->err, "%s: more than %d entries in one directory", rel, n - 1);

    const char** names = orb_arena_push_array(c->scratch, const char*, n);
    bool* dirs = orb_arena_push_array(c->scratch, bool, n);

    for (int i = 0; i < n; i++) {
        names[i] = cast_path(c->scratch, rel, entries[i].name);
        dirs[i] = entries[i].dir;
    }

    for (int i = 0; i < n; i++) {
        if (dirs[i]) {
            if (!cast_walk_into(c, names[i], suffix, skip, files)) return false;
        } else if (orb_has_suffix(names[i], suffix) && strcmp(names[i], skip) != 0) {
            if (!cast_note(c, names[i])) return false;

            files->stems[files->count] = cast_stem(c->scratch, names[i]);
            files->paths[files->count++] = names[i];
        }
    }

    return true;
}

static bool cast_walk(
    cast* c,
    const char* rel,
    const char* suffix,
    const char* skip,
    cast_files* files
) {
    files->paths = orb_arena_push_array(c->scratch, const char*, ORB_CAST_MAX_READS);
    files->stems = orb_arena_push_array(c->scratch, const char*, ORB_CAST_MAX_READS);
    files->count = 0;
    files->grid_x = nullptr;
    files->grid_y = nullptr;
    files->grid_width = nullptr;
    files->grid_height = nullptr;
    return cast_walk_into(c, rel, suffix, skip, files);
}

// The index below i whose id matches ids[i], or -1.
static int cast_dup_id(const uint64_t* ids, int i) {
    for (int p = 0; p < i; p++)
        if (ids[p] == ids[i]) return p;

    return -1;
}

// Two files of one kind with one stem would be one name to find; refuse the pair.
static bool cast_unique(cast* c, const cast_files* files) {
    uint64_t* ids = orb_arena_push_array(c->scratch, uint64_t, files->count);

    for (int i = 0; i < files->count; i++) {
        ids[i] = orb_asset_id(files->stems[i], "");

        int dup = cast_dup_id(ids, i);

        if (dup >= 0)
            return orb_error_set(
                c->err, "%s and %s share a stem, so one name would find both", files->paths[dup],
                files->paths[i]
            );
    }

    return true;
}

static bool cast_load_ase(cast* c, const char* rel, orb_ase* ase) {
    orb_span file;

    if (!cast_read(c, rel, &file)) return false;

    orb_error inner;

    if (!orb_ase_parse(c->scratch, file, ase, &inner))
        return orb_error_set(c->err, "%s: %s", rel, inner.text);

    return true;
}

// The master palette: orb's 256 RGB entries, the truth every other file's colors
// are matched against.
static bool cast_pal(cast* c, orb_ase* master, orb_assets* as) {
    if (!cast_load_ase(c, c->m->pal, master)) return false;

    uint8_t* pal = orb_arena_push(c->scratch, 256 * 4, 16);

    for (int i = 0; i < master->color_count; i++) {
        pal[i * 4 + 0] = master->rgb[i][0];
        pal[i * 4 + 1] = master->rgb[i][1];
        pal[i * 4 + 2] = master->rgb[i][2];
    }

    as->pal = pal;
    return true;
}

// Remaps one file's palette indices onto the master palette, in place.
static bool cast_art_remap(cast* c, const orb_ase* master, orb_ase* ase, const char* path) {
    uint8_t remap[256] = {0};

    for (int k = 0; k < ase->color_count; k++) {
        if (k == ase->transparent) continue;

        int master_index = 0;

        for (int j = 1; j < master->color_count; j++) {
            if (memcmp(master->rgb[j], ase->rgb[k], 3) == 0) {
                master_index = j;
                break;
            }
        }

        if (!master_index)
            return orb_error_set(
                c->err, "%s: color %d (%d,%d,%d) is not in the master palette", path, k,
                ase->rgb[k][0], ase->rgb[k][1], ase->rgb[k][2]
            );

        remap[k] = (uint8_t)master_index;
    }

    size_t pixel_count = (size_t)ase->width * ase->height * ase->frame_count;

    for (size_t p = 0; p < pixel_count; p++)
        ase->frames[p] = remap[ase->frames[p]];

    return true;
}

// Every sprite file: one packed sheet each, a sprite per frame, an animation per tag.
// Font and tileset files, appended after the art files, take a plainer path: one
// unpacked sheet each, no sprites or animations.
static bool cast_art(
    cast* c,
    const orb_ase* master,
    const orb_ldtk* world,
    cast_files* fonts,
    uint32_t* first_font_sheet,
    uint32_t* first_tileset_sheet,
    orb_assets* as
) {
    orb_arena* scratch = c->scratch;
    cast_files art;

    if (!cast_walk(c, c->m->art, ".aseprite", c->m->pal, &art) || !cast_unique(c, &art))
        return false;

    int art_count = art.count;
    int font_count = fonts->count;
    int file_count = art_count + font_count + world->tileset_count;
    const char* world_dir = cast_dir_of(scratch, c->m->world);
    const char** paths = orb_arena_push_array(scratch, const char*, file_count);

    memcpy(paths, art.paths, sizeof *paths * art_count);
    memcpy(paths + art_count, fonts->paths, sizeof *paths * font_count);

    for (int i = 0; i < world->tileset_count; i++)
        paths[art_count + font_count + i] = cast_path(scratch, world_dir, world->tilesets[i].path);

    *first_font_sheet = (uint32_t)art_count;
    *first_tileset_sheet = (uint32_t)(art_count + font_count);

    fonts->grid_x = orb_arena_push_array(scratch, int16_t, font_count);
    fonts->grid_y = orb_arena_push_array(scratch, int16_t, font_count);
    fonts->grid_width = orb_arena_push_array(scratch, uint16_t, font_count);
    fonts->grid_height = orb_arena_push_array(scratch, uint16_t, font_count);

    // Generous upper bounds so tables can be filled in one pass; tileset files
    // add no sprites or animations.
    uint32_t max_sprites = 0, max_anims = 0;
    orb_ase* files = orb_arena_push_array(scratch, orb_ase, file_count);

    for (int i = 0; i < file_count; i++) {
        if (!cast_load_ase(c, paths[i], &files[i])) return false;

        if (i < art_count) {
            max_sprites += files[i].frame_count;
            max_anims += files[i].tag_count;
        }
    }

    orb_sheet_desc* sheets = orb_arena_push_array(scratch, orb_sheet_desc, file_count);
    orb_sprite_desc* sprites = orb_arena_push_array(scratch, orb_sprite_desc, max_sprites);
    orb_anim_desc* anims = orb_arena_push_array(scratch, orb_anim_desc, max_anims);
    uint16_t* durations = orb_arena_push_array(scratch, uint16_t, max_sprites);
    uint64_t* sprite_ids = orb_arena_push_array(scratch, uint64_t, max_sprites);
    uint64_t* anim_ids = orb_arena_push_array(scratch, uint64_t, max_anims);
    orb_pack* packs = orb_arena_push_array(scratch, orb_pack, file_count);
    uint32_t sprite_count = 0, anim_count = 0, pixel_total = 0, duration_count = 0;

    for (int i = 0; i < file_count; i++) {
        orb_ase* ase = &files[i];

        if (!cast_art_remap(c, master, ase, paths[i])) return false;

        if (i >= art_count && i < art_count + font_count) {
            if (ase->frame_count != 1)
                return orb_error_set(
                    c->err, "%s: a font has one frame, this file has %u", paths[i], ase->frame_count
                );

            int fi = i - art_count;

            fonts->grid_x[fi] = ase->grid_x;
            fonts->grid_y[fi] = ase->grid_y;
            fonts->grid_width[fi] = ase->grid_width;
            fonts->grid_height[fi] = ase->grid_height;
        }

        if (i >= art_count + font_count) {
            const orb_ldtk_tileset* t = &world->tilesets[i - art_count - font_count];

            if (ase->frame_count != 1)
                return orb_error_set(
                    c->err, "%s: a tileset has one frame, this file has %u", paths[i],
                    ase->frame_count
                );
            if (ase->width != t->width || ase->height != t->height)
                return orb_error_set(
                    c->err, "%s: %ux%u, but the project says %dx%d", paths[i], ase->width,
                    ase->height, t->width, t->height
                );
        }

        if (i >= art_count) {
            sheets[i] = (orb_sheet_desc) {
                .width = ase->width, .height = ase->height, .pixels = pixel_total
            };
            pixel_total += (uint32_t)ase->width * ase->height;
            continue;
        }

        const char* stem = art.stems[i];
        orb_pack* pack = &packs[i];

        orb_pack_frames(
            scratch, ase->frames, ase->frame_count, (orb_size) {ase->width, ase->height}, pack
        );
        sheets[i] = (orb_sheet_desc) {
            .width = pack->sheet_width, .height = pack->sheet_height, .pixels = pixel_total
        };
        pixel_total += (uint32_t)pack->sheet_width * pack->sheet_height;

        uint32_t first_sprite = sprite_count;

        for (uint32_t f = 0; f < ase->frame_count; f++) {
            const orb_pack_rect* rect = &pack->rects[pack->frames[f].rect];

            sprites[sprite_count] = (orb_sprite_desc) {
                .sheet = (uint16_t)i,
                .x = rect->x,
                .y = rect->y,
                .width = rect->width,
                .height = rect->height,
                .ox = pack->frames[f].ox,
                .oy = pack->frames[f].oy,
                .frame_width = ase->width,
                .frame_height = ase->height
            };

            sprite_ids[sprite_count++] = orb_sprite_id(stem, (int)f);
        }

        for (int t = 0; t < ase->tag_count; t++) {
            const orb_ase_tag* tag = &ase->tags[t];

            if (tag->direction != 0)
                return orb_error_set(
                    c->err, "%s: tag \"%s\" uses a loop direction other than forward", paths[i],
                    tag->name
                );

            anims[anim_count] = (orb_anim_desc) {
                .first_sprite = first_sprite + tag->from,
                .first_duration = duration_count,
                .count = (uint16_t)(tag->to - tag->from + 1)
            };

            for (int f = tag->from; f <= tag->to; f++) {
                uint32_t ticks = (ase->durations[f] * ORB_TICK_RATE + 500u) / 1000u;

                durations[duration_count++] = (uint16_t)(ticks ? ticks : 1);
            }

            anim_ids[anim_count++] = orb_asset_id(stem, tag->name);
        }
    }

    uint8_t* pixels = orb_arena_push(scratch, pixel_total, 16);

    for (int i = 0; i < file_count; i++) {
        const uint8_t* src = i < art_count ? packs[i].pixels : files[i].frames;
        size_t n = i < art_count ? (size_t)packs[i].sheet_width * packs[i].sheet_height
                                 : (size_t)files[i].width * files[i].height;

        memcpy(pixels + sheets[i].pixels, src, n);
    }

    as->sheets = sheets;
    as->sheet_count = (uint32_t)file_count;
    as->pixels = pixels;
    as->pixel_count = pixel_total;
    as->sprites = sprites;
    as->sprite_count = sprite_count;
    as->anims = anims;
    as->anim_count = anim_count;
    as->durations = durations;
    as->duration_count = duration_count;
    as->sprite_ids = sprite_ids;
    as->anim_ids = anim_ids;
    return true;
}

static orb_sample_desc cast_sample_desc(const orb_wav* w, uint32_t first) {
    bool loop = w->has_loop;

    return (orb_sample_desc) {
        .first = first,
        .count = w->count,
        .loop_start = loop ? w->loop_start : 0,
        .loop_end = loop ? w->loop_end : 0,
        .rate = w->rate,
        .channels = w->channels
    };
}

static bool cast_load_wav(cast* c, const char* rel, orb_wav* wav) {
    orb_span file;

    if (!cast_read(c, rel, &file)) return false;

    orb_error inner;

    if (!orb_wav_parse(file, wav, &inner)) return orb_error_set(c->err, "%s: %s", rel, inner.text);

    if (wav->has_loop && wav->loop_end <= wav->loop_start) {
        orb_log(
            "%s: loop start %u is not before its end %u; dropped", rel, wav->loop_start,
            wav->loop_end
        );
        wav->has_loop = false;
        wav->loop_start = 0;
        wav->loop_end = 0;
    }

    return true;
}

// The orb.json songs entry for a stem, or -1 when there is none.
static int cast_song_index(const orb_manifest* m, const char* stem) {
    uint64_t id = orb_asset_id(stem, "");

    for (int i = 0; i < m->song_count; i++)
        if (orb_asset_id(m->songs[i].stem, "") == id) return i;

    return -1;
}

// Sounds first, then each song's sample, so a song and a sound may share a stem.
// Two passes, each file's bytes dropped after use: the first sizes the packed PCM,
// the second decodes into it, so scratch holds one file beside the pack.
static bool cast_audio(cast* c, orb_assets* as) {
    orb_arena* scratch = c->scratch;
    const orb_manifest* m = c->m;
    cast_files wavs;

    if (!cast_walk(c, m->sfx, ".wav", "", &wavs)) return false;

    uint32_t sound_count = (uint32_t)wavs.count;

    if (!cast_walk_into(c, m->music, ".wav", "", &wavs)) return false;

    cast_files sounds = {.paths = wavs.paths, .stems = wavs.stems, .count = (int)sound_count};
    cast_files music = {
        .paths = wavs.paths + sound_count,
        .stems = wavs.stems + sound_count,
        .count = wavs.count - (int)sound_count
    };

    if (!cast_unique(c, &sounds) || !cast_unique(c, &music)) return false;

    uint32_t wav_count = (uint32_t)wavs.count;
    orb_sample_desc* samples = orb_arena_push_array(scratch, orb_sample_desc, wav_count);
    uint64_t* sample_ids = orb_arena_push_array(scratch, uint64_t, wav_count);
    orb_song_desc* songs = orb_arena_push_array(scratch, orb_song_desc, music.count);
    uint64_t* song_ids = orb_arena_push_array(scratch, uint64_t, music.count);
    bool* has_file = orb_arena_push_array(scratch, bool, m->song_count);
    uint32_t pcm_total = 0;

    for (int i = 0; i < music.count; i++) {
        int index = cast_song_index(m, music.stems[i]);

        if (index < 0) return orb_error_set(c->err, "%s: no bpm in orb.json songs", music.paths[i]);

        has_file[index] = true;
        songs[i] = (orb_song_desc) {
            .sample = sound_count + (uint32_t)i,
            .millibpm = (uint32_t)(m->songs[index].bpm * 1000 + 0.5f)
        };
        song_ids[i] = orb_asset_id(music.stems[i], "");
    }

    for (int i = 0; i < m->song_count; i++) {
        if (has_file[i]) continue;

        return orb_error_set(
            c->err, "orb.json: songs: no %s/%s.wav for \"%s\"", m->music, m->songs[i].stem,
            m->songs[i].stem
        );
    }

    for (uint32_t i = 0; i < wav_count; i++) {
        bool song = i >= sound_count;
        const char* rel = wavs.paths[i];
        size_t mark = scratch->used;
        orb_wav w;

        if (!cast_load_wav(c, rel, &w)) return false;

        scratch->used = mark; // the header is all this pass needs

        if (!song && w.channels != 1)
            return orb_error_set(
                c->err,
                "%s: sounds must be mono, since pan positions them; only songs may be stereo", rel
            );

        samples[i] = cast_sample_desc(&w, pcm_total);
        sample_ids[i] = orb_asset_id(wavs.stems[i], song ? "song" : "");
        pcm_total += w.count * w.channels;
    }

    int16_t* pcm = orb_arena_push_array(scratch, int16_t, pcm_total);

    for (uint32_t i = 0; i < wav_count; i++) {
        const char* rel = wavs.paths[i];
        size_t mark = scratch->used;
        orb_wav w;

        if (!cast_load_wav(c, rel, &w)) return false;

        orb_sample_desc again = cast_sample_desc(&w, samples[i].first);

        if (memcmp(&again, &samples[i], sizeof again) != 0) { // the pack is sized by pass one
            return orb_error_set(c->err, "%s changed while casting", rel);
        }

        orb_wav_decode(&w, pcm + samples[i].first);
        scratch->used = mark;
    }

    as->samples = samples;
    as->sample_count = wav_count;
    as->pcm = pcm;
    as->pcm_count = pcm_total;
    as->songs = songs;
    as->song_count = (uint32_t)music.count;
    as->sample_ids = sample_ids;
    as->song_ids = song_ids;
    return true;
}

#include "world.c"

#include "font.c"

static bool cast_body(cast* c) {
    orb_ase master;
    orb_ldtk world;
    orb_assets as = {};
    orb_info_desc info = {
        .width = (uint16_t)c->m->size.width, .height = (uint16_t)c->m->size.height
    };

    snprintf(info.name, sizeof info.name, "%s", c->m->name);
    as.info = &info;

    cast_files fonts;
    uint32_t first_font_sheet = 0, first_tileset_sheet = 0;

    if (!cast_pal(c, &master, &as) || !world_parse(c, &world) ||
        !cast_walk(c, c->m->fonts, ".aseprite", "", &fonts) || !cast_unique(c, &fonts) ||
        !cast_art(c, &master, &world, &fonts, &first_font_sheet, &first_tileset_sheet, &as) ||
        !font_cast(c, &fonts, first_font_sheet, &as) ||
        !world_cast(c, &world, first_tileset_sheet, &as) || !cast_audio(c, &as))
        return false;

    orb_binding_desc bindings[ORB_BTN_COUNT] = {};

    for (int i = 0; i < ORB_BTN_COUNT; i++)
        snprintf(
            bindings[i].symbol, sizeof bindings[i].symbol, "%s",
            c->m->buttons[i][0] ? c->m->buttons[i] : orb_button_defaults[i]
        );

    as.bindings = bindings;
    as.binding_count = ORB_BTN_COUNT;

    c->r->file = orb_file_write(c->out, &as);
    return true;
}

bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* m,
    orb_cast_result* r,
    orb_error* err
) {
    // Both arenas armed: running out of room unwinds here as an ordinary cast
    // error instead of ending the process, so scry keeps the live half.
    jmp_buf recover;
    cast c = {scratch, out, game_dir, m, r, err};

    scratch->recover = out->recover = &recover;
    scratch->overflow = out->overflow = 0;
    memset(r, 0, sizeof *r);

    bool ok;

    if (setjmp(recover) == 0) {
        r->reads = orb_arena_push_array(scratch, const char*, ORB_CAST_MAX_READS);
        r->reads[r->read_count++] = "orb.json";
        ok = orb_manifest_load(scratch, game_dir, m, err) && cast_body(&c);
    } else {
        const orb_arena* full = scratch->overflow ? scratch : out;

        ok = orb_error_set(
            err, "%s exhausted by %zu bytes: raise asset_headroom in orb.json", full->name,
            full->overflow
        );
    }

    scratch->recover = out->recover = nullptr;
    return ok;
}

// A string key; with no fallback it is required.
static bool cast_string(
    const orb_json* root,
    const char* key,
    const char* fallback,
    const char** out,
    orb_error* err
) {
    const orb_json* v = orb_json_get(root, key);

    if ((!v && !fallback) || (v && v->kind != ORB_JSON_STRING))
        return orb_error_set(err, "orb.json: \"%s\" must be a string", key);

    *out = v ? v->str : fallback;
    return true;
}

// "songs": {"title": 140, "forest": 96}: each song under music/ by stem, and its tempo.
static bool cast_song_map(orb_arena* a, const orb_json* root, orb_manifest* m, orb_error* err) {
    const orb_json* map = orb_json_get(root, "songs");

    if (!map) return true;

    if (map->kind != ORB_JSON_OBJECT)
        return orb_error_set(err, "orb.json: \"songs\" must be an object of stem to bpm");

    orb_manifest_song* songs = orb_arena_push_array(a, orb_manifest_song, map->count);

    for (const orb_json* s = map->first; s; s = s->next) {
        if (s->kind != ORB_JSON_NUMBER || !(s->num > 0 && s->num <= ORB_MAX_BPM))
            return orb_error_set(
                err, "orb.json: songs: \"%s\" must be a bpm within 0..%g", s->key,
                (double)ORB_MAX_BPM
            );

        songs[m->song_count++] = (orb_manifest_song) {.bpm = (float)s->num, .stem = s->key};
    }

    m->songs = songs;
    return true;
}

// "buttons": {"select": "m", "a": "space"}: a key symbol per button it names.
static bool cast_button_map(const orb_json* root, orb_manifest* m, orb_error* err) {
    const orb_json* map = orb_json_get(root, "buttons");

    if (!map) return true;

    if (map->kind != ORB_JSON_OBJECT)
        return orb_error_set(err, "orb.json: \"buttons\" must be an object of button to key");

    for (const orb_json* b = map->first; b; b = b->next) {
        int button = -1;

        for (int i = 0; i < ORB_BTN_COUNT; i++)
            if (strcmp(b->key, orb_button_names[i]) == 0) button = i;

        if (button < 0)
            return orb_error_set(err, "orb.json: \"buttons\" names no button \"%s\"", b->key);
        if (m->buttons[button][0])
            return orb_error_set(err, "orb.json: \"buttons\" names \"%s\" twice", b->key);
        if (b->kind != ORB_JSON_STRING || !orb_input_symbol_valid(b->str))
            return orb_error_set(
                err, "orb.json: \"buttons.%s\" must be a key name or one character", b->key
            );

        snprintf(m->buttons[button], sizeof m->buttons[button], "%s", b->str);
    }

    return true;
}

bool orb_manifest_load(orb_arena* a, const char* game_dir, orb_manifest* m, orb_error* err) {
    orb_span text;
    const char* path = cast_path(a, game_dir, "orb.json");

    if (!orb_os_read_file(path, a, &text)) return orb_error_set(err, "cannot read %s", path);

    orb_json* root = orb_json_parse(a, (const char*)text.ptr, text.len, err);

    if (!root) return false;

    memset(m, 0, sizeof *m);

    const orb_json* size = orb_json_get(root, "size");

    if (!size || size->kind != ORB_JSON_ARRAY || size->count != 2)
        return orb_error_set(err, "orb.json: \"size\" must be [width, height]");

    m->size = (orb_size) {(int)size->first->num, (int)size->first->next->num};

    if (m->size.width < 1 || m->size.width > 4096 || m->size.height < 1 || m->size.height > 4096)
        return orb_error_set(err, "orb.json: \"size\" must be within 1..4096");

    const orb_json* headroom = orb_json_get(root, "asset_headroom");

    if (headroom && headroom->kind != ORB_JSON_NUMBER)
        return orb_error_set(err, "orb.json: \"asset_headroom\" must be a number of bytes");

    m->asset_headroom = headroom ? (size_t)headroom->num : 16 << 20;

    return cast_string(root, "id", nullptr, &m->id, err) &&
           cast_string(root, "name", nullptr, &m->name, err) &&
           cast_string(root, "palette", "art/palette.aseprite", &m->pal, err) &&
           cast_string(root, "art", "art", &m->art, err) &&
           cast_string(root, "fonts", "fonts", &m->fonts, err) &&
           cast_string(root, "sfx", "sfx", &m->sfx, err) &&
           cast_string(root, "music", "music", &m->music, err) &&
           cast_string(root, "world", "levels/world.ldtk", &m->world, err) &&
           cast_song_map(a, root, m, err) && cast_button_map(root, m, err);
}
