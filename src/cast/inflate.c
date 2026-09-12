#include "inflate.h"

#include <string.h>

typedef struct {
    const uint8_t* in;
    size_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t* out;
    size_t outlen, outpos;
} inflate_state;

typedef struct {
    uint16_t count[16];   // number of codes of each length
    uint16_t symbol[288]; // symbols ordered by code
} inflate_huffman;

// Read n bits, least significant first. -1 at end of input.
static int inflate_bits(inflate_state* s, int n) {
    while (s->bitcnt < n) {
        if (s->inpos >= s->inlen) return -1;
        s->bitbuf |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }

    int v = (int)(s->bitbuf & ((1u << n) - 1));

    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static void inflate_build(inflate_huffman* h, const uint8_t* lengths, int n) {
    uint16_t offset[16];

    memset(h->count, 0, sizeof h->count);

    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;

    h->count[0] = 0;
    offset[1] = 0;

    for (int len = 1; len < 15; len++)
        offset[len + 1] = offset[len] + h->count[len];

    for (int i = 0; i < n; i++) {
        if (lengths[i]) h->symbol[offset[lengths[i]]++] = (uint16_t)i;
    }
}

static int inflate_decode(inflate_state* s, const inflate_huffman* h) {
    int code = 0, first = 0, index = 0;

    for (int len = 1; len < 16; len++) {
        int bit = inflate_bits(s, 1);

        if (bit < 0) return -1;

        code |= bit;

        int count = h->count[len];

        if (code - count < first) return h->symbol[index + (code - first)];

        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return -1;
}

static int inflate_codes(
    inflate_state* s,
    const inflate_huffman* lencode,
    const inflate_huffman* distcode
) {
    static const uint16_t lbase[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                       15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                       67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1,    2,    3,    4,     5,     7,    9,    13,
                                       17,   25,   33,   49,    65,    97,   129,  193,
                                       257,  385,  513,  769,   1025,  1537, 2049, 3073,
                                       4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    for (;;) {
        int sym = inflate_decode(s, lencode);

        if (sym < 0) return -1;

        if (sym < 256) {
            if (s->outpos >= s->outlen) return -1;
            s->out[s->outpos++] = (uint8_t)sym;
            continue;
        }

        if (sym == 256) return 0;

        sym -= 257;

        if (sym >= 29) return -1;

        int extra = inflate_bits(s, lext[sym]);

        if (extra < 0) return -1;

        size_t len = lbase[sym] + (size_t)extra;
        int dsym = inflate_decode(s, distcode);

        if (dsym < 0 || dsym >= 30) return -1;

        extra = inflate_bits(s, dext[dsym]);

        if (extra < 0) return -1;

        size_t dist = dbase[dsym] + (size_t)extra;

        if (dist > s->outpos || s->outpos + len > s->outlen) return -1;

        for (size_t i = 0; i < len; i++, s->outpos++)
            s->out[s->outpos] = s->out[s->outpos - dist];
    }
}

static int inflate_stored(inflate_state* s) {
    s->bitbuf = 0; // discard the partial byte; inflate_bits never buffers a whole one
    s->bitcnt = 0;

    if (s->inpos + 4 > s->inlen) return -1;

    unsigned len = s->in[s->inpos] | (unsigned)s->in[s->inpos + 1] << 8;
    unsigned nlen = s->in[s->inpos + 2] | (unsigned)s->in[s->inpos + 3] << 8;

    s->inpos += 4;

    if (len != (~nlen & 0xffffu)) return -1;
    if (s->inpos + len > s->inlen || s->outpos + len > s->outlen) return -1;

    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static int inflate_fixed(inflate_state* s) {
    inflate_huffman lencode, distcode;
    uint8_t lengths[288];
    int i = 0;

    for (; i < 144; i++)
        lengths[i] = 8;

    for (; i < 256; i++)
        lengths[i] = 9;

    for (; i < 280; i++)
        lengths[i] = 7;

    for (; i < 288; i++)
        lengths[i] = 8;

    inflate_build(&lencode, lengths, 288);

    for (i = 0; i < 30; i++)
        lengths[i] = 5;

    inflate_build(&distcode, lengths, 30);
    return inflate_codes(s, &lencode, &distcode);
}

static int inflate_dynamic(inflate_state* s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    inflate_huffman lencode, distcode;
    uint8_t lengths[320];
    int nlen = inflate_bits(s, 5) + 257;
    int ndist = inflate_bits(s, 5) + 1;
    int ncode = inflate_bits(s, 4) + 4;

    if (nlen > 286 || ndist > 30 || s->inpos > s->inlen) return -1;

    memset(lengths, 0, 19);

    for (int i = 0; i < ncode; i++) {
        int v = inflate_bits(s, 3);

        if (v < 0) return -1;

        lengths[order[i]] = (uint8_t)v;
    }

    inflate_build(&lencode, lengths, 19);

    int index = 0;

    while (index < nlen + ndist) {
        int sym = inflate_decode(s, &lencode);

        if (sym < 0) return -1;

        if (sym < 16) {
            lengths[index++] = (uint8_t)sym;
            continue;
        }

        int repeat, value = 0;

        if (sym == 16) {
            if (index == 0) return -1;
            value = lengths[index - 1];
            repeat = 3 + inflate_bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + inflate_bits(s, 3);
        } else {
            repeat = 11 + inflate_bits(s, 7);
        }

        if (repeat < 3 || index + repeat > nlen + ndist) return -1;

        while (repeat--)
            lengths[index++] = (uint8_t)value;
    }

    inflate_build(&lencode, lengths, nlen);
    inflate_build(&distcode, lengths + nlen, ndist);
    return inflate_codes(s, &lencode, &distcode);
}

ptrdiff_t orb_inflate(const uint8_t* in, size_t inlen, uint8_t* out, size_t outlen) {
    if (inlen < 6 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0) return -1;

    inflate_state s = {.in = in, .inlen = inlen - 4, .inpos = 2, .out = out, .outlen = outlen};
    int last;

    do {
        last = inflate_bits(&s, 1);
        int type = inflate_bits(&s, 2);

        if (last < 0 || type < 0) return -1;

        int err;

        if (type == 0)
            err = inflate_stored(&s);
        else if (type == 1)
            err = inflate_fixed(&s);
        else if (type == 2)
            err = inflate_dynamic(&s);
        else
            return -1;

        if (err) return -1;
    } while (!last);

    return (ptrdiff_t)s.outpos;
}
