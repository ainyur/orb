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
    const orb_manifest* manifest;
    orb_cast_result* result;
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
static const char* cast_path(orb_arena* arena, const char* dir, const char* rel) {
    if (orb_path_absolute(rel) || !*dir) {
        size_t n = strlen(rel) + 1;
        return memcpy(orb_arena_push(arena, n, 1), rel, n);
    }

    size_t n = strlen(dir) + 1 + strlen(rel) + 1;
    char* path = orb_arena_push(arena, n, 1);

    snprintf(path, n, "%s/%s", dir, rel);
    return path;
}

// The directory part of a path, "" when there is none: cast_dir_of("levels/world.ldtk")
// is "levels", cast_dir_of("w.ldtk") is "", cast_dir_of("/abs/dir/w.ldtk") is "/abs/dir".
static const char* cast_dir_of(orb_arena* arena, const char* rel) {
    const char* slash = strrchr(rel, '/');

#ifdef _WIN32
    const char* backslash = strrchr(rel, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif

    if (!slash) return "";

    size_t n = (size_t)(slash - rel);
    char* path = orb_arena_push(arena, n + 1, 1);

    memcpy(path, rel, n);
    path[n] = 0;
    return path;
}

// Record a path under game_dir as read, once: the list scry watches.
static bool cast_note(cast* context, const char* rel) {
    orb_cast_result* result = context->result;

    for (int i = 0; i < result->read_count; i++)
        if (strcmp(result->reads[i], rel) == 0) return true;

    if (result->read_count == ORB_CAST_MAX_READS)
        return orb_error_set(
            context->err, "more than %d files and directories in one cast", ORB_CAST_MAX_READS
        );

    result->reads[result->read_count++] = rel;
    return true;
}

static bool cast_read(cast* context, const char* rel, orb_span* out) {
    if (!cast_note(context, rel)) return false;

    const char* path = cast_path(context->scratch, context->game_dir, rel);

    if (!orb_os_read_file(path, context->scratch, out))
        return orb_error_set(context->err, "cannot read %s", path);

    return true;
}

// "art/player.aseprite" -> "player"; orb_asset_id folds the case.
static const char* cast_stem(orb_arena* arena, const char* path) {
    const char* slash = strrchr(path, '/');
    const char* start = slash ? slash + 1 : path;
    const char* dot = strrchr(start, '.');
    size_t n = dot ? (size_t)(dot - start) : strlen(start);

    return memcpy(orb_arena_push(arena, n + 1, 1), start, n);
}

// Every file with the suffix under rel, recursing, but the one path to skip. A
// missing directory is empty. The listing buffer is one static reused by every
// level, so a level copies its names out before it recurses. Each file is noted
// as it is found, so the reads cap bounds this list too.
static bool cast_walk_into(
    cast* context,
    const char* rel,
    const char* suffix,
    const char* skip,
    cast_files* files
) {
    static orb_os_entry entries[ORB_CAST_MAX_READS];

    if (!cast_note(context, rel)) return false;

    const char* dir = cast_path(context->scratch, context->game_dir, rel);
    int n = orb_os_list_dir(dir, entries, ORB_CAST_MAX_READS);

    if (n == ORB_CAST_MAX_READS)
        return orb_error_set(context->err, "%s: more than %d entries in one directory", rel, n - 1);

    const char** names = orb_arena_push_array(context->scratch, const char*, n);
    bool* dirs = orb_arena_push_array(context->scratch, bool, n);

    for (int i = 0; i < n; i++) {
        names[i] = cast_path(context->scratch, rel, entries[i].name);
        dirs[i] = entries[i].dir;
    }

    for (int i = 0; i < n; i++) {
        if (dirs[i]) {
            if (!cast_walk_into(context, names[i], suffix, skip, files)) return false;
        } else if (orb_has_suffix(names[i], suffix) && strcmp(names[i], skip) != 0) {
            if (!cast_note(context, names[i])) return false;

            files->stems[files->count] = cast_stem(context->scratch, names[i]);
            files->paths[files->count++] = names[i];
        }
    }

    return true;
}

static bool cast_walk(
    cast* context,
    const char* rel,
    const char* suffix,
    const char* skip,
    cast_files* files
) {
    files->paths = orb_arena_push_array(context->scratch, const char*, ORB_CAST_MAX_READS);
    files->stems = orb_arena_push_array(context->scratch, const char*, ORB_CAST_MAX_READS);
    files->count = 0;
    files->grid_x = nullptr;
    files->grid_y = nullptr;
    files->grid_width = nullptr;
    files->grid_height = nullptr;
    return cast_walk_into(context, rel, suffix, skip, files);
}

// The index below i whose id matches ids[i], or -1.
static int cast_dup_id(const uint64_t* ids, int i) {
    for (int j = 0; j < i; j++)
        if (ids[j] == ids[i]) return j;

    return -1;
}

// Two files of one kind with one stem would be one name to find; refuse the pair.
static bool cast_unique(cast* context, const cast_files* files) {
    uint64_t* ids = orb_arena_push_array(context->scratch, uint64_t, files->count);

    for (int i = 0; i < files->count; i++) {
        ids[i] = orb_asset_id(files->stems[i], "");

        int dup = cast_dup_id(ids, i);

        if (dup >= 0)
            return orb_error_set(
                context->err, "%s and %s share a stem, so one name would find both",
                files->paths[dup], files->paths[i]
            );
    }

    return true;
}

static bool cast_load_ase(cast* context, const char* rel, orb_ase* ase) {
    orb_span file;

    if (!cast_read(context, rel, &file)) return false;

    orb_error inner;

    if (!orb_ase_parse(context->scratch, file, ase, &inner))
        return orb_error_set(context->err, "%s: %s", rel, inner.text);

    return true;
}

// The master palette: orb's 256 RGB entries, the truth every other file's colors
// are matched against.
static bool cast_pal(cast* context, orb_ase* master, orb_assets* assets) {
    if (!cast_load_ase(context, context->manifest->pal, master)) return false;

    uint8_t* pal = orb_arena_push(context->scratch, 256 * 4, 16);

    for (int i = 0; i < master->color_count; i++) {
        pal[i * 4 + 0] = master->rgb[i][0];
        pal[i * 4 + 1] = master->rgb[i][1];
        pal[i * 4 + 2] = master->rgb[i][2];
    }

    assets->pal = pal;
    return true;
}

// Remaps one file's palette indices onto the master palette, in place.
static bool cast_art_remap(cast* context, const orb_ase* master, orb_ase* ase, const char* path) {
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
                context->err, "%s: color %d (%d,%d,%d) is not in the master palette", path, k,
                ase->rgb[k][0], ase->rgb[k][1], ase->rgb[k][2]
            );

        remap[k] = (uint8_t)master_index;
    }

    size_t pixel_count = (size_t)ase->width * ase->height * ase->frame_count;

    for (size_t i = 0; i < pixel_count; i++)
        ase->frames[i] = remap[ase->frames[i]];

    return true;
}

// Every sprite file: one packed sheet each, a sprite per frame, an animation per tag.
// Font and tileset files, appended after the art files, take a plainer path: one
// unpacked sheet each, no sprites or animations.
static bool cast_art(
    cast* context,
    const orb_ase* master,
    const orb_ldtk* world,
    cast_files* fonts,
    uint32_t* first_font_sheet,
    uint32_t* first_tileset_sheet,
    orb_assets* assets
) {
    orb_arena* scratch = context->scratch;
    cast_files art;

    if (!cast_walk(context, context->manifest->art, ".aseprite", context->manifest->pal, &art) ||
        !cast_unique(context, &art))
        return false;

    int art_count = art.count;
    int font_count = fonts->count;
    int file_count = art_count + font_count + world->tileset_count;
    const char* world_dir = cast_dir_of(scratch, context->manifest->world);
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
        if (!cast_load_ase(context, paths[i], &files[i])) return false;

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

        if (!cast_art_remap(context, master, ase, paths[i])) return false;

        if (i >= art_count && i < art_count + font_count) {
            if (ase->frame_count != 1)
                return orb_error_set(
                    context->err, "%s: a font has one frame, this file has %u", paths[i],
                    ase->frame_count
                );

            int font_index = i - art_count;

            fonts->grid_x[font_index] = ase->grid_x;
            fonts->grid_y[font_index] = ase->grid_y;
            fonts->grid_width[font_index] = ase->grid_width;
            fonts->grid_height[font_index] = ase->grid_height;
        }

        if (i >= art_count + font_count) {
            const orb_ldtk_tileset* tileset = &world->tilesets[i - art_count - font_count];

            if (ase->frame_count != 1)
                return orb_error_set(
                    context->err, "%s: a tileset has one frame, this file has %u", paths[i],
                    ase->frame_count
                );
            if (ase->width != tileset->width || ase->height != tileset->height)
                return orb_error_set(
                    context->err, "%s: %ux%u, but the project says %dx%d", paths[i], ase->width,
                    ase->height, tileset->width, tileset->height
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

        for (uint32_t frame = 0; frame < ase->frame_count; frame++) {
            const orb_pack_rect* rect = &pack->rects[pack->frames[frame].rect];

            sprites[sprite_count] = (orb_sprite_desc) {
                .sheet = (uint16_t)i,
                .x = rect->x,
                .y = rect->y,
                .width = rect->width,
                .height = rect->height,
                .ox = pack->frames[frame].ox,
                .oy = pack->frames[frame].oy,
                .frame_width = ase->width,
                .frame_height = ase->height
            };

            sprite_ids[sprite_count++] = orb_sprite_id(stem, (int)frame);
        }

        for (int tag_index = 0; tag_index < ase->tag_count; tag_index++) {
            const orb_ase_tag* tag = &ase->tags[tag_index];

            if (tag->direction != 0)
                return orb_error_set(
                    context->err, "%s: tag \"%s\" uses a loop direction other than forward",
                    paths[i], tag->name
                );

            anims[anim_count] = (orb_anim_desc) {
                .first_sprite = first_sprite + tag->from,
                .first_duration = duration_count,
                .count = (uint16_t)(tag->to - tag->from + 1)
            };

            for (int frame = tag->from; frame <= tag->to; frame++) {
                uint32_t ticks = (ase->durations[frame] * ORB_TICK_RATE + 500u) / 1000u;

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

    assets->sheets = sheets;
    assets->sheet_count = (uint32_t)file_count;
    assets->pixels = pixels;
    assets->pixel_count = pixel_total;
    assets->sprites = sprites;
    assets->sprite_count = sprite_count;
    assets->anims = anims;
    assets->anim_count = anim_count;
    assets->durations = durations;
    assets->duration_count = duration_count;
    assets->sprite_ids = sprite_ids;
    assets->anim_ids = anim_ids;
    return true;
}

static orb_sample_desc cast_sample_desc(const orb_wav* wav, uint32_t first) {
    bool loop = wav->has_loop;

    return (orb_sample_desc) {
        .first = first,
        .count = wav->count,
        .loop_start = loop ? wav->loop_start : 0,
        .loop_end = loop ? wav->loop_end : 0,
        .rate = wav->rate,
        .channels = wav->channels
    };
}

static bool cast_load_wav(cast* context, const char* rel, orb_wav* wav) {
    orb_span file;

    if (!cast_read(context, rel, &file)) return false;

    orb_error inner;

    if (!orb_wav_parse(file, wav, &inner))
        return orb_error_set(context->err, "%s: %s", rel, inner.text);

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
static int cast_song_index(const orb_manifest* manifest, const char* stem) {
    uint64_t id = orb_asset_id(stem, "");

    for (int i = 0; i < manifest->song_count; i++)
        if (orb_asset_id(manifest->songs[i].stem, "") == id) return i;

    return -1;
}

// Sounds first, then each song's sample, so a song and a sound may share a stem.
// Two passes, each file's bytes dropped after use: the first sizes the packed PCM,
// the second decodes into it, so scratch holds one file beside the pack.
static bool cast_audio(cast* context, orb_assets* assets) {
    orb_arena* scratch = context->scratch;
    const orb_manifest* manifest = context->manifest;
    cast_files wavs;

    if (!cast_walk(context, manifest->sfx, ".wav", "", &wavs)) return false;

    uint32_t sound_count = (uint32_t)wavs.count;

    if (!cast_walk_into(context, manifest->music, ".wav", "", &wavs)) return false;

    cast_files sounds = {.paths = wavs.paths, .stems = wavs.stems, .count = (int)sound_count};
    cast_files music = {
        .paths = wavs.paths + sound_count,
        .stems = wavs.stems + sound_count,
        .count = wavs.count - (int)sound_count
    };

    if (!cast_unique(context, &sounds) || !cast_unique(context, &music)) return false;

    uint32_t wav_count = (uint32_t)wavs.count;
    orb_sample_desc* samples = orb_arena_push_array(scratch, orb_sample_desc, wav_count);
    uint64_t* sample_ids = orb_arena_push_array(scratch, uint64_t, wav_count);
    orb_song_desc* songs = orb_arena_push_array(scratch, orb_song_desc, music.count);
    uint64_t* song_ids = orb_arena_push_array(scratch, uint64_t, music.count);
    bool* has_file = orb_arena_push_array(scratch, bool, manifest->song_count);
    uint32_t pcm_total = 0;

    for (int i = 0; i < music.count; i++) {
        int index = cast_song_index(manifest, music.stems[i]);

        if (index < 0)
            return orb_error_set(context->err, "%s: no bpm in orb.json songs", music.paths[i]);

        has_file[index] = true;
        songs[i] = (orb_song_desc) {
            .sample = sound_count + (uint32_t)i,
            .millibpm = (uint32_t)(manifest->songs[index].bpm * 1000 + 0.5f)
        };
        song_ids[i] = orb_asset_id(music.stems[i], "");
    }

    for (int i = 0; i < manifest->song_count; i++) {
        if (has_file[i]) continue;

        return orb_error_set(
            context->err, "orb.json: songs: no %s/%s.wav for \"%s\"", manifest->music,
            manifest->songs[i].stem, manifest->songs[i].stem
        );
    }

    for (uint32_t i = 0; i < wav_count; i++) {
        bool song = i >= sound_count;
        const char* rel = wavs.paths[i];
        size_t mark = scratch->used;
        orb_wav wav;

        if (!cast_load_wav(context, rel, &wav)) return false;

        scratch->used = mark; // the header is all this pass needs

        if (!song && wav.channels != 1)
            return orb_error_set(
                context->err,
                "%s: sounds must be mono, since pan positions them; only songs may be stereo", rel
            );

        samples[i] = cast_sample_desc(&wav, pcm_total);
        sample_ids[i] = orb_asset_id(wavs.stems[i], song ? "song" : "");
        pcm_total += wav.count * wav.channels;
    }

    int16_t* pcm = orb_arena_push_array(scratch, int16_t, pcm_total);

    for (uint32_t i = 0; i < wav_count; i++) {
        const char* rel = wavs.paths[i];
        size_t mark = scratch->used;
        orb_wav wav;

        if (!cast_load_wav(context, rel, &wav)) return false;

        orb_sample_desc again = cast_sample_desc(&wav, samples[i].first);

        if (memcmp(&again, &samples[i], sizeof again) != 0) { // the pack is sized by pass one
            return orb_error_set(context->err, "%s changed while casting", rel);
        }

        orb_wav_decode(&wav, pcm + samples[i].first);
        scratch->used = mark;
    }

    assets->samples = samples;
    assets->sample_count = wav_count;
    assets->pcm = pcm;
    assets->pcm_count = pcm_total;
    assets->songs = songs;
    assets->song_count = (uint32_t)music.count;
    assets->sample_ids = sample_ids;
    assets->song_ids = song_ids;
    return true;
}

#include "world.c"

#include "font.c"

static bool cast_body(cast* context) {
    orb_ase master;
    orb_ldtk world;
    orb_assets assets = {};
    orb_info_desc info = {
        .width = (uint16_t)context->manifest->size.width,
        .height = (uint16_t)context->manifest->size.height
    };

    snprintf(info.name, sizeof info.name, "%s", context->manifest->name);
    assets.info = &info;

    cast_files fonts;
    uint32_t first_font_sheet = 0, first_tileset_sheet = 0;

    if (!cast_pal(context, &master, &assets) || !world_parse(context, &world) ||
        !cast_walk(context, context->manifest->fonts, ".aseprite", "", &fonts) ||
        !cast_unique(context, &fonts) ||
        !cast_art(
            context, &master, &world, &fonts, &first_font_sheet, &first_tileset_sheet, &assets
        ) ||
        !font_cast(context, &fonts, first_font_sheet, &assets) ||
        !world_cast(context, &world, first_tileset_sheet, &assets) || !cast_audio(context, &assets))
        return false;

    orb_binding_desc bindings[ORB_BTN_COUNT] = {};

    for (int i = 0; i < ORB_BTN_COUNT; i++)
        snprintf(
            bindings[i].symbol, sizeof bindings[i].symbol, "%s",
            context->manifest->buttons[i][0] ? context->manifest->buttons[i]
                                             : orb_button_defaults[i]
        );

    assets.bindings = bindings;
    assets.binding_count = ORB_BTN_COUNT;

    context->result->file = orb_file_write(context->out, &assets);
    return true;
}

bool orb_cast_game(
    orb_arena* scratch,
    orb_arena* out,
    const char* game_dir,
    orb_manifest* manifest,
    orb_cast_result* result,
    orb_error* err
) {
    // Both arenas armed: running out of room unwinds here as an ordinary cast
    // error instead of ending the process, so scry keeps the live half.
    jmp_buf recover;
    cast context = {scratch, out, game_dir, manifest, result, err};

    scratch->recover = out->recover = &recover;
    scratch->overflow = out->overflow = 0;
    memset(result, 0, sizeof *result);

    bool ok;

    if (setjmp(recover) == 0) {
        result->reads = orb_arena_push_array(scratch, const char*, ORB_CAST_MAX_READS);
        result->reads[result->read_count++] = "orb.json";
        ok = orb_manifest_load(scratch, game_dir, manifest, err) && cast_body(&context);
    } else
        ok = orb_arena_error(scratch->overflow ? scratch : out, err);

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
    const orb_json* value = orb_json_get(root, key);

    if ((!value && !fallback) || (value && value->kind != ORB_JSON_STRING))
        return orb_error_set(err, "orb.json: \"%s\" must be a string", key);

    *out = value ? value->str : fallback;
    return true;
}

// "songs": {"title": 140, "forest": 96}: each song under music/ by stem, and its tempo.
static bool cast_song_map(
    orb_arena* arena,
    const orb_json* root,
    orb_manifest* manifest,
    orb_error* err
) {
    const orb_json* map = orb_json_get(root, "songs");

    if (!map) return true;

    if (map->kind != ORB_JSON_OBJECT)
        return orb_error_set(err, "orb.json: \"songs\" must be an object of stem to bpm");

    orb_manifest_song* songs = orb_arena_push_array(arena, orb_manifest_song, map->count);

    for (const orb_json* node = map->first; node; node = node->next) {
        if (node->kind != ORB_JSON_NUMBER || !(node->num > 0 && node->num <= ORB_MAX_BPM))
            return orb_error_set(
                err, "orb.json: songs: \"%s\" must be a bpm within 0..%g", node->key,
                (double)ORB_MAX_BPM
            );

        songs[manifest->song_count++] =
            (orb_manifest_song) {.bpm = (float)node->num, .stem = node->key};
    }

    manifest->songs = songs;
    return true;
}

// "buttons": {"select": "m", "a": "space"}: a key symbol per button it names.
static bool cast_button_map(const orb_json* root, orb_manifest* manifest, orb_error* err) {
    const orb_json* map = orb_json_get(root, "buttons");

    if (!map) return true;

    if (map->kind != ORB_JSON_OBJECT)
        return orb_error_set(err, "orb.json: \"buttons\" must be an object of button to key");

    for (const orb_json* node = map->first; node; node = node->next) {
        int button = -1;

        for (int i = 0; i < ORB_BTN_COUNT; i++)
            if (strcmp(node->key, orb_button_names[i]) == 0) button = i;

        if (button < 0)
            return orb_error_set(err, "orb.json: \"buttons\" names no button \"%s\"", node->key);
        if (manifest->buttons[button][0])
            return orb_error_set(err, "orb.json: \"buttons\" names \"%s\" twice", node->key);
        if (node->kind != ORB_JSON_STRING || !orb_input_symbol_valid(node->str))
            return orb_error_set(
                err, "orb.json: \"buttons.%s\" must be a key name or one character", node->key
            );

        snprintf(manifest->buttons[button], sizeof manifest->buttons[button], "%s", node->str);
    }

    return true;
}

bool orb_manifest_load(
    orb_arena* arena,
    const char* game_dir,
    orb_manifest* manifest,
    orb_error* err
) {
    orb_span text;
    const char* path = cast_path(arena, game_dir, "orb.json");

    if (!orb_os_read_file(path, arena, &text)) return orb_error_set(err, "cannot read %s", path);

    orb_json* root = orb_json_parse(arena, (const char*)text.ptr, text.len, err);

    if (!root) return false;

    memset(manifest, 0, sizeof *manifest);

    const orb_json* size = orb_json_get(root, "size");

    if (!size || size->kind != ORB_JSON_ARRAY || size->count != 2)
        return orb_error_set(err, "orb.json: \"size\" must be [width, height]");

    manifest->size = (orb_size) {(int)size->first->num, (int)size->first->next->num};

    if (manifest->size.width < 1 || manifest->size.width > 4096 || manifest->size.height < 1 ||
        manifest->size.height > 4096)
        return orb_error_set(err, "orb.json: \"size\" must be within 1..4096");

    return cast_string(root, "id", nullptr, &manifest->id, err) &&
           cast_string(root, "name", nullptr, &manifest->name, err) &&
           cast_string(root, "palette", "art/palette.aseprite", &manifest->pal, err) &&
           cast_string(root, "art", "art", &manifest->art, err) &&
           cast_string(root, "fonts", "fonts", &manifest->fonts, err) &&
           cast_string(root, "sfx", "sfx", &manifest->sfx, err) &&
           cast_string(root, "music", "music", &manifest->music, err) &&
           cast_string(root, "world", "levels/world.ldtk", &manifest->world, err) &&
           cast_song_map(arena, root, manifest, err) && cast_button_map(root, manifest, err);
}
