#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <sqlite3.h>
#include <curl/curl.h>

#include "common.h"
#include "logging.h"
#include "config.h"
#include "daemon.h"

#define VERSION "1.2.0"
#define DEFAULT_CONFIG_FILE "aggregation.ini"
#define DEFAULT_PID_FILE "/tmp/aggregation.pid"
#define BUFFER_SIZE 8192
#define MAX_RESPONSE_SIZE (50 * 1024 * 1024)  /* 50MB max response */
#define MAX_RETRIES 5
#define INITIAL_RETRY_DELAY_MS 1000
#define MAX_RETRY_DELAY_MS 30000
#define HTTP_TIMEOUT_SECONDS 120

typedef struct {
    char ingestion_url[256];
    char output_database[256];
    int aggregation_interval_seconds;
    int api_port;
} AggregationConfig;

typedef struct {
    AggregationConfig config;
    sqlite3 *output_db;
    Logger logger;
    DaemonState daemon;
    int server_socket;
    CURL *curl;
} AggregationState;

static AggregationState g_state = {0};

static int parse_config_handler(const char *key, const char *value, void *user_data);
static int init_output_database(const char *db_path);
static int init_curl(void);
static int fetch_and_aggregate(void);
static int http_get_with_retry(const char *url, char **response_buffer);
static size_t write_callback(const void *contents, size_t size, size_t nmemb, void *userp);
static int start_api_server(void);
static void cleanup(void);

static size_t write_callback(const void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    char **response = (char **)userp;
    size_t current_len = *response ? strlen(*response) : 0;
    char *new_response = realloc(*response, current_len + total_size + 1);
    if (!new_response) return 0;
    *response = new_response;
    memcpy(*response + current_len, contents, total_size);
    (*response)[current_len + total_size] = '\0';
    return total_size;
}

static int http_get_with_retry(const char *url, char **response_buffer) {
    int retries = 0;
    long delay_ms = INITIAL_RETRY_DELAY_MS;
    CURLcode res;
    *response_buffer = NULL;
    
    if (!g_state.curl) {
        LOG_ERROR(&g_state.logger, "CURL not initialized");
        return -1;
    }
    
    while (retries < MAX_RETRIES) {
        curl_easy_reset(g_state.curl);
        curl_easy_setopt(g_state.curl, CURLOPT_URL, url);
        curl_easy_setopt(g_state.curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(g_state.curl, CURLOPT_WRITEDATA, response_buffer);
        curl_easy_setopt(g_state.curl, CURLOPT_TIMEOUT, HTTP_TIMEOUT_SECONDS);
        curl_easy_setopt(g_state.curl, CURLOPT_CONNECTTIMEOUT, 10);
        curl_easy_setopt(g_state.curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        res = curl_easy_perform(g_state.curl);
        
        if (res == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(g_state.curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            if (http_code == 200) {
                LOG_INFO(&g_state.logger, "HTTP GET successful: %s (attempt %d/%d)", 
                        url, retries + 1, MAX_RETRIES);
                return 0;
            } else {
                LOG_WARN(&g_state.logger, "HTTP GET returned code %ld: %s", http_code, url);
                if (http_code >= 400 && http_code < 500) break;
            }
        } else {
            LOG_WARN(&g_state.logger, "HTTP GET failed: %s (attempt %d/%d) - %s", 
                    url, retries + 1, MAX_RETRIES, curl_easy_strerror(res));
        }
        
        retries++;
        if (retries < MAX_RETRIES) {
            LOG_INFO(&g_state.logger, "Retrying in %ld ms...", delay_ms);
            usleep(delay_ms * 1000);
            delay_ms = delay_ms * 2;
            if (delay_ms > MAX_RETRY_DELAY_MS) delay_ms = MAX_RETRY_DELAY_MS;
            long jitter = delay_ms / 4;
            delay_ms = delay_ms - jitter + (rand() % (2 * jitter + 1));
        }
        
        if (response_buffer) {
            free(response_buffer);
            response_buffer = NULL;
        }
    }
    
    if (response_buffer) free(response_buffer);
    LOG_ERROR(&g_state.logger, "HTTP GET failed after %d retries: %s", MAX_RETRIES, url);
    return -1;
}

static int parse_config_handler(const char *key, const char *value, void *user_data) {
    AggregationConfig *config = (AggregationConfig *)user_data;
    int log_level;
    char log_file[256];
    int daemon_mode;
    
    if (config_handle_common(key, value, &log_level, log_file, sizeof(log_file), &daemon_mode)) {
        if (strcmp(key, "log_level") == 0) {
            g_state.logger.level = (LogLevel)log_level;
        }
        return 0;
    }
    
    if (strcmp(key, "ingestion_url") == 0) {
        SAFE_STRCPY(config->ingestion_url, value, sizeof(config->ingestion_url));
    } else if (strcmp(key, "output_database") == 0) {
        SAFE_STRCPY(config->output_database, value, sizeof(config->output_database));
    } else if (strcmp(key, "aggregation_interval_seconds") == 0) {
        config->aggregation_interval_seconds = atoi(value);
    } else if (strcmp(key, "api_port") == 0) {
        config->api_port = atoi(value);
    } else {
        LOG_WARN(&g_state.logger, "Unknown config key: %s", key);
    }
    return 0;
}

static int init_output_database(const char *db_path) {
    int rc = sqlite3_open(db_path, &g_state.output_db);
    if (rc != SQLITE_OK) {
        LOG_ERROR(&g_state.logger, "Cannot open output database: %s", sqlite3_errmsg(g_state.output_db));
        return -1;
    }
    
    const char *create_daily_sql = 
        "CREATE TABLE IF NOT EXISTS daily_aggregates ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "station_id TEXT NOT NULL,"
        "date TEXT NOT NULL,"
        "metric TEXT NOT NULL,"
        "avg_value REAL,"
        "min_value REAL,"
        "max_value REAL,"
        "count INTEGER,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(station_id, date, metric)"
        ");";
    
    char *err_msg = NULL;
    rc = sqlite3_exec(g_state.output_db, create_daily_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR(&g_state.logger, "SQL error creating daily_aggregates: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    const char *create_hourly_sql = 
        "CREATE TABLE IF NOT EXISTS hourly_aggregates ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "station_id TEXT NOT NULL,"
        "hour TEXT NOT NULL,"
        "metric TEXT NOT NULL,"
        "avg_value REAL,"
        "min_value REAL,"
        "max_value REAL,"
        "count INTEGER,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(station_id, hour, metric)"
        ");";
    
    rc = sqlite3_exec(g_state.output_db, create_hourly_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_ERROR(&g_state.logger, "SQL error creating hourly_aggregates: %s", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    LOG_INFO(&g_state.logger, "Output database initialized: %s", db_path);
    return 0;
}

static int init_curl(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_state.curl = curl_easy_init();
    if (!g_state.curl) {
        LOG_ERROR(&g_state.logger, "Failed to initialize CURL");
        return -1;
    }
    LOG_INFO(&g_state.logger, "CURL initialized successfully");
    return 0;
}

static int fetch_and_aggregate(void) {
    int offset = 0;
    int limit = 10000;
    int total_records = 0;
    int records_processed = 0;
    
    LOG_INFO(&g_state.logger, "Fetching data from ingestion service with pagination...");
    
    /* Clear existing aggregates */
    sqlite3_exec(g_state.output_db, "DELETE FROM daily_aggregates;", NULL, NULL, NULL);
    sqlite3_exec(g_state.output_db, "DELETE FROM hourly_aggregates;", NULL, NULL, NULL);
    
    /* Fetch data in chunks using pagination */
    int has_more = 1;
    while (has_more) {
        char url[512];
        snprintf(url, sizeof(url), "%s?offset=%d&limit=%d", 
                 g_state.config.ingestion_url, offset, limit);
        
        char *response = NULL;
        if (http_get_with_retry(url, &response) != 0) {
            LOG_ERROR(&g_state.logger, "Failed to fetch data from ingestion service at offset %d", offset);
            return -1;
        }
        
        if (!response) {
            LOG_ERROR(&g_state.logger, "Empty response from ingestion service");
            return -1;
        }
        
        /* Parse total and count from response */
        int chunk_count = 0;
        char *total_ptr = strstr(response, "\"total\":");
        if (total_ptr) {
            sscanf(total_ptr, "\"total\":%d", &total_records);
        }
        char *count_ptr = strstr(response, "\"count\":");
        if (count_ptr) {
            sscanf(count_ptr, "\"count\":%d", &chunk_count);
        }
        
        /* Process records */
        const char *ptr = response;
        while ((ptr = strstr(ptr, "\"station_id\"")) != NULL) {
            char station_id[32] = {0};
            char date[16] = {0};
            char element[8] = {0};
            double value = 0.0;
            
            sscanf(ptr, "\"station_id\":\"%31[^\"]\"", station_id);
            
            char *date_ptr = strstr(ptr, "\"date\"");
            if (date_ptr) sscanf(date_ptr, "\"date\":\"%15[^\"]\"", date);
            
            char *elem_ptr = strstr(ptr, "\"element\"");
            if (elem_ptr) sscanf(elem_ptr, "\"element\":\"%7[^\"]\"", element);
            
            char *val_ptr = strstr(ptr, "\"value\":");
            if (val_ptr) sscanf(val_ptr, "\"value\":%lf", &value);
            
            if (station_id[0] && date[0] && element[0]) {
                char day[9] = {0};
                strncpy(day, date, 8);
                day[8] = '\0';
                
                const char *insert_sql = 
                    "INSERT INTO daily_aggregates (station_id, date, metric, avg_value, min_value, max_value, count) "
                    "VALUES (?, ?, ?, ?, ?, ?, 1) "
                    "ON CONFLICT(station_id, date, metric) DO UPDATE SET "
                    "avg_value = (daily_aggregates.avg_value * daily_aggregates.count + excluded.avg_value) / (daily_aggregates.count + 1), "
                    "min_value = MIN(daily_aggregates.min_value, excluded.min_value), "
                    "max_value = MAX(daily_aggregates.max_value, excluded.max_value), "
                    "count = daily_aggregates.count + 1;";
                
                sqlite3_stmt *stmt;
                int rc = sqlite3_prepare_v2(g_state.output_db, insert_sql, -1, &stmt, NULL);
                if (rc == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, station_id, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 2, day, -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmt, 3, element, -1, SQLITE_STATIC);
                    sqlite3_bind_double(stmt, 4, value);
                    sqlite3_bind_double(stmt, 5, value);
                    sqlite3_bind_double(stmt, 6, value);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                    records_processed++;
                }
            }
            ptr++;
        }
        
        free(response);
        
        LOG_INFO(&g_state.logger, "Processed chunk: offset=%d, count=%d, total_processed=%d", 
                 offset, chunk_count, records_processed);
        
        /* Check if there are more records */
        if (chunk_count < limit || records_processed >= total_records) {
            has_more = 0;
        } else {
            offset += limit;
        }
    }
    
    LOG_INFO(&g_state.logger, "Aggregation complete: %d records processed from %d total", 
             records_processed, total_records);
    return 0;
}

static int start_api_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_ERROR(&g_state.logger, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(g_state.config.api_port);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to bind to port %d: %s", g_state.config.api_port, strerror(errno));
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 10) < 0) {
        close(server_fd);
        return -1;
    }
    
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    g_state.server_socket = server_fd;
    LOG_INFO(&g_state.logger, "API server listening on port %d", g_state.config.api_port);
    return 0;
}

static void handle_api_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }
    buffer[bytes_read] = '\0';
    
    char method[16], path[256], protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3) {
        close(client_socket);
        return;
    }
    
    if (strcmp(method, "GET") != 0) {
        close(client_socket);
        return;
    }
    
    char response[1024];
    if (strcmp(path, "/health") == 0) {
        snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 40\r\nConnection: close\r\n\r\n"
            "{\"status\": \"healthy\", \"version\": \"%s\"}", VERSION);
    } else {
        snprintf(response, sizeof(response),
            "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: 25\r\nConnection: close\r\n\r\n"
            "{\"error\": \"Not Found\"}");
    }
    
    send(client_socket, response, strlen(response), 0);
    close(client_socket);
}

static void cleanup(void) {
    LOG_INFO(&g_state.logger, "Cleaning up...");
    
    if (g_state.curl) {
        curl_easy_cleanup(g_state.curl);
        curl_global_cleanup();
        g_state.curl = NULL;
    }
    
    if (g_state.server_socket > 0) {
        close(g_state.server_socket);
        g_state.server_socket = -1;
    }
    
    if (g_state.output_db) {
        sqlite3_close(g_state.output_db);
        g_state.output_db = NULL;
    }
    
    daemon_cleanup(&g_state.daemon);
    logger_close(&g_state.logger);
}

int main(int argc, char *const argv[]) {
    const char *config_file = DEFAULT_CONFIG_FILE;
    int validate_only = 0;
    int daemon_mode = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_file = argv[++i];
        } else if (strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "--validate") == 0) {
            validate_only = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Aggregation Service v%s\n", VERSION);
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --config <file>    Configuration file (default: %s)\n", DEFAULT_CONFIG_FILE);
            printf("  --daemon           Run as daemon\n");
            printf("  --validate         Validate config and exit\n");
            printf("  --help             Show this help\n");
            return 0;
        }
    }
    
    logger_init(&g_state.logger, "aggregation", LOG_LEVEL_INFO, NULL);
    
    strcpy(g_state.config.ingestion_url, "http://weather-station-ingestion:8080/api/v1/weather/raw");
    strcpy(g_state.config.output_database, "aggregated.db");
    g_state.config.aggregation_interval_seconds = 60;
    g_state.config.api_port = 8080;
    
    if (config_parse(config_file, parse_config_handler, &g_state.config) < 0) {
        fprintf(stderr, "Failed to parse configuration\n");
        return 1;
    }
    
    if (validate_only) {
        printf("Configuration validated successfully\n");
        printf("  Ingestion URL: %s\n", g_state.config.ingestion_url);
        printf("  Output Database: %s\n", g_state.config.output_database);
        printf("  Aggregation Interval: %d seconds\n", g_state.config.aggregation_interval_seconds);
        printf("  API Port: %d\n", g_state.config.api_port);
        return 0;
    }
    
    if (daemon_mode) {
        if (daemon_fork() != 0) {
            return 0;
        }
    }
    
    daemon_init(&g_state.daemon, &g_state.logger, DEFAULT_PID_FILE, cleanup);
    daemon_setup_signals(&g_state.daemon);
    
    if (init_output_database(g_state.config.output_database) != 0) {
        cleanup();
        return 1;
    }
    
    if (init_curl() != 0) {
        cleanup();
        return 1;
    }
    
    if (start_api_server() != 0) {
        LOG_WARN(&g_state.logger, "Failed to start API server, continuing without it");
    }
    
    LOG_INFO(&g_state.logger, "Aggregation Service v%s started", VERSION);
    LOG_INFO(&g_state.logger, "Ingestion URL: %s", g_state.config.ingestion_url);
    
    /* Write PID file for health checks */
    daemon_write_pid_file(DEFAULT_PID_FILE);
    LOG_INFO(&g_state.logger, "Output Database: %s", g_state.config.output_database);
    
    time_t last_aggregation = 0;
    fetch_and_aggregate();
    last_aggregation = time(NULL);
    
    while (!daemon_should_stop(&g_state.daemon)) {
        if (daemon_should_reload(&g_state.daemon)) {
            LOG_INFO(&g_state.logger, "Reloading configuration...");
            config_parse(config_file, parse_config_handler, &g_state.config);
        }
        
        if (g_state.server_socket > 0) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_socket = accept(g_state.server_socket, (struct sockaddr *)&client_addr, &addr_len);
            if (client_socket >= 0) {
                handle_api_request(client_socket);
            }
        }
        
        time_t now = time(NULL);
        if (now - last_aggregation >= g_state.config.aggregation_interval_seconds) {
            fetch_and_aggregate();
            last_aggregation = now;
        }
        
        usleep(10000);
    }
    
    cleanup();
    LOG_INFO(&g_state.logger, "Aggregation Service stopped");
    return 0;
}
