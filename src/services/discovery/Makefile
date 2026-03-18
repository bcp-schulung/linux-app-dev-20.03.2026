# Discovery Service Makefile
# Links with shared weather station library

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L \
         -I../shared/include
LDFLAGS = -L../shared/lib -lweather
TARGET = discovery
SRC = discovery.c

SHARED_LIB = ../shared/lib/libweather.a

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(SRC) $(SHARED_LIB)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

$(SHARED_LIB):
	$(MAKE) -C ../shared

clean:
	rm -f $(TARGET)
	rm -f *.o
	rm -f *.log
	rm -f /tmp/discovery.pid

test: all
	@echo "Running Discovery Service tests..."
	@echo "1. Testing configuration validation..."
	./$(TARGET) --validate
	@echo "2. Testing help..."
	./$(TARGET) --help

install: all
	@echo "Installing Discovery Service..."
	@mkdir -p /usr/local/bin
	@cp $(TARGET) /usr/local/bin/
	@echo "Installed to /usr/local/bin/$(TARGET)"

run: all
	./$(TARGET) --config discovery.ini

run-daemon: all
	./$(TARGET) --config discovery.ini --daemon

# Development targets
dev: CFLAGS += -g -DDEBUG
dev: all

valgrind: dev
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) --validate
