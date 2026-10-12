#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

int main(void) {
    orb_arena out;
    static u8 mem[1 << 16];
    orb_arena_init(&out, "test", mem, sizeof mem);

    CHECK_EQ(
        sizeof(orb_json), 32
    ); // the LDtk document lives in cast scratch, a node per tile field

    orb_error err;
    const char* text =
        "{\"name\": \"Hollow\\n\\\"Lantern\\\"\", \"size\": [320, 180], \"on\": true,"
        " \"off\": false, \"nil\": null, \"neg\": -2.5, \"empty\": {}, \"list\": []}";
    orb_json* root = orb_json_parse(&out, text, strlen(text), &err);
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

    CHECK(orb_json_parse(&out, "[1, 2", 5, &err) == nullptr);
    CHECK(strstr(err.text, "line 1") != nullptr);
    CHECK(orb_json_parse(&out, "{\"a\":1} x", 9, &err) == nullptr);

    // \u escapes decode to UTF-8: one byte, two, three, and a surrogate pair to four.
    const char* escaped = "\"\\u0041\\u00e9\\u20ac\\ud83d\\ude00\"";
    orb_json* decoded = orb_json_parse(&out, escaped, strlen(escaped), &err);
    CHECK(decoded != nullptr);
    CHECK(strcmp(decoded->str, "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80") == 0);
    CHECK(orb_json_parse(&out, "\"\\u12\"", 6, &err) == nullptr);
    CHECK(orb_json_parse(&out, "\"\\ud83d\"", 8, &err) == nullptr);

    // no nan, inf, hex, overflow, leading dot or plus, or a sign or exponent without digits
    static const char* const bad_numbers[] = {
        "nan", "inf", "-inf", "-nan", "0x10", "1e999", "-1e999", ".5", "-", "1e", "+1",
    };

    for (usize i = 0; i < sizeof bad_numbers / sizeof bad_numbers[0]; i++)
        CHECK(orb_json_parse(&out, bad_numbers[i], strlen(bad_numbers[i]), &err) == nullptr);

    CHECK(strstr(err.text, "line 1") != nullptr);

    orb_json* minus_zero = orb_json_parse(&out, "-0", 2, &err);
    CHECK(minus_zero != nullptr && minus_zero->num == 0);
    orb_json* exponent = orb_json_parse(&out, "1.5e3", 5, &err);
    CHECK(exponent != nullptr && exponent->num == 1500);
    orb_json* in_array = orb_json_parse(&out, "[0, -1E+2, 0.25]", 16, &err);
    CHECK(in_array != nullptr && in_array->count == 3);

    // nesting past the depth limit fails instead of exhausting the stack
    char deep[300];
    memset(deep, '[', sizeof deep);
    CHECK(orb_json_parse(&out, deep, sizeof deep, &err) == nullptr);
    CHECK(strstr(err.text, "nested too deeply") != nullptr);

    return 0;
}
