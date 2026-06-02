#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgrapheme.h"

static void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static size_t skipspaces(const unsigned char *buf, size_t i) {
    while (isspace(buf[i])) {
        ++i;
    }
    return i;
}

static size_t encode_utf8(uint8_t *dst, unsigned int cp) {
    if (cp < 0x80) {
        dst[0] = (uint8_t) cp;
        return 1;
    }
    if (cp < 0x800) {
        dst[0] = (uint8_t) (0xC0 + (cp >> 6));
        dst[1] = (uint8_t) (0x80 + (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = (uint8_t) (0xE0 + (cp >> 12));
        dst[1] = (uint8_t) (0x80 + ((cp >> 6) & 0x3F));
        dst[2] = (uint8_t) (0x80 + (cp & 0x3F));
        return 3;
    }
    dst[0] = (uint8_t) (0xF0 + (cp >> 18));
    dst[1] = (uint8_t) (0x80 + ((cp >> 12) & 0x3F));
    dst[2] = (uint8_t) (0x80 + ((cp >> 6) & 0x3F));
    dst[3] = (uint8_t) (0x80 + (cp & 0x3F));
    return 4;
}

static void checkline(const unsigned char *line) {
    uint8_t expected[1024];
    uint8_t actual[1024];
    uint8_t text[1024];
    size_t bi = 0;
    size_t ei = 0;
    size_t ai = 0;
    size_t ti = 0;
    wgrapheme_iter_t iter;
    size_t start;
    size_t end;
    wgrapheme_status_t status;

    while (line[bi]) {
        unsigned int cp;
        int consumed;

        bi = skipspaces(line, bi);
        /* ÷ can break */
        if (line[bi] == 0xC3 && line[bi + 1] == 0xB7) {
            expected[ei++] = '/';
            bi += 2;
            continue;
        }
        /* × can't break */
        if (line[bi] == 0xC3 && line[bi + 1] == 0x97) {
            bi += 2;
            continue;
        }
        if (line[bi] == '#' || line[bi] == '\0') {
            break;
        }

        if (sscanf((const char *) (line + bi), "%x%n", &cp, &consumed) != 1) {
            fail("invalid hex input in GraphemeBreakTest");
        }
        ei += encode_utf8(expected + ei, cp);
        ti += encode_utf8(text + ti, cp);
        bi += (size_t) consumed;
    }

    if (ei && expected[ei - 1] == '/') {
        --ei;
    }
    expected[ei] = 0;
    text[ti] = 0;

    wgrapheme_iter_init(&iter, text, ti);
    while ((status = wgrapheme_iter_next(&iter, &start, &end)) == WGRAPHEME_OK) {
        size_t i;
        actual[ai++] = '/';
        for (i = start; i < end; ++i) {
            actual[ai++] = text[i];
        }
    }

    if (status != WGRAPHEME_DONE) {
        fail("wgrapheme iterator failed");
    }
    actual[ai] = 0;
    if (strcmp((const char *) expected, (const char *) actual) != 0) {
        fail("grapheme segmentation mismatch");
    }
}

int main(int argc, char **argv) {
    unsigned char line[8192];
    FILE *file;

    if (argc != 2) {
        fail("usage: wgraphemetest <GraphemeBreakTest.txt>");
    }

    file = fopen(argv[1], "rb");
    if (!file) {
        fail("could not open GraphemeBreakTest.txt");
    }

    while (fgets((char *) line, (int) sizeof(line), file) != NULL) {
        if (line[0] == '#') {
            continue;
        }
        checkline(line);
    }

    fclose(file);
    return 0;
}
