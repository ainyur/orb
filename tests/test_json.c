#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    orb_arena a;
    static uint8_t mem[1 << 16];
    orb_arena_init(&a, "test", mem, sizeof mem);

    CHECK_EQ(
        sizeof(orb_json), 32
    ); // the LDtk document lives in cast scratch, a node per tile field

    orb_error err;
    const char* text =
        "{\"name\": \"Hollow\\n\\\"Lantern\\\"\", \"size\": [320, 180], \"on\": true,"
        " \"off\": false, \"nil\": null, \"neg\": -2.5, \"empty\": {}, \"list\": []}";
    orb_json* root = orb_json_parse(&a, text, strlen(text), &err);
    CHECK(root != nullptr);
    CHECK_EQ(root->kind, ORB_JSON_OBJECT);
    CHECK_EQ(root->count, 8);

    const orb_json* name = orb_json_get(root, "name");
    CHECK(name && name->kind == ORB_JSON_STRING);
    CHECK(strcmp(name->str, "Hollow\n\"Lantern\"") == 0);

    const orb_json* size = orb_json_get(root, "size");
    CHECK(size && size->kind == ORB_JSON_ARRAY && size->count == 2);
    CHECK_EQ((int)size->first->num, 320);
    CHECK_EQ((int)size->first->next->num, 180);
    CHECK(size->first->next->next == nullptr);

    CHECK(orb_json_get(root, "on")->boolean == true);
    CHECK(orb_json_get(root, "off")->boolean == false);
    CHECK_EQ(orb_json_get(root, "nil")->kind, ORB_JSON_NULL);
    CHECK(orb_json_get(root, "neg")->num == -2.5);
    CHECK_EQ(orb_json_get(root, "empty")->count, 0);
    CHECK_EQ(orb_json_get(root, "list")->count, 0);
    CHECK(orb_json_get(root, "missing") == nullptr);

    CHECK(orb_json_parse(&a, "[1, 2", 5, &err) == nullptr);
    CHECK(strstr(err.text, "line 1") != nullptr);
    CHECK(orb_json_parse(&a, "{\"a\":1} x", 9, &err) == nullptr);

    // \u escapes decode to UTF-8: one byte, two, three, and a surrogate pair to four.
    const char* escaped = "\"\\u0041\\u00e9\\u20ac\\ud83d\\ude00\"";
    orb_json* u = orb_json_parse(&a, escaped, strlen(escaped), &err);
    CHECK(u != nullptr);
    CHECK(strcmp(u->str, "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80") == 0);
    CHECK(orb_json_parse(&a, "\"\\u12\"", 6, &err) == nullptr);
    CHECK(orb_json_parse(&a, "\"\\ud83d\"", 8, &err) == nullptr);

    return 0;
}
