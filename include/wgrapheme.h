/*
 * Minimal UTF-8 grapheme segmentation library.
 *
 * wgrapheme exposes only the small API surface needed to walk grapheme
 * cluster boundaries in UTF-8 text, suitable for editor cursor movement and
 * grapheme splitting.
 */

#ifndef WGRAPHEME_H
#define WGRAPHEME_H

#include <stddef.h>
#include <stdint.h>

#define WGRAPHEME_VER_MAJOR 0
#define WGRAPHEME_VER_MINOR 2
#define WGRAPHEME_VER_PATCH 0

#ifndef WGRAPHEME_SHARED_DEFINE
    #ifndef WGRAPHEME_EXPORT
        #define WGRAPHEME_EXPORT
    #endif
#else
    #ifndef WGRAPHEME_EXPORT
        #if defined _WIN32 || defined __CYGWIN__
            #ifdef WGRAPHEME_EXPORTS
                #define WGRAPHEME_EXPORT __declspec(dllexport)
            #else
                #define WGRAPHEME_EXPORT __declspec(dllimport)
            #endif
        #elif __GNUC__ >= 4
            #define WGRAPHEME_EXPORT __attribute__((visibility("default")))
        #else
            #define WGRAPHEME_EXPORT
        #endif
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  WGRAPHEME_OK = 0,
  WGRAPHEME_DONE = 1,
  WGRAPHEME_INVALID_UTF8 = -1,
  WGRAPHEME_INVALID_OFFSET = -2,
  WGRAPHEME_INVALID_ARGUMENT = -3
} wgrapheme_status_t;

typedef struct {
  const char *string;
  size_t length;
  size_t cursor;
} wgrapheme_iter_t;

WGRAPHEME_EXPORT unsigned wgrapheme_version(void);

WGRAPHEME_EXPORT const char *wgrapheme_unicode_version(void);

WGRAPHEME_EXPORT void wgrapheme_iter_init(
  wgrapheme_iter_t *iter,
  const char *string,
  size_t length
);

WGRAPHEME_EXPORT wgrapheme_status_t wgrapheme_iter_next(
  wgrapheme_iter_t *iter,
  size_t *start,
  size_t *end
);

/*
 * Find the next grapheme boundary at or after `offset`.
 *
 * `offset` must be a UTF-8 codepoint boundary. For best performance, call this
 * with an offset that is already known to be a grapheme boundary (for example,
 * the output of a previous wgrapheme call).
 */
WGRAPHEME_EXPORT wgrapheme_status_t wgrapheme_next_boundary(
  const char *string,
  size_t length,
  size_t offset,
  size_t *next
);

/*
 * Find the previous grapheme boundary before `offset`.
 */
WGRAPHEME_EXPORT wgrapheme_status_t wgrapheme_prev_boundary(
  const char *string,
  size_t length,
  size_t offset,
  size_t *previous
);

/* Count graphemes in a string */
WGRAPHEME_EXPORT wgrapheme_status_t wgrapheme_count(
  const char *string,
  size_t length,
  size_t *count
);

#ifdef __cplusplus
}
#endif

#endif
