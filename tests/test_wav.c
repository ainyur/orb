#include "test.h"
#define ORB_OS_HEADLESS 1
#include "../src/orb.c"

typedef struct {
    uint8_t bytes[4096];
    size_t len;
} wav_buffer;

static void put8(wav_buffer* buffer, uint8_t value) {
    buffer->bytes[buffer->len++] = value;
}

static void put16(wav_buffer* buffer, uint16_t value) {
    put8(buffer, (uint8_t)(value & 0xff));
    put8(buffer, (uint8_t)(value >> 8));
}

static void put32(wav_buffer* buffer, uint32_t value) {
    put16(buffer, (uint16_t)(value & 0xffff));
    put16(buffer, (uint16_t)(value >> 16));
}

static void put_tag(wav_buffer* buffer, const char* tag) {
    memcpy(buffer->bytes + buffer->len, tag, 4);
    buffer->len += 4;
}

static void put_header(wav_buffer* buffer) {
    buffer->len = 0;
    put_tag(buffer, "RIFF");
    put32(buffer, 0); // patched by finish
    put_tag(buffer, "WAVE");
}

// format 1 is PCM, 3 is float; extensible wraps either in a 40-byte fmt chunk
static void put_fmt(
    wav_buffer* buffer,
    uint16_t format,
    uint16_t channels,
    uint32_t rate,
    uint16_t bits,
    bool ext
) {
    put_tag(buffer, "fmt ");
    put32(buffer, ext ? 40 : 16);
    put16(buffer, ext ? 0xFFFE : format);
    put16(buffer, channels);
    put32(buffer, rate);
    put32(buffer, rate * channels * bits / 8);
    put16(buffer, (uint16_t)(channels * bits / 8));
    put16(buffer, bits);

    if (ext) {
        put16(buffer, 22);     // cbSize
        put16(buffer, bits);   // valid bits
        put32(buffer, 3);      // channel mask
        put16(buffer, format); // subformat GUID: data1 low word is the plain format tag
        put16(buffer, 0);
        put32(buffer, 0x00100000);
        put32(buffer, 0xAA000080);
        put32(buffer, 0x719B3800);
    }
}

static void put_data16(wav_buffer* buffer, const int16_t* samples, int n) {
    put_tag(buffer, "data");
    put32(buffer, (uint32_t)n * 2);

    for (int i = 0; i < n; i++)
        put16(buffer, (uint16_t)samples[i]);
}

static void put_data8(wav_buffer* buffer, const uint8_t* samples, int n) {
    put_tag(buffer, "data");
    put32(buffer, (uint32_t)n);

    for (int i = 0; i < n; i++)
        put8(buffer, samples[i]);
}

static void put_smpl(wav_buffer* buffer, uint32_t start, uint32_t end_inclusive) {
    put_tag(buffer, "smpl");
    put32(buffer, 60);

    for (int i = 0; i < 7; i++)
        put32(buffer, 0); // manufacturer .. smpte offset

    put32(buffer, 1); // loop count
    put32(buffer, 0); // sampler data
    put32(buffer, 0); // cue id
    put32(buffer, 0); // type: forward
    put32(buffer, start);
    put32(buffer, end_inclusive);
    put32(buffer, 0); // fraction
    put32(buffer, 0); // play count
}

static orb_span finish(wav_buffer* buffer) {
    uint32_t size = (uint32_t)(buffer->len - 8);

    buffer->bytes[4] = (uint8_t)(size & 0xff);
    buffer->bytes[5] = (uint8_t)(size >> 8 & 0xff);
    buffer->bytes[6] = (uint8_t)(size >> 16 & 0xff);
    buffer->bytes[7] = (uint8_t)(size >> 24);
    return (orb_span) {buffer->bytes, buffer->len};
}

int main(void) {
    static alignas(16) uint8_t mem[1 << 20];
    orb_arena arena;
    orb_arena_init(&arena, "test", mem, sizeof mem);
    wav_buffer buffer;
    orb_wav wav;
    orb_error err;
    int16_t* pcm = orb_arena_push_array(&arena, int16_t, 96000 * 2); // room for loop.wav

    // 16-bit mono
    static const int16_t mono[4] = {0, 1000, -1000, 32767};
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 22050, 16, false);
    put_data16(&buffer, mono, 4);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.channels, 1);
    CHECK_EQ(wav.rate, 22050);
    CHECK_EQ(wav.bits, 16);
    CHECK_EQ(wav.count, 4);
    orb_wav_decode(&wav, pcm);
    CHECK_EQ(pcm[1], 1000);
    CHECK_EQ(pcm[2], -1000);
    CHECK_EQ(pcm[3], 32767);
    CHECK(!wav.has_loop);

    // 8-bit unsigned is widened: 128 is silence, 255 is near full scale
    static const uint8_t eight[3] = {128, 255, 0};
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 8000, 8, false);
    put_data8(&buffer, eight, 3);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.count, 3);
    CHECK_EQ(wav.bits, 8);
    orb_wav_decode(&wav, pcm);
    CHECK_EQ(pcm[0], 0);
    CHECK_EQ(pcm[1], 32512);
    CHECK_EQ(pcm[2], -32768);

    // stereo, interleaved
    static const int16_t stereo[4] = {10, -10, 20, -20};
    put_header(&buffer);
    put_fmt(&buffer, 1, 2, 48000, 16, false);
    put_data16(&buffer, stereo, 4);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.channels, 2);
    CHECK_EQ(wav.count, 2);
    orb_wav_decode(&wav, pcm);
    CHECK_EQ(pcm[3], -20);

    // extensible PCM is accepted, extensible float is not
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 44100, 16, true);
    put_data16(&buffer, mono, 4);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.rate, 44100);
    put_header(&buffer);
    put_fmt(&buffer, 3, 1, 44100, 16, true);
    put_data16(&buffer, mono, 4);
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "float") != nullptr);

    // a smpl loop: end is inclusive in the file, exclusive here, clamped to the data
    static const int16_t ten[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_smpl(&buffer, 2, 7); // before data, as some editors write it
    put_data16(&buffer, ten, 10);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(wav.has_loop);
    CHECK_EQ(wav.loop_start, 2);
    CHECK_EQ(wav.loop_end, 8);
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_data16(&buffer, ten, 10);
    put_smpl(&buffer, 4, 500);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.loop_end, 10);

    // a loop whose start is not before its end, after clamping, still parses: the cast
    // is what drops it
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_smpl(&buffer, 5, 4); // end_inclusive 4 -> loop_end 5, so start == end
    put_data16(&buffer, ten, 10);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(wav.has_loop);
    CHECK_EQ(wav.loop_start, 5);
    CHECK_EQ(wav.loop_end, 5);

    // the raw inclusive end 0xFFFFFFFF (loop to the last frame) must not wrap to 0
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_smpl(&buffer, 0, 0xFFFFFFFF);
    put_data16(&buffer, ten, 10);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.loop_end, 10);

    // an unknown chunk of odd size is padded and skipped
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_tag(&buffer, "LIST");
    put32(&buffer, 3);
    put8(&buffer, 'a');
    put8(&buffer, 'b');
    put8(&buffer, 'c');
    put8(&buffer, 0); // pad
    put_data16(&buffer, mono, 4);
    CHECK(orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK_EQ(wav.count, 4);

    // rejected by name
    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 24, false);
    put_data16(&buffer, mono, 4);
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "24-bit") != nullptr);

    put_header(&buffer);
    put_fmt(&buffer, 1, 3, 48000, 16, false);
    put_data16(&buffer, mono, 4);
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "channels") != nullptr);

    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 0, 16, false);
    put_data16(&buffer, mono, 4);
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "rate") != nullptr);

    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "data") != nullptr);

    put_header(&buffer);
    put_fmt(&buffer, 1, 1, 48000, 16, false);
    put_data16(&buffer, mono, 4);
    buffer.bytes[buffer.len - 10] = 200; // data chunk claims more bytes than follow
    CHECK(!orb_wav_parse(finish(&buffer), &wav, &err));
    CHECK(strstr(err.text, "past the end") != nullptr);

    CHECK(!orb_wav_parse((orb_span) {(const uint8_t*)"not a wav file", 14}, &wav, &err));
    CHECK(strstr(err.text, "RIFF") != nullptr);

    // the generated fixtures, as make fixtures writes them
    orb_span file;
    CHECK(orb_os_read_file("tests/fixtures/sfx/beep.wav", &arena, &file));
    CHECK(orb_wav_parse(file, &wav, &err));
    CHECK_EQ(wav.channels, 1);
    CHECK_EQ(wav.rate, 48000);
    CHECK_EQ(wav.count, 4800);
    orb_wav_decode(&wav, pcm);
    CHECK_EQ(pcm[0], -12000);
    CHECK(!wav.has_loop);

    CHECK(orb_os_read_file("tests/fixtures/music/loop.wav", &arena, &file));
    CHECK(orb_wav_parse(file, &wav, &err));
    CHECK_EQ(wav.channels, 2);
    CHECK_EQ(wav.count, 96000);
    CHECK(wav.has_loop);
    CHECK_EQ(wav.loop_start, 0);
    CHECK_EQ(wav.loop_end, 96000);
    orb_wav_decode(&wav, pcm);
    CHECK_EQ(pcm[0], 0); // faded in: the seam is silent
    CHECK(pcm[2 * 6000] != 0);

    return 0;
}
