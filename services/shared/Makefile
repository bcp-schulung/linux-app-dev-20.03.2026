# Shared Library Makefile for Weather Station Services

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -D_POSIX_C_SOURCE=200809L -I./include
LDFLAGS = 
AR = ar
ARFLAGS = rcs

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
LIB_DIR = lib

# Source files
SOURCES = $(SRC_DIR)/logging.c \
          $(SRC_DIR)/config.c \
          $(SRC_DIR)/daemon.c

# Object files
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Library name
STATIC_LIB = $(LIB_DIR)/libweather.a

.PHONY: all clean dirs

all: dirs $(STATIC_LIB)

dirs:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(LIB_DIR)

$(STATIC_LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)
	@echo "Created static library: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(LIB_DIR)

# Install library (optional)
install: all
	@echo "Installing shared library..."
	@echo "  Copy $(STATIC_LIB) to your service directories"
	@echo "  Include headers from $(INC_DIR)"

# Test compilation
test: all
	@echo "Testing library compilation..."
	@echo '#include "common.h"' > /tmp/test_compile.c
	@echo '#include "logging.h"' >> /tmp/test_compile.c
	@echo '#include "config.h"' >> /tmp/test_compile.c
	@echo '#include "daemon.h"' >> /tmp/test_compile.c
	@echo 'int main() { return 0; }' >> /tmp/test_compile.c
	$(CC) $(CFLAGS) -c /tmp/test_compile.c -o /tmp/test_compile.o
	@rm -f /tmp/test_compile.c /tmp/test_compile.o
	@echo "Library headers compile successfully!"
