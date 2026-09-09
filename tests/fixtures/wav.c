// build/wav beep|loop|song out.wav. Writes the audio fixtures and the demo's
// sounds with integer synthesis only, so every host produces the same bytes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RATE 48000
#define BEEP_FRAMES 4800
#define LOOP_FRAMES 96000
#define NOTE_FRAMES 12000
#define FADE_FRAMES 600
#define SLOT_FRAMES 12000
#define BAR_FRAMES (SLOT_FRAMES * 8)

static void put16(FILE* f, uint16_t v) {
    fputc(v & 0xff, f);
    fputc(v >> 8, f);
}

static void put32(FILE* f, uint32_t v) {
    put16(f, (uint16_t)(v & 0xffff));
    put16(f, (uint16_t)(v >> 16));
}

static void put_tag(FILE* f, const char* tag) {
    fwrite(tag, 1, 4, f);
}

// RIFF, fmt, an optional smpl loop from loop_start to the last frame, then the
// data chunk header.
static void header(FILE* f, uint16_t channels, uint32_t frames, bool loop, uint32_t loop_start) {
    uint32_t data = frames * channels * 2;

    put_tag(f, "RIFF");
    put32(f, 4 + 24 + (loop ? 68 : 0) + 8 + data);
    put_tag(f, "WAVE");
    put_tag(f, "fmt ");
    put32(f, 16);
    put16(f, 1);
    put16(f, channels);
    put32(f, RATE);
    put32(f, RATE * channels * 2);
    put16(f, (uint16_t)(channels * 2));
    put16(f, 16);

    if (loop) {
        put_tag(f, "smpl");
        put32(f, 60);

        for (int i = 0; i < 7; i++)
            put32(f, 0); // manufacturer .. smpte offset

        put32(f, 1); // one loop
        put32(f, 0); // sampler data
        put32(f, 0); // cue id
        put32(f, 0); // forward
        put32(f, loop_start);
        put32(f, frames - 1);
        put32(f, 0); // fraction
        put32(f, 0); // play count
    }

    put_tag(f, "data");
    put32(f, data);
}

// A 440 Hz square wave (half period 55 frames) decaying to silence.
static void beep(FILE* f) {
    header(f, 1, BEEP_FRAMES, false, 0);

    for (int i = 0; i < BEEP_FRAMES; i++) {
        int amp = 12000 * (BEEP_FRAMES - i) / BEEP_FRAMES;

        put16(f, (uint16_t)(int16_t)((i / 55) & 1 ? amp : -amp));
    }
}

// The fade gain, 0..FADE_FRAMES, at frame `at` of a note `length` frames long.
static int envelope(int at, int length) {
    return at < FADE_FRAMES ? at : length - at < FADE_FRAMES ? length - at : FADE_FRAMES;
}

static int square(int phase, int period, int amp) {
    return phase < period / 2 ? amp : -amp;
}

static int triangle(int phase, int period, int amp) {
    int half = period / 2;

    return phase < half ? phase * 2 * amp / half - amp : amp - (phase - half) * 2 * amp / half;
}

// Eight notes, C4 E4 G4 C5 G4 E4 C4 E4 as periods in frames, the right channel
// an octave down, every note faded at both ends so the loop seam is silent.
static void loop(FILE* f) {
    static const int periods[8] = {183, 146, 122, 92, 122, 146, 183, 146};

    header(f, 2, LOOP_FRAMES, true, 0);

    for (int i = 0; i < LOOP_FRAMES; i++) {
        int note = i / NOTE_FRAMES, at = i % NOTE_FRAMES, period = periods[note];
        int env = envelope(at, NOTE_FRAMES);
        int left = triangle(at % period, period, 6000) * env / FADE_FRAMES;
        int right = triangle(at % (period * 2), period * 2, 6000) * env / FADE_FRAMES;

        put16(f, (uint16_t)(int16_t)left);
        put16(f, (uint16_t)(int16_t)right);
    }
}

#define HOLD (-1)
#define SONG_BARS 16
#define SONG_LOOP_BAR 4

static const int song_melody[SONG_BARS * 8] = {
    // verse A by itself
    146,
    122,
    109,
    122,
    146,
    163,
    183,
    0, // E4 G4 A4 G4 E4 D4 C4 .
    109,
    92,
    82,
    92,
    109,
    122,
    146,
    0, // A4 C5 D5 C5 A4 G4 E4 .
    92,
    109,
    122,
    109,
    92,
    82,
    73,
    82, // C5 A4 G4 A4 C5 D5 E5 D5
    82,
    122,
    109,
    82,
    92,
    109,
    122,
    0, // D5 G4 A4 D5 C5 A4 G4 .
    // 1st verse with the bass
    146,
    122,
    109,
    122,
    146,
    163,
    183,
    0,
    109,
    92,
    82,
    92,
    109,
    122,
    146,
    0,
    92,
    109,
    122,
    109,
    92,
    82,
    73,
    82,
    82,
    122,
    109,
    82,
    92,
    109,
    122,
    0,
    // bridge with longer, higher climbing notes
    109,
    HOLD,
    92,
    HOLD,
    73,
    HOLD,
    82,
    HOLD, // A4 C5 E5 D5
    92,
    HOLD,
    109,
    HOLD,
    122,
    HOLD,
    109,
    HOLD, // C5 A4 G4 A4
    109,
    HOLD,
    92,
    HOLD,
    73,
    HOLD,
    61,
    HOLD, // A4 C5 E5 G5
    82,
    HOLD,
    73,
    HOLD,
    82,
    HOLD,
    0,
    0, // D5 E5 D5 .
    // 2nd verse settles on C
    122,
    146,
    163,
    146,
    122,
    109,
    92,
    HOLD, // G4 E4 D4 E4 G4 A4 C5 ~
    109,
    122,
    146,
    163,
    146,
    HOLD,
    183,
    HOLD, // A4 G4 E4 D4 E4 ~ C4 ~
    92,
    HOLD,
    109,
    122,
    109,
    92,
    82,
    HOLD, // C5 ~ A4 G4 A4 C5 D5 ~
    82,
    92,
    109,
    122,
    146,
    163,
    183,
    HOLD, // D5 C5 A4 G4 E4 D4 C4 ~
};

// Root and fifth per half bar: C Am F G for the verses, Am F Am G for the bridge.
static const int song_bass[SONG_BARS * 2] = {
    367, 245, 436, 291, 275, 183, 245, 327, // intro: unused, the bass is silent
    367, 245, 436, 291, 275, 183, 245, 327, // C3 G3 A2 E3 F3 C4 G3 D3
    436, 291, 275, 183, 436, 291, 245, 327, // A2 E3 F3 C4 A2 E3 G3 D3
    367, 245, 436, 291, 275, 183, 245, 327, // C3 G3 A2 E3 F3 C4 G3 D3
};

static void song(FILE* f) {
    int frames = SONG_BARS * BAR_FRAMES;

    header(f, 2, (uint32_t)frames, true, SONG_LOOP_BAR * BAR_FRAMES);

    int note_start = 0, note_length = 0, period = 0;

    for (int i = 0; i < frames; i++) {
        int slot = i / SLOT_FRAMES;

        if (i % SLOT_FRAMES == 0 && song_melody[slot] != HOLD) {
            note_start = i;
            note_length = SLOT_FRAMES;
            period = song_melody[slot];

            for (int s = slot + 1; s < SONG_BARS * 8 && song_melody[s] == HOLD; s++)
                note_length += SLOT_FRAMES;
        }

        int at = i - note_start;
        int half_at = i % (BAR_FRAMES / 2), low = song_bass[i / (BAR_FRAMES / 2)];
        int lead =
            period ? triangle(at % period, period, 3500) * envelope(at, note_length) / FADE_FRAMES
                   : 0;
        int root = i < SONG_LOOP_BAR * BAR_FRAMES
                       ? 0
                       : square(half_at % low, low, 1800) * envelope(half_at, BAR_FRAMES / 2) /
                             FADE_FRAMES;

        put16(f, (uint16_t)(int16_t)(lead + root / 2));
        put16(f, (uint16_t)(int16_t)(lead / 2 + root));
    }
}

int main(int argc, char** argv) {
    bool known = argc == 3 && (strcmp(argv[1], "beep") == 0 || strcmp(argv[1], "loop") == 0 ||
                               strcmp(argv[1], "song") == 0);

    if (!known) {
        fprintf(stderr, "usage: wav beep|loop|song out.wav\n");
        return 2;
    }

    FILE* f = fopen(argv[2], "wb");

    if (!f) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "beep") == 0)
        beep(f);
    else if (strcmp(argv[1], "loop") == 0)
        loop(f);
    else
        song(f);

    return fclose(f) == 0 ? 0 : 1;
}
