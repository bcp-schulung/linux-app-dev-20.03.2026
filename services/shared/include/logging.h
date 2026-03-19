/*
 * Logging subsystem for weather station services
 * Provides consistent logging across all services
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels in order of severity */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

/* Logger configuration and state */
typedef struct {
    LogLevel level;           /* Minimum level to log */
    FILE *file;              /* Log file (NULL for stdout) */
    char *service_name;      /* Service identifier */
    int use_colors;          /* ANSI color codes (default: 0) */
} Logger;

/* Initialize logger
 * @param logger Logger instance
 * @param service_name Name of the service (e.g., "ingestion")
 * @param level Minimum log level
 * @param log_file Path to log file (NULL for stdout)
 * @return WS_SUCCESS on success, WS_ERROR on failure
 */
int logger_init(Logger *logger, const char *service_name, LogLevel level, const char *log_file);

/* Log a message
 * @param logger Logger instance
 * @param level Log level for this message
 * @param format printf-style format string
 * @param ... Additional arguments
 */
void logger_log(Logger *logger, LogLevel level, const char *format, ...);

/* Convenience macros for logging at different levels */
#define LOG_DEBUG(logger, ...) logger_log(logger, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(logger, ...)  logger_log(logger, LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(logger, ...)  logger_log(logger, LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(logger, ...) logger_log(logger, LOG_LEVEL_ERROR, __VA_ARGS__)

/* Convert string to log level
 * @param str String representation ("debug", "info", "warn", "error")
 * @return LogLevel value, LOG_LEVEL_INFO if invalid
 */
LogLevel logger_parse_level(const char *str);

/* Convert log level to string
 * @param level Log level
 * @return String representation
 */
const char* logger_level_to_string(LogLevel level);

/* Close logger and cleanup
 * @param logger Logger instance
 */
void logger_close(Logger *logger);

/* Get current timestamp as string
 * @param buf Buffer to store timestamp
 * @param size Buffer size
 */
void logger_get_timestamp(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LOGGING_H */
