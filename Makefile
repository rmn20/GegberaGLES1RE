CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Wno-unused-parameter -Isrc/include
BUILD   := build

LIB_DIRS := src/hg src/imath src/gerbera src/gerbera/hc \
            src/gerbera/api_opengl src/gerbera/egl
LIB_SRC := $(foreach dir,$(LIB_DIRS),$(wildcard $(dir)/*.c))
LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o)

TEST_NAMES := hg imath geometry core blit opengl egl symbian_surface symbian_state egl_threads
TEST_OBJ := $(addprefix $(BUILD)/tests/test_,$(addsuffix .o,$(TEST_NAMES)))
TEST_BINS := $(addprefix $(BUILD)/test_,$(TEST_NAMES))
SYMBIAN_TEST_FLAGS := -DNDEBUG -ffunction-sections -fdata-sections -Wno-attributes -idirafter symbian/include

ifeq ($(shell uname -s),Darwin)
TEST_GC_LDFLAGS := -Wl,-dead_strip
else
TEST_GC_LDFLAGS := -Wl,--gc-sections
endif

.PHONY: all test check differential differential-build differential-harness-check differential-coverage symbian-entry-check native-dispatch-check arm-jit-check library-sources win32 symbian clean
all: $(BUILD)/libgerbera.a

library-sources:
	@printf '%s\n' $(LIB_SRC)

win32:
	./demo/build_win32.sh

symbian:
	./symbian/build_symbian.sh

symbian-entry-check: differential-build
	GERBERA_DIFFERENTIAL_LIBRARY=$(DIFF_LIB) $(PYTHON) tests/test_symbian_entry.py

.PHONY: arm-division-check
arm-division-check: differential-build
	GERBERA_DIFFERENTIAL_LIBRARY=$(DIFF_LIB) $(PYTHON) tests/test_arm_division.py

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/libgerbera.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test_%: $(BUILD)/tests/test_%.o $(BUILD)/libgerbera.a
	$(CC) $(CFLAGS) $(TEST_FLAGS) $^ -lm -o $@

$(BUILD)/tests/test_egl_threads.o: EXTRA_CFLAGS := -pthread
$(BUILD)/test_egl_threads: TEST_FLAGS := -pthread

$(BUILD)/tests/test_symbian_surface.o $(BUILD)/tests/test_symbian_state.o: EXTRA_CFLAGS := $(SYMBIAN_TEST_FLAGS)
$(BUILD)/symbian/%.o: src/hg/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SYMBIAN_TEST_FLAGS) -MMD -MP -c $< -o $@

$(BUILD)/test_symbian_surface: $(BUILD)/tests/test_symbian_surface.o $(BUILD)/symbian/hgInt32.o
$(BUILD)/test_symbian_state: $(BUILD)/tests/test_symbian_state.o
$(BUILD)/test_symbian_surface $(BUILD)/test_symbian_state:
	$(CC) $(CFLAGS) $^ $(TEST_GC_LDFLAGS) -o $@

.SECONDARY: $(TEST_OBJ)
test: $(TEST_BINS)
	@set -e; for test in $(TEST_BINS); do "$$test"; done

# Optional original/source checks require Python with unicorn, capstone and pefile.
PYTHON ?= python3
DIFF_ARGS ?=
DIFF_ADAPTERS := $(wildcard tests/differential_*.c)
DIFF_LIB := $(BUILD)/differential/libgerbera_diff.so
DIFF_HARNESS_CHECKS := $(wildcard tests/test_differential_*.py)
DIFF_OBJ := $(LIB_SRC:%.c=$(BUILD)/differential/%.o) $(DIFF_ADAPTERS:%.c=$(BUILD)/differential/%.o)

$(BUILD)/differential/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

$(DIFF_LIB): $(DIFF_OBJ)
	$(CC) -shared $(CFLAGS) $^ -lm -o $@

differential-build: $(DIFF_LIB)

differential-harness-check: differential-build
	@set -e; for test in $(DIFF_HARNESS_CHECKS); do \
	    GERBERA_DIFFERENTIAL_LIBRARY=$(DIFF_LIB) $(PYTHON) $$test; \
	done

differential: differential-harness-check
	$(PYTHON) tests/differential.py --library $(DIFF_LIB) $(DIFF_ARGS)

check: test differential

# Cross-target dispatch ABI checks; requires Clang, ld.lld and Unicorn.
native-dispatch-check: differential-build
	GERBERA_DIFFERENTIAL_LIBRARY=$(DIFF_LIB) $(PYTHON) tests/test_native_dispatch.py

arm-jit-check:
	@set -e; for mode in 0 1; do \
	    dir=$(BUILD)/arm-jit-$$mode; armv5=; \
	    if [ "$$mode" = 0 ]; then armv5=--armv5; fi; \
	    $(MAKE) BUILD=$$dir CFLAGS='$(CFLAGS) -DGERBERA_ARMV6_JIT='$$mode differential-build; \
	    $(PYTHON) tests/test_arm_jit.py --library $$dir/differential/libgerbera_diff.so $$armv5; \
	done

# Optional recovered-C coverage, measured during the same binary comparisons.
COVERAGE_CC ?= clang
LLVM_PROFDATA ?= $(shell command -v llvm-profdata 2>/dev/null || xcrun --find llvm-profdata 2>/dev/null)
LLVM_COV ?= $(shell command -v llvm-cov 2>/dev/null || xcrun --find llvm-cov 2>/dev/null)
COVERAGE_BUILD = $(BUILD)/differential-coverage
COVERAGE_LIB = $(COVERAGE_BUILD)/differential/libgerbera_diff.so

differential-coverage:
	$(MAKE) BUILD=$(COVERAGE_BUILD) CC=$(COVERAGE_CC) CFLAGS='$(CFLAGS) -fprofile-instr-generate -fcoverage-mapping' differential-build
	rm -f $(COVERAGE_BUILD)/differential-*.profraw
	LLVM_PROFILE_FILE=$(COVERAGE_BUILD)/differential-%p.profraw $(PYTHON) tests/differential.py --library $(COVERAGE_LIB) $(DIFF_ARGS)
	$(LLVM_PROFDATA) merge -sparse $(COVERAGE_BUILD)/differential-*.profraw -o $(COVERAGE_BUILD)/differential.profdata
	$(LLVM_COV) report $(COVERAGE_LIB) -instr-profile=$(COVERAGE_BUILD)/differential.profdata $(LIB_SRC)

-include $(LIB_OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(DIFF_OBJ:.o=.d) $(BUILD)/symbian/hgInt32.d

clean:
	rm -rf $(BUILD)
