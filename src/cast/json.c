#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    orb_arena* a;
    const char* p;
    const char* end;
    orb_error* err;
    int line;
} json_parser;

static void json_skip(json_parser* j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        if (*j->p == '\n') j->line++;
        j->p++;
    }
}

static bool json_fail(json_parser* j, const char* msg) {
    return orb_error_set(j->err, "json line %d: %s", j->line, msg);
}

static orb_json* json_node(json_parser* j, orb_json_kind kind) {
    orb_json* n = orb_arena_push(j->a, sizeof *n, alignof(orb_json));
    n->kind = kind;

    return n;
}

static long json_hex4(json_parser* j, const char** q) {
    long v = 0;

    for (int i = 0; i < 4; i++, (*q)++) {
        if (*q >= j->end) return -1;

        char c = **q;
        int d = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;

        if (d < 0) return -1;

        v = v << 4 | d;
    }

    return v;
}

// The code point of a \uXXXX escape at *q (q past the 'u'), joining a surrogate
// pair into one; -1 when the hex or the pair is malformed.
static long json_code_point(json_parser* j, const char** q) {
    long cp = json_hex4(j, q);

    if (cp < 0xd800 || cp > 0xdfff) return cp;
    if (cp >= 0xdc00) return -1;
    if (*q + 6 > j->end || (*q)[0] != '\\' || (*q)[1] != 'u') return -1;

    *q += 2;

    long low = json_hex4(j, q);

    if (low < 0xdc00 || low > 0xdfff) return -1;

    return 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
}

static int json_utf8(long cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }

    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }

    out[0] = (char)(0xf0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

// Decoded straight into the arena: pushes of alignment 1 are contiguous, so the
// string is wherever the arena stood when the scan began.
static bool json_string(json_parser* j, const char** out) {
    if (j->p >= j->end || *j->p != '"') return json_fail(j, "expected string");

    char* s = (char*)j->a->base + j->a->used;

    j->p++;

    for (;;) {
        if (j->p >= j->end) return json_fail(j, "unterminated string");

        char c = *j->p++;
        char buf[4] = {c};
        int n = 1;

        if (c == '"') break;

        if (c == '\\') {
            if (j->p >= j->end) return json_fail(j, "unterminated string");

            c = *j->p++;

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
                long cp = json_code_point(j, &j->p);

                if (cp < 0) return json_fail(j, "bad \\u escape");

                n = json_utf8(cp, buf);
            } else
                buf[0] = c;
        }

        memcpy(orb_arena_push(j->a, (size_t)n, 1), buf, (size_t)n);
    }

    orb_arena_push(j->a, 1, 1); // the terminator, since pushes are zeroed
    *out = s;
    return true;
}

static bool json_literal(json_parser* j, const char* lit) {
    size_t n = strlen(lit);

    if ((size_t)(j->end - j->p) < n || memcmp(j->p, lit, n) != 0) return false;

    j->p += n;
    return true;
}

static bool json_value(json_parser* j, orb_json** out) {
    json_skip(j);

    if (j->p >= j->end) return json_fail(j, "unexpected end of input");

    char c = *j->p;

    if (c == '{' || c == '[') {
        bool object = c == '{';
        char close = object ? '}' : ']';
        orb_json* n = json_node(j, object ? ORB_JSON_OBJECT : ORB_JSON_ARRAY);
        orb_json** tail = &n->first;

        j->p++;
        json_skip(j);

        if (j->p < j->end && *j->p == close) {
            j->p++;
            *out = n;
            return true;
        }

        for (;;) {
            const char* key = nullptr;

            if (object) {
                json_skip(j);

                if (!json_string(j, &key)) return false;

                json_skip(j);

                if (j->p >= j->end || *j->p != ':') return json_fail(j, "expected ':'");

                j->p++;
            }

            orb_json* child;

            if (!json_value(j, &child)) return false;

            child->key = key;
            *tail = child;
            tail = &child->next;
            n->count++;
            json_skip(j);

            if (j->p >= j->end) return json_fail(j, "unterminated array or object");

            if (*j->p == ',') {
                j->p++;
                continue;
            }

            if (*j->p == close) {
                j->p++;
                *out = n;
                return true;
            }

            return json_fail(j, "expected ',' or closing bracket");
        }
    }

    if (c == '"') {
        orb_json* n = json_node(j, ORB_JSON_STRING);

        if (!json_string(j, &n->str)) return false;

        *out = n;
        return true;
    }

    if (json_literal(j, "true")) {
        orb_json* n = json_node(j, ORB_JSON_BOOL);

        n->boolean = true;
        *out = n;
        return true;
    }

    if (json_literal(j, "false")) {
        *out = json_node(j, ORB_JSON_BOOL);
        return true;
    }

    if (json_literal(j, "null")) {
        *out = json_node(j, ORB_JSON_NULL);
        return true;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        char* after;
        double v = strtod(j->p, &after);

        if (after == j->p) return json_fail(j, "bad number");

        orb_json* n = json_node(j, ORB_JSON_NUMBER);

        n->num = v;
        j->p = after;
        *out = n;
        return true;
    }

    return json_fail(j, "unexpected character");
}

orb_json* orb_json_parse(orb_arena* a, const char* text, size_t len, orb_error* err) {
    json_parser j = {.a = a, .p = text, .end = text + len, .err = err, .line = 1};
    orb_json* root;

    if (!json_value(&j, &root)) return nullptr;

    json_skip(&j);

    if (j.p != j.end) {
        json_fail(&j, "trailing characters after the document");
        return nullptr;
    }

    return root;
}

const orb_json* orb_json_get(const orb_json* object, const char* key) {
    if (!object || object->kind != ORB_JSON_OBJECT) return nullptr;

    for (const orb_json* c = object->first; c; c = c->next) {
        if (strcmp(c->key, key) == 0) return c;
    }

    return nullptr;
}
