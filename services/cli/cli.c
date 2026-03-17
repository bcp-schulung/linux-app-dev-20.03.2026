#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <getopt.h>
#include <ctype.h>

#define VERSION "1.0.0"
#define DEFAULT_CONFIG_FILE ".weathercli"
#define BUFFER_SIZE 8192
#define MAX_URL_LEN 256

/* Configuration */
typedef struct {
    char api_endpoint[MAX_URL_LEN];
    char output_format[16];
    char default_station[32];
} Config;

/* Global config */
static Config g_config = {
    .api_endpoint = "http://localhost:8080",
    .output_format = "table",
    .default_station = ""
};

/* Function prototypes */
static void print_usage(const char *program);
static void print_version(void);
static int parse_config(const char *filename);
static int make_http_request(const char *path, char *response, size_t response_size);
static void parse_json_value(const char *json, const char *key, char *value, size_t value_size);
static void output_table(const char *json);
static void output_json(const char *json);
static void output_csv(const char *json);
static int cmd_daily(int argc, char *const argv[]);
static int cmd_hourly(int argc, char *const argv[]);
static int cmd_stations(int argc, char *const argv[]);
static int cmd_health(int argc, char *const argv[]);

/* Print usage */
static void print_usage(const char *program) {
    printf("Weather Station CLI v%s\n\n", VERSION);
    printf("Usage: %s <command> [options]\n\n", program);
    printf("Commands:\n");
    printf("  daily      Query daily weather aggregates\n");
    printf("  hourly     Query hourly weather aggregates\n");
    printf("  stations   List all weather stations\n");
    printf("  health     Check API health\n");
    printf("  help       Show this help message\n");
    printf("  version    Show version information\n");
    printf("\nOptions:\n");
    printf("  --station <id>     Station ID (e.g., USW00094846)\n");
    printf("  --date <YYYYMMDD>  Date for daily query\n");
    printf("  --hour <YYYYMMDDHH> Hour for hourly query\n");
    printf("  --metric <type>    Metric type (TMAX, TMIN, PRCP, SNOW)\n");
    printf("  --format <type>    Output format: table, json, csv\n");
    printf("  --api <url>        API endpoint URL\n");
    printf("  --config <file>    Config file path\n");
    printf("\nExamples:\n");
    printf("  %s daily --station USW00094846 --date 20200101\n", program);
    printf("  %s hourly --station USW00094846 --hour 2020010100\n", program);
    printf("  %s stations\n", program);
    printf("  %s daily --station USW00094846 --format json\n", program);
}

/* Print version */
static void print_version(void) {
    printf("Weather Station CLI v%s\n", VERSION);
}

/* Parse configuration file */
static int parse_config(const char *filename) {
    char path[256];
    if (filename[0] == '/') {
        strncpy(path, filename, sizeof(path) - 1);
    } else if (filename[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) return -1;
        snprintf(path, sizeof(path), "%s%s", home, filename + 1);
    } else {
        strncpy(path, filename, sizeof(path) - 1);
    }
    
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove comments and whitespace */
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        /* Trim whitespace */
        int len = strlen(line);
        while (len > 0 && isspace(line[len-1])) line[--len] = '\0';
        if (len == 0) continue;
        
        /* Parse key=value */
        char *equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        
        /* Trim */
        while (isspace(*key)) key++;
        while (isspace(*value)) value++;
        while (len > 0 && isspace(key[len-1])) key[--len] = '\0';
        
        if (strcmp(key, "api_endpoint") == 0) {
            strncpy(g_config.api_endpoint, value, MAX_URL_LEN - 1);
        } else if (strcmp(key, "output_format") == 0) {
            strncpy(g_config.output_format, value, sizeof(g_config.output_format) - 1);
        } else if (strcmp(key, "default_station") == 0) {
            strncpy(g_config.default_station, value, sizeof(g_config.default_station) - 1);
        }
    }
    
    fclose(fp);
    return 0;
}

/* Make HTTP GET request */
static int make_http_request(const char *path, char *response, size_t response_size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Error: Failed to create socket\n");
        return -1;
    }
    
    /* Parse URL */
    char host[128];
    int port = 80;
    
    if (strncmp(g_config.api_endpoint, "http://", 7) == 0) {
        char *url = g_config.api_endpoint + 7;
        char *colon = strchr(url, ':');
        char *slash = strchr(url, '/');
        
        if (colon && (!slash || colon < slash)) {
            int host_len = colon - url;
            strncpy(host, url, host_len);
            host[host_len] = '\0';
            port = atoi(colon + 1);
        } else if (slash) {
            int host_len = slash - url;
            strncpy(host, url, host_len);
            host[host_len] = '\0';
        } else {
            strncpy(host, url, sizeof(host) - 1);
        }
    } else {
        strncpy(host, g_config.api_endpoint, sizeof(host) - 1);
    }
    
    struct hostent *server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "Error: Could not resolve host %s\n", host);
        close(sock);
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error: Could not connect to API at %s:%d\n", host, port);
        close(sock);
        return -1;
    }
    
    /* Build HTTP request */
    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: WeatherCLI/%s\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host, VERSION);
    
    if (send(sock, request, strlen(request), 0) < 0) {
        fprintf(stderr, "Error: Failed to send request\n");
        close(sock);
        return -1;
    }
    
    /* Receive response */
    char buffer[BUFFER_SIZE];
    int total_received = 0;
    int bytes_received;
    
    while ((bytes_received = recv(sock, buffer + total_received, 
                                  sizeof(buffer) - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
    }
    
    buffer[total_received] = '\0';
    close(sock);
    
    /* Find body (after \r\n\r\n) */
    char *body = strstr(buffer, "\r\n\r\n");
    if (!body) {
        fprintf(stderr, "Error: Invalid HTTP response\n");
        return -1;
    }
    body += 4;
    
    strncpy(response, body, response_size - 1);
    response[response_size - 1] = '\0';
    
    return 0;
}

/* Parse JSON value (simple parser) */
static void parse_json_value(const char *json, const char *key, char *value, size_t value_size) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    
    char *found = strstr(json, search_key);
    if (!found) {
        value[0] = '\0';
        return;
    }
    
    found += strlen(search_key);
    while (isspace(*found)) found++;
    
    if (*found == '"') {
        /* String value */
        found++;
        char *end = strchr(found, '"');
        if (end) {
            size_t len = end - found;
            if (len >= value_size) len = value_size - 1;
            strncpy(value, found, len);
            value[len] = '\0';
        }
    } else {
        /* Number or other value */
        char *end = found;
        while (*end && *end != ',' && *end != '}' && !isspace(*end)) end++;
        size_t len = end - found;
        if (len >= value_size) len = value_size - 1;
        strncpy(value, found, len);
        value[len] = '\0';
    }
}

/* Output as table */
static void output_table(const char *json) {
    /* Check for error */
    if (strstr(json, "\"error\"")) {
        char error_msg[256];
        parse_json_value(json, "error", error_msg, sizeof(error_msg));
        printf("Error: %s\n", error_msg);
        return;
    }
    
    /* Check if it's stations list */
    if (strstr(json, "\"stations\"")) {
        printf("\nWeather Stations:\n");
        printf("-----------------\n");
        
        char *stations_start = strstr(json, "[") + 1;
        char *stations_end = strstr(json, "]");
        if (!stations_start || !stations_end) {
            printf("No stations found.\n");
            return;
        }
        
        *stations_end = '\0';
        
        char *station = strtok(stations_start, ",");
        while (station) {
            /* Remove quotes */
            while (*station == '"' || isspace(*station)) station++;
            char *end = station + strlen(station) - 1;
            while (end > station && (*end == '"' || isspace(*end))) *end-- = '\0';
            
            printf("  %s\n", station);
            station = strtok(NULL, ",");
        }
        printf("\n");
        return;
    }
    
    /* Weather data table */
    printf("\n%-15s %-12s %-8s %-10s %-10s %-10s %-6s\n",
           "Station", "Date", "Metric", "Avg", "Min", "Max", "Count");
    printf("%-15s %-12s %-8s %-10s %-10s %-10s %-6s\n",
           "---------------", "------------", "--------", "----------", 
           "----------", "----------", "------");
    
    /* Simple parsing for array of objects */
    char *data_start = strstr(json, "\"data\":");
    if (data_start) {
        data_start = strchr(data_start, '[');
        if (data_start) {
            data_start++;
            char station_id[32], date[16], metric[8];
            char avg[16], min[16], max[16], count[8];
            
            char *obj = strstr(data_start, "{");
            while (obj) {
                parse_json_value(obj, "station_id", station_id, sizeof(station_id));
                parse_json_value(obj, "date", date, sizeof(date));
                parse_json_value(obj, "metric", metric, sizeof(metric));
                parse_json_value(obj, "avg", avg, sizeof(avg));
                parse_json_value(obj, "min", min, sizeof(min));
                parse_json_value(obj, "max", max, sizeof(max));
                parse_json_value(obj, "count", count, sizeof(count));
                
                if (station_id[0]) {
                    printf("%-15s %-12s %-8s %-10s %-10s %-10s %-6s\n",
                           station_id, date, metric, avg, min, max, count);
                }
                
                obj = strstr(obj + 1, "{");
            }
        }
    }
    printf("\n");
}

/* Output as JSON (pretty print) */
static void output_json(const char *json) {
    /* Simple pretty printing */
    int indent = 0;
    int in_string = 0;
    
    for (const char *p = json; *p; p++) {
        if (*p == '"' && (p == json || *(p-1) != '\\')) {
            in_string = !in_string;
        }
        
        if (!in_string) {
            if (*p == '{' || *p == '[') {
                putchar(*p);
                putchar('\n');
                indent += 2;
                for (int i = 0; i < indent; i++) putchar(' ');
            } else if (*p == '}' || *p == ']') {
                putchar('\n');
                indent -= 2;
                for (int i = 0; i < indent; i++) putchar(' ');
                putchar(*p);
            } else if (*p == ',') {
                putchar(*p);
                putchar('\n');
                for (int i = 0; i < indent; i++) putchar(' ');
            } else {
                putchar(*p);
            }
        } else {
            putchar(*p);
        }
    }
    putchar('\n');
}

/* Output as CSV */
static void output_csv(const char *json) {
    /* Check for error */
    if (strstr(json, "\"error\"")) {
        printf("error\n");
        return;
    }
    
    /* Print header for weather data */
    if (strstr(json, "\"data\"")) {
        printf("station_id,date,metric,avg,min,max,count\n");
        
        char *data_start = strstr(json, "\"data\":");
        if (data_start) {
            data_start = strchr(data_start, '[');
            if (data_start) {
                data_start++;
                char station_id[32], date[16], metric[8];
                char avg[16], min[16], max[16], count[8];
                
                char *obj = strstr(data_start, "{");
                while (obj) {
                    parse_json_value(obj, "station_id", station_id, sizeof(station_id));
                    parse_json_value(obj, "date", date, sizeof(date));
                    parse_json_value(obj, "metric", metric, sizeof(metric));
                    parse_json_value(obj, "avg", avg, sizeof(avg));
                    parse_json_value(obj, "min", min, sizeof(min));
                    parse_json_value(obj, "max", max, sizeof(max));
                    parse_json_value(obj, "count", count, sizeof(count));
                    
                    if (station_id[0]) {
                        printf("%s,%s,%s,%s,%s,%s,%s\n",
                               station_id, date, metric, avg, min, max, count);
                    }
                    
                    obj = strstr(obj + 1, "{");
                }
            }
        }
    } else if (strstr(json, "\"stations\"")) {
        printf("station_id\n");
        char *stations_start = strstr(json, "[") + 1;
        char *stations_end = strstr(json, "]");
        if (stations_start && stations_end) {
            *stations_end = '\0';
            char *station = strtok(stations_start, ",");
            while (station) {
                while (*station == '"' || isspace(*station)) station++;
                char *end = station + strlen(station) - 1;
                while (end > station && (*end == '"' || isspace(*end))) *end-- = '\0';
                printf("%s\n", station);
                station = strtok(NULL, ",");
            }
        }
    }
}

/* Daily command */
static int cmd_daily(int argc, char *const argv[]) {
    char station_id[32] = "";
    char date[16] = "";
    char metric[8] = "";
    
    /* Parse arguments */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--station") == 0 && i + 1 < argc) {
            strncpy(station_id, argv[++i], sizeof(station_id) - 1);
        } else if (strcmp(argv[i], "--date") == 0 && i + 1 < argc) {
            strncpy(date, argv[++i], sizeof(date) - 1);
        } else if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) {
            strncpy(metric, argv[++i], sizeof(metric) - 1);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            strncpy(g_config.output_format, argv[++i], sizeof(g_config.output_format) - 1);
        } else if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            strncpy(g_config.api_endpoint, argv[++i], sizeof(g_config.api_endpoint) - 1);
        }
    }
    
    /* Use default station if not specified */
    if (!station_id[0] && g_config.default_station[0]) {
        strncpy(station_id, g_config.default_station, sizeof(station_id) - 1);
    }
    
    /* Build query path */
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/weather/daily?");
    
    int first_param = 1;
    if (station_id[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path), 
                 "%sstation_id=%s", first_param ? "" : "&", station_id);
        first_param = 0;
    }
    if (date[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "%sdate=%s", first_param ? "" : "&", date);
        first_param = 0;
    }
    if (metric[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "%smetric=%s", first_param ? "" : "&", metric);
    }
    
    /* Make request */
    char response[BUFFER_SIZE];
    if (make_http_request(path, response, sizeof(response)) != 0) {
        return 3;
    }
    
    /* Output results */
    if (strcmp(g_config.output_format, "json") == 0) {
        output_json(response);
    } else if (strcmp(g_config.output_format, "csv") == 0) {
        output_csv(response);
    } else {
        output_table(response);
    }
    
    return 0;
}

/* Hourly command */
static int cmd_hourly(int argc, char *const argv[]) {
    char station_id[32] = "";
    char hour[16] = "";
    char metric[8] = "";
    
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--station") == 0 && i + 1 < argc) {
            strncpy(station_id, argv[++i], sizeof(station_id) - 1);
        } else if (strcmp(argv[i], "--hour") == 0 && i + 1 < argc) {
            strncpy(hour, argv[++i], sizeof(hour) - 1);
        } else if (strcmp(argv[i], "--metric") == 0 && i + 1 < argc) {
            strncpy(metric, argv[++i], sizeof(metric) - 1);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            strncpy(g_config.output_format, argv[++i], sizeof(g_config.output_format) - 1);
        } else if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            strncpy(g_config.api_endpoint, argv[++i], sizeof(g_config.api_endpoint) - 1);
        }
    }
    
    if (!station_id[0] && g_config.default_station[0]) {
        strncpy(station_id, g_config.default_station, sizeof(station_id) - 1);
    }
    
    char path[512];
    snprintf(path, sizeof(path), "/api/v1/weather/hourly?");
    
    int first_param = 1;
    if (station_id[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "%sstation_id=%s", first_param ? "" : "&", station_id);
        first_param = 0;
    }
    if (hour[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "%shour=%s", first_param ? "" : "&", hour);
        first_param = 0;
    }
    if (metric[0]) {
        snprintf(path + strlen(path), sizeof(path) - strlen(path),
                 "%smetric=%s", first_param ? "" : "&", metric);
    }
    
    char response[BUFFER_SIZE];
    if (make_http_request(path, response, sizeof(response)) != 0) {
        return 3;
    }
    
    if (strcmp(g_config.output_format, "json") == 0) {
        output_json(response);
    } else if (strcmp(g_config.output_format, "csv") == 0) {
        output_csv(response);
    } else {
        output_table(response);
    }
    
    return 0;
}

/* Stations command */
static int cmd_stations(int argc, char *const argv[]) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            strncpy(g_config.output_format, argv[++i], sizeof(g_config.output_format) - 1);
        } else if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            strncpy(g_config.api_endpoint, argv[++i], sizeof(g_config.api_endpoint) - 1);
        }
    }
    
    char response[BUFFER_SIZE];
    if (make_http_request("/api/v1/stations", response, sizeof(response)) != 0) {
        return 3;
    }
    
    if (strcmp(g_config.output_format, "json") == 0) {
        output_json(response);
    } else if (strcmp(g_config.output_format, "csv") == 0) {
        output_csv(response);
    } else {
        output_table(response);
    }
    
    return 0;
}

/* Health command */
static int cmd_health(int argc, char *const argv[]) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            strncpy(g_config.api_endpoint, argv[++i], sizeof(g_config.api_endpoint) - 1);
        }
    }
    
    char response[BUFFER_SIZE];
    if (make_http_request("/health", response, sizeof(response)) != 0) {
        printf("API is not responding\n");
        return 3;
    }
    
    char status[32];
    parse_json_value(response, "status", status, sizeof(status));
    char version[32];
    parse_json_value(response, "version", version, sizeof(version));
    
    printf("API Status: %s\n", status);
    printf("API Version: %s\n", version);
    printf("API Endpoint: %s\n", g_config.api_endpoint);
    
    return 0;
}

/* Main function */
int main(int argc, char *const argv[]) {
    /* Parse global options first */
    int cmd_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            parse_config(argv[++i]);
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            strncpy(g_config.api_endpoint, argv[++i], MAX_URL_LEN - 1);
            cmd_start = i + 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            strncpy(g_config.output_format, argv[++i], sizeof(g_config.output_format) - 1);
            cmd_start = i + 1;
        }
    }
    
    /* Load default config file if exists */
    if (cmd_start == 1) {
        const char *home = getenv("HOME");
        if (home) {
            char config_path[256];
            snprintf(config_path, sizeof(config_path), "%s/%s", home, DEFAULT_CONFIG_FILE);
            parse_config(config_path);
        }
    }
    
    /* Check for command */
    if (cmd_start >= argc) {
        print_usage(argv[0]);
        return 2;
    }
    
    const char *command = argv[cmd_start];
    int cmd_argc = argc - cmd_start - 1;
    char *const *cmd_argv = argv + cmd_start + 1;
    
    /* Dispatch command */
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    } else if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0) {
        print_version();
        return 0;
    } else if (strcmp(command, "daily") == 0) {
        return cmd_daily(cmd_argc, cmd_argv);
    } else if (strcmp(command, "hourly") == 0) {
        return cmd_hourly(cmd_argc, cmd_argv);
    } else if (strcmp(command, "stations") == 0) {
        return cmd_stations(cmd_argc, cmd_argv);
    } else if (strcmp(command, "health") == 0) {
        return cmd_health(cmd_argc, cmd_argv);
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        return 2;
    }
}
