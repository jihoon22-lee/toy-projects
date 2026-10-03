# AbiLens has no generated build-system or third-party dependency.  Every
# variant is rooted below OUT, so instrumented builds cannot overwrite a
# release artifact.

.DEFAULT_GOAL := all

OUT ?= build
CXX ?= c++
BASE_CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CXXFLAGS ?= $(BASE_CXXFLAGS)
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

INCLUDES := -Iinclude
OUT_GUARD := tools/guard-out.sh
WITH_DWARF ?= 0
ifeq ($(WITH_DWARF),1)
CPPFLAGS += -DABILENS_HAVE_LIBDW $(shell pkg-config --cflags libdw)
LDLIBS += $(shell pkg-config --libs libdw) -lelf
endif

CORE_SOURCES := src/evidence.cpp src/dwarf.cpp src/loader.cpp src/input.cpp src/elf.cpp src/inspect.cpp src/report.cpp src/report_helpers.cpp src/report_json_parser.cpp src/report_json.cpp src/policy.cpp src/diff.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(OUT)/obj/%.o)
TEST_SOURCES := tests/test_parser.cpp tests/test_integration.cpp
TEST_OBJECTS := $(TEST_SOURCES:tests/%.cpp=$(OUT)/obj/tests/%.o)

RELEASE_BIN := $(OUT)/bin/abilens
STATIC_LIB := $(OUT)/lib/libabilens.a
SHARED_FIXTURE := $(OUT)/lib/libabilens-fixture.so
PARSER_TEST := $(OUT)/tests/test_parser
INTEGRATION_TEST := $(OUT)/tests/test_integration

.PHONY: all release test check cli-check coverage sanitize thread-sanitize clean guard-out FORCE

guard-out:
	$(OUT_GUARD) build "$(OUT)"

# Changing feature/compiler flags in an existing OUT must rebuild its objects.
FORCE:
$(OUT)/build-config: FORCE | guard-out
	@mkdir -p $(OUT)
	@printf '%s\n' '$(CXX)' '$(CPPFLAGS)' '$(CXXFLAGS)' '$(LDFLAGS)' '$(LDLIBS)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" || cp "$@.tmp" "$@"
	@rm -f "$@.tmp"

$(CORE_OBJECTS) $(TEST_OBJECTS) $(SHARED_FIXTURE): $(OUT)/build-config

all: release

release: $(RELEASE_BIN) $(STATIC_LIB) $(SHARED_FIXTURE)

$(OUT)/obj/%.o: src/%.cpp | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/tests/%.o: tests/%.cpp | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) -Isrc $(CXXFLAGS) -MMD -MP -c $< -o $@

$(STATIC_LIB): $(CORE_OBJECTS) | guard-out
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(RELEASE_BIN): src/main.cpp $(STATIC_LIB) | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(SHARED_FIXTURE): src/fixture.cpp src/fixture.map | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) -fPIC -shared $< \
	    -Wl,--version-script=src/fixture.map $(LDFLAGS) $(LDLIBS) -o $@

$(PARSER_TEST): $(OUT)/obj/tests/test_parser.o $(STATIC_LIB) | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(INTEGRATION_TEST): $(OUT)/obj/tests/test_integration.o $(STATIC_LIB) | guard-out
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

test: $(PARSER_TEST) $(INTEGRATION_TEST) $(SHARED_FIXTURE) $(RELEASE_BIN) tests/test_clean.sh | guard-out
	cd "$(OUT)/tests" && ./test_parser
	cd "$(OUT)/tests" && ./test_integration ../lib/libabilens-fixture.so ../bin/abilens
	tests/test_clean.sh
	python3 tests/test_evidence.py $(RELEASE_BIN)

cli-check: $(RELEASE_BIN) $(SHARED_FIXTURE) | guard-out
	@mkdir -p $(OUT)/reports
	$(RELEASE_BIN) inspect --json $(RELEASE_BIN) > $(OUT)/reports/abilens.json
	$(RELEASE_BIN) inspect --json $(SHARED_FIXTURE) > $(OUT)/reports/fixture.json
	$(RELEASE_BIN) diff --json $(OUT)/reports/abilens.json $(OUT)/reports/fixture.json > $(OUT)/reports/diff.json
	$(RELEASE_BIN) inspect --json $(RELEASE_BIN) > $(OUT)/reports/abilens-repeat.json
	cmp $(OUT)/reports/abilens.json $(OUT)/reports/abilens-repeat.json
	$(RELEASE_BIN) diff --json $(RELEASE_BIN) $(SHARED_FIXTURE) > $(OUT)/reports/binary-diff.json

check: release test cli-check

coverage: | guard-out
	+$(MAKE) --no-print-directory OUT=$(OUT)/coverage CXXFLAGS="$(BASE_CXXFLAGS) -O0 --coverage" LDFLAGS="--coverage" check

sanitize: | guard-out
	+ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(MAKE) --no-print-directory OUT=$(OUT)/sanitize CXXFLAGS="$(BASE_CXXFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined" check

thread-sanitize: | guard-out
	+TSAN_OPTIONS=halt_on_error=1 $(MAKE) --no-print-directory OUT=$(OUT)/thread-sanitize CXXFLAGS="$(BASE_CXXFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=thread" LDFLAGS="-fsanitize=thread" check

clean:
	$(OUT_GUARD) clean "$(OUT)"

-include $(CORE_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

PREFIX ?= /usr/local
DESTDIR ?=
.PHONY: install
install: release
	install -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/lib" "$(DESTDIR)$(PREFIX)/include/abilens" "$(DESTDIR)$(PREFIX)/share/abilens/schemas" "$(DESTDIR)$(PREFIX)/share/doc/abilens"
	install -m 755 $(RELEASE_BIN) "$(DESTDIR)$(PREFIX)/bin/abilens"
	install -m 644 $(STATIC_LIB) "$(DESTDIR)$(PREFIX)/lib/libabilens.a"
	install -m 644 include/abilens/*.hpp "$(DESTDIR)$(PREFIX)/include/abilens/"
	install -m 644 schemas/*.json "$(DESTDIR)$(PREFIX)/share/abilens/schemas/"
	install -m 644 README.md CHANGELOG.md "$(DESTDIR)$(PREFIX)/share/doc/abilens/"
