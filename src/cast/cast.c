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
        if (s->kind != ORB_JSON_NUMBER || !(s->num > 0 && s->num <= 1000))
            return orb_error_set(
                err, "orb.json: songs: \"%s\" must be a bpm within 0..1000", s->key
            );

        songs[m->song_count++] = (orb_manifest_song) {.bpm = (float)s->num, .stem = s->key};
    }

    m->songs = songs;
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

    m->size_w = (int)size->first->num;
    m->size_h = (int)size->first->next->num;

    if (m->size_w < 1 || m->size_w > 4096 || m->size_h < 1 || m->size_h > 4096)
        return orb_error_set(err, "orb.json: \"size\" must be within 1..4096");

    const orb_json* headroom = orb_json_get(root, "asset_headroom");

    if (headroom && headroom->kind != ORB_JSON_NUMBER)
        return orb_error_set(err, "orb.json: \"asset_headroom\" must be a number of bytes");

    m->asset_headroom = headroom ? (size_t)headroom->num : 16 << 20;

    return cast_string(root, "id", nullptr, &m->id, err) &&
           cast_string(root, "name", nullptr, &m->name, err) &&
           cast_string(root, "palette", "art/palette.aseprite", &m->palette, err) &&
           cast_string(root, "art", "art", &m->art, err) &&
           cast_string(root, "sfx", "sfx", &m->sfx, err) &&
           cast_string(root, "music", "music", &m->music, err) && cast_song_map(a, root, m, err);
}

// Everything one cast needs, so each stage takes one argument.
typedef struct cast {
    orb_arena* scratch;
    orb_arena* out;
    const char* game_dir;
    const orb_manifest* m;
    orb_cast_result* r;
    orb_error* err;
} cast;

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

typedef struct cast_files {
    const char** paths; // relative to game_dir, in walk order: sorted at each level
    const char** stems; // one per path, what the asset is found by
    int count;
} cast_files;

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
    static orb_path entries[ORB_CAST_MAX_READS];

    if (!cast_note(c, rel)) return false;

    const char* dir = cast_path(c->scratch, c->game_dir, rel);
    int n = orb_os_list_dir(dir, "", entries, ORB_CAST_MAX_READS);

    if (n == ORB_CAST_MAX_READS)
        return orb_error_set(c->err, "%s: more than %d entries in one directory", rel, n - 1);

    const char** names = orb_arena_push_array(c->scratch, const char*, n);
    bool* dirs = orb_arena_push_array(c->scratch, bool, n);

    for (int i = 0; i < n; i++) {
        names[i] = cast_path(c->scratch, rel, strrchr(entries[i], '/') + 1);
        dirs[i] = orb_os_is_dir(entries[i]);
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
    return cast_walk_into(c, rel, suffix, skip, files);
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

static bool cast_load_ase(cast* c, const char* rel, orb_ase* ase) {
    orb_span file;

    if (!cast_read(c, rel, &file)) return false;

    orb_error inner;

    if (!orb_ase_parse(c->scratch, file, ase, &inner))
        return orb_error_set(c->err, "%s: %s", rel, inner.text);

    return true;
}

static bool cast_load_wav(cast* c, const char* rel, orb_wav* wav) {
    orb_span file;

    if (!cast_read(c, rel, &file)) return false;

    orb_error inner;

    if (!orb_wav_parse(file, wav, &inner)) return orb_error_set(c->err, "%s: %s", rel, inner.text);

    return true;
}

// Two files of one kind with one stem would be one name to find; refuse the pair.
static bool cast_unique(cast* c, const cast_files* files) {
    uint64_t* ids = orb_arena_push_array(c->scratch, uint64_t, files->count);

    for (int i = 0; i < files->count; i++) {
        ids[i] = orb_asset_id(files->stems[i], "");

        for (int j = 0; j < i; j++) {
            if (ids[j] != ids[i]) continue;

            return orb_error_set(
                c->err, "%s and %s share a stem, so one name would find both", files->paths[j],
                files->paths[i]
            );
        }
    }

    return true;
}

// The master palette: orb's 256 RGB entries, the truth every other file's colors
// are matched against.
static bool cast_palette(cast* c, orb_ase* master, orb_assets* as) {
    if (!cast_load_ase(c, c->m->palette, master)) return false;

    uint8_t* palette = orb_arena_push(c->scratch, 256 * 4, 16);

    for (int i = 0; i < master->color_count; i++) {
        palette[i * 4 + 0] = master->rgb[i][0];
        palette[i * 4 + 1] = master->rgb[i][1];
        palette[i * 4 + 2] = master->rgb[i][2];
    }

    as->palette = palette;
    return true;
}

// Every sprite file: one packed sheet each, a sprite per frame, an animation per tag.
static bool cast_art(cast* c, const orb_ase* master, orb_assets* as) {
    orb_arena* scratch = c->scratch;
    cast_files art;

    if (!cast_walk(c, c->m->art, ".aseprite", c->m->palette, &art) || !cast_unique(c, &art))
        return false;

    int file_count = art.count;
    const char** paths = art.paths;

    // Generous upper bounds so tables can be filled in one pass.
    uint32_t max_sprites = 0, max_anims = 0;
    orb_ase* files = orb_arena_push_array(scratch, orb_ase, file_count);

    for (int i = 0; i < file_count; i++) {
        if (!cast_load_ase(c, paths[i], &files[i])) return false;
        max_sprites += files[i].frame_count;
        max_anims += files[i].tag_count;
    }

    orb_sheet_desc* sheets = orb_arena_push_array(scratch, orb_sheet_desc, file_count);
    orb_sprite_desc* sprites = orb_arena_push_array(scratch, orb_sprite_desc, max_sprites);
    orb_animation_desc* animations = orb_arena_push_array(scratch, orb_animation_desc, max_anims);
    uint16_t* durations = orb_arena_push_array(scratch, uint16_t, max_sprites);
    uint64_t* sprite_ids = orb_arena_push_array(scratch, uint64_t, max_sprites);
    uint64_t* animation_ids = orb_arena_push_array(scratch, uint64_t, max_anims);
    orb_pack* packs = orb_arena_push_array(scratch, orb_pack, file_count);
    uint32_t sprite_count = 0, animation_count = 0, pixel_total = 0, duration_count = 0;

    for (int i = 0; i < file_count; i++) {
        orb_ase* ase = &files[i];
        const char* stem = art.stems[i];

        uint8_t remap[256] = {0};

        for (int k = 0; k < ase->color_count; k++) {
            if (k == ase->transparent) continue;

            int found = 0;

            for (int j = 1; j < master->color_count; j++) {
                if (memcmp(master->rgb[j], ase->rgb[k], 3) == 0) {
                    found = j;
                    break;
                }
            }

            if (!found)
                return orb_error_set(
                    c->err, "%s: color %d (%d,%d,%d) is not in the master palette", paths[i], k,
                    ase->rgb[k][0], ase->rgb[k][1], ase->rgb[k][2]
                );

            remap[k] = (uint8_t)found;
        }

        size_t pixel_count = (size_t)ase->w * ase->h * ase->frame_count;

        for (size_t p = 0; p < pixel_count; p++)
            ase->frames[p] = remap[ase->frames[p]];

        orb_pack* pack = &packs[i];

        orb_pack_frames(scratch, ase->frames, ase->frame_count, (orb_size) {ase->w, ase->h}, pack);
        sheets[i] =
            (orb_sheet_desc) {.w = pack->sheet_w, .h = pack->sheet_h, .pixels = pixel_total};
        pixel_total += (uint32_t)pack->sheet_w * pack->sheet_h;

        uint32_t first_sprite = sprite_count;

        for (uint32_t f = 0; f < ase->frame_count; f++) {
            const orb_pack_rect* rect = &pack->rects[pack->frames[f].rect];

            sprites[sprite_count] = (orb_sprite_desc) {
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
            sprite_ids[sprite_count++] = orb_asset_id(stem, suffix);
        }

        for (int t = 0; t < ase->tag_count; t++) {
            const orb_ase_tag* tag = &ase->tags[t];

            if (tag->direction != 0)
                return orb_error_set(
                    c->err, "%s: tag \"%s\" uses a loop direction other than forward", paths[i],
                    tag->name
                );

            animations[animation_count] = (orb_animation_desc) {
                .first_sprite = first_sprite + tag->from,
                .first_duration = duration_count,
                .count = (uint16_t)(tag->to - tag->from + 1)
            };

            for (int f = tag->from; f <= tag->to; f++) {
                uint32_t ticks = (ase->durations[f] * 60u + 500u) / 1000u;

                durations[duration_count++] = (uint16_t)(ticks ? ticks : 1);
            }

            animation_ids[animation_count++] = orb_asset_id(stem, tag->name);
        }
    }

    uint8_t* pixels = orb_arena_push(scratch, pixel_total, 16);

    for (int i = 0; i < file_count; i++) {
        memcpy(
            pixels + sheets[i].pixels, packs[i].pixels, (size_t)packs[i].sheet_w * packs[i].sheet_h
        );
    }

    as->sheets = sheets;
    as->sheet_count = (uint32_t)file_count;
    as->pixels = pixels;
    as->pixel_count = pixel_total;
    as->sprites = sprites;
    as->sprite_count = sprite_count;
    as->animations = animations;
    as->animation_count = animation_count;
    as->durations = durations;
    as->duration_count = duration_count;
    as->sprite_ids = sprite_ids;
    as->animation_ids = animation_ids;
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

    cast_files sounds = {wavs.paths, wavs.stems, (int)sound_count};
    cast_files music = {
        wavs.paths + sound_count, wavs.stems + sound_count, wavs.count - (int)sound_count
    };

    if (!cast_unique(c, &sounds) || !cast_unique(c, &music)) return false;

    uint32_t wav_count = (uint32_t)wavs.count;
    orb_sample_desc* samples = orb_arena_push_array(scratch, orb_sample_desc, wav_count);
    uint64_t* sample_ids = orb_arena_push_array(scratch, uint64_t, wav_count);
    orb_song_desc* songs = orb_arena_push_array(scratch, orb_song_desc, music.count);
    uint64_t* song_ids = orb_arena_push_array(scratch, uint64_t, music.count);
    bool* named = orb_arena_push_array(scratch, bool, m->song_count);
    uint32_t pcm_total = 0;

    for (int i = 0; i < music.count; i++) {
        int index = cast_song_index(m, music.stems[i]);

        if (index < 0) return orb_error_set(c->err, "%s: no bpm in orb.json songs", music.paths[i]);

        named[index] = true;
        songs[i] =
            (orb_song_desc) {.sample = sound_count + (uint32_t)i, .bpm = m->songs[index].bpm};
        song_ids[i] = orb_asset_id(music.stems[i], "");
    }

    for (int i = 0; i < m->song_count; i++) {
        if (named[i]) continue;

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

        bool loop = w.has_loop && w.loop_end > w.loop_start;

        if (!song && w.channels != 1)
            return orb_error_set(
                c->err,
                "%s: sounds must be mono, since pan positions them; only songs may be stereo", rel
            );

        if (w.has_loop && !loop) orb_log("%s: loop points ignored, the loop is empty", rel);

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

// Arm both arenas so that running out of room unwinds to the cast entry point
// as an ordinary error instead of ending the process.
static bool cast_body(cast* c) {
    orb_ase master;
    orb_assets as = {};
    orb_info_desc info = {.w = (uint16_t)c->m->size_w, .h = (uint16_t)c->m->size_h};

    snprintf(info.name, sizeof info.name, "%s", c->m->name);
    as.info = &info;

    if (!cast_palette(c, &master, &as) || !cast_art(c, &master, &as) || !cast_audio(c, &as))
        return false;

    c->r->sprite_count = as.sprite_count;
    c->r->animation_count = as.animation_count;
    c->r->sample_count = as.sample_count;
    c->r->song_count = as.song_count;
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
