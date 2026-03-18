/*
 * Configuration file parsing implementation
 */

#include "../include/config.h"
#include "../include/logging.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

char* config_trim(char *str) {
    if (str == NULL) return NULL;
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)*str)) str++;
    
    /* Remove trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    
    return str;
}

char* config_remove_comment(char *line) {
    if (line == NULL) return NULL;
    
    char *comment = strchr(line, '#');
    if (comment != NULL) {
        *comment = '\0';
    }
    
    return line;
}

int config_parse_line(const char *line, char *key, size_t key_size,
                      char *value, size_t value_size) {
    if (line == NULL || key == NULL || value == NULL) {
        return 0;
    }
    
    /* Find equals sign */
    const char *equals = strchr(line, '=');
    if (equals == NULL) {
        return 0;  /* Not a key=value line */
    }
    
    /* Extract key */
    size_t key_len = equals - line;
    if (key_len >= key_size) {
        key_len = key_size - 1;
    }
    strncpy(key, line, key_len);
    key[key_len] = '\0';
    
    /* Trim key */
    char *trimmed_key = config_trim(key);
    if (trimmed_key != key) {
        memmove(key, trimmed_key, strlen(trimmed_key) + 1);
    }
    
    /* Extract value */
    const char *val_start = equals + 1;
    while (isspace((unsigned char)*val_start)) val_start++;
    
    strncpy(value, val_start, value_size - 1);
    value[value_size - 1] = '\0';
    
    /* Trim value */
    char *trimmed_val = config_trim(value);
    if (trimmed_val != value) {
        memmove(value, trimmed_val, strlen(trimmed_val) + 1);
    }
    
    return 1;
}

int config_parse(const char *filename, ConfigHandler handler, void *user_data) {
    if (filename == NULL || handler == NULL) {
        return -1;
    }
    
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        return -1;
    }
    
    char line[CONFIG_MAX_LINE_LEN];
    char key[CONFIG_MAX_KEY_LEN];
    char value[CONFIG_MAX_VALUE_LEN];
    int count = 0;
    int line_num = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;
        
        /* Remove newline */
        line[strcspn(line, "\n")] = '\0';
        
        /* Remove comments */
        config_remove_comment(line);
        
        /* Trim whitespace */
        char *trimmed = config_trim(line);
        
        /* Skip empty lines */
        if (trimmed[0] == '\0') {
            continue;
        }
        
        /* Parse key=value */
        if (config_parse_line(trimmed, key, sizeof(key), value, sizeof(value))) {
            if (handler(key, value, user_data) == 0) {
                count++;
            }
        }
    }
    
    fclose(fp);
    return count;
}

int config_parse_string(const char *config_str, ConfigHandler handler, void *user_data) {
    if (config_str == NULL || handler == NULL) {
        return -1;
    }
    
    /* Copy string since we need to modify it */
    char *str = strdup(config_str);
    if (str == NULL) {
        return -1;
    }
    
    char key[CONFIG_MAX_KEY_LEN];
    char value[CONFIG_MAX_VALUE_LEN];
    int count = 0;
    
    char *line = strtok(str, "\n");
    while (line != NULL) {
        /* Remove comments */
        config_remove_comment(line);
        
        /* Trim whitespace */
        char *trimmed = config_trim(line);
        
        /* Skip empty lines */
        if (trimmed[0] != '\0') {
            /* Parse key=value */
            if (config_parse_line(trimmed, key, sizeof(key), value, sizeof(value))) {
                if (handler(key, value, user_data) == 0) {
                    count++;
                }
            }
        }
        
        line = strtok(NULL, "\n");
    }
    
    free(str);
    return count;
}

int config_handle_common(const char *key, const char *value,
                         int *log_level, char *log_file, size_t log_file_size,
                         int *daemon_mode) {
    if (key == NULL || value == NULL) {
        return 0;
    }
    
    if (strcmp(key, "log_level") == 0) {
        if (log_level != NULL) {
            if (strcasecmp(value, "debug") == 0) {
                *log_level = 0;  /* LOG_LEVEL_DEBUG */
            } else if (strcasecmp(value, "info") == 0) {
                *log_level = 1;  /* LOG_LEVEL_INFO */
            } else if (strcasecmp(value, "warn") == 0) {
                *log_level = 2;  /* LOG_LEVEL_WARN */
            } else if (strcasecmp(value, "error") == 0) {
                *log_level = 3;  /* LOG_LEVEL_ERROR */
            }
        }
        return 1;
    }
    
    if (strcmp(key, "log_file") == 0) {
        if (log_file != NULL && log_file_size > 0) {
            strncpy(log_file, value, log_file_size - 1);
            log_file[log_file_size - 1] = '\0';
        }
        return 1;
    }
    
    if (strcmp(key, "daemon_mode") == 0) {
        if (daemon_mode != NULL) {
            *daemon_mode = (strcasecmp(value, "true") == 0 || 
                           strcasecmp(value, "yes") == 0 ||
                           strcmp(value, "1") == 0);
        }
        return 1;
    }
    
    return 0;  /* Not a common key */
}
