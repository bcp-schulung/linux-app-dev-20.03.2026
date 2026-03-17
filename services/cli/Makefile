# CLI Client Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = 
TARGET = cli
SRC = cli.c

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -f *.o

test: all
	@echo "Testing CLI client..."
	./$(TARGET) --help
	./$(TARGET) version

install: all
	@echo "Installing CLI client..."
	@mkdir -p /usr/local/bin
	@cp $(TARGET) /usr/local/bin/weather
	@echo "Installed to /usr/local/bin/weather"

# Development targets
dev: CFLAGS += -g -DDEBUG
dev: all
