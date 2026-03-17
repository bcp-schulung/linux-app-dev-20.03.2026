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
#include <netdb.h>
#include <sys/time.h>

/* Shared library headers */
#include "common.h"
#include "logging.h"
#include "config.h"
#include "daemon.h"

#define VERSION "1.0.0"
#define DEFAULT_CONFIG_FILE "discovery.ini"
#define DEFAULT_PID_FILE "/tmp/discovery.pid"
#define BUFFER_SIZE 4096
#define MAX_NODES 32
#define MAX_NODE_ID_LEN 64

/* Node states */
typedef enum {
    NODE_STATE_FOLLOWER = 0,
    NODE_STATE_CANDIDATE,
    NODE_STATE_LEADER
} NodeStateEnum;

/* Node information */
typedef struct {
    char id[MAX_NODE_ID_LEN];
    char address[64];
    int port;
    time_t last_seen;
    int is_healthy;
    int is_leader;
} NodeInfo;

/* Peer endpoint configuration for cross-namespace/cross-cluster communication */
typedef struct {
    char fqdn[256];          /* FQDN or IP address */
    int port;                /* TCP port */
    char node_id[MAX_NODE_ID_LEN]; /* Optional node ID */
    int use_tcp;             /* Use TCP instead of UDP for this peer */
} PeerEndpoint;

/* Configuration structure */
typedef struct {
    char node_id[MAX_NODE_ID_LEN];
    char bind_address[64];
    int discovery_port;
    int election_timeout_ms;
    int heartbeat_interval_ms;
    int tcp_health_port;     /* TCP port for health checks */
    NodeInfo known_nodes[MAX_NODES];
    int known_node_count;
    PeerEndpoint peer_endpoints[MAX_NODES]; /* FQDN-based peer endpoints */
    int peer_endpoint_count;
} DiscoveryConfig;

/* Global state */
typedef struct {
    DiscoveryConfig config;
    int udp_socket;
    NodeStateEnum node_state;
    time_t last_election;
    time_t last_heartbeat;
    NodeInfo cluster_nodes[MAX_NODES];
    int node_count;
    Logger logger;
    DaemonState daemon;
} DiscoveryState;

static DiscoveryState g_state = {0};

/* Message types */
#define MSG_DISCOVER "DISCOVER"
#define MSG_HEARTBEAT "HEARTBEAT"
#define MSG_ELECTION "ELECTION"
#define MSG_COORDINATOR "COORDINATOR"

/* Forward declarations */
static int parse_config_handler(const char *key, const char *value, void *user_data);
static int init_udp_socket(void);
static void send_discovery_message(void);
static void send_heartbeat(void);
static void handle_discovery_message(const char *msg, struct sockaddr_in *from);
static void start_election(void);
static void handle_election_message(const char *sender_id);
static void handle_coordinator_message(const char *leader_id);
static void check_leader_health(void);
static void cleanup(void);

/* Configuration handler callback */
static int parse_config_handler(const char *key, const char *value, void *user_data) {
    DiscoveryConfig *config = (DiscoveryConfig *)user_data;
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
    
    /* Handle discovery-specific keys */
    if (strcmp(key, "node_id") == 0) {
        SAFE_STRCPY(config->node_id, value, sizeof(config->node_id));
    } else if (strcmp(key, "bind_address") == 0) {
        SAFE_STRCPY(config->bind_address, value, sizeof(config->bind_address));
    } else if (strcmp(key, "discovery_port") == 0) {
        config->discovery_port = atoi(value);
    } else if (strcmp(key, "election_timeout_ms") == 0) {
        config->election_timeout_ms = atoi(value);
    } else if (strcmp(key, "heartbeat_interval_ms") == 0) {
        config->heartbeat_interval_ms = atoi(value);
    } else if (strcmp(key, "cluster_node") == 0 && config->known_node_count < MAX_NODES) {
        /* Parse node format: id:address:port */
        char *colon1 = strchr(value, ':');
        char *colon2 = colon1 ? strchr(colon1 + 1, ':') : NULL;
        if (colon1 && colon2) {
            *colon1 = '\0';
            *colon2 = '\0';
            SAFE_STRCPY(config->known_nodes[config->known_node_count].id, value, MAX_NODE_ID_LEN);
            SAFE_STRCPY(config->known_nodes[config->known_node_count].address, colon1 + 1, 64);
            config->known_nodes[config->known_node_count].port = atoi(colon2 + 1);
            config->known_node_count++;
        }
    } else if (strcmp(key, "peer_endpoint") == 0 && config->peer_endpoint_count < MAX_NODES) {
        /* Parse peer endpoint format: fqdn:port[:node_id] */
        /* Examples:
         *   peer_endpoint = weather-station-2-query.station2.svc.cluster.local:8080
         *   peer_endpoint = weather-station-2-query.station2.svc.cluster.local:8080:station2
         */
        char *colon1 = strchr(value, ':');
        if (colon1) {
            *colon1 = '\0';
            char *colon2 = strchr(colon1 + 1, ':');
            char *node_id = NULL;
            
            if (colon2) {
                *colon2 = '\0';
                node_id = colon2 + 1;
            }
            
            SAFE_STRCPY(config->peer_endpoints[config->peer_endpoint_count].fqdn, value, 256);
            config->peer_endpoints[config->peer_endpoint_count].port = atoi(colon1 + 1);
            config->peer_endpoints[config->peer_endpoint_count].use_tcp = 1;
            
            if (node_id && strlen(node_id) > 0) {
                SAFE_STRCPY(config->peer_endpoints[config->peer_endpoint_count].node_id, node_id, MAX_NODE_ID_LEN);
            } else {
                /* Generate node_id from FQDN if not provided */
                snprintf(config->peer_endpoints[config->peer_endpoint_count].node_id, MAX_NODE_ID_LEN,
                        "peer-%d", config->peer_endpoint_count);
            }
            
            config->peer_endpoint_count++;
            LOG_INFO(&g_state.logger, "Added peer endpoint: %s:%d (%s)",
                    value, atoi(colon1 + 1),
                    config->peer_endpoints[config->peer_endpoint_count - 1].node_id);
        }
    } else if (strcmp(key, "tcp_health_port") == 0) {
        config->tcp_health_port = atoi(value);
    } else {
        LOG_WARN(&g_state.logger, "Unknown config key: %s", key);
    }
    
    return 0;
}

/* Initialize UDP socket */
static int init_udp_socket(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERROR(&g_state.logger, "Failed to create UDP socket: %s", strerror(errno));
        return -1;
    }
    
    /* Allow socket reuse */
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to set socket options: %s", strerror(errno));
        close(sock);
        return -1;
    }
    
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(g_state.config.bind_address);
    address.sin_port = htons(g_state.config.discovery_port);
    
    if (bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(&g_state.logger, "Failed to bind to %s:%d: %s",
                   g_state.config.bind_address, g_state.config.discovery_port, strerror(errno));
        close(sock);
        return -1;
    }
    
    /* Set non-blocking */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    g_state.udp_socket = sock;
    
    LOG_INFO(&g_state.logger, "UDP socket listening on %s:%d",
               g_state.config.bind_address, g_state.config.discovery_port);
    
    return 0;
}

/* Send discovery message */
static void send_discovery_message(void) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s:%s", MSG_DISCOVER, g_state.config.node_id);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_state.config.discovery_port);
    
    /* Send to broadcast address */
    addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    sendto(g_state.udp_socket, msg, strlen(msg), 0, 
           (struct sockaddr *)&addr, sizeof(addr));
    
    /* Send to known nodes */
    for (int i = 0; i < g_state.config.known_node_count; i++) {
        addr.sin_addr.s_addr = inet_addr(g_state.config.known_nodes[i].address);
        addr.sin_port = htons(g_state.config.known_nodes[i].port);
        sendto(g_state.udp_socket, msg, strlen(msg), 0,
               (struct sockaddr *)&addr, sizeof(addr));
    }
    
    LOG_DEBUG(&g_state.logger, "Sent discovery message");
}

/* Send heartbeat */
static void send_heartbeat(void) {
    char msg[256];
    int is_leader = (g_state.node_state == NODE_STATE_LEADER) ? 1 : 0;
    snprintf(msg, sizeof(msg), "%s:%s:%d", MSG_HEARTBEAT, g_state.config.node_id, is_leader);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_state.config.discovery_port);
    
    /* Send to all known cluster nodes */
    for (int i = 0; i < g_state.node_count; i++) {
        if (strcmp(g_state.cluster_nodes[i].id, g_state.config.node_id) != 0) {
            addr.sin_addr.s_addr = inet_addr(g_state.cluster_nodes[i].address);
            addr.sin_port = htons(g_state.cluster_nodes[i].port);
            sendto(g_state.udp_socket, msg, strlen(msg), 0,
                   (struct sockaddr *)&addr, sizeof(addr));
        }
    }
    
    g_state.last_heartbeat = time(NULL);
}

/* Handle discovery message */
static void handle_discovery_message(const char *msg, struct sockaddr_in *from) {
    char node_id[MAX_NODE_ID_LEN];
    if (sscanf(msg, "DISCOVER:%63s", node_id) != 1) return;
    
    /* Don't process our own messages */
    if (strcmp(node_id, g_state.config.node_id) == 0) return;
    
    LOG_DEBUG(&g_state.logger, "Received discovery from %s", node_id);
    
    /* Add or update node */
    int found = 0;
    for (int i = 0; i < g_state.node_count; i++) {
        if (strcmp(g_state.cluster_nodes[i].id, node_id) == 0) {
            g_state.cluster_nodes[i].last_seen = time(NULL);
            g_state.cluster_nodes[i].is_healthy = 1;
            found = 1;
            break;
        }
    }
    
    if (!found && g_state.node_count < MAX_NODES) {
        SAFE_STRCPY(g_state.cluster_nodes[g_state.node_count].id, node_id, MAX_NODE_ID_LEN);
        SAFE_STRCPY(g_state.cluster_nodes[g_state.node_count].address, 
                inet_ntoa(from->sin_addr), 64);
        g_state.cluster_nodes[g_state.node_count].port = ntohs(from->sin_port);
        g_state.cluster_nodes[g_state.node_count].last_seen = time(NULL);
        g_state.cluster_nodes[g_state.node_count].is_healthy = 1;
        g_state.node_count++;
        LOG_INFO(&g_state.logger, "New node discovered: %s", node_id);
    }
}

/* Start leader election (Bully algorithm) */
static void start_election(void) {
    LOG_INFO(&g_state.logger, "Starting leader election");
    
    g_state.node_state = NODE_STATE_CANDIDATE;
    g_state.last_election = time(NULL);
    
    /* Send election message to nodes with higher IDs */
    int higher_nodes = 0;
    char msg[256];
    snprintf(msg, sizeof(msg), "%s:%s", MSG_ELECTION, g_state.config.node_id);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_state.config.discovery_port);
    
    for (int i = 0; i < g_state.node_count; i++) {
        if (strcmp(g_state.cluster_nodes[i].id, g_state.config.node_id) > 0) {
            addr.sin_addr.s_addr = inet_addr(g_state.cluster_nodes[i].address);
            addr.sin_port = htons(g_state.cluster_nodes[i].port);
            sendto(g_state.udp_socket, msg, strlen(msg), 0,
                   (struct sockaddr *)&addr, sizeof(addr));
            higher_nodes++;
        }
    }
    
    /* If no higher nodes, become leader */
    if (higher_nodes == 0) {
        g_state.node_state = NODE_STATE_LEADER;
        LOG_INFO(&g_state.logger, "No higher nodes found, becoming leader");
        
        /* Announce leadership */
        snprintf(msg, sizeof(msg), "%s:%s", MSG_COORDINATOR, g_state.config.node_id);
        addr.sin_addr.s_addr = inet_addr("255.255.255.255");
        sendto(g_state.udp_socket, msg, strlen(msg), 0,
               (struct sockaddr *)&addr, sizeof(addr));
    }
}

/* Handle election message */
static void handle_election_message(const char *sender_id) {
    LOG_DEBUG(&g_state.logger, "Received election message from %s", sender_id);
    
    /* If we have higher ID, respond and start our own election */
    if (strcmp(g_state.config.node_id, sender_id) > 0) {
        LOG_INFO(&g_state.logger, "Higher ID than %s, starting election", sender_id);
        start_election();
    }
}

/* Handle coordinator message */
static void handle_coordinator_message(const char *leader_id) {
    LOG_INFO(&g_state.logger, "New leader elected: %s", leader_id);
    
    if (strcmp(leader_id, g_state.config.node_id) == 0) {
        g_state.node_state = NODE_STATE_LEADER;
    } else {
        g_state.node_state = NODE_STATE_FOLLOWER;
        
        /* Update leader in node list */
        for (int i = 0; i < g_state.node_count; i++) {
            if (strcmp(g_state.cluster_nodes[i].id, leader_id) == 0) {
                g_state.cluster_nodes[i].is_leader = 1;
            } else {
                g_state.cluster_nodes[i].is_leader = 0;
            }
        }
    }
}

/* Check leader health */
static void check_leader_health(void) {
    if (g_state.node_state == NODE_STATE_LEADER) return;
    
    /* Find current leader */
    time_t now = time(NULL);
    int leader_found = 0;
    
    for (int i = 0; i < g_state.node_count; i++) {
        if (g_state.cluster_nodes[i].is_leader) {
            leader_found = 1;
            if (now - g_state.cluster_nodes[i].last_seen > 
                g_state.config.election_timeout_ms / 1000) {
                LOG_WARN(&g_state.logger, "Leader %s is unresponsive, starting election",
                          g_state.cluster_nodes[i].id);
                start_election();
            }
            break;
        }
    }
    
    /* If no leader found, start election */
    if (!leader_found && g_state.node_count > 0) {
        LOG_INFO(&g_state.logger, "No leader found, starting election");
        start_election();
    }
}

/* Resolve FQDN to IP address */
static int resolve_fqdn(const char *fqdn, char *ip_buffer, size_t buffer_size) {
    struct addrinfo hints, *res;
    int err;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 only for simplicity */
    hints.ai_socktype = SOCK_STREAM;
    
    err = getaddrinfo(fqdn, NULL, &hints, &res);
    if (err != 0) {
        LOG_WARN(&g_state.logger, "Failed to resolve %s: %s", fqdn, gai_strerror(err));
        return -1;
    }
    
    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    strncpy(ip_buffer, inet_ntoa(addr->sin_addr), buffer_size - 1);
    ip_buffer[buffer_size - 1] = '\0';
    
    freeaddrinfo(res);
    return 0;
}

/* Check TCP health of a peer endpoint */
static int check_tcp_health(const char *fqdn, int port) {
    char ip[64];
    if (resolve_fqdn(fqdn, ip, sizeof(ip)) != 0) {
        return -1;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    /* Set timeout */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    
    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    
    return (result == 0) ? 0 : -1;
}

/* Check health of configured peer endpoints */
static void check_peer_endpoints(void) {
    for (int i = 0; i < g_state.config.peer_endpoint_count; i++) {
        PeerEndpoint *peer = &g_state.config.peer_endpoints[i];
        int is_healthy = (check_tcp_health(peer->fqdn, peer->port) == 0);
        
        /* Check if we already know about this peer */
        int found = 0;
        for (int j = 0; j < g_state.node_count; j++) {
            if (strcmp(g_state.cluster_nodes[j].id, peer->node_id) == 0) {
                if (is_healthy) {
                    g_state.cluster_nodes[j].last_seen = time(NULL);
                    g_state.cluster_nodes[j].is_healthy = 1;
                } else {
                    g_state.cluster_nodes[j].is_healthy = 0;
                    LOG_WARN(&g_state.logger, "Peer %s (%s:%d) is unhealthy",
                            peer->node_id, peer->fqdn, peer->port);
                }
                found = 1;
                break;
            }
        }
        
        /* Add new peer if healthy */
        if (!found && is_healthy && g_state.node_count < MAX_NODES) {
            char ip[64];
            if (resolve_fqdn(peer->fqdn, ip, sizeof(ip)) == 0) {
                SAFE_STRCPY(g_state.cluster_nodes[g_state.node_count].id, peer->node_id, MAX_NODE_ID_LEN);
                SAFE_STRCPY(g_state.cluster_nodes[g_state.node_count].address, ip, 64);
                g_state.cluster_nodes[g_state.node_count].port = peer->port;
                g_state.cluster_nodes[g_state.node_count].last_seen = time(NULL);
                g_state.cluster_nodes[g_state.node_count].is_healthy = 1;
                g_state.cluster_nodes[g_state.node_count].is_leader = 0;
                g_state.node_count++;
                LOG_INFO(&g_state.logger, "Discovered peer via FQDN: %s (%s:%d)",
                        peer->node_id, peer->fqdn, peer->port);
            }
        }
    }
}

/* Send heartbeat to peer endpoints via TCP */
static void send_tcp_heartbeat_to_peers(void) {
    for (int i = 0; i < g_state.config.peer_endpoint_count; i++) {
        PeerEndpoint *peer = &g_state.config.peer_endpoints[i];
        
        char ip[64];
        if (resolve_fqdn(peer->fqdn, ip, sizeof(ip)) != 0) {
            continue;
        }
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;
        
        /* Set short timeout */
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(peer->port);
        addr.sin_addr.s_addr = inet_addr(ip);
        
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            /* Send simple heartbeat message */
            char msg[256];
            int is_leader = (g_state.node_state == NODE_STATE_LEADER) ? 1 : 0;
            snprintf(msg, sizeof(msg), "HEARTBEAT:%s:%d", g_state.config.node_id, is_leader);
            send(sock, msg, strlen(msg), 0);
        }
        
        close(sock);
    }
}

/* Cleanup function */
static void cleanup(void) {
    LOG_INFO(&g_state.logger, "Cleaning up...");
    
    if (g_state.udp_socket > 0) {
        close(g_state.udp_socket);
        g_state.udp_socket = -1;
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
            printf("Discovery Service v%s\n", VERSION);
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
    logger_init(&g_state.logger, "discovery", LOG_LEVEL_INFO, NULL);
    
    /* Set default configuration */
    snprintf(g_state.config.node_id, sizeof(g_state.config.node_id), "node-%d", getpid());
    strcpy(g_state.config.bind_address, "0.0.0.0");
    g_state.config.discovery_port = 9000;
    g_state.config.election_timeout_ms = 5000;
    g_state.config.heartbeat_interval_ms = 1000;
    
    /* Parse configuration */
    if (config_parse(config_file, parse_config_handler, &g_state.config) < 0) {
        fprintf(stderr, "Failed to parse configuration\n");
        return 1;
    }
    
    /* Validate only mode */
    if (validate_only) {
        printf("Configuration validated successfully\n");
        printf("  Node ID: %s\n", g_state.config.node_id);
        printf("  Bind Address: %s\n", g_state.config.bind_address);
        printf("  Discovery Port: %d\n", g_state.config.discovery_port);
        printf("  Known Nodes: %d\n", g_state.config.known_node_count);
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
    
    /* Initialize UDP socket */
    if (init_udp_socket() != 0) {
        cleanup();
        return 1;
    }
    
    /* Initialize state */
    g_state.node_state = NODE_STATE_FOLLOWER;
    
    LOG_INFO(&g_state.logger, "Discovery Service v%s started", VERSION);
    LOG_INFO(&g_state.logger, "Node ID: %s", g_state.config.node_id);
    
    /* Write PID file for health checks */
    daemon_write_pid_file(DEFAULT_PID_FILE);
    
    /* Send initial discovery */
    send_discovery_message();
    
    /* Main loop */
    time_t last_discovery = time(NULL);
    time_t last_health_check = time(NULL);
    
    while (!daemon_should_stop(&g_state.daemon)) {
        /* Check for config reload */
        if (daemon_should_reload(&g_state.daemon)) {
            LOG_INFO(&g_state.logger, "Reloading configuration...");
            config_parse(config_file, parse_config_handler, &g_state.config);
        }
        
        /* Receive and process messages */
        char buffer[BUFFER_SIZE];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        
        int len = recvfrom(g_state.udp_socket, buffer, sizeof(buffer) - 1, 0,
                          (struct sockaddr *)&from, &from_len);
        
        if (len > 0) {
            buffer[len] = '\0';
            
            if (strncmp(buffer, MSG_DISCOVER, strlen(MSG_DISCOVER)) == 0) {
                handle_discovery_message(buffer, &from);
            } else if (strncmp(buffer, MSG_HEARTBEAT, strlen(MSG_HEARTBEAT)) == 0) {
                /* Update node health */
                char node_id[MAX_NODE_ID_LEN];
                int is_leader;
                if (sscanf(buffer, "HEARTBEAT:%63s:%d", node_id, &is_leader) == 2) {
                    for (int i = 0; i < g_state.node_count; i++) {
                        if (strcmp(g_state.cluster_nodes[i].id, node_id) == 0) {
                            g_state.cluster_nodes[i].last_seen = time(NULL);
                            g_state.cluster_nodes[i].is_healthy = 1;
                            g_state.cluster_nodes[i].is_leader = is_leader;
                            break;
                        }
                    }
                }
            } else if (strncmp(buffer, MSG_ELECTION, strlen(MSG_ELECTION)) == 0) {
                char sender_id[MAX_NODE_ID_LEN];
                if (sscanf(buffer, "ELECTION:%63s", sender_id) == 1) {
                    handle_election_message(sender_id);
                }
            } else if (strncmp(buffer, MSG_COORDINATOR, strlen(MSG_COORDINATOR)) == 0) {
                char leader_id[MAX_NODE_ID_LEN];
                if (sscanf(buffer, "COORDINATOR:%63s", leader_id) == 1) {
                    handle_coordinator_message(leader_id);
                }
            }
        }
        
        /* Periodic tasks */
        time_t now = time(NULL);
        
        /* Send discovery periodically */
        if (now - last_discovery >= 5) {
            send_discovery_message();
            last_discovery = now;
        }
        
        /* Send heartbeat */
        if (now - g_state.last_heartbeat >= g_state.config.heartbeat_interval_ms / 1000) {
            send_heartbeat();
            /* Also send TCP heartbeat to peer endpoints */
            if (g_state.config.peer_endpoint_count > 0) {
                send_tcp_heartbeat_to_peers();
            }
        }
        
        /* Check leader health */
        if (now - last_health_check >= 2) {
            check_leader_health();
            /* Also check peer endpoints via TCP */
            if (g_state.config.peer_endpoint_count > 0) {
                check_peer_endpoints();
            }
            last_health_check = now;
        }
        
        usleep(10000); /* 10ms */
    }
    
    cleanup();
    
    LOG_INFO(&g_state.logger, "Discovery Service stopped");
    
    return 0;
}
