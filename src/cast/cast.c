#include "cast.h"
#include "../core/asset.h"
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
    arena* scratch;
    arena* out;
    const char* game_dir;
    const orb_manifest* manifest;
    orb_cast_result* result;
    orb_error* err;
} cast;

typedef struct cast_files {
    const char** paths; // relative to game_dir, in walk order: sorted at each level
    const char** stems; // one per path, what the asset is found by
    int count;
    i16* grid_x; // a font's Aseprite grid, filled by cast_art; null otherwise
    i16* grid_y;
    u16* grid_width;
    u16* grid_height;
} cast_files;

// dir/rel, unless rel is absolute or dir is empty, in which case rel is returned unchanged.
static const char* cast_path(arena* out, const char* dir, const char* rel) {
    if (orb_path_absolute(rel) || !*dir) {
        usize n = strlen(rel) + 1;
        return memcpy(orb_arena_push(out, n, 1), rel, n);
    }

    usize n = strlen(dir) + 1 + strlen(rel) + 1;
    char* path = orb_arena_push(out, n, 1);

    snprintf(path, n, "%s/%s", dir, rel);
    return path;
}

// The directory part of a path, "" when there is none: cast_dir_of("levels/world.ldtk")
// is "levels", cast_dir_of("w.ldtk") is "", cast_dir_of("/abs/dir/w.ldtk") is "/abs/dir".
static const char* cast_dir_of(arena* out, const char* rel) {
    const char* slash = strrchr(rel, '/');

#ifdef _WIN32
    const char* backslash = strrchr(rel, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
#endif

    if (!slash) return "";

    usize n = (usize)(slash - rel);
    char* path = orb_arena_push(out, n + 1, 1);

    memcpy(path, rel, n);
    path[n] = 0;
    return path;
}

// Record a path under game_dir as read, once: the list scry watches.
static bool cast_note(cast* context, const char* rel) {
    orb_cast_result* result = context->result;

    for (u32 i = 0; i < result->reads.len; i++)
        if (strcmp(result->reads.elems[i], rel) == 0) return true;

    if (result->reads.len == ORB_CAST_MAX_READS)
        return orb_error_set(
            context->err, "more than %d files and directories in one cast", ORB_CAST_MAX_READS
        );

    (void)push(context->scratch, &result->reads, rel);
    return true;
}

static bool cast_read(cast* context, const char* rel, u8_span* out) {
    if (!cast_note(context, rel)) return false;

    const char* path = cast_path(context->scratch, context->game_dir, rel);

    if (!orb_os_read_file(path, context->scratch, out))
        return orb_error_set(context->err, "cannot read %s", path);

    return true;
}

// "art/player.aseprite" -> "player"; orb_asset_id folds the case.
static const char* cast_stem(arena* out, const char* path) {
    const char* slash = strrchr(path, '/');
    const char* start = slash ? slash + 1 : path;
    const char* dot = strrchr(start, '.');
    usize n = dot ? (usize)(dot - start) : strlen(start);

    return memcpy(orb_arena_push(out, n + 1, 1), start, n);
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
static int cast_dup_id(const u64* ids, int i) {
    for (int j = 0; j < i; j++)
        if (ids[j] == ids[i]) return j;

    return -1;
}

// Two files of one kind with one stem would be one name to find; refuse the pair.
static bool cast_unique(cast* context, const cast_files* files) {
    u64* ids = orb_arena_push_array(context->scratch, u64, files->count);

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
    u8_span file;

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

    u8* pal = orb_arena_push(context->scratch, 256 * 4, 16);

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
    u8 remap[256] = {0};

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

        remap[k] = (u8)master_index;
    }

    usize pixel_count = (usize)ase->width * ase->height * ase->frame_count;

    for (usize i = 0; i < pixel_count; i++)
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
    u32* first_font_sheet,
    u32* first_tileset_sheet,
    orb_assets* assets
) {
    arena* scratch = context->scratch;
    cast_files art;

    if (!cast_walk(context, context->manifest->art, ".aseprite", context->manifest->pal, &art) ||
        !cast_unique(context, &art))
        return false;

    int art_count = art.count;
    int font_count = fonts->count;
    int file_count = art_count + font_count + world->tilesets.len;
    const char* world_dir = cast_dir_of(scratch, context->manifest->world);
    const char** paths = orb_arena_push_array(scratch, const char*, file_count);

    memcpy(paths, art.paths, sizeof *paths * art_count);
    memcpy(paths + art_count, fonts->paths, sizeof *paths * font_count);

    for (u32 i = 0; i < world->tilesets.len; i++)
        paths[art_count + font_count + i] =
            cast_path(scratch, world_dir, world->tilesets.elems[i].path);

    *first_font_sheet = (u32)art_count;
    *first_tileset_sheet = (u32)(art_count + font_count);

    fonts->grid_x = orb_arena_push_array(scratch, i16, font_count);
    fonts->grid_y = orb_arena_push_array(scratch, i16, font_count);
    fonts->grid_width = orb_arena_push_array(scratch, u16, font_count);
    fonts->grid_height = orb_arena_push_array(scratch, u16, font_count);

    // Generous upper bounds so tables can be filled in one pass; tileset files
    // add no sprites or animations.
    u32 max_sprites = 0, max_anims = 0;
    orb_ase* files = orb_arena_push_array(scratch, orb_ase, file_count);

    for (int i = 0; i < file_count; i++) {
        if (!cast_load_ase(context, paths[i], &files[i])) return false;

        if (i < art_count) {
            max_sprites += files[i].frame_count;
            max_anims += files[i].tags.len;
        }
    }

    orb_sheet_desc* sheets = orb_arena_push_array(scratch, orb_sheet_desc, file_count);
    orb_sprite_desc* sprites = orb_arena_push_array(scratch, orb_sprite_desc, max_sprites);
    orb_anim_desc* anims = orb_arena_push_array(scratch, orb_anim_desc, max_anims);
    u16* durations = orb_arena_push_array(scratch, u16, max_sprites);
    u64* sprite_ids = orb_arena_push_array(scratch, u64, max_sprites);
    u64* anim_ids = orb_arena_push_array(scratch, u64, max_anims);
    orb_pack* packs = orb_arena_push_array(scratch, orb_pack, file_count);
    u32 sprite_count = 0, anim_count = 0, pixel_total = 0, duration_count = 0;

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
            const orb_ldtk_tileset* tileset = &world->tilesets.elems[i - art_count - font_count];

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
            pixel_total += (u32)ase->width * ase->height;
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
        pixel_total += (u32)pack->sheet_width * pack->sheet_height;

        u32 first_sprite = sprite_count;

        for (u32 frame = 0; frame < ase->frame_count; frame++) {
            const orb_pack_rect* rect = &pack->rects.elems[pack->frames[frame].rect];

            sprites[sprite_count] = (orb_sprite_desc) {
                .sheet = (u16)i,
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

        for (u32 tag_index = 0; tag_index < ase->tags.len; tag_index++) {
            const orb_ase_tag* tag = &ase->tags.elems[tag_index];

            if (tag->direction != 0)
                return orb_error_set(
                    context->err, "%s: tag \"%s\" uses a loop direction other than forward",
                    paths[i], tag->name
                );

            anims[anim_count] = (orb_anim_desc) {
                .first_sprite = first_sprite + tag->from,
                .first_duration = duration_count,
                .count = (u16)(tag->to - tag->from + 1)
            };

            for (int frame = tag->from; frame <= tag->to; frame++) {
                u32 ticks = (ase->durations[frame] * ORB_TICK_RATE + 500u) / 1000u;

                durations[duration_count++] = (u16)(ticks ? ticks : 1);
            }

            anim_ids[anim_count++] = orb_asset_id(stem, tag->name);
        }
    }

    u8* pixels = orb_arena_push(scratch, pixel_total, 16);

    for (int i = 0; i < file_count; i++) {
        const u8* src = i < art_count ? packs[i].pixels : files[i].frames;
        usize n = i < art_count ? (usize)packs[i].sheet_width * packs[i].sheet_height
                                : (usize)files[i].width * files[i].height;

        memcpy(pixels + sheets[i].pixels, src, n);
    }

    assets->sheets = (orb_sheet_desc_span) {sheets, (u32)file_count};
    assets->pixels = (u8_span) {pixels, pixel_total};
    assets->sprites = (orb_sprite_desc_span) {sprites, sprite_count};
    assets->anims = (orb_anim_desc_span) {anims, anim_count};
    assets->durations = (u16_span) {durations, duration_count};
    assets->sprite_ids = sprite_ids;
    assets->anim_ids = anim_ids;
    return true;
}

static orb_sample_desc cast_sample_desc(const orb_wav* wav, u32 first) {
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
    u8_span file;

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
    u64 id = orb_asset_id(stem, "");

    for (u32 i = 0; i < manifest->songs.len; i++)
        if (orb_asset_id(manifest->songs.elems[i].stem, "") == id) return i;

    return -1;
}

// Sounds first, then each song's sample, so a song and a sound may share a stem.
// Two passes, each file's bytes dropped after use: the first sizes the packed PCM,
// the second decodes into it, so scratch holds one file beside the pack.
static bool cast_audio(cast* context, orb_assets* assets) {
    arena* scratch = context->scratch;
    const orb_manifest* manifest = context->manifest;
    cast_files wavs;

    if (!cast_walk(context, manifest->sfx, ".wav", "", &wavs)) return false;

    u32 sound_count = (u32)wavs.count;

    if (!cast_walk_into(context, manifest->music, ".wav", "", &wavs)) return false;

    cast_files sounds = {.paths = wavs.paths, .stems = wavs.stems, .count = (int)sound_count};
    cast_files music = {
        .paths = wavs.paths + sound_count,
        .stems = wavs.stems + sound_count,
        .count = wavs.count - (int)sound_count
    };

    if (!cast_unique(context, &sounds) || !cast_unique(context, &music)) return false;

    u32 wav_count = (u32)wavs.count;
    orb_sample_desc* samples = orb_arena_push_array(scratch, orb_sample_desc, wav_count);
    u64* sample_ids = orb_arena_push_array(scratch, u64, wav_count);
    orb_song_desc* songs = orb_arena_push_array(scratch, orb_song_desc, music.count);
    u64* song_ids = orb_arena_push_array(scratch, u64, music.count);
    bool* has_file = orb_arena_push_array(scratch, bool, manifest->songs.len);
    u32 pcm_total = 0;

    for (int i = 0; i < music.count; i++) {
        int index = cast_song_index(manifest, music.stems[i]);

        if (index < 0)
            return orb_error_set(context->err, "%s: no bpm in orb.json songs", music.paths[i]);

        has_file[index] = true;
        songs[i] = (orb_song_desc) {
            .sample = sound_count + (u32)i,
            .millibpm = (u32)(manifest->songs.elems[index].bpm * 1000 + 0.5f)
        };
        song_ids[i] = orb_asset_id(music.stems[i], "");
    }

    for (u32 i = 0; i < manifest->songs.len; i++) {
        if (has_file[i]) continue;

        return orb_error_set(
            context->err, "orb.json: songs: no %s/%s.wav for \"%s\"", manifest->music,
            manifest->songs.elems[i].stem, manifest->songs.elems[i].stem
        );
    }

    for (u32 i = 0; i < wav_count; i++) {
        bool song = i >= sound_count;
        const char* rel = wavs.paths[i];
        usize mark = scratch->used;
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

    i16* pcm = orb_arena_push_array(scratch, i16, pcm_total);

    for (u32 i = 0; i < wav_count; i++) {
        const char* rel = wavs.paths[i];
        usize mark = scratch->used;
        orb_wav wav;

        if (!cast_load_wav(context, rel, &wav)) return false;

        orb_sample_desc again = cast_sample_desc(&wav, samples[i].first);

        if (memcmp(&again, &samples[i], sizeof again) != 0) { // the pack is sized by pass one
            return orb_error_set(context->err, "%s changed while casting", rel);
        }

        orb_wav_decode(&wav, pcm + samples[i].first);
        scratch->used = mark;
    }

    assets->samples = (orb_sample_desc_span) {samples, wav_count};
    assets->pcm = (i16_span) {pcm, pcm_total};
    assets->songs = (orb_song_desc_span) {songs, (u32)music.count};
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
        .width = (u16)context->manifest->size.width, .height = (u16)context->manifest->size.height
    };

    snprintf(info.name, sizeof info.name, "%s", context->manifest->name);
    assets.info = &info;

    cast_files fonts;
    u32 first_font_sheet = 0, first_tileset_sheet = 0;

    if (!cast_pal(context, &master, &assets) || !world_parse(context, &world) ||
        !cast_walk(context, context->manifest->fonts, ".aseprite", "", &fonts) ||
        !cast_unique(context, &fonts) ||
        !cast_art(
            context, &master, &world, &fonts, &first_font_sheet, &first_tileset_sheet, &assets
        ) ||
        !font_cast(context, &fonts, first_font_sheet, &assets) ||
        !world_cast(context, &world, first_tileset_sheet, &assets) || !cast_audio(context, &assets))
        return false;

    context->result->file = orb_file_write(context->out, &assets);
    return true;
}

bool orb_cast_game(
    arena* scratch,
    arena* out,
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
        result->reads = (reads_list) {
            .elems = orb_arena_push_array(scratch, const char*, ORB_CAST_MAX_READS),
            .cap = ORB_CAST_MAX_READS
        };
        (void)push(scratch, &result->reads, "orb.json");
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
    arena* out,
    const orb_json* root,
    orb_manifest* manifest,
    orb_error* err
) {
    const orb_json* map = orb_json_get(root, "songs");

    if (!map) return true;

    if (map->kind != ORB_JSON_OBJECT)
        return orb_error_set(err, "orb.json: \"songs\" must be an object of stem to bpm");

    orb_manifest_song_list songs = {
        .elems = orb_arena_push_array(out, orb_manifest_song, map->count), .cap = (u32)map->count
    };

    for (const orb_json* node = map->first; node; node = node->next) {
        if (node->kind != ORB_JSON_NUMBER || !(node->num > 0 && node->num <= ORB_MAX_BPM))
            return orb_error_set(
                err, "orb.json: songs: \"%s\" must be a bpm within 0..%g", node->key,
                (f64)ORB_MAX_BPM
            );

        (void)push(out, &songs, ((orb_manifest_song) {.bpm = (f32)node->num, .stem = node->key}));
    }

    manifest->songs = songs.span;
    return true;
}

bool orb_manifest_load(arena* out, const char* game_dir, orb_manifest* manifest, orb_error* err) {
    u8_span text;
    const char* path = cast_path(out, game_dir, "orb.json");

    if (!orb_os_read_file(path, out, &text)) return orb_error_set(err, "cannot read %s", path);

    orb_json* root = orb_json_parse(out, (const char*)text.elems, text.len, err);

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
           cast_song_map(out, root, manifest, err);
}
