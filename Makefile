CC ?= cc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -Iinclude
BASE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

BIN := build/mypaas-statd
PROD_SRC := src/main.c src/cgroup_parse.c
SMOKE_BIN := build/test_smoke
PARSER_TEST_BIN := build/test_cgroup_parse

.PHONY: all clean test test-phase1 sanitize lint format verify

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(PROD_SRC) | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(PROD_SRC) $(LDFLAGS) $(LDLIBS) -o $@

$(SMOKE_BIN): tests/test_smoke.c | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 $< -o $@

$(PARSER_TEST_BIN): tests/test_cgroup_parse.c src/cgroup_parse.c include/cgroup_parse.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_cgroup_parse.c src/cgroup_parse.c -o $@

test: $(SMOKE_BIN) test-phase1
	./$(SMOKE_BIN)

test-phase1: $(PARSER_TEST_BIN)
	./$(PARSER_TEST_BIN)

sanitize: | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined $(PROD_SRC) -o build/mypaas-statd-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_smoke.c -o build/test-smoke-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined tests/test_cgroup_parse.c src/cgroup_parse.c -o build/test-cgroup-parse-sanitize
	./build/test-smoke-sanitize
	./build/test-cgroup-parse-sanitize

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not installed"; exit 1; }
	clang-tidy $(PROD_SRC) -- $(CPPFLAGS) $(BASE_CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not installed"; exit 1; }
	clang-format -i src/*.c include/*.h tests/*.c

verify: clean all test sanitize lint

clean:
	rm -rf build
