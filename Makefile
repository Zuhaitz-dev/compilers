# Root convenience targets: run every project from the top.

.PHONY: test format lint check_format clean

test:
	@echo "=== b-c-x64 ==="
	@./b-c-x64/tests/run.sh
	@echo "=== forth-nasm-x64 ==="
	@./forth-nasm-x64/tests/run.sh
	@echo "=== forth-c-x64-3ds (Linux target) ==="
	@$(MAKE) -C forth-c-x64-3ds test
	@echo "=== circ ==="
	@$(MAKE) -C circ test
	@echo "=== pinnacle-c-pinnacle ==="
	@$(MAKE) -C pinnacle-c-pinnacle smoke
	@echo "=== pascal-cpp-x64 ==="
	@./pascal-cpp-x64/tests/run.sh

format:
	clang-format -i \
	    b-c-x64/src/*.c \
	    forth-c-x64-3ds/main.c forth-c-x64-3ds/include/*.h \
	    forth-c-x64-3ds/src/core/*.c forth-c-x64-3ds/src/pal/*.c \
	    circ/src/*.c circ/src/*.h \
	    pinnacle-c-pinnacle/assembler/*.c pinnacle-c-pinnacle/common/*.c \
	    pinnacle-c-pinnacle/disassembler/*.c pinnacle-c-pinnacle/simulator/*.c \
	    pinnacle-c-pinnacle/include/*.h \
	    pascal-cpp-x64/include/pascal/*/*.hpp \
	    pascal-cpp-x64/src/*/*.cpp pascal-cpp-x64/src/*.cpp

lint:
	@$(MAKE) -C circ lint

check_format:
	@./scripts/check-format.sh

clean:
	@$(MAKE) -C b-c-x64 clean
	@$(MAKE) -C forth-nasm-x64 clean
	@$(MAKE) -C forth-c-x64-3ds clean
	@$(MAKE) -C circ clean
	@$(MAKE) -C pinnacle-c-pinnacle clean
	@rm -rf pascal-cpp-x64/build
