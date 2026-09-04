# AbiLens has no generated build-system or third-party dependency.  Every
# variant is rooted below OUT, so instrumented builds cannot overwrite a
# release artifact.

OUT ?= build
CXX ?= c++
BASE_CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CXXFLAGS ?= $(BASE_CXXFLAGS)
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

INCLUDES := -Iinclude
OUT_GUARD := tools/guard-out.sh
CORE_SOURCES := src/elf.cpp src/readelf.cpp src/readelf_process.cpp src/report.cpp src/report_helpers.cpp src/report_json_parser.cpp src/report_json.cpp src/policy.cpp src/diff.cpp
CORE_OBJECTS := $(CORE_SOURCES:src/%.cpp=$(OUT)/obj/%.o)
TEST_SOURCES := tests/test_parser.cpp tests/test_integration.cpp
TEST_OBJECTS := $(TEST_SOURCES:tests/%.cpp=$(OUT)/obj/tests/%.o)

RELEASE_BIN := $(OUT)/bin/abilens
STATIC_LIB := $(OUT)/lib/libabilens.a
SHARED_FIXTURE := $(OUT)/lib/libabilens-fixture.so
PARSER_TEST := $(OUT)/tests/test_parser
INTEGRATION_TEST := $(OUT)/tests/test_integration

.PHONY: all release test check cli-check coverage sanitize thread-sanitize clean

all: release

release: $(RELEASE_BIN) $(STATIC_LIB) $(SHARED_FIXTURE)

$(OUT)/obj/%.o: src/%.cpp
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OUT)/obj/tests/%.o: tests/%.cpp
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(STATIC_LIB): $(CORE_OBJECTS)
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(RELEASE_BIN): src/main.cpp $(STATIC_LIB)
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) $< $(STATIC_LIB) $(LDFLAGS) $(LDLIBS) -o $@

$(SHARED_FIXTURE): src/fixture.cpp
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(INCLUDES) $(CXXFLAGS) -fPIC -shared $< $(LDFLAGS) $(LDLIBS) -o $@

$(PARSER_TEST): $(OUT)/obj/tests/test_parser.o $(STATIC_LIB)
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(INTEGRATION_TEST): $(OUT)/obj/tests/test_integration.o $(STATIC_LIB)
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@

test: $(PARSER_TEST) $(INTEGRATION_TEST) $(SHARED_FIXTURE) $(RELEASE_BIN) tests/test_clean.sh
	$(OUT_GUARD) build "$(OUT)"
	cd "$(OUT)/tests" && ./test_parser
	cd "$(OUT)/tests" && ./test_integration ../lib/libabilens-fixture.so ../bin/abilens
	tests/test_clean.sh

cli-check: $(RELEASE_BIN) $(SHARED_FIXTURE)
	$(OUT_GUARD) build "$(OUT)"
	@mkdir -p $(OUT)/reports
	$(RELEASE_BIN) inspect --json $(RELEASE_BIN) > $(OUT)/reports/abilens.json
	$(RELEASE_BIN) inspect --json $(SHARED_FIXTURE) > $(OUT)/reports/fixture.json
	$(RELEASE_BIN) diff --json $(OUT)/reports/abilens.json $(OUT)/reports/fixture.json > $(OUT)/reports/diff.json
	$(RELEASE_BIN) inspect --json $(RELEASE_BIN) > $(OUT)/reports/abilens-repeat.json
	cmp $(OUT)/reports/abilens.json $(OUT)/reports/abilens-repeat.json
	$(RELEASE_BIN) diff --json $(RELEASE_BIN) $(SHARED_FIXTURE) > $(OUT)/reports/binary-diff.json

check: release test cli-check

coverage:
	$(OUT_GUARD) build "$(OUT)"
	+$(MAKE) --no-print-directory OUT=$(OUT)/coverage CXXFLAGS="$(BASE_CXXFLAGS) -O0 --coverage" LDFLAGS="--coverage" check

sanitize:
	$(OUT_GUARD) build "$(OUT)"
	+ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(MAKE) --no-print-directory OUT=$(OUT)/sanitize CXXFLAGS="$(BASE_CXXFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined" check

thread-sanitize:
	$(OUT_GUARD) build "$(OUT)"
	+TSAN_OPTIONS=halt_on_error=1 $(MAKE) --no-print-directory OUT=$(OUT)/thread-sanitize CXXFLAGS="$(BASE_CXXFLAGS) -O1 -fno-omit-frame-pointer -fsanitize=thread" LDFLAGS="-fsanitize=thread" check

clean:
	$(OUT_GUARD) clean "$(OUT)"

-include $(CORE_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)
