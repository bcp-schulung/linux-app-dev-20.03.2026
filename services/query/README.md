# Query Service

Weather data query service providing HTTP REST API for the LFD401 Weather Station Microservices system.

## Overview

Query Service provides a REST API to access aggregated weather data. It handles:

- HTTP REST API endpoints
- JSON responses
- Query parameters for filtering
- Signal handling (SIGTERM for graceful shutdown, SIGHUP for config reload)
- Daemon mode support

## API Endpoints

### Health Check
```bash
GET /health
```

### Query Daily Aggregates
```bash
GET /api/v1/weather/daily?station_id={id}&date={YYYYMMDD}&metric={metric}
```

### Query Hourly Aggregates
```bash
GET /api/v1/weather/hourly?station_id={id}&hour={YYYYMMDDHH}&metric={metric}
```

### Query All Stations
```bash
GET /api/v1/stations
```

## Building

```bash
cd services/query
make
```

## Running

```bash
# Run in foreground
./query --config query.ini

# Run as daemon
./query --config query.ini --daemon

# Validate configuration
./query --config query.ini --validate
```

## Configuration

See `query.ini` for configuration options:
- `database_path`: Path to SQLite database (from aggregation)
- `bind_address`: IP address to bind to
- `port`: HTTP port (default: 8080)
- `log_level`: debug, info, warn, error

## Testing

```bash
# Test with harness
../../test-harness/bin/test-harness validate --service query
../../test-harness/bin/test-harness test --service query

# Test API manually
curl http://localhost:8080/health
curl "http://localhost:8080/api/v1/weather/daily?station_id=USW00094846"
```
