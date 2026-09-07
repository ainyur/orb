#include "json.h"

#include <stdalign.h>
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
    orb_error_set(j->err, "json line %d: %s", j->line, msg);

    return false;
}

static orb_json* json_node(json_parser* j, orb_json_kind kind) {
    orb_json* n = orb_arena_push(j->a, sizeof *n, alignof(orb_json));
    n->kind = kind;

    return n;
}

static bool json_string(json_parser* j, const char** out) {
    if (j->p >= j->end || *j->p != '"') return json_fail(j, "expected string");

    j->p++;

    size_t len = 0;

    for (const char* q = j->p;; q++, len++) {
        if (q >= j->end) return json_fail(j, "unterminated string");
        if (*q == '"') break;

        if (*q == '\\') {
            q++;

            if (q >= j->end) return json_fail(j, "unterminated string");
            if (*q == 'u') return json_fail(j, "\\u escapes are not supported");
        }
    }

    char* s = orb_arena_push(j->a, len + 1, 1);

    for (size_t i = 0; i < len; i++) {
        char c = *j->p++;

        if (c == '\\') {
            c = *j->p++;

            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c == 'r')
                c = '\r';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
        }

        s[i] = c;
    }

    j->p++; // closing quote
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
