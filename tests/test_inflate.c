#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

static size_t unhex(const char* hex, uint8_t* out) {
    size_t n = 0;

    for (; hex[0] && hex[1]; hex += 2, n++) {
        unsigned v;

        sscanf(hex, "%2x", &v);
        out[n] = (uint8_t)v;
    }

    return n;
}

int main(void) {
    uint8_t in[512], out[512];
    size_t n;

    // stored block: zlib.compress(b"abc", 0)
    n = unhex("7801010300fcff616263024d0127", in);
    CHECK_EQ(orb_inflate(in, n, out, sizeof out), 3);
    CHECK(memcmp(out, "abc", 3) == 0);

    // fixed huffman: zlib.compress(b"hello hello hello hello", 9)
    n = unhex("78dacb48cdc9c957c8402701680308b1", in);
    CHECK_EQ(orb_inflate(in, n, out, sizeof out), 23);
    CHECK(memcmp(out, "hello hello hello hello", 23) == 0);

    // dynamic huffman: 200 bytes of skewed random text, expected prefix checked below
    n = unhex(
        "78da0dcecb0184200c05c056682dcac344091112f153fdee69ae13770221271e4bfb8bc65bc2ced6"
        "a940002c17da41dcb152e2ac2474501a06d7057176c327fda95c6798955cbd60d0e73ea3e97e7c0d"
        "3c2b62f5963b06ec794bf125a212e252f22ab70c9bba75b4afc6aac418edd6a89ec505d7f5be3236"
        "d0195a024a38fe27464a468637c410416b4cfb01c506520b",
        in
    );
    CHECK_EQ(orb_inflate(in, n, out, sizeof out), 200);
    CHECK(
        memcmp(out, "tw eaed hrbnaedenhg ejhoqafeieeebuenkahqeca hdmaiaka roesmbetpqoezi", 66) == 0
    );

    // output too small
    CHECK_EQ(orb_inflate(in, n, out, 100), -1);
    // not zlib
    CHECK_EQ(orb_inflate((const uint8_t*)"nope!!", 6, out, sizeof out), -1);
    return 0;
}
