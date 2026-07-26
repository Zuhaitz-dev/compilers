CC = gcc
CFLAGS = -Wall -Wextra -O2
NASM = nasm
NASMFLAGS = -f elf64

# Binary names
COMPILER = bc
OBJ_DIR = obj
LIB_OBJ = $(OBJ_DIR)/libb.o
LIB_RAYB_OBJ = $(OBJ_DIR)/librayb.o

# Paths
SRC_DIR = src
EXAMPLES_DIR = examples

# Raylib settings
RAYLIB_FLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

all: $(COMPILER) $(LIB_OBJ)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(COMPILER): $(SRC_DIR)/compiler.c
	$(CC) $(CFLAGS) $< -o $@

$(LIB_OBJ): $(SRC_DIR)/libb.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_RAYB_OBJ): $(SRC_DIR)/librayb.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to compile any B file to an executable.
# It automatically detects if Raylib integration is needed by checking for 'b_InitWindow'.
%: %.b $(COMPILER) $(LIB_OBJ) | $(OBJ_DIR)
	@echo "Compiling B source: $<"
	./$(COMPILER) $< > $(OBJ_DIR)/$(notdir $*).asm
	$(NASM) $(NASMFLAGS) $(OBJ_DIR)/$(notdir $*).asm -o $(OBJ_DIR)/$(notdir $*).o
	@if grep -q "b_InitWindow" $<; then \
		echo "Raylib detected, linking with Raylib integration..."; \
		make $(LIB_RAYB_OBJ); \
		$(CC) -no-pie -nostartfiles $(OBJ_DIR)/$(notdir $*).o $(LIB_OBJ) $(LIB_RAYB_OBJ) $(RAYLIB_FLAGS) -o $*; \
	else \
		echo "Linking with core B runtime..."; \
		$(CC) -no-pie -nostartfiles $(OBJ_DIR)/$(notdir $*).o $(LIB_OBJ) -o $*; \
	fi

clean:
	rm -f $(COMPILER)
	rm -rf $(OBJ_DIR)
	find $(EXAMPLES_DIR) -type f -executable -delete
	rm -f test_raylib

.PHONY: all clean
