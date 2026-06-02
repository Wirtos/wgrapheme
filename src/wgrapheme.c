#include <wgrapheme.h>

#include <stdbool.h>

#include "wgrapheme_categories.h"
#include "wgrapheme_data.h"

#include <stdio.h>

#define arrlen(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef struct {
    uint32_t lower;
    uint32_t upper;
    uint8_t prop;
} WPropRange;

static uint8_t wgrapheme_prop_pack(enum WGraphemeCategory cat, enum WGraphemeINCB incb) {
    return (uint8_t) (cat & 0x1F | (incb & 0x03) << 5);
}

static enum WGraphemeCategory wgrapheme_prop_cat(uint8_t prop) { return prop & 0x1F; }

static enum WGraphemeINCB wgrapheme_prop_incb(uint8_t prop) { return (prop >> 5) & 0x03; }

static bool wgrapheme_is_codepoint_boundary(const uint8_t *string, size_t length, size_t offset) {
    if (offset > length) {
        return false;
    }
    if (offset == 0 || offset == length) {
        return true;
    }
    return (string[offset] & 0xC0) != 0x80;
}

static wgrapheme_status_t wgrapheme_decode_utf8(const uint8_t *string, size_t length, uint32_t *codepoint,
                                                size_t *consumed) {
    uint32_t first;

    if (!string || length == 0) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }

    first = string[0];
    if (first < 0x80) {
        *codepoint = first;
        *consumed = 1;
        return WGRAPHEME_OK;
    }

    if (first < 0xC2 || first > 0xF4) {
        return WGRAPHEME_INVALID_UTF8;
    }

    if (first < 0xE0) {
        if (length < 2 || (string[1] & 0xC0) != 0x80) {
            return WGRAPHEME_INVALID_UTF8;
        }
        *codepoint = ((first & 0x1F) << 6) | (string[1] & 0x3F);
        *consumed = 2;
        return WGRAPHEME_OK;
    }

    if (first < 0xF0) {
        if (length < 3 || (string[1] & 0xC0) != 0x80 || (string[2] & 0xC0) != 0x80) {
            return WGRAPHEME_INVALID_UTF8;
        }
        if (first == 0xE0 && string[1] < 0xA0) {
            return WGRAPHEME_INVALID_UTF8;
        }
        if (first == 0xED && string[1] > 0x9F) {
            return WGRAPHEME_INVALID_UTF8;
        }
        *codepoint = ((first & 0x0F) << 12) | ((string[1] & 0x3F) << 6) | (string[2] & 0x3F);
        *consumed = 3;
        return WGRAPHEME_OK;
    }

    if (length < 4 || (string[1] & 0xC0) != 0x80 || (string[2] & 0xC0) != 0x80 || (string[3] & 0xC0) != 0x80) {
        return WGRAPHEME_INVALID_UTF8;
    }
    if (first == 0xF0 && string[1] < 0x90) {
        return WGRAPHEME_INVALID_UTF8;
    }
    if (first == 0xF4 && string[1] > 0x8F) {
        return WGRAPHEME_INVALID_UTF8;
    }
    *codepoint = ((first & 0x07) << 18) | ((string[1] & 0x3F) << 12) | ((string[2] & 0x3F) << 6) | (string[3] & 0x3F);
    *consumed = 4;
    return WGRAPHEME_OK;
}

static void wgrapheme_ascii_prop(uint32_t codepoint, WPropRange *range) {
    if (codepoint >= 0x20) {
        range->lower = 0x20;
        range->upper = 0x7E;
        range->prop = WGRAPHEME_CAT_OTHER;
    } else if (codepoint == '\n') {
        range->lower = '\n';
        range->upper = '\n';
        range->prop = WGRAPHEME_CAT_LF;
    } else if (codepoint == '\r') {
        range->lower = '\r';
        range->upper = '\r';
        range->prop = WGRAPHEME_CAT_CR;
    } else {
        range->lower = codepoint;
        range->upper = codepoint;
        range->prop = WGRAPHEME_CAT_CONTROL;
    }
}

static void wgrapheme_lookup_prop(uint32_t codepoint, WPropRange *cache) {
    uint16_t idx;
    uint32_t lo;
    uint32_t hi;

    if (codepoint <= 0x7E) {
        wgrapheme_ascii_prop(codepoint, cache);
        return;
    }

    if (codepoint >= cache->lower && codepoint <= cache->upper) {
        return;
    }

    idx = codepoint / WGRAPHEME_LOOKUP_INTERVAL;
    if (idx < arrlen(wgrapheme_lookup) - 1) {
        /* Codepoint is possibly covered by range index lookup table
         * (if it's CAT_OTHER, then there's no range covering the codepoint, and we will fail the search later) */
        lo = wgrapheme_lookup[idx]; /* lower range index for 128 codepoints */
        hi = wgrapheme_lookup[idx + 1] + 1; /* higher range index for 128 codepoints */
    } else {
        /* Codepoint not covered by lookup table, do a full binary search */
        lo = WGRAPHEME_LOOKUP_FALLBACK;
        hi = WGRAPHEME_RANGE_COUNT;
    }

    /* Binary search, find range index that includes current codepoint */
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) / 2);
        if (codepoint < wgrapheme_range_cp_lo[mid]) {
            hi = mid;
        } else if (codepoint > wgrapheme_range_cp_hi[mid]) {
            lo = mid + 1;
        } else {
            /* found range index containing codepoint, fetch its property from range prop table */
            cache->lower = wgrapheme_range_cp_lo[mid];
            cache->upper = wgrapheme_range_cp_hi[mid];
            cache->prop = wgrapheme_range_prop[mid];
            return;
        }
    }

    /* Binary search failed, assume CAT_OTHER */
    cache->lower = (lo > 0) ? wgrapheme_range_cp_hi[lo - 1] + 1 : 0;
    cache->upper = (lo < WGRAPHEME_RANGE_COUNT) ? wgrapheme_range_cp_lo[lo] - 1 : 0x10FFFF;
    cache->prop = WGRAPHEME_CAT_OTHER;
}

/*
 * ÷ Boundary (allow break here)
 * × No boundary (do not allow break here)
 * → Treat whatever on the left side as if it were what is on the right side
 *
 * Break at the start and end of text, unless the text is empty.
 *   GB1 sot ÷ Any
 *   GB2 Any ÷ eot
 * Do not break between a CR and LF. Otherwise, break before and after controls.
 *   GB3 CR × LF
 *   GB4 (Control | CR | LF) ÷
 *   GB5 ÷ (Control | CR | LF)
 * Do not break Hangul syllable sequences.
 *   GB6 L × (L | V | LV | LVT)
 *   GB7 (LV | V) × (V | T)
 *   GB8 (LVT | T) × T
 * Do not break before extending characters or ZWJ.
 *   GB9   × (Extend | ZWJ)
 * Only for extended grapheme clusters:
 * Do not break before SpacingMarks, or after Prepend characters.
 *   GB9a   × SpacingMark
 *   GB9b Prepend ×
 * The GB9c rule only applies to extended grapheme clusters:
 * Do not break within certain combinations with Indic_Conjunct_Break (InCB)=Linker.
 *   GB9c 	InCB_Consonant [ InCB_Extend InCB_Linker ]* InCB_Linker [ InCB_Extend InCB_Linker ]* × InCB_Consonant
 * Do not break within emoji modifier sequences or emoji zwj sequences.
 *   [DEPRECATED] GB10 (E_Base | EBG) Extend* × E_Modifier
 *   GB11 Extended_Pictographic Extend* ZWJ × Extended_Pictographic
 * Further customization of this rule may be necessary for best behavior of emoji zwj sequences [UTR51], using data
 * planned for inclusion in CLDR Version 30 [CLDR]. Do not break within emoji flag sequences. That is, do not break
 * between regional indicator (RI) symbols if there is an odd number of RI characters before the break point.
 *   GB12 (RI RI)* RI × RI
 *   GB13 [^RI] (RI RI)* RI × RI
 *  Otherwise, break everywhere:
 *   GB999 Any ÷ Any
 */
/* clang-format off */
/* https://www.unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules */
static bool wgrapheme_break_simple(int left, int right) {
    return (
    /* GB1     */ (left == WGRAPHEME_CAT_START) ? true :
    /* GB3     */ (left == WGRAPHEME_CAT_CR && right == WGRAPHEME_CAT_LF) ? false :
    /* GB4     */ (left >= WGRAPHEME_CAT_CR && left <= WGRAPHEME_CAT_CONTROL) ? true :
    /* GB5     */ (right >= WGRAPHEME_CAT_CR && right <= WGRAPHEME_CAT_CONTROL) ? true :
    /* GB6     */ (left == WGRAPHEME_CAT_L &&
    /* ------- */ (right == WGRAPHEME_CAT_L ||
    /* ------- */ right == WGRAPHEME_CAT_V ||
    /* ------- */ right == WGRAPHEME_CAT_LV ||
    /* ------- */ right == WGRAPHEME_CAT_LVT)) ? false :
    /* GB7     */ ((left == WGRAPHEME_CAT_LV || left == WGRAPHEME_CAT_V) &&
    /* ------- */ (right == WGRAPHEME_CAT_V || right == WGRAPHEME_CAT_T)) ? false :
    /* GB8     */((left == WGRAPHEME_CAT_LVT || left == WGRAPHEME_CAT_T) && right == WGRAPHEME_CAT_T) ? false :
    /* GB9     */(right == WGRAPHEME_CAT_EXTEND ||
    /* ------- */right == WGRAPHEME_CAT_ZWJ ||
    /* GB9a    */right == WGRAPHEME_CAT_SPACINGMARK ||
    /* GB9b    */left == WGRAPHEME_CAT_PREPEND) ? false :
    /* !!for below additional handling required in wgrapheme_break_extended since this is stateful!! */
    /* GB11    */ (left == WGRAPHEME_CAT_E_ZWG &&
    /* ------- */ right == WGRAPHEME_CAT_EXTENDED_PICTOGRAPHIC) ? false :
    /* GB12/13 */ (left == WGRAPHEME_CAT_REGIONAL_INDICATOR &&
    /* ------- */ right == WGRAPHEME_CAT_REGIONAL_INDICATOR) ? false :
    true
    );
}
/* clang-format on */

static bool wgrapheme_break_extended(enum WGraphemeCategory left, enum WGraphemeCategory right,
                                     enum WGraphemeINCB left_incb, enum WGraphemeINCB right_incb, uint8_t *state) {
    enum WGraphemeCategory state_cat;
    enum WGraphemeINCB state_incb;
    bool break_permitted;

    if (*state == 0) {
        /* state 0 means we're on a beginning of a grapheme boundary */
        state_cat = left;
        state_incb = (left_incb == WGRAPHEME_INCB_CONSONANT) ? WGRAPHEME_INCB_CONSONANT : WGRAPHEME_INCB_NONE;
    } else {
        state_cat = wgrapheme_prop_cat(*state);
        state_incb = wgrapheme_prop_incb(*state);
    }

    break_permitted = wgrapheme_break_simple(state_cat, right) &&
                      !(state_incb == WGRAPHEME_INCB_LINKER && right_incb == WGRAPHEME_INCB_CONSONANT); /* GB9c */

    if (right_incb == WGRAPHEME_INCB_CONSONANT || state_incb == WGRAPHEME_INCB_CONSONANT ||
        state_incb == WGRAPHEME_INCB_EXTEND) {
        state_incb = right_incb;
    } else if (state_incb == WGRAPHEME_INCB_LINKER) {
        state_incb = (right_incb == WGRAPHEME_INCB_EXTEND) ? WGRAPHEME_INCB_LINKER : right_incb;
    }

    /* After two RI
     * class codepoints we want to force a break. Do this by resetting the
     * second RI's bound class to OTHER, to force a break
     * after that character according to GB999 (unless of course such a break is
     * forbidden by a different rule such as GB9).
     * GB12/13
     */
    if (state_cat == WGRAPHEME_CAT_REGIONAL_INDICATOR && right == WGRAPHEME_CAT_REGIONAL_INDICATOR) {
        state_cat = WGRAPHEME_CAT_OTHER;
    } else if (state_cat == WGRAPHEME_CAT_EXTENDED_PICTOGRAPHIC) { /* GB11 */
        if (right == WGRAPHEME_CAT_EXTEND) {
            state_cat = WGRAPHEME_CAT_EXTENDED_PICTOGRAPHIC;
        } else if (right == WGRAPHEME_CAT_ZWJ) {
            state_cat = WGRAPHEME_CAT_E_ZWG;
        } else {
            state_cat = right;
        }
    } else {
        state_cat = right;
    }

    *state = wgrapheme_prop_pack(state_cat, state_incb);
    return break_permitted;
}

static bool wgrapheme_break_props(uint8_t left_prop, uint8_t right_prop, uint8_t *state) {
    return wgrapheme_break_extended(wgrapheme_prop_cat(left_prop), wgrapheme_prop_cat(right_prop),
                                    wgrapheme_prop_incb(left_prop), wgrapheme_prop_incb(right_prop), state);
}

unsigned wgrapheme_version(void) {
    return 1000000 * WGRAPHEME_VER_MAJOR + 1000 * WGRAPHEME_VER_MINOR + WGRAPHEME_VER_PATCH;
}

const char *wgrapheme_unicode_version(void) { return WGRAPHEME_UNICODE_VERSION; }

void wgrapheme_iter_init(wgrapheme_iter_t *iter, const char *string, size_t length) {
    if (!iter) {
        return;
    }
    iter->string = string;
    iter->length = length;
    iter->cursor = 0;
}

wgrapheme_status_t wgrapheme_next_boundary(const char *str, size_t length, size_t offset, size_t *next) {
    uint8_t *string = (uint8_t *) str;
    WPropRange cache = {0, 0, 0};
    uint8_t left_prop;
    uint32_t codepoint;
    size_t consumed;
    size_t cursor;
    uint8_t state = 0;
    wgrapheme_status_t status;

    if (!string || !next) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }

    if (!wgrapheme_is_codepoint_boundary(string, length, offset)) {
        return WGRAPHEME_INVALID_OFFSET;
    }
    if (offset == length) {
        *next = length;
        return WGRAPHEME_DONE;
    }

    status = wgrapheme_decode_utf8(string + offset, length - offset, &codepoint, &consumed);
    if (status != WGRAPHEME_OK) {
        return status;
    }

    wgrapheme_lookup_prop(codepoint, &cache);
    left_prop = cache.prop;
    cursor = offset + consumed;

    while (cursor < length) {
        uint8_t right_prop;

        status = wgrapheme_decode_utf8(string + cursor, length - cursor, &codepoint, &consumed);
        if (status != WGRAPHEME_OK) {
            return status;
        }
        wgrapheme_lookup_prop(codepoint, &cache);
        right_prop = cache.prop;
        if (wgrapheme_break_props(left_prop, right_prop, &state)) {
            *next = cursor;
            return WGRAPHEME_OK;
        }
        left_prop = right_prop;
        cursor += consumed;
    }

    *next = length;
    return WGRAPHEME_OK;
}

wgrapheme_status_t wgrapheme_prev_boundary(const char *str, size_t length, size_t offset, size_t *previous) {
    uint8_t *string = (uint8_t *) str;
    size_t cursor = 0;
    size_t next = 0;
    size_t last = 0;
    wgrapheme_status_t status;

    if (!string && length != 0) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }
    if (!previous) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }
    if (!wgrapheme_is_codepoint_boundary(string, length, offset)) {
        return WGRAPHEME_INVALID_OFFSET;
    }
    if (offset == 0) {
        *previous = 0;
        return WGRAPHEME_DONE;
    }

    while (cursor < length) {
        status = wgrapheme_next_boundary((char *) string, length, cursor, &next);
        if (status < 0) {
            return status;
        }
        if (next >= offset) {
            *previous = last;
            return WGRAPHEME_OK;
        }
        last = next;
        cursor = next;
    }

    *previous = last;
    return WGRAPHEME_OK;
}

wgrapheme_status_t wgrapheme_iter_next(wgrapheme_iter_t *iter, size_t *start, size_t *end) {
    wgrapheme_status_t status;
    size_t next;

    if (!iter || !start || !end) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }
    if (iter->cursor >= iter->length) {
        return WGRAPHEME_DONE;
    }

    *start = iter->cursor;
    status = wgrapheme_next_boundary(iter->string, iter->length, iter->cursor, &next);
    if (status < 0) {
        return status;
    }
    if (status == WGRAPHEME_DONE) {
        next = iter->length;
    }
    iter->cursor = next;
    *end = next;
    return WGRAPHEME_OK;
}

wgrapheme_status_t wgrapheme_count(const char *str, size_t length, size_t *count) {
    uint8_t *string = (uint8_t *) str;
    wgrapheme_iter_t iter;
    size_t start;
    size_t end;
    size_t total = 0;
    wgrapheme_status_t status;

    if (!count) {
        return WGRAPHEME_INVALID_ARGUMENT;
    }

    wgrapheme_iter_init(&iter, (char *) string, length);
    while ((status = wgrapheme_iter_next(&iter, &start, &end)) == WGRAPHEME_OK) {
        (void) start;
        (void) end;
        total++;
    }
    if (status != WGRAPHEME_DONE) {
        return status;
    }
    *count = total;
    return WGRAPHEME_OK;
}
