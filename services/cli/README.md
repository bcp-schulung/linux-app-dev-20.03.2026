# Weather Station CLI Client

Command-line interface for querying weather data from the weather station microservices.

## Overview

C1 CLI Client provides a command-line interface to:
- Query daily and hourly weather aggregates
- List available weather stations
- Display results in various formats (table, JSON, CSV)
- Connect to the query service API

## Building

```bash
cd services/cli
make
```

## Usage

```bash
# Query daily weather data
./cli daily --station USW00094846 --date 20200101

# Query hourly weather data
./cli hourly --station USW00094846 --hour 2020010100

# List all stations
./cli stations

# Output as JSON
./cli daily --station USW00094846 --format json

# Query with custom API endpoint
./cli daily --station USW00094846 --api http://localhost:8080
```

## Configuration

Create a `~/.weathercli` config file:
```
api_endpoint=http://localhost:8080
output_format=table
default_station=USW00094846
```

## Exit Codes

- 0: Success
- 1: General error
- 2: Invalid arguments
- 3: API connection error
- 4: No data found
