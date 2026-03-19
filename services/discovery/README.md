# Discovery Service

Peer-to-peer discovery and clustering service for the LFD401 Weather Station Microservices system.

## Overview

Discovery Service enables automatic service discovery, leader election, and health monitoring across the weather station microservices cluster. It handles:

- Peer-to-peer UDP discovery
- Leader election (bully algorithm)
- Health monitoring of cluster nodes
- Service registration and discovery
- Signal handling (SIGTERM for graceful shutdown, SIGHUP for config reload)
- Daemon mode support

## Building

```bash
cd services/discovery
make
```

## Running

```bash
# Run in foreground
./discovery --config discovery.ini

# Run as daemon
./discovery --config discovery.ini --daemon

# Validate configuration
./discovery --config discovery.ini --validate
```

## Configuration

See `discovery.ini` for configuration options:
- `node_id`: Unique identifier for this node
- `bind_address`: IP address to bind to
- `discovery_port`: UDP port for peer discovery (default: 9000)
- `cluster_nodes`: List of known cluster nodes
- `election_timeout_ms`: Leader election timeout
- `heartbeat_interval_ms`: Health check interval

## Protocol

### Discovery Protocol (UDP)
```
DISCOVER: Node announces itself to the cluster
HEARTBEAT: Node reports health status
ELECTION: Leader election message
COORDINATOR: New leader announcement
```

## Testing

```bash
# Test with harness
../../test-harness/bin/test-harness validate --service discovery
../../test-harness/bin/test-harness test --service discovery

# Start multiple instances
./discovery --config discovery.ini &
./discovery --config discovery_node2.ini &
```
