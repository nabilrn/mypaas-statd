CC ?= cc
CPPFLAGS ?= -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Iinclude
BASE_CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wundef
CFLAGS ?= -O2
LDFLAGS ?=
LDLIBS ?=
PREFIX ?= /usr/local
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system
INSTALL ?= install
VERSION ?=
DIST_DIR ?= dist

BIN := build/mypaas-statd
PROD_SRC := src/main.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c src/proc_cgroup.c src/ipc.c src/host_metrics.c
SMOKE_BIN := build/test_smoke
PHASE1_TEST_BIN := build/test_cgroup_parse
PHASE2_TEST_BIN := build/test_sampler
PHASE3_TEST_BIN := build/test_ipc
PHASE4_TEST_BIN := build/test_proc_cgroup
PHASE4_EVICTION_BIN := build/test_eviction
PHASE6_HOST_TEST_BIN := build/test_host_metrics

.PHONY: all clean test test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-packaging test-release-package test-benchmark-harness sanitize lint format verify install package

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

$(PHASE3_TEST_BIN): tests/test_ipc.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c src/proc_cgroup.c src/ipc.c \
		include/cgroup_parse.h include/cgroup_reader.h include/sampler.h include/proc_cgroup.h include/ipc.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_ipc.c src/cgroup_parse.c \
		src/cgroup_reader.c src/sampler.c src/proc_cgroup.c src/ipc.c -lm -o $@

$(PHASE4_TEST_BIN): tests/test_proc_cgroup.c src/proc_cgroup.c include/proc_cgroup.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_proc_cgroup.c src/proc_cgroup.c -o $@

$(PHASE4_EVICTION_BIN): tests/test_eviction.c src/cgroup_parse.c src/cgroup_reader.c src/sampler.c \
		include/cgroup_parse.h include/cgroup_reader.h include/sampler.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_eviction.c src/cgroup_parse.c \
		src/cgroup_reader.c src/sampler.c -lm -o $@

$(PHASE6_HOST_TEST_BIN): tests/test_host_metrics.c src/host_metrics.c include/host_metrics.h | build
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O0 -g3 tests/test_host_metrics.c src/host_metrics.c -o $@

test: $(SMOKE_BIN) test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-packaging test-release-package test-benchmark-harness
	./$(SMOKE_BIN)

test-phase1: $(PHASE1_TEST_BIN)
	./$(PHASE1_TEST_BIN)

test-phase2: $(PHASE2_TEST_BIN)
	./$(PHASE2_TEST_BIN)

test-phase3: $(PHASE3_TEST_BIN)
	./$(PHASE3_TEST_BIN)

test-phase4: $(PHASE4_TEST_BIN) $(PHASE4_EVICTION_BIN)
	./$(PHASE4_TEST_BIN)
	./$(PHASE4_EVICTION_BIN)

test-phase5: all
	python3 tests/test_phase5_process.py

test-phase6: $(PHASE6_HOST_TEST_BIN)
	./$(PHASE6_HOST_TEST_BIN)

test-packaging: all
	bash tests/test_packaging.sh

test-release-package: all
	bash tests/test_release_packaging.sh

test-benchmark-harness:
	python3 -m py_compile benchmarks/compare.py benchmarks/test_compare.py
	python3 -m unittest benchmarks.test_compare

install: all
	$(INSTALL) -Dm0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/mypaas-statd
	$(INSTALL) -Dm0644 packaging/mypaas-statd.service $(DESTDIR)$(SYSTEMD_UNIT_DIR)/mypaas-statd.service

package: all
	@test -n "$(VERSION)" || { echo "VERSION is required, for example: make package VERSION=v0.1.0" >&2; exit 2; }
	OUT_DIR="$(DIST_DIR)" bash packaging/package-release.sh "$(VERSION)"

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
		src/sampler.c src/proc_cgroup.c src/ipc.c -lm -o build/test-ipc-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_proc_cgroup.c src/proc_cgroup.c \
		-o build/test-proc-cgroup-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_eviction.c src/cgroup_parse.c src/cgroup_reader.c \
		src/sampler.c -lm -o build/test-eviction-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined tests/test_host_metrics.c src/host_metrics.c \
		-o build/test-host-metrics-sanitize
	$(CC) $(CPPFLAGS) $(BASE_CFLAGS) -O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(PROD_SRC) -o build/mypaas-statd
	./build/test-smoke-sanitize
	./build/test-cgroup-parse-sanitize
	./build/test-sampler-sanitize
	./build/test-ipc-sanitize
	./build/test-proc-cgroup-sanitize
	./build/test-eviction-sanitize
	./build/test-host-metrics-sanitize
	python3 tests/test_phase5_process.py

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not installed"; exit 1; }
	clang-tidy $(PROD_SRC) -- $(CPPFLAGS) $(BASE_CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not installed"; exit 1; }
	clang-format -i src/*.c include/*.h tests/*.c

verify: clean all test sanitize lint

clean:
	rm -rf build "$(DIST_DIR)"
