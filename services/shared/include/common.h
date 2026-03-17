/*
 * Common definitions and macros for weather station services
 */

#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
#define WS_VERSION "1.0.0"

/* Buffer sizes */
#define WS_BUFFER_SIZE 4096
#define WS_MAX_PATH_LEN 256
#define WS_MAX_LINE_LEN 1024

/* Time constants */
#define WS_SECOND_MS 1000
#define WS_MINUTE_MS (60 * WS_SECOND_MS)

/* Utility macros */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Safe string copy that always null-terminates */
#define SAFE_STRCPY(dst, src, size) do { \
    if (size > 0) { \
        strncpy((dst), (src), (size) - 1); \
        (dst)[(size) - 1] = '\0'; \
    } \
} while(0)

/* Check if string is empty or NULL */
#define IS_EMPTY_STR(s) ((s) == NULL || *(s) == '\0')

/* Return code definitions */
#define WS_SUCCESS 0
#define WS_ERROR -1
#define WS_ERROR_INVALID_ARG -2
#define WS_ERROR_MEMORY -3
#define WS_ERROR_IO -4
#define WS_ERROR_NETWORK -5
#define WS_ERROR_DATABASE -6

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */
