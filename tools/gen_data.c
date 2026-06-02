#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wgrapheme_categories.h>

/* macros are lazily expanded so in order to expand __LINE__ we need a second macro call */
#define _wstatic_cat_(a, b) a##b
#define _wstatic_cat(a, b) _wstatic_cat_(a, b)
#define _wstatic_assert(cond, msg) typedef char _wstatic_cat(_assert_, __LINE__)[(cond) ? 1 : -1];

#define arrlen(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Last codepoint defined by unicode standard is U+10FFFF */
#define UNICODE_CODEPOINT_COUNT (0x10FFFF + 1)

/* The lookup accelerator covers only the common portion of Unicode and falls
 * back to a final binary-search slice for the rest.
 * Ranges are concentrated below this cutoff, so a small
 * index can cheaply narrow the search interval for the common case.
 *
 * 0x20000 Supplementary Ideographic Plane
 */
#define LOOKUP_CUTOFF 0x20000

/* Number of entries in the lookup accelerator. This must divide LOOKUP_CUTOFF
 * so that each slot covers a fixed-width code point interval.
 *
 * 0x400 entries over 0x20000 code points yields 0x80-code-point buckets.
 * That is small enough to keep the accelerator tiny and large enough to narrow
 * the binary search significantly, so worst case is 4 binary searches. */
#define LOOKUP_TABLE_LEN 0x400

_wstatic_assert(LOOKUP_CUTOFF % LOOKUP_TABLE_LEN == 0,
                "#define LOOKUP_CUTOFF must be a multiple of LOOKUP_TABLE_LEN for equal intervals");

/* each lookup entry covers 128 codepoints */
#define LOOKUP_INTERVAL (LOOKUP_CUTOFF / LOOKUP_TABLE_LEN)

typedef struct {
    const char *str;
    size_t len;
} WStrView;

#define WSTRV(s) ((WStrView) {s, sizeof(s) - 1})
#define WSTRVC(s) {s, sizeof(s) - 1}

int wstrv_equal(WStrView a, WStrView b) {
    if (a.len != b.len)
        return 0;
    return memcmp(a.str, b.str, a.len) == 0;
}

void fail(const char *msg) {
    puts(msg);
    exit(EXIT_FAILURE);
}

typedef struct {
    const WStrView name;
    uint8_t cat;
} WUnicodeNameToCat;


const WUnicodeNameToCat unicode_cat_to_wgrapheme_cat[] = {
        {WSTRVC("CR"), WGRAPHEME_CAT_CR},
        {WSTRVC("LF"), WGRAPHEME_CAT_LF},
        {WSTRVC("Control"), WGRAPHEME_CAT_CONTROL},
        {WSTRVC("Extend"), WGRAPHEME_CAT_EXTEND},
        {WSTRVC("L"), WGRAPHEME_CAT_L},
        {WSTRVC("V"), WGRAPHEME_CAT_V},
        {WSTRVC("T"), WGRAPHEME_CAT_T},
        {WSTRVC("LV"), WGRAPHEME_CAT_LV},
        {WSTRVC("LVT"), WGRAPHEME_CAT_LVT},
        {WSTRVC("Regional_Indicator"), WGRAPHEME_CAT_REGIONAL_INDICATOR},
        {WSTRVC("SpacingMark"), WGRAPHEME_CAT_SPACINGMARK},
        {WSTRVC("Prepend"), WGRAPHEME_CAT_PREPEND},
        {WSTRVC("ZWJ"), WGRAPHEME_CAT_ZWJ},
        {WSTRVC("Extended_Pictographic"), WGRAPHEME_CAT_EXTENDED_PICTOGRAPHIC},
};


const WUnicodeNameToCat unicode_incb_to_wgrapheme_incb[] = {{WSTRVC("None"), WGRAPHEME_INCB_NONE},
                                                            {WSTRVC("Linker"), WGRAPHEME_INCB_LINKER},
                                                            {WSTRVC("Consonant"), WGRAPHEME_INCB_CONSONANT},
                                                            {WSTRVC("Extend"), WGRAPHEME_INCB_EXTEND}};


typedef struct WGraphemeGenCtx {
    WStrView DerivedCoreProperties;
    WStrView EmojiData;
    WStrView GraphemeBreakProperty;
    uint8_t properties[UNICODE_CODEPOINT_COUNT];
} WGraphemeGenCtx;

long get_file_size(FILE *fp) {
    long size = 0;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return size;
}

static int wis_alpha(int ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); }

static int wis_digit(int ch) { return (ch >= '0' && ch <= '9'); }

static int wis_first_ident(char x) { return wis_alpha(x) || x == '_'; }

static int wis_ident(char x) { return wis_first_ident(x) || wis_digit(x); }

static WStrView parse_ident(const char *str) {
    while (*str) {
        if (isspace(*str))
            str++;
        else if (wis_first_ident(*str)) {
            WStrView res = {str};
            while (wis_ident(*str))
                str++;
            res.len = str - res.str;
            return res;
        } else {
            fail("Expected ident");
        }
    }
    return (WStrView) {0};
}

WStrView find_data_line(const char *s) {
    while (*s) {
        const char *endl = s;
        while (*endl != '\n' && *endl != '\0')
            endl++;

        /* skip comments and empty lines */
        if (*s == '#') {
            s = endl;
        } else if (*s == '\n') {
            s++;
        } else {
            return (WStrView) {s, endl - s};
        }
    }
    return (WStrView) {0};
}


uint8_t find_category(WStrView tok) {
    for (int i = 0; i < arrlen(unicode_cat_to_wgrapheme_cat); i++) {
        WUnicodeNameToCat name2cat = unicode_cat_to_wgrapheme_cat[i];
        if (wstrv_equal(name2cat.name, tok))
            return name2cat.cat;
    }
    return 0;
}

uint8_t find_incb(WStrView tok) {
    for (int i = 0; i < arrlen(unicode_incb_to_wgrapheme_incb); i++) {
        WUnicodeNameToCat name2cat = unicode_incb_to_wgrapheme_incb[i];
        if (wstrv_equal(name2cat.name, tok))
            return name2cat.cat;
    }
    return 0;
}

int parse_ucd_cat_line(const char *begin, const char **next, uint32_t *cp_start, uint32_t *cp_end, WStrView *p1,
                       WStrView *p2) {
    *cp_start = 0;
    *cp_end = 0;
    *p1 = *p2 = (WStrView) {0};

    WStrView line = find_data_line(begin);
    if (!line.str)
        return 0;
    *next = line.str + line.len;

    const char *p = line.str;
    char *end;
    {
        *cp_start = strtol(p, &end, 16);
        if (p == end)
            fail("expected codepoint");

        p = end;

        if (p[0] == '.' && p[1] == '.') {
            p += 2;
            *cp_end = strtol(p, &end, 16);
            if (p == end)
                fail("expected codepoint after range symbol");

        } else {
            *cp_end = *cp_start;
        }
        p = end;
        *cp_end += 1;
    }

    /* parse params */
    {
        p = strpbrk(p, ";\n");
        if (p == NULL)
            fail("expected semicolon, found eof");


        if (*p == '\n')
            fail("expected semicolon, found newline");

        p += 1;
        WStrView tok = parse_ident(p);
        if (tok.str == NULL)
            fail("expected identifier after semicolon");

        *p1 = tok;
    }

    /* try parsing second optional param */
    {
        p = strpbrk(p, ";\n");
        if (p == NULL)
            fail("expected semicolon, found eof");


        if (*p != '\n') {
            p += 1;
            WStrView tok = parse_ident(p);
            if (tok.str == NULL) {
                fail("expected identifier after semicolon");
            }
            *p2 = tok;
        }
    }

    return 1;
}


void ctx_init_categories(WGraphemeGenCtx *ctx) {
    WStrView p1, p2;
    uint32_t cp_start = 0, cp_end = 0;

    memset(&ctx->properties, WGRAPHEME_CAT_OTHER, sizeof(ctx->properties));
    {
        const char *it = ctx->GraphemeBreakProperty.str;
        while (parse_ucd_cat_line(it, &it, &cp_start, &cp_end, &p1, &p2)) {
            uint8_t cat = find_category(p1);
            if(p2.str != NULL) fail("Expected GraphemeBreakProperty line to only have 1 property");
            if (!cat)
                continue;
            for (uint32_t cp = cp_start; cp < cp_end; cp++) {
                ctx->properties[cp] = cat;
            }
        }
    }
    {
        const char *it = ctx->EmojiData.str;

        while (parse_ucd_cat_line(it, &it, &cp_start, &cp_end, &p1, &p2)) {
            uint8_t cat = find_category(p1);
            if(p2.str != NULL) fail("Expected emoji-data to only have 1 property");
            if (!cat)
                continue;
            for (uint32_t cp = cp_start; cp < cp_end; cp++) {
                ctx->properties[cp] = cat;
            }
        }
    }
    {
        const char *it = ctx->DerivedCoreProperties.str;

        while (parse_ucd_cat_line(it, &it, &cp_start, &cp_end, &p1, &p2)) {
            if (p2.str == NULL) {
                continue;
            }

            /* if Indic Conjunct break property, read the second one */
            if (!wstrv_equal(p1, WSTRV("InCB"))) {
                continue;
            }

            uint8_t cat = find_incb(p2);

            for (uint32_t cp = cp_start; cp < cp_end; cp++) {
                ctx->properties[cp] |= (cat << 5);
            }
        }
    }
}


typedef struct {
    uint32_t begin;
    uint32_t end;
    uint8_t prop;
} WPropertyRange;

WGraphemeGenCtx ctx;

WStrView read_file_into_buf(FILE *fp, size_t read_max, char *buf, char **end) {
    size_t len = fread(buf, 1, read_max, fp);
    if (fgetc(fp) != EOF)
        fail("Invalid file size read");
    buf[len] = '\0';
    *end = buf + len + 1;
    return (WStrView){buf, len};
}

int main(int argc, char *argv[]) {
    if (argc != 4)
        fail("usage: gen_data <UCD_DIR> <UNICODE_VER> <OUTPUT_HEADER_FILE>");

    const char *ucd_dir = argv[1];
    const char *unicode_ver = argv[2];
    const char *output_file = argv[3];
    char dir_path[1024];
    if (strlen(ucd_dir) > sizeof(dir_path) - 26) {
        fail("ucd_dir too long");
    }
    {
        sprintf(dir_path, "%s/DerivedCoreProperties.txt", ucd_dir);
        FILE *DerivedCoreProperties = fopen(dir_path, "r");
        sprintf(dir_path, "%s/emoji-data.txt", ucd_dir);
        FILE *EmojiData = fopen(dir_path, "r");
        sprintf(dir_path, "%s/GraphemeBreakProperty.txt", ucd_dir);
        FILE *GraphemeBreakProperty = fopen(dir_path, "r");
        if (!(DerivedCoreProperties && EmojiData && GraphemeBreakProperty)) {
            fail("Missing required unicode spec files");
        }

        size_t DerivedCorePropertiesLen = get_file_size(DerivedCoreProperties);
        size_t EmojiDataLen = get_file_size(EmojiData);
        size_t GraphemeBreakPropertyLen = get_file_size(GraphemeBreakProperty);

        char *sbuf_mem = malloc(DerivedCorePropertiesLen + 1 + EmojiDataLen + 1 + GraphemeBreakPropertyLen + 1);
        if (!sbuf_mem) {
            fail("Out of memory");
        }

        char *sbuf = sbuf_mem;
        ctx.DerivedCoreProperties = read_file_into_buf(DerivedCoreProperties, DerivedCorePropertiesLen, sbuf, &sbuf);
        ctx.EmojiData = read_file_into_buf(EmojiData, EmojiDataLen, sbuf, &sbuf);
        ctx.GraphemeBreakProperty = read_file_into_buf(GraphemeBreakProperty, GraphemeBreakPropertyLen, sbuf, &sbuf);

        ctx_init_categories(&ctx);

        free(sbuf_mem);
    }

    uint8_t prev_prop;

    WPropertyRange *prop_ranges;
    size_t len_prop_ranges = 0;

    /* first full iteration to calculate number of ranges */
    {
        size_t n_ranges = 1;
        prev_prop = ctx.properties[0];
        for (uint32_t cp = 0; cp < UNICODE_CODEPOINT_COUNT; cp++) {
            uint8_t prop = ctx.properties[cp];
            /* exclude ranges of other */
            if (prev_prop != prop && prop != WGRAPHEME_CAT_OTHER) {
                n_ranges++;
            }
            prev_prop = prop;
        }
        prop_ranges = malloc(n_ranges * sizeof(WPropertyRange));
        if (!prop_ranges)
            fail("Out of memory");
    }

    /* fill out property ranges */
    {
        prev_prop = ctx.properties[0];
        prop_ranges[len_prop_ranges] = (WPropertyRange) {.begin = 0x000000u, .prop = prev_prop};
        for (uint32_t cp = 0; cp < UNICODE_CODEPOINT_COUNT; cp++) {
            uint8_t prop = ctx.properties[cp];
            /* exclude ranges of other */
            if (prev_prop != prop) {
                if (prev_prop != WGRAPHEME_CAT_OTHER) {
                    prop_ranges[len_prop_ranges++].end = cp;
                }

                if (prop != WGRAPHEME_CAT_OTHER) {
                    /* open new range */
                    prop_ranges[len_prop_ranges] = (WPropertyRange) {.begin = cp, .prop = prop};
                }
            }
            prev_prop = prop;
        }
        if (prev_prop != WGRAPHEME_CAT_OTHER) {
            prop_ranges[len_prop_ranges++].end = UNICODE_CODEPOINT_COUNT;
        }
    }

    /* write header file */
    {
        FILE *res = fopen(output_file, "w");
        if (!res)
            fail("can't open output file");

        fprintf(res, "#ifndef WGRAPHEME_GEN_DATA_H\n");
        fprintf(res, "#define WGRAPHEME_GEN_DATA_H\n\n");
        fprintf(res, "#include <stdint.h>\n");

        fprintf(res, "#define WGRAPHEME_UNICODE_VERSION \"%s\"\n", unicode_ver);
        fprintf(res, "#define WGRAPHEME_RANGE_COUNT %zuu\n", len_prop_ranges);

        fprintf(res, "/* properties are packed bytes: (INCB << 5 | CAT). 5 bits for category, 2 for incb */\n");
        fprintf(res, "/* codepoints from wgrapheme_range_cp_hi[i] to wgrapheme_range_cp_hi[i] inclusive, are of property wgrapheme_range_prop[i] */\n");

        fprintf(res, "const uint8_t wgrapheme_range_prop[WGRAPHEME_RANGE_COUNT] = {");
        for (size_t i = 0; i < len_prop_ranges; i++) {
            fprintf(res, ",%d" + (i == 0), prop_ranges[i].prop);
        }
        fprintf(res, "};\n");

        /* split the [cp_begin, cp_end - 1] range into lo and hi arrays */
        fprintf(res, "const uint32_t wgrapheme_range_cp_hi[WGRAPHEME_RANGE_COUNT] = {");
        for (size_t i = 0; i < len_prop_ranges; i++) {
            fprintf(res, ",%d" + (i == 0), prop_ranges[i].end - 1); /* ranges are inclusive */
        }
        fprintf(res, "};\n");
        fprintf(res, "const uint32_t wgrapheme_range_cp_lo[WGRAPHEME_RANGE_COUNT] = {");
        for (size_t i = 0; i < len_prop_ranges; i++) {
            fprintf(res, ",%d" + (i == 0), prop_ranges[i].begin);
        }
        fprintf(res, "};\n");

        fprintf(res, "const uint16_t wgrapheme_lookup[%d] = {", LOOKUP_TABLE_LEN);
        int lookup_range_idx = 0;
        for (size_t i = 0; i < LOOKUP_TABLE_LEN; i++) {
            uint32_t cp_lookup_from = i * LOOKUP_INTERVAL;
            /* find first range that potentially contains this codepoint (CAT_OTHER codepoints are excluded to save space), end is exclusive thus <= */
            while (lookup_range_idx < len_prop_ranges && prop_ranges[lookup_range_idx].end <= cp_lookup_from)
                lookup_range_idx += 1;
            fprintf(res, ",%d" + (i == 0), lookup_range_idx);
        }
        fprintf(res, "};\n");

        fprintf(res, "#define WGRAPHEME_LOOKUP_INTERVAL %du\n", LOOKUP_INTERVAL);
        fprintf(res, "#define WGRAPHEME_LOOKUP_CUTOFF %du\n", LOOKUP_CUTOFF);
        fprintf(res, "#define WGRAPHEME_LOOKUP_FALLBACK %du\n", lookup_range_idx);

        fprintf(res, "\n#endif");

        fclose(res);
    }

    free(prop_ranges);
    return EXIT_SUCCESS;
}
