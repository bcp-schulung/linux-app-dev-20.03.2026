/*
 * Configuration file parsing for weather station services
 * Supports INI-style configuration files
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum lengths */
#define CONFIG_MAX_LINE_LEN 1024
#define CONFIG_MAX_KEY_LEN 128
#define CONFIG_MAX_VALUE_LEN 512

/* Configuration key handler callback
 * Called for each key-value pair found in config file
 * @param key Configuration key
 * @param value Configuration value
 * @param user_data Pointer to service-specific config struct
 * @return 0 on success, -1 on error
 */
typedef int (*ConfigHandler)(const char *key, const char *value, void *user_data);

/* Parse configuration file
 * @param filename Path to config file
 * @param handler Callback function for each key-value pair
 * @param user_data Pointer to pass to handler (usually config struct)
 * @return Number of keys parsed, or -1 on error
 */
int config_parse(const char *filename, ConfigHandler handler, void *user_data);

/* Parse configuration from string (for testing)
 * @param config_str Configuration as string
 * @param handler Callback function
 * @param user_data Pointer to pass to handler
 * @return Number of keys parsed, or -1 on error
 */
int config_parse_string(const char *config_str, ConfigHandler handler, void *user_data);

/* Trim whitespace from string (modifies in place)
 * @param str String to trim
 * @return Pointer to trimmed string
 */
char* config_trim(char *str);

/* Remove comments from line (modifies in place)
 * @param line Line to process
 * @return Pointer to line
 */
char* config_remove_comment(char *line);

/* Parse key=value pair from line
 * @param line Input line
 * @param key Buffer for key
 * @param key_size Size of key buffer
 * @param value Buffer for value
 * @param value_size Size of value buffer
 * @return 1 if parsed successfully, 0 if not a key=value line
 */
int config_parse_line(const char *line, char *key, size_t key_size, 
                      char *value, size_t value_size);

/* Common configuration keys handler
 * Handles standard keys like log_level, log_file, daemon_mode
 * @param key Configuration key
 * @param value Configuration value
 * @param log_level Output: parsed log level (can be NULL)
 * @param log_file Output: parsed log file path (can be NULL)
 * @param daemon_mode Output: parsed daemon mode flag (can be NULL)
 * @return 1 if handled, 0 if not a common key
 */
int config_handle_common(const char *key, const char *value,
                         int *log_level, char *log_file, size_t log_file_size,
                         int *daemon_mode);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
