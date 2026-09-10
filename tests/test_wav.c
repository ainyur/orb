#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

typedef struct {
    uint8_t bytes[4096];
    size_t len;
} wav_buffer;

static void put8(wav_buffer* b, uint8_t v) {
    b->bytes[b->len++] = v;
}

static void put16(wav_buffer* b, uint16_t v) {
    put8(b, (uint8_t)(v & 0xff));
    put8(b, (uint8_t)(v >> 8));
}

static void put32(wav_buffer* b, uint32_t v) {
    put16(b, (uint16_t)(v & 0xffff));
    put16(b, (uint16_t)(v >> 16));
}

static void put_tag(wav_buffer* b, const char* tag) {
    memcpy(b->bytes + b->len, tag, 4);
    b->len += 4;
}

static void put_header(wav_buffer* b) {
    b->len = 0;
    put_tag(b, "RIFF");
    put32(b, 0); // patched by finish
    put_tag(b, "WAVE");
}

// format 1 is PCM, 3 is float; extensible wraps either in a 40-byte fmt chunk
static void
put_fmt(wav_buffer* b, uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits, bool ext) {
    put_tag(b, "fmt ");
    put32(b, ext ? 40 : 16);
    put16(b, ext ? 0xFFFE : format);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * bits / 8);
    put16(b, (uint16_t)(channels * bits / 8));
    put16(b, bits);

    if (ext) {
        put16(b, 22);     // cbSize
        put16(b, bits);   // valid bits
        put32(b, 3);      // channel mask
        put16(b, format); // subformat GUID: data1 low word is the plain format tag
        put16(b, 0);
        put32(b, 0x00100000);
        put32(b, 0xAA000080);
        put32(b, 0x719B3800);
    }
}

static void put_data16(wav_buffer* b, const int16_t* samples, int n) {
    put_tag(b, "data");
    put32(b, (uint32_t)n * 2);

    for (int i = 0; i < n; i++)
        put16(b, (uint16_t)samples[i]);
}

static void put_data8(wav_buffer* b, const uint8_t* samples, int n) {
    put_tag(b, "data");
    put32(b, (uint32_t)n);

    for (int i = 0; i < n; i++)
        put8(b, samples[i]);
}

static void put_smpl(wav_buffer* b, uint32_t start, uint32_t end_inclusive) {
    put_tag(b, "smpl");
    put32(b, 60);

    for (int i = 0; i < 7; i++)
        put32(b, 0); // manufacturer .. smpte offset

    put32(b, 1); // loop count
    put32(b, 0); // sampler data
    put32(b, 0); // cue id
    put32(b, 0); // type: forward
    put32(b, start);
    put32(b, end_inclusive);
    put32(b, 0); // fraction
    put32(b, 0); // play count
}

static orb_span finish(wav_buffer* b) {
    uint32_t size = (uint32_t)(b->len - 8);

    b->bytes[4] = (uint8_t)(size & 0xff);
    b->bytes[5] = (uint8_t)(size >> 8 & 0xff);
    b->bytes[6] = (uint8_t)(size >> 16 & 0xff);
    b->bytes[7] = (uint8_t)(size >> 24);
    return (orb_span) {b->bytes, b->len};
}

int main(void) {
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena a;
    orb_arena_init(&a, "test", mem, sizeof mem);
    wav_buffer b;
    orb_wav w;
    orb_error err;
    int16_t* pcm = orb_arena_push_array(&a, int16_t, 96000 * 2); // room for loop.wav

    // 16-bit mono
    static const int16_t mono[4] = {0, 1000, -1000, 32767};
    put_header(&b);
    put_fmt(&b, 1, 1, 22050, 16, false);
    put_data16(&b, mono, 4);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.channels, 1);
    CHECK_EQ(w.rate, 22050);
    CHECK_EQ(w.bits, 16);
    CHECK_EQ(w.count, 4);
    orb_wav_decode(&w, pcm);
    CHECK_EQ(pcm[1], 1000);
    CHECK_EQ(pcm[2], -1000);
    CHECK_EQ(pcm[3], 32767);
    CHECK(!w.has_loop);

    // 8-bit unsigned is widened: 128 is silence, 255 is near full scale
    static const uint8_t eight[3] = {128, 255, 0};
    put_header(&b);
    put_fmt(&b, 1, 1, 8000, 8, false);
    put_data8(&b, eight, 3);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.count, 3);
    CHECK_EQ(w.bits, 8);
    orb_wav_decode(&w, pcm);
    CHECK_EQ(pcm[0], 0);
    CHECK_EQ(pcm[1], 32512);
    CHECK_EQ(pcm[2], -32768);

    // stereo, interleaved
    static const int16_t stereo[4] = {10, -10, 20, -20};
    put_header(&b);
    put_fmt(&b, 1, 2, 48000, 16, false);
    put_data16(&b, stereo, 4);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.channels, 2);
    CHECK_EQ(w.count, 2);
    orb_wav_decode(&w, pcm);
    CHECK_EQ(pcm[3], -20);

    // extensible PCM is accepted, extensible float is not
    put_header(&b);
    put_fmt(&b, 1, 1, 44100, 16, true);
    put_data16(&b, mono, 4);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.rate, 44100);
    put_header(&b);
    put_fmt(&b, 3, 1, 44100, 16, true);
    put_data16(&b, mono, 4);
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "float") != nullptr);

    // a smpl loop: end is inclusive in the file, exclusive here, clamped to the data
    static const int16_t ten[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 16, false);
    put_smpl(&b, 2, 7); // before data, as some editors write it
    put_data16(&b, ten, 10);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK(w.has_loop);
    CHECK_EQ(w.loop_start, 2);
    CHECK_EQ(w.loop_end, 8);
    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 16, false);
    put_data16(&b, ten, 10);
    put_smpl(&b, 4, 500);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.loop_end, 10);

    // an unknown chunk of odd size is padded and skipped
    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 16, false);
    put_tag(&b, "LIST");
    put32(&b, 3);
    put8(&b, 'a');
    put8(&b, 'b');
    put8(&b, 'c');
    put8(&b, 0); // pad
    put_data16(&b, mono, 4);
    CHECK(orb_wav_parse(finish(&b), &w, &err));
    CHECK_EQ(w.count, 4);

    // rejected by name
    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 24, false);
    put_data16(&b, mono, 4);
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "24-bit") != nullptr);

    put_header(&b);
    put_fmt(&b, 1, 3, 48000, 16, false);
    put_data16(&b, mono, 4);
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "channels") != nullptr);

    put_header(&b);
    put_fmt(&b, 1, 1, 0, 16, false);
    put_data16(&b, mono, 4);
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "rate") != nullptr);

    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 16, false);
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "data") != nullptr);

    put_header(&b);
    put_fmt(&b, 1, 1, 48000, 16, false);
    put_data16(&b, mono, 4);
    b.bytes[b.len - 10] = 200; // data chunk claims more bytes than follow
    CHECK(!orb_wav_parse(finish(&b), &w, &err));
    CHECK(strstr(err.text, "past the end") != nullptr);

    CHECK(!orb_wav_parse((orb_span) {(const uint8_t*)"not a wav file", 14}, &w, &err));
    CHECK(strstr(err.text, "RIFF") != nullptr);

    // the generated fixtures, as make fixtures writes them
    orb_span file;
    CHECK(orb_os_read_file("tests/fixtures/beep.wav", &a, &file));
    CHECK(orb_wav_parse(file, &w, &err));
    CHECK_EQ(w.channels, 1);
    CHECK_EQ(w.rate, 48000);
    CHECK_EQ(w.count, 4800);
    orb_wav_decode(&w, pcm);
    CHECK_EQ(pcm[0], -12000);
    CHECK(!w.has_loop);

    CHECK(orb_os_read_file("tests/fixtures/loop.wav", &a, &file));
    CHECK(orb_wav_parse(file, &w, &err));
    CHECK_EQ(w.channels, 2);
    CHECK_EQ(w.count, 96000);
    CHECK(w.has_loop);
    CHECK_EQ(w.loop_start, 0);
    CHECK_EQ(w.loop_end, 96000);
    orb_wav_decode(&w, pcm);
    CHECK_EQ(pcm[0], 0); // faded in: the seam is silent
    CHECK(pcm[2 * 6000] != 0);

    return 0;
}
