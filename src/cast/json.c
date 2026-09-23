#include "json.h"
#include "../core/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int JSON_MAX_DEPTH = 256;

typedef struct {
    orb_arena* arena;
    const char* cursor;
    const char* end;
    orb_error* err;
    int line;
    int depth; // open arrays and objects
} json_parser;

static void json_skip(json_parser* parser) {
    while (parser->cursor < parser->end && (*parser->cursor == ' ' || *parser->cursor == '\t' ||
                                            *parser->cursor == '\n' || *parser->cursor == '\r')) {
        if (*parser->cursor == '\n') parser->line++;
        parser->cursor++;
    }
}

static bool json_fail(json_parser* parser, const char* msg) {
    return orb_error_set(parser->err, "json line %d: %s", parser->line, msg);
}

static orb_json* json_node(json_parser* parser, orb_json_kind kind) {
    orb_json* node = orb_arena_push(parser->arena, sizeof *node, alignof(orb_json));
    node->kind = kind;

    return node;
}

static long json_hex4(json_parser* parser, const char** cursor) {
    long value = 0;

    for (int i = 0; i < 4; i++, (*cursor)++) {
        if (*cursor >= parser->end) return -1;

        char c = **cursor;
        int digit = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;

        if (digit < 0) return -1;

        value = value << 4 | digit;
    }

    return value;
}

// The code point of a \uXXXX escape at *cursor (cursor past the 'u'), joining a surrogate
// pair into one; -1 when the hex or the pair is malformed.
static long json_code_point(json_parser* parser, const char** cursor) {
    long code_point = json_hex4(parser, cursor);

    if (code_point < 0xd800 || code_point > 0xdfff) return code_point;
    if (code_point >= 0xdc00) return -1;
    if (*cursor + 6 > parser->end || (*cursor)[0] != '\\' || (*cursor)[1] != 'u') return -1;

    *cursor += 2;

    long low = json_hex4(parser, cursor);

    if (low < 0xdc00 || low > 0xdfff) return -1;

    return 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
}

// Decoded straight into the arena: pushes of alignment 1 are contiguous, so the
// string is wherever the arena stood when the scan began.
static bool json_string(json_parser* parser, const char** out) {
    if (parser->cursor >= parser->end || *parser->cursor != '"')
        return json_fail(parser, "expected string");

    char* start = (char*)parser->arena->base + parser->arena->used;

    parser->cursor++;

    for (;;) {
        if (parser->cursor >= parser->end) return json_fail(parser, "unterminated string");

        char c = *parser->cursor++;
        char buf[4] = {c};
        int n = 1;

        if (c == '"') break;

        if (c == '\\') {
            if (parser->cursor >= parser->end) return json_fail(parser, "unterminated string");

            c = *parser->cursor++;

            if (c == 'n')
                buf[0] = '\n';
            else if (c == 't')
                buf[0] = '\t';
            else if (c == 'r')
                buf[0] = '\r';
            else if (c == 'b')
                buf[0] = '\b';
            else if (c == 'f')
                buf[0] = '\f';
            else if (c == 'u') {
                long code_point = json_code_point(parser, &parser->cursor);

                if (code_point < 0) return json_fail(parser, "bad \\u escape");

                n = orb_bytes_utf8((uint32_t)code_point, buf);
            } else
                buf[0] = c;
        }

        memcpy(orb_arena_push(parser->arena, (size_t)n, 1), buf, (size_t)n);
    }

    orb_arena_push(parser->arena, 1, 1); // the terminator, since pushes are zeroed
    *out = start;
    return true;
}

static bool json_literal(json_parser* parser, const char* lit) {
    size_t n = strlen(lit);

    if ((size_t)(parser->end - parser->cursor) < n || memcmp(parser->cursor, lit, n) != 0)
        return false;

    parser->cursor += n;
    return true;
}

static bool json_value(json_parser* parser, orb_json** out) {
    json_skip(parser);

    if (parser->cursor >= parser->end) return json_fail(parser, "unexpected end of input");

    char c = *parser->cursor;

    if (c == '{' || c == '[') {
        if (parser->depth >= JSON_MAX_DEPTH) return json_fail(parser, "nested too deeply");

        parser->depth++;

        bool object = c == '{';
        char close = object ? '}' : ']';
        orb_json* node = json_node(parser, object ? ORB_JSON_OBJECT : ORB_JSON_ARRAY);
        orb_json** tail = &node->first;

        parser->cursor++;
        json_skip(parser);

        if (parser->cursor < parser->end && *parser->cursor == close) {
            parser->cursor++;
            parser->depth--;
            *out = node;
            return true;
        }

        for (;;) {
            const char* key = nullptr;

            if (object) {
                json_skip(parser);

                if (!json_string(parser, &key)) return false;

                json_skip(parser);

                if (parser->cursor >= parser->end || *parser->cursor != ':')
                    return json_fail(parser, "expected ':'");

                parser->cursor++;
            }

            orb_json* child;

            if (!json_value(parser, &child)) return false;

            child->key = key;
            *tail = child;
            tail = &child->next;
            node->count++;
            json_skip(parser);

            if (parser->cursor >= parser->end)
                return json_fail(parser, "unterminated array or object");

            if (*parser->cursor == ',') {
                parser->cursor++;
                continue;
            }

            if (*parser->cursor == close) {
                parser->cursor++;
                parser->depth--;
                *out = node;
                return true;
            }

            return json_fail(parser, "expected ',' or closing bracket");
        }
    }

    if (c == '"') {
        orb_json* node = json_node(parser, ORB_JSON_STRING);

        if (!json_string(parser, &node->str)) return false;

        *out = node;
        return true;
    }

    if (json_literal(parser, "true")) {
        orb_json* node = json_node(parser, ORB_JSON_BOOL);

        node->boolean = true;
        *out = node;
        return true;
    }

    if (json_literal(parser, "false")) {
        *out = json_node(parser, ORB_JSON_BOOL);
        return true;
    }

    if (json_literal(parser, "null")) {
        *out = json_node(parser, ORB_JSON_NULL);
        return true;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        char* after;
        double value = strtod(parser->cursor, &after);

        if (after == parser->cursor) return json_fail(parser, "bad number");

        orb_json* node = json_node(parser, ORB_JSON_NUMBER);

        node->num = value;
        parser->cursor = after;
        *out = node;
        return true;
    }

    return json_fail(parser, "unexpected character");
}

orb_json* orb_json_parse(orb_arena* arena, const char* text, size_t len, orb_error* err) {
    json_parser parser = {.arena = arena, .cursor = text, .end = text + len, .err = err, .line = 1};
    orb_json* root;

    if (!json_value(&parser, &root)) return nullptr;

    json_skip(&parser);

    if (parser.cursor != parser.end) {
        json_fail(&parser, "trailing characters after the document");
        return nullptr;
    }

    return root;
}

const orb_json* orb_json_get(const orb_json* object, const char* key) {
    if (!object || object->kind != ORB_JSON_OBJECT) return nullptr;

    for (const orb_json* child = object->first; child; child = child->next) {
        if (strcmp(child->key, key) == 0) return child;
    }

    return nullptr;
}
