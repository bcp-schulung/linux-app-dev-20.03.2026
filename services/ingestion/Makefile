# Ingestion Service Makefile
# Links with shared weather station library

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L \
         -I../shared/include
LDFLAGS = -lsqlite3 \
          -L../shared/lib -lweather
TARGET = ingestion
SRC = ingestion.c

# Directories
DATA_DIR = data
PROCESSED_DIR = $(DATA_DIR)/processed
SHARED_LIB = ../shared/lib/libweather.a

.PHONY: all clean dirs test install

all: $(TARGET) dirs

$(TARGET): $(SRC) $(SHARED_LIB)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

$(SHARED_LIB):
	$(MAKE) -C ../shared

dirs:
	@mkdir -p $(DATA_DIR)
	@mkdir -p $(PROCESSED_DIR)

clean:
	rm -f $(TARGET)
	rm -f *.o
	rm -f *.log
	rm -f *.db
	rm -f /tmp/ingestion.pid

test: all
	@echo "Running Ingestion Service tests..."
	@echo "1. Testing configuration validation..."
	./$(TARGET) --validate
	@echo "2. Testing help..."
	./$(TARGET) --help

install: all
	@echo "Installing Ingestion Service..."
	@mkdir -p /usr/local/bin
	@cp $(TARGET) /usr/local/bin/
	@echo "Installed to /usr/local/bin/$(TARGET)"

run: all
	./$(TARGET) --config ingestion.ini

run-daemon: all
	./$(TARGET) --config ingestion.ini --daemon

# Development targets
dev: CFLAGS += -g -DDEBUG
dev: all

valgrind: dev
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) --validate
