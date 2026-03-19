/*
 * Logging subsystem implementation
 */

#include "../include/logging.h"
#include "../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int logger_init(Logger *logger, const char *service_name, LogLevel level, const char *log_file) {
    if (logger == NULL || service_name == NULL) {
        return WS_ERROR_INVALID_ARG;
    }
    
    memset(logger, 0, sizeof(Logger));
    
    logger->level = level;
    logger->service_name = strdup(service_name);
    
    if (logger->service_name == NULL) {
        return WS_ERROR_MEMORY;
    }
    
    /* Open log file if specified, otherwise use stdout */
    if (log_file != NULL && log_file[0] != '\0') {
        logger->file = fopen(log_file, "a");
        if (logger->file == NULL) {
            free(logger->service_name);
            logger->service_name = NULL;
            return WS_ERROR_IO;
        }
    } else {
        logger->file = stdout;
    }
    
    /* Only use colors if writing to a terminal */
    logger->use_colors = (logger->file == stdout && isatty(STDOUT_FILENO));
    
    return WS_SUCCESS;
}

void logger_log(Logger *logger, LogLevel level, const char *format, ...) {
    if (logger == NULL || format == NULL) {
        return;
    }
    
    /* Skip if below minimum level */
    if (level < logger->level) {
        return;
    }
    
    FILE *output = logger->file ? logger->file : stdout;
    
    /* Get timestamp */
    char timestamp[32];
    logger_get_timestamp(timestamp, sizeof(timestamp));
    
    /* Get level string */
    const char *level_str = logger_level_to_string(level);
    
    /* ANSI color codes (only for terminal output) */
    const char *color_start = "";
    const char *color_end = "";
    
    if (logger->use_colors) {
        switch (level) {
            case LOG_LEVEL_DEBUG:
                color_start = "\033[36m"; /* Cyan */
                break;
            case LOG_LEVEL_INFO:
                color_start = "\033[32m"; /* Green */
                break;
            case LOG_LEVEL_WARN:
                color_start = "\033[33m"; /* Yellow */
                break;
            case LOG_LEVEL_ERROR:
                color_start = "\033[31m"; /* Red */
                break;
        }
        color_end = "\033[0m";
    }
    
    /* Print timestamp and level */
    fprintf(output, "[%s] [%s%s%s] ", timestamp, color_start, level_str, color_end);
    
    /* Print message */
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
}

LogLevel logger_parse_level(const char *str) {
    if (str == NULL) {
        return LOG_LEVEL_INFO;
    }
    
    if (strcasecmp(str, "debug") == 0) {
        return LOG_LEVEL_DEBUG;
    } else if (strcasecmp(str, "info") == 0) {
        return LOG_LEVEL_INFO;
    } else if (strcasecmp(str, "warn") == 0 || strcasecmp(str, "warning") == 0) {
        return LOG_LEVEL_WARN;
    } else if (strcasecmp(str, "error") == 0) {
        return LOG_LEVEL_ERROR;
    }
    
    return LOG_LEVEL_INFO;  /* Default */
}

const char* logger_level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

void logger_close(Logger *logger) {
    if (logger == NULL) {
        return;
    }
    
    if (logger->file != NULL && logger->file != stdout && logger->file != stderr) {
        fclose(logger->file);
    }
    
    if (logger->service_name != NULL) {
        free(logger->service_name);
    }
    
    memset(logger, 0, sizeof(Logger));
}

void logger_get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
