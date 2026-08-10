# Root convenience targets: run every project from the top.

.PHONY: test format lint check_format clean

test:
	@echo "=== b-compiler-x64 ==="
	@./b-compiler-x64/tests/run.sh
	@echo "=== forth-nasm-x64 ==="
	@./forth-nasm-x64/tests/run.sh
	@echo "=== forth-c-x64_3ds (Linux target) ==="
	@$(MAKE) -C forth-c-x64_3ds test
	@echo "=== circ ==="
	@$(MAKE) -C circ test
	@echo "=== Pinnacle ==="
	@$(MAKE) -C Pinnacle smoke

format:
	clang-format -i \
	    b-compiler-x64/src/*.c \
	    forth-c-x64_3ds/main.c forth-c-x64_3ds/include/*.h \
	    forth-c-x64_3ds/src/core/*.c forth-c-x64_3ds/src/pal/*.c \
	    circ/src/*.c circ/src/*.h \
	    Pinnacle/assembler/*.c Pinnacle/common/*.c \
	    Pinnacle/disassembler/*.c Pinnacle/simulator/*.c \
	    Pinnacle/include/*.h

lint:
	@$(MAKE) -C circ lint

check_format:
	@./scripts/check-format.sh

clean:
	@$(MAKE) -C b-compiler-x64 clean
	@$(MAKE) -C forth-nasm-x64 clean
	@$(MAKE) -C forth-c-x64_3ds clean
	@$(MAKE) -C circ clean
	@$(MAKE) -C Pinnacle clean
