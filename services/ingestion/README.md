# Ingestion Service

Weather data ingestion service for the LFD401 Weather Station Microservices system.

## Overview

Ingestion Service reads weather data from CSV files (NOAA GHCN-Daily format) and stores it in SQLite3 database. It handles:

- CSV file ingestion with configurable polling intervals
- SQLite3 database storage with proper schema
- Signal handling (SIGTERM for graceful shutdown, SIGHUP for config reload)
- Daemon mode support
- Comprehensive logging

## Building

```bash
cd services/s1_ingestion
make
```

## Running

```bash
# Run in foreground
./s1_ingestion --config s1_ingestion.ini

# Run as daemon
./s1_ingestion --config s1_ingestion.ini --daemon

# Validate configuration
./s1_ingestion --config s1_ingestion.ini --validate
```

## Signals

- **SIGTERM**: Graceful shutdown (finish current ingestion, close database)
- **SIGHUP**: Reload configuration (re-read config file)
- **SIGINT**: Same as SIGTERM (Ctrl+C)

## Configuration

See `s1_ingestion.ini` for configuration options:
- `database_path`: Path to SQLite database
- `csv_directory`: Directory to watch for CSV files
- `poll_interval_seconds`: How often to check for new files
- `log_level`: debug, info, warn, error

## Database Schema

```sql
CREATE TABLE weather_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    date TEXT NOT NULL,
    element TEXT NOT NULL,
    value REAL,
    mflag TEXT,
    qflag TEXT,
    sflag TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_station_date ON weather_data(station_id, date);
CREATE INDEX idx_element ON weather_data(element);
```

## Testing

```bash
# Test with harness
../../test-harness/bin/test-harness validate --service s1_ingestion
../../test-harness/bin/test-harness test --service s1_ingestion
```
