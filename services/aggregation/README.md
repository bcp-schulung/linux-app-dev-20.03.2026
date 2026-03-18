# Aggregation Service

Weather data aggregation service for the LFD401 Weather Station Microservices system.

## Overview

Aggregation Service reads raw weather data from the SQLite database (written by ingestion service), performs aggregations and calculations, and stores the results. It handles:

- Daily, weekly, and monthly aggregations
- Average, min, max calculations for each metric
- Efficient batch processing
- Signal handling (SIGTERM for graceful shutdown, SIGHUP for config reload)
- Daemon mode support

## Building

```bash
cd services/aggregation
make
```

## Running

```bash
# Run in foreground
./aggregation --config aggregation.ini

# Run as daemon
./aggregation --config aggregation.ini --daemon

# Validate configuration
./aggregation --config aggregation.ini --validate
```

## Configuration

See `aggregation.ini` for configuration options:
- `input_database`: Path to source SQLite database (from ingestion)
- `output_database`: Path to output SQLite database (aggregated data)
- `aggregation_interval_seconds`: How often to run aggregation
- `log_level`: debug, info, warn, error

## Database Schema

### Input (from ingestion service)
```sql
CREATE TABLE weather_data (
    id INTEGER PRIMARY KEY,
    station_id TEXT,
    date TEXT,
    element TEXT,
    value REAL,
    mflag TEXT,
    qflag TEXT,
    sflag TEXT
);
```

### Output (aggregated data)
```sql
CREATE TABLE daily_aggregates (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    date TEXT NOT NULL,
    metric TEXT NOT NULL,
    avg_value REAL,
    min_value REAL,
    max_value REAL,
    count INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(station_id, date, metric)
);

CREATE TABLE hourly_aggregates (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    hour TEXT NOT NULL,
    metric TEXT NOT NULL,
    avg_value REAL,
    min_value REAL,
    max_value REAL,
    count INTEGER,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(station_id, hour, metric)
);
```

## Testing

```bash
# Test with harness
../../test-harness/bin/test-harness validate --service aggregation
../../test-harness/bin/test-harness test --service aggregation
```
