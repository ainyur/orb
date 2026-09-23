#pragma once

#include "../core/arena.h"
#include "../core/log.h"

#include <stddef.h>

typedef enum orb_json_kind {
    ORB_JSON_NULL,
    ORB_JSON_BOOL,
    ORB_JSON_NUMBER,
    ORB_JSON_STRING,
    ORB_JSON_ARRAY,
    ORB_JSON_OBJECT
} orb_json_kind;

typedef struct orb_json {
    orb_json_kind kind;
    int count; // array or object: children
    const char* key;
    union {
        const char* str;
        double num;
        bool boolean;
        struct orb_json* first;
    };
    struct orb_json* next;
} orb_json;

[[nodiscard]] orb_json* orb_json_parse(
    orb_arena* arena,
    const char* text,
    size_t len,
    orb_error* err
);
const orb_json* orb_json_get(const orb_json* object, const char* key);
