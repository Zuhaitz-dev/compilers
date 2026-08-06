CC = gcc
CFLAGS = -Wall -Wextra -std=c23 -Iinclude -O3 -fomit-frame-pointer
SRC = main.c src/core/engine.c src/pal/linux_pal.c src/pal/3ds_pal.c
INCLUDE = include/forth.h include/pal.h
OBJ = $(SRC:.c=.o)
TARGET = forth_linux
CLANG_FORMAT ?= clang-format

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

format:
	$(CLANG_FORMAT) -style=file -i $(SRC) $(INCLUDE)

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	@tests/run_tests.sh ./$(TARGET) tests/test.fs

.PHONY: all format clean test
