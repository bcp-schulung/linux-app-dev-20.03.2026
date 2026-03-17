# Weather Station Services - Shared Library

This directory contains the shared library (`libweather.a`) used by all weather station services. It provides common functionality to reduce code duplication and improve maintainability.

## Components

### 1. Logging Subsystem (`logging.h` / `logging.c`)

Provides consistent logging across all services with:
- Multiple log levels (DEBUG, INFO, WARN, ERROR)
- Timestamped log entries
- File or stdout output
- Optional ANSI color codes for terminal output

**Usage:**
```c
#include "logging.h"

Logger logger;
logger_init(&logger, "ingestion", LOG_LEVEL_INFO, "ingestion.log");
LOG_INFO(&logger, "Service started");
LOG_ERROR(&logger, "Failed to open database: %s", error_msg);
logger_close(&logger);
```

### 2. Configuration Parser (`config.h` / `config.c`)

Parses INI-style configuration files with:
- Comment support (#)
- Whitespace trimming
- Key-value pair extraction
- Common configuration handling (log_level, log_file, daemon_mode)

**Usage:**
```c
#include "config.h"

int my_config_handler(const char *key, const char *value, void *user_data) {
    MyConfig *cfg = (MyConfig*)user_data;
    
    // Try common keys first
    if (config_handle_common(key, value, &cfg->log_level, 
                             cfg->log_file, sizeof(cfg->log_file),
                             &cfg->daemon_mode)) {
        return 0;
    }
    
    // Handle service-specific keys
    if (strcmp(key, "database_path") == 0) {
        strncpy(cfg->database_path, value, sizeof(cfg->database_path)-1);
    }
    return 0;
}

config_parse("service.ini", my_config_handler, &config);
```

### 3. Daemon Utilities (`daemon.h` / `daemon.c`)

Provides daemon functionality:
- PID file management
- Signal handling (SIGTERM, SIGINT, SIGHUP)
- Daemon forking
- Main loop management
- Cleanup handling

**Usage:**
```c
#include "daemon.h"

DaemonState state;
daemon_init(&state, &logger, "/tmp/service.pid", my_cleanup_fn);
daemon_setup_signals(&state);

if (config.daemon_mode) {
    daemon_fork();
}

daemon_write_pid_file(state.pid_file);

// Main loop
daemon_run(&state, my_loop_function, 1000);  // 1000ms interval

daemon_cleanup(&state);
```

### 4. Common Definitions (`common.h`)

Provides:
- Version constant
- Buffer size definitions
- Utility macros (ARRAY_SIZE, SAFE_STRCPY, IS_EMPTY_STR)
- Return code definitions

## Building

```bash
cd services/shared
make
```

This creates:
- `lib/libweather.a` - Static library
- `obj/*.o` - Object files

## Using in Services

### 1. Update Service Makefile

```makefile
CFLAGS += -I../shared/include
LDFLAGS += ../shared/lib/libweather.a
```

### 2. Include Headers

```c
#include "common.h"
#include "logging.h"
#include "config.h"
#include "daemon.h"
```

### 3. Link Library

```bash
gcc service.c -o service -I../shared/include ../shared/lib/libweather.a -lsqlite3
```

## Benefits

1. **Code Reduction**: ~200-250 lines removed per service (40-50% reduction)
2. **Consistency**: All services use identical logging, config, and daemon patterns
3. **Maintainability**: Bug fixes and improvements apply to all services
4. **Testing**: Shared code can be unit tested independently
5. **Documentation**: Single point of documentation for common functionality

## Migration Guide

To migrate an existing service to use the shared library:

1. **Remove duplicate code:**
   - Delete local logging functions
   - Delete local config parsing
   - Delete local daemon utilities
   - Delete PID file functions

2. **Update includes:**
   ```c
   #include "../shared/include/common.h"
   #include "../shared/include/logging.h"
   #include "../shared/include/config.h"
   #include "../shared/include/daemon.h"
   ```

3. **Replace function calls:**
   - `log_message()` → `logger_log()`
   - `parse_config()` → `config_parse()`
   - `signal_handler()` → Use `daemon_setup_signals()`
   - `write_pid_file()` → `daemon_write_pid_file()`

4. **Update Makefile:**
   Add `-I../shared/include` and `../shared/lib/libweather.a`

## Testing

```bash
make test
```

This compiles a test program that verifies all headers can be included and used together.

## API Stability

The shared library API is designed to be stable. Changes should maintain backward compatibility or be clearly documented as breaking changes.
