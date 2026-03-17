#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sqlite3.h>

/* Shared library headers */
#include "common.h"
#include "logging.h"
#include "config.h"
#include "daemon.h"

#define VERSION "1.0.0"
#define DEFAULT_CONFIG_FILE "query.ini"
#define DEFAULT_PID_FILE "/tmp/query.pid"
#define BUFFER_SIZE 8192
#define MAX_REQUEST_SIZE 4096

/* Configuration structure */
typedef struct {
    char database_path[256];
    char bind_address[64];
    int port;
} QueryConfig;

/* Global state */
typedef struct {
    QueryConfig config;
    sqlite3 *db;
    int server_socket;
    Logger logger;
    DaemonState daemon;
} QueryState;

static QueryState g_state = {0};

/* HTTP response structure */
typedef struct {
    int status_code;
    const char *content_type;
    char body[BUFFER_SIZE];
} HttpResponse;

/* Forward declarations */
static int parse_config_handler(const char *key, const char *value, void *user_data);
static int init_database(const char *db_path);
static int start_server(void);
static void handle_request(int client_socket);
static void handle_health_check(int client_socket);
static void handle_daily_query(int client_socket, const char *query_string);
static void handle_hourly_query(int client_socket, const char *query_string);
static void handle_stations(int client_socket);
static void send_http_response(int client_socket, HttpResponse *response);
static const char *get_status_text(int code);
static char *url_decode(const char *str);
static void cleanup(void);

/* Configuration handler callback */
static int parse_config_handler(const char *key, const char *value, void *user_data) {
    QueryConfig *config = (QueryConfig *)user_data;
    int log_level;
    char log_file[256];
    int daemon_mode;
    
    /* Handle common configuration keys */
    if (config_handle_common(key, value, &log_level, log_file, sizeof(log_file), &daemon_mode)) {
        if (strcmp(key, "log_level") == 0) {
            g_state.logger.level = (LogLevel)log_level;
        }
        return 0;
    }
    
    /* Handle query-specific keys */
    if (strcmp(key, "database_path") == 0) {
        SAFE_STRCPY(config->database_path, value, sizeof(config->database_path));
    } else if (strcmp(key, "bind_address") == 0) {
        SAFE_STRCPY(config->bind_address, value, sizeof(config->bind_address));
    } else if (strcmp(key, "port") == 0) {
        config->port = atoi(value);
    } else {
        LOG_WARN(&g_state.logger, "Unknown config key: %s", key);
    }
    
    return 0;
}

/* Initialize SQLite database */
static int init_database(const char *db_path) {
    int rc = sqlite3_open(db_path, &g_state.db);
    if (rc != SQLITE_OK) {
        LOG_ERROR(&g_state.logger, "Cannot open database: %s", sqlite3_errmsg(g_state.db));
        return -1;
    }
    
    LOG_INFO(&g_state.logger, "Database opened: %s", db_path);
    return 0;
}

/* Start HTTP server */
static int start_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR(&g_state.logger, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    /* Allow socket reuse */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to set socket options: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(g_state.config.bind_address);
    address.sin_port = htons(g_state.config.port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to bind to %s:%d: %s",
                   g_state.config.bind_address, g_state.config.port, strerror(errno));
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 10) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to listen: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    
    /* Set non-blocking */
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    g_state.server_socket = server_fd;
    
    LOG_INFO(&g_state.logger, "Server listening on %s:%d",
               g_state.config.bind_address, g_state.config.port);
    
    return 0;
}

/* Handle HTTP request */
static void handle_request(int client_socket) {
    char buffer[MAX_REQUEST_SIZE];
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    /* Parse request line */
    char method[16], path[256], protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
        HttpResponse response; response.status_code = 400; response.content_type = "application/json"; strncpy(response.body, "{\"error\": \"Bad Request\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
        send_http_response(client_socket, &response);
        close(client_socket);
        return;
    }
    
    LOG_DEBUG(&g_state.logger, "%s %s", method, path);
    
    /* Only handle GET requests */
    if (strcmp(method, "GET") != 0) {
        HttpResponse response; response.status_code = 405; response.content_type = "application/json"; strncpy(response.body, "{\"error\": \"Method Not Allowed\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
        send_http_response(client_socket, &response);
        close(client_socket);
        return;
    }
    
    /* Route request */
    if (strcmp(path, "/health") == 0) {
        handle_health_check(client_socket);
    } else if (strncmp(path, "/api/v1/weather/daily", 21) == 0) {
        char *query = strchr(path, '?');
        handle_daily_query(client_socket, query ? query + 1 : NULL);
    } else if (strncmp(path, "/api/v1/weather/hourly", 22) == 0) {
        char *query = strchr(path, '?');
        handle_hourly_query(client_socket, query ? query + 1 : NULL);
    } else if (strcmp(path, "/api/v1/stations") == 0) {
        handle_stations(client_socket);
    } else {
        HttpResponse response; response.status_code = 404; response.content_type = "application/json"; strncpy(response.body, "{\"error\": \"Not Found\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
        send_http_response(client_socket, &response);
    }
    
    close(client_socket);
}

/* Handle health check */
static void handle_health_check(int client_socket) {
    HttpResponse response; response.status_code = 200; response.content_type = "application/json"; snprintf(response.body, BUFFER_SIZE, "{\"status\": \"healthy\", \"version\": \"%s\"}", VERSION);
    send_http_response(client_socket, &response);
}

/* Handle daily weather query */
static void handle_daily_query(int client_socket, const char *query_string) {
    char station_id[64] = "";
    char date[16] = "";
    char metric[16] = "";
    
    /* Parse query parameters */
    if (query_string) {
        char *query = strdup(query_string);
        char *param = strtok(query, "&");
        while (param) {
            char *eq = strchr(param, '=');
            if (eq) {
                *eq = '\0';
                char *value = url_decode(eq + 1);
                if (strcmp(param, "station_id") == 0) {
                    strncpy(station_id, value, sizeof(station_id) - 1);
                } else if (strcmp(param, "date") == 0) {
                    strncpy(date, value, sizeof(date) - 1);
                } else if (strcmp(param, "metric") == 0) {
                    strncpy(metric, value, sizeof(metric) - 1);
                }
                free(value);
            }
            param = strtok(NULL, "&");
        }
        free(query);
    }
    
    /* Build query */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT station_id, date, metric, avg_value, min_value, max_value, count "
             "FROM daily_aggregates WHERE 1=1");
    
    if (station_id[0]) {
        strcat(sql, " AND station_id = ?");
    }
    if (date[0]) {
        strcat(sql, " AND date = ?");
    }
    if (metric[0]) {
        strcat(sql, " AND metric = ?");
    }
    strcat(sql, " ORDER BY date DESC LIMIT 100");
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_state.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        HttpResponse response; response.status_code = 500; response.content_type = "application/json"; strncpy(response.body, "{\"error\": \"Database error\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
        send_http_response(client_socket, &response);
        return;
    }
    
    /* Bind parameters */
    int param_idx = 1;
    if (station_id[0]) {
        sqlite3_bind_text(stmt, param_idx++, station_id, -1, SQLITE_STATIC);
    }
    if (date[0]) {
        sqlite3_bind_text(stmt, param_idx++, date, -1, SQLITE_STATIC);
    }
    if (metric[0]) {
        sqlite3_bind_text(stmt, param_idx++, metric, -1, SQLITE_STATIC);
    }
    
    /* Build JSON response */
    char json[BUFFER_SIZE];
    int pos = snprintf(json, sizeof(json), "{\"data\": [");
    int first = 1;
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!first) {
            pos += snprintf(json + pos, sizeof(json) - pos, ",");
        }
        first = 0;
        
        const char *sid = (const char *)sqlite3_column_text(stmt, 0);
        const char *d = (const char *)sqlite3_column_text(stmt, 1);
        const char *m = (const char *)sqlite3_column_text(stmt, 2);
        double avg = sqlite3_column_double(stmt, 3);
        double min = sqlite3_column_double(stmt, 4);
        double max = sqlite3_column_double(stmt, 5);
        int count = sqlite3_column_int(stmt, 6);
        
        pos += snprintf(json + pos, sizeof(json) - pos,
                       "{\"station_id\":\"%s\",\"date\":\"%s\",\"metric\":\"%s\","
                       "\"avg\":%.2f,\"min\":%.2f,\"max\":%.2f,\"count\":%d}",
                       sid, d, m, avg, min, max, count);
    }
    
    sqlite3_finalize(stmt);
    
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    
    HttpResponse response;
    response.status_code = 200;
    response.content_type = "application/json";
    strncpy(response.body, json, BUFFER_SIZE - 1);
    response.body[BUFFER_SIZE - 1] = '\0';
    send_http_response(client_socket, &response);
}

/* Handle hourly weather query */
static void handle_hourly_query(int client_socket, const char *query_string __attribute__((unused))) {
    /* Similar to daily query but for hourly_aggregates table */
    HttpResponse response; response.status_code = 200; response.content_type = "application/json"; strncpy(response.body, "{\"data\": [], \"note\": \"Hourly data not yet implemented\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
    send_http_response(client_socket, &response);
}

/* Handle stations list */
static void handle_stations(int client_socket) {
    const char *sql = "SELECT DISTINCT station_id FROM daily_aggregates ORDER BY station_id LIMIT 100";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_state.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        HttpResponse response; response.status_code = 500; response.content_type = "application/json"; strncpy(response.body, "{\"error\": \"Database error\"}", BUFFER_SIZE - 1); response.body[BUFFER_SIZE - 1] = '\0';
        send_http_response(client_socket, &response);
        return;
    }
    
    char json[BUFFER_SIZE];
    int pos = snprintf(json, sizeof(json), "{\"stations\": [");
    int first = 1;
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (!first) {
            pos += snprintf(json + pos, sizeof(json) - pos, ",");
        }
        first = 0;
        
        const char *sid = (const char *)sqlite3_column_text(stmt, 0);
        pos += snprintf(json + pos, sizeof(json) - pos, "\"%s\"", sid);
    }
    
    sqlite3_finalize(stmt);
    
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    
    HttpResponse response;
    response.status_code = 200;
    response.content_type = "application/json";
    strncpy(response.body, json, BUFFER_SIZE - 1);
    response.body[BUFFER_SIZE - 1] = '\0';
    send_http_response(client_socket, &response);
}

/* Send HTTP response */
static void send_http_response(int client_socket, HttpResponse *response) {
    char headers[512];
    int header_len = snprintf(headers, sizeof(headers),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              response->status_code,
                              get_status_text(response->status_code),
                              response->content_type,
                              strlen(response->body));
    
    send(client_socket, headers, header_len, 0);
    send(client_socket, response->body, strlen(response->body), 0);
}

/* Get HTTP status text */
static const char *get_status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

/* URL decode */
static char *url_decode(const char *str) {
    char *decoded = malloc(strlen(str) + 1);
    char *p = decoded;
    
    while (*str) {
        if (*str == '%' && str[1] && str[2]) {
            unsigned int hex;
            sscanf(str + 1, "%2x", &hex);
            *p++ = (char)hex;
            str += 3;
        } else if (*str == '+') {
            *p++ = ' ';
            str++;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
    
    return decoded;
}

/* Cleanup function */
static void cleanup(void) {
    LOG_INFO(&g_state.logger, "Cleaning up...");
    
    if (g_state.server_socket > 0) {
        close(g_state.server_socket);
        g_state.server_socket = -1;
    }
    
    if (g_state.db) {
        sqlite3_close(g_state.db);
        g_state.db = NULL;
    }
    
    daemon_cleanup(&g_state.daemon);
    logger_close(&g_state.logger);
}

/* Main function */
int main(int argc, char *const argv[]) {
    const char *config_file = DEFAULT_CONFIG_FILE;
    int validate_only = 0;
    int daemon_mode = 0;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
        } else if (strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "--validate") == 0) {
            validate_only = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Query Service v%s\n", VERSION);
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --config <file>    Configuration file (default: %s)\n", DEFAULT_CONFIG_FILE);
            printf("  --daemon           Run as daemon\n");
            printf("  --validate         Validate config and exit\n");
            printf("  --help             Show this help\n");
            return 0;
        }
    }
    
    /* Initialize logger */
    logger_init(&g_state.logger, "query", LOG_LEVEL_INFO, NULL);
    
    /* Set default configuration */
    strcpy(g_state.config.database_path, "aggregated.db");
    strcpy(g_state.config.bind_address, "0.0.0.0");
    g_state.config.port = 8080;
    
    /* Parse configuration */
    if (config_parse(config_file, parse_config_handler, &g_state.config) < 0) {
        fprintf(stderr, "Failed to parse configuration\n");
        return 1;
    }
    
    /* Validate only mode */
    if (validate_only) {
        printf("Configuration validated successfully\n");
        printf("  Database: %s\n", g_state.config.database_path);
        printf("  Bind Address: %s\n", g_state.config.bind_address);
        printf("  Port: %d\n", g_state.config.port);
        return 0;
    }
    
    /* Daemon mode */
    if (daemon_mode) {
        if (daemon_fork() != 0) {
            return 0;
        }
    }
    
    /* Initialize daemon state */
    daemon_init(&g_state.daemon, &g_state.logger, DEFAULT_PID_FILE, cleanup);
    
    /* Setup signal handlers */
    daemon_setup_signals(&g_state.daemon);
    
    /* Initialize database */
    if (init_database(g_state.config.database_path) != 0) {
        cleanup();
        return 1;
    }
    
    /* Start server */
    if (start_server() != 0) {
        cleanup();
        return 1;
    }
    
    LOG_INFO(&g_state.logger, "Query Service v%s started", VERSION);
    
    /* Write PID file for health checks */
    daemon_write_pid_file(DEFAULT_PID_FILE);
    
    /* Main loop */
    while (!daemon_should_stop(&g_state.daemon)) {
        /* Check for config reload */
        if (daemon_should_reload(&g_state.daemon)) {
            LOG_INFO(&g_state.logger, "Reloading configuration...");
            config_parse(config_file, parse_config_handler, &g_state.config);
        }
        
        /* Accept connections */
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_socket = accept(g_state.server_socket, (struct sockaddr *)&client_addr, &addr_len);
        
        if (client_socket < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000); /* 10ms */
                continue;
            }
            LOG_ERROR(&g_state.logger, "Accept failed: %s", strerror(errno));
            continue;
        }
        
        /* Handle request */
        handle_request(client_socket);
    }
    
    cleanup();
    
    LOG_INFO(&g_state.logger, "Query Service stopped");
    
    return 0;
}
