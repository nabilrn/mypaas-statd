CC ?= cc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -Iinclude
BASE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=

BIN := build/mypaas-statd
SRC := src/main.c
TEST_BIN := build/test_smoke
TEST_SRC := tests/test_smoke.c

.PHONY: all clean test sanitize lint format

all: $(BIN)

build:
	mkdir -p build

$(BIN): $(SRC) | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $@

$(TEST_BIN): $(TEST_SRC) | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 $(TEST_SRC) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

sanitize: | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined $(SRC) -o build/mypaas-statd-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined $(TEST_SRC) -o build/test-smoke-sanitize
	./build/test-smoke-sanitize

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not installed"; exit 1; }
	clang-tidy $(SRC) -- $(CPPFLAGS) $(BASE_CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not installed"; exit 1; }
	clang-format -i src/*.c include/*.h tests/*.c 2>/dev/null || true

clean:
	rm -rf build
