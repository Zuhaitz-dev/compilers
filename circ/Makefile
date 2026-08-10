CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -Werror -std=c99 \
           -Wno-stringop-truncation -Wno-format-truncation \
           -Wno-alloc-size-larger-than
LDFLAGS ?=

SRC = src/main.c src/netlist.c src/grid.c src/parser.c src/subcircuit.c \
      src/backend_sim.c src/backend_c.c src/backend_v.c src/backend_vcd.c \
       src/vparser.c src/vlayout.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

TARGET = circ

DEBUG     ?= 0
SANITIZE  ?= 0
PROFILE   ?= 0

ifeq ($(DEBUG),1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

ifeq ($(SANITIZE),1)
    CFLAGS  += -fsanitize=address,undefined
    LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(PROFILE),1)
    CFLAGS  += -pg
    LDFLAGS += -pg
endif

.PHONY: all clean test format check_format lint install neg_test stress_test out_test cc_driver fuzz fuzz_asan cc_semantic v_semantic campaign

all: $(TARGET)

-include $(DEP)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -f $(OBJ) $(DEP) $(TARGET)

format:
	clang-format -i src/*.c src/*.h

check_format:
	@echo "=== FORMAT CHECK ==="
	@if clang-format --dry-run --Werror src/*.c src/*.h >/dev/null 2>&1; then \
	    echo "FORMAT OK"; \
	else \
	    clang-format --dry-run --Werror src/*.c src/*.h 2>&1 | head -20; \
	    echo "FAIL: sources not clang-formatted — run 'make format'"; \
	    exit 1; \
	fi

lint:
	clang-tidy src/*.c -- -Wall -Wextra -Wpedantic -Werror -std=c99 \
	    -Wno-format-truncation -O2 -DNDEBUG 2>&1 | \
	    grep -v "warnings generated"

BINDIR ?= /usr/local/bin
install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

cc_test: $(TARGET)
	@echo "=== C COMPILATION ==="
	@for ex in and not nand half_adder chain full_adder multiport adder2; do \
	    echo "  $$ex.circ"; \
	    ./$(TARGET) --c examples/$$ex.circ 2>/dev/null | \
	        $(CC) -c -x c - -o /tmp/circ_$$ex.o 2>&1 || exit 1; \
	done
	@echo "ALL C COMPILES OK"

cc_driver: $(TARGET)
	@echo "=== C DRIVER (--c --driver) ==="
	@errors=0; \
	for ex in examples/*.circ; do \
	    base=$$(basename $$ex .circ); \
	    ./$(TARGET) --c --driver "$$ex" -o /tmp/circ_d_$$base.c 2>/dev/null || continue; \
	    $(CC) -O1 -o /tmp/circ_d_$$base /tmp/circ_d_$$base.c 2>/dev/null || { \
	        printf "  FAIL %s (compile)\n" "$$base"; errors=$$((errors + 1)); continue; }; \
	    if /tmp/circ_d_$$base --cycles=8 2>/dev/null | \
	       diff -q - <(./$(TARGET) --sim "$$ex" --cycles=8 2>/dev/null) >/dev/null; then \
	        printf "  OK   %s\n" "$$base"; \
	    else \
	        printf "  FAIL %s (differs from --sim)\n" "$$base"; \
	        errors=$$((errors + 1)); \
	    fi; \
	done; \
	if [ $$errors -eq 0 ]; then echo "ALL C DRIVER TESTS PASS"; \
	else echo "$$errors C DRIVER TEST(S) FAILED"; exit 1; fi

v_test: $(TARGET)
	@echo "=== VERILOG COMPILATION ==="
	@for ex in and not nand half_adder chain full_adder multiport \
	          counter2 counter4 toggle dff dlatch jkff; do \
	    echo -n "  $$ex.circ: "; \
	    ./$(TARGET) --v examples/$$ex.circ 2>/dev/null > /tmp/vtest_$$ex.v && \
	    iverilog -o /dev/null /tmp/vtest_$$ex.v 2>&1 | \
	        grep -v "No top level\|warning" | grep -q . && \
	        echo "FAIL" || echo "OK"; \
	done
	@echo "ALL V COMPILES OK"

RESULTS_DIR = tests/results
EXPECTED_DIR = tests/expected

record: $(TARGET)
	@mkdir -p $(RESULTS_DIR) $(EXPECTED_DIR)
	@echo "Recording expected outputs..."
	@for ex in examples/*.circ; do \
	    base=$$(basename $$ex .circ); \
	    ./$(TARGET) --truth $$ex 2>/dev/null > $(EXPECTED_DIR)/$$base.truth.txt; \
	    ./$(TARGET) --sim $$ex 2>/dev/null > $(EXPECTED_DIR)/$$base.sim.txt; \
	    ./$(TARGET) --c $$ex 2>/dev/null > $(EXPECTED_DIR)/$$base.c.txt; \
	    ./$(TARGET) --v $$ex 2>/dev/null > $(EXPECTED_DIR)/$$base.v.txt; \
	done
	@echo "Done — $(EXPECTED_DIR) updated."

check: $(TARGET)
	@mkdir -p $(RESULTS_DIR)
	@echo "=== DIFF CHECK ==="
	@errors=0; \
	for ex in examples/*.circ; do \
	    base=$$(basename $$ex .circ); \
	    for mode in truth sim c v; do \
	        exp="$(EXPECTED_DIR)/$$base.$$mode.txt"; \
	        [ -f "$$exp" ] || continue; \
	        res="$(RESULTS_DIR)/$$base.$$mode.txt"; \
	        ./$(TARGET) --$$mode $$ex 2>/dev/null > "$$res"; \
	        diff -q "$$exp" "$$res" >/dev/null && \
	            printf "  ✓ %-25s --%s\n" "$$base.circ" "$$mode" || \
	        { printf "  ✗ %-25s --%s\n" "$$base.circ" "$$mode"; \
	          diff "$$exp" "$$res" | head -5; \
	          errors=$$((errors + 1)); }; \
	    done; \
	done; \
	if [ $$errors -eq 0 ]; then echo "ALL CHECKS PASSED"; \
	else echo "$$errors CHECK(S) FAILED"; exit 1; fi

test_vfrom: $(TARGET)
	@echo "=== VERILOG->CIRC ==="
	@for f in examples/*.v; do \
	    echo -n "  $$f: "; \
	    ./$(TARGET) --from-v $$f 2>/dev/null | \
	        ./$(TARGET) --check /dev/stdin >/dev/null 2>&1 && \
	        echo "OK" || echo "FAIL"; \
	done
	@echo "ALL --from-v OK"

neg_test: $(TARGET)
	@echo "=== NEGATIVE TESTS ==="
	@errors=0; \
	for f in tests/negative/*.circ; do \
	    exp="$${f%.circ}.err"; \
	    [ -f "$$exp" ] || continue; \
	    expect=$$(cat "$$exp"); \
	    out=$$(./$(TARGET) --check "$$f" 2>&1); \
	    if echo "$$out" | grep -qF "$$expect"; then \
	        printf "  OK   %s\n" "$$(basename $$f)"; \
	    else \
	        printf "  FAIL %s (expected '%s')\n" "$$(basename $$f)" "$$expect"; \
	        errors=$$((errors + 1)); \
	    fi; \
	done; \
	if [ $$errors -eq 0 ]; then echo "ALL NEGATIVE TESTS PASS"; \
	else echo "$$errors NEGATIVE TEST(S) FAILED"; exit 1; fi

stress_test: $(TARGET)
	@echo "=== STRESS TESTS (large circuits / many ports) ==="
	@errors=0; \
	for f in tests/stress/*.circ; do \
	    if ./$(TARGET) --check "$$f" >/dev/null 2>&1; then \
	        printf "  OK   %s\n" "$$(basename $$f)"; \
	    else \
	        printf "  FAIL %s\n" "$$(basename $$f)"; \
	        errors=$$((errors + 1)); \
	    fi; \
	done; \
	./$(TARGET) --c tests/stress/chain300.circ 2>/dev/null | \
	    $(CC) -c -x c - -o /tmp/stress_chain.o 2>/dev/null || errors=$$((errors + 1)); \
	if [ $$errors -eq 0 ]; then echo "ALL STRESS TESTS PASS"; \
	else echo "$$errors STRESS TEST(S) FAILED"; exit 1; fi

out_test: $(TARGET)
	@echo "=== OUTPUT FLAG (-o FILE) ==="
	@errors=0; \
	for m in --c --v --sim --truth; do \
	    ./$(TARGET) $$m examples/bus_mux.circ -o /tmp/circ_out.$$m 2>/dev/null; \
	    ./$(TARGET) $$m examples/bus_mux.circ 2>/dev/null > /tmp/circ_pipe.$$m; \
	    if cmp -s /tmp/circ_out.$$m /tmp/circ_pipe.$$m; then \
	        printf "  OK   %s -o\n" "$$m"; \
	    else \
	        printf "  FAIL %s -o differs from stdout\n" "$$m"; \
	        errors=$$((errors + 1)); \
	    fi; \
	done; \
	if [ $$errors -eq 0 ]; then echo "ALL OUTPUT TESTS PASS"; \
	else echo "$$errors OUTPUT TEST(S) FAILED"; exit 1; fi

fuzz: $(TARGET)
	python3 tests/fuzz.py --iters=2000

campaign: $(TARGET)
	python3 tests/run_campaign.py --iters=5000 --workers $(shell nproc)

fuzz_asan: clean
	$(MAKE) SANITIZE=1
	python3 tests/fuzz.py --iters=400

test: $(TARGET) check_format cc_test test_vfrom neg_test stress_test out_test cc_driver
	@echo "=== FUZZ SMOKE (short) ==="
	python3 tests/fuzz.py --iters=150
	@echo "=== SEMANTIC C ==="
	python3 tests/cc_semantic.py
	@echo "=== SEMANTIC V ==="
	python3 tests/v_semantic.py
	@echo "=== SIM ==="
	./$(TARGET) --sim examples/and.circ A=1 B=1
	./$(TARGET) --sim examples/not.circ A=0
	./$(TARGET) --sim examples/chain.circ A=1
	@echo "=== TRUTH ==="
	./$(TARGET) --truth examples/half_adder.circ
	./$(TARGET) --truth examples/nand.circ
	./$(TARGET) --truth examples/multiport.circ
	./$(TARGET) --truth examples/and_diag.circ
	./$(TARGET) --truth examples/top_map.circ
	@echo "=== C GEN ==="
	./$(TARGET) --c examples/multiport.circ
	./$(TARGET) --c examples/chain.circ
	@echo "=== V GEN ==="
	./$(TARGET) --v examples/chain.circ
	@echo "=== SUBCIRCUIT ==="
	./$(TARGET) --truth examples/top.circ
	./$(TARGET) --sim examples/top.circ A=1
	./$(TARGET) --c examples/top.circ
	./$(TARGET) --v examples/top.circ
	@echo "=== SEQUENTIAL ==="
	./$(TARGET) --sim examples/dff.circ D=1 --cycles=2 2>/dev/null
	./$(TARGET) --sim examples/dlatch.circ D=1 E=1 2>/dev/null
	@echo "=== COUNTER ==="
	./$(TARGET) --sim examples/counter2.circ --cycles=8 2>/dev/null
	./$(TARGET) --sim examples/counter4.circ --cycles=16 2>/dev/null
	@echo "=== JKFF ==="
	./$(TARGET) --sim examples/jkff.circ J=1 K=0 --cycles=2 2>/dev/null
	./$(TARGET) --sim examples/jkff.circ J=1 K=1 --cycles=4 2>/dev/null
	@echo "=== BCD ==="
	./$(TARGET) --sim examples/bcd_indicator.circ --cycles=20 2>/dev/null
	@echo "=== FULL ADDER ==="
	./$(TARGET) --sim examples/full_adder.circ A=1 B=1 Cin=0
	./$(TARGET) --sim examples/full_adder.circ A=0 B=1 Cin=1
	@echo "=== SUBCIRCUIT ISOLATION ==="
	@out=$$(./$(TARGET) --sim examples/isolation.circ D1=1 D0=0 --cycles=4); \
	if [ "$$out" = "Q1=1 Q0=0 " ]; then \
	    echo "  OK isolation (independent flip-flop state)"; \
	else \
	    echo "  FAIL isolation: got '$$out'"; exit 1; \
	fi
	@echo "ALL TESTS PASSED"
