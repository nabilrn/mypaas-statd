CC ?= cc
CPPFLAGS ?= -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Iinclude
BASE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

BIN := build/mypaas-statd
PROD_SRC := src/main.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c src/ipc.c
SMOKE_BIN := build/test_smoke
PHASE1_TEST_BIN := build/test_cgroup_parse
PHASE2_TEST_BIN := build/test_sampler
PHASE3_TEST_BIN := build/test_ipc

.PHONY: all clean test test-phase1 test-phase2 test-phase3 sanitize lint format verify

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(PROD_SRC) | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(PROD_SRC) $(LDFLAGS) $(LDLIBS) -o $@

$(SMOKE_BIN): tests/test_smoke.c | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 $< -o $@

$(PHASE1_TEST_BIN): tests/test_cgroup_parse.c src/cgroup_parse.c include/cgroup_parse.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_cgroup_parse.c src/cgroup_parse.c -o $@

$(PHASE2_TEST_BIN): tests/test_sampler.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c \
		include/cgroup_parse.h include/cgroup_reader.h include/sampler.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_sampler.c src/cgroup_parse.c \
		src/cgroup_reader.c src/sampler.c -lm -o $@

$(PHASE3_TEST_BIN): tests/test_ipc.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c src/ipc.c \
		include/cgroup_parse.h include/cgroup_reader.h include/sampler.h include/ipc.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_ipc.c src/cgroup_parse.c \
		src/cgroup_reader.c src/sampler.c src/ipc.c -lm -o $@

test: $(SMOKE_BIN) test-phase1 test-phase2 test-phase3
	./$(SMOKE_BIN)

test-phase1: $(PHASE1_TEST_BIN)
	./$(PHASE1_TEST_BIN)

test-phase2: $(PHASE2_TEST_BIN)
	./$(PHASE2_TEST_BIN)

test-phase3: $(PHASE3_TEST_BIN)
	./$(PHASE3_TEST_BIN)

sanitize: | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(PROD_SRC) -o build/mypaas-statd-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_smoke.c -o build/test-smoke-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_cgroup_parse.c src/cgroup_parse.c \
		-o build/test-cgroup-parse-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_sampler.c src/cgroup_parse.c src/cgroup_reader.c \
		src/sampler.c -lm -o build/test-sampler-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_ipc.c src/cgroup_parse.c src/cgroup_reader.c \
		src/sampler.c src/ipc.c -lm -o build/test-ipc-sanitize
	./build/test-smoke-sanitize
	./build/test-cgroup-parse-sanitize
	./build/test-sampler-sanitize
	./build/test-ipc-sanitize

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not installed"; exit 1; }
	clang-tidy $(PROD_SRC) -- $(CPPFLAGS) $(BASE_CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not installed"; exit 1; }
	clang-format -i src/*.c include/*.h tests/*.c

verify: clean all test sanitize lint

clean:
	rm -rf build
