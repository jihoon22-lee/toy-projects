#include "tracelens/core.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace tracelens;
#define REQUIRE(x)                                                                                 \
  do {                                                                                             \
    if (!(x))                                                                                      \
      throw std::runtime_error(std::string("failed: ") + #x + " at " + std::to_string(__LINE__));  \
  } while (0)
void write(const std::string &p, const std::string &b) {
  std::ofstream f(p, std::ios::binary);
  f << b;
}
int main(int argc, char **argv) {
  try {
    REQUIRE(argc == 2);
    std::string base = argv[1];
    auto r = analyze({base + "/mixed.strace"});
    REQUIRE(r.calls == 7);
    REQUIRE(!r.partial);
    REQUIRE(r.syscalls.at("read").total_ns == 20000);
    REQUIRE(r.missing_duration == 1);
    REQUIRE(r.errors.at("ENOENT").count == 1);
    REQUIRE(r.paths.at("/tmp/missing").errors == 1);
    REQUIRE(r.paths.count("hello") == 0);
    REQUIRE(r.processes.at("101:0").parent == "100:0");
    REQUIRE(r.processes.at("101:0").relation == "thread");
    REQUIRE(r.processes.at("102:1").calls == 1);
    bool joined = false;
    for (auto &e : r.retained)
      if (e.syscall == "read") {
        REQUIRE(e.start.line == 3);
        REQUIRE(e.end->line == 5);
        REQUIRE(e.timestamp == "1700000000.000060");
        REQUIRE(source_excerpt(r.sources[0], e.start).find("unfinished") != std::string::npos);
        joined = true;
      }
    REQUIRE(joined);
    auto split = analyze({base + "/split.201", base + "/split.202"});
    REQUIRE(split.calls == 3);
    REQUIRE(split.processes.at("202:0").parent == "201:0");
    REQUIRE(split.missing_duration == 1);
    auto reversed = analyze({base + "/split.202", base + "/split.201"});
    REQUIRE(reversed.processes.at("202:0").parent == "201:0");
    REQUIRE(!reversed.processes.contains("202:1"));
    auto partial = analyze({base + "/partial.strace"});
    REQUIRE(partial.partial);
    REQUIRE(partial.incomplete_calls == 1);
    REQUIRE(partial.unknown == 2);
    auto temp =
        (std::filesystem::temp_directory_path() / ("tracelens-core-" + std::to_string(getpid())))
            .string();
    std::filesystem::create_directory(temp);
    auto f = temp + "/input";
    write(f, "abc");
    REQUIRE(file_digest(f) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    write(f, "open(\"a\\377\", O_RDONLY) = 3 <0.000001>\nwrite(1, \"x\\\"(x)\", 6) = 6\n");
    auto bytes = analyze({f});
    REQUIRE(bytes.calls == 2);
    REQUIRE(bytes.paths.count(std::string("a") + char(255)) == 1);
    auto saved = bytes.sources[0];
    write(f, "changed\n");
    bool rejected = false;
    try {
      source_excerpt(saved, bytes.retained[0].start);
    } catch (...) {
      rejected = true;
    }
    REQUIRE(rejected);
    write(f, "open(\"a\", 0) = 3\nopen(\"b\", 0) = 4\nopen(\"c\", 0) = 5\n");
    Limits lim;
    lim.paths = 1;
    lim.retained = 1;
    lim.slow = 1;
    auto bounded = analyze({f}, lim);
    REQUIRE(bounded.partial);
    REQUIRE(bounded.paths.at("<overflow>").count == 2);
    REQUIRE(bounded.retained.size() == 1);
    lim.events = 1;
    auto budget = analyze({f}, lim);
    REQUIRE(budget.calls == 1);
    REQUIRE(budget.sources[0].scanned_bytes < budget.sources[0].size);
    lim = Limits{};
    lim.line_bytes = 8;
    auto longline = analyze({f}, lim);
    REQUIRE(longline.partial);
    REQUIRE(longline.unknown == 3);
    lim = Limits{};
    lim.nesting = 2;
    write(f, "foo({x={y={z=1}}}) = 0\n");
    REQUIRE(analyze({f}, lim).unknown == 1);
    write(f, "close(1) = 0\n");
    lim = Limits{};
    lim.events = 1;
    REQUIRE(!analyze({f}, lim).partial);
    write(f, "1 read(3, <unfinished ...>\n2 read(3, <unfinished ...>\n");
    auto pending_budget = analyze({f}, lim);
    REQUIRE(pending_budget.events == 1);
    REQUIRE(pending_budget.partial);
    write(f, "close(1) = 0");
    REQUIRE(analyze({f}).partial);
    write(f, "5 read(3,  <unfinished ...>\n5 +++ exited with 0 +++\n5 close(3) = 0 <0.000002>\n");
    auto exits = analyze({f});
    REQUIRE(exits.incomplete_calls == 1);
    REQUIRE(exits.processes.at("5:1").calls == 1);
    std::atomic_bool cancel = true;
    REQUIRE(analyze({f}, {}, {}, &cancel).cancelled);
    // Mutation during a progress callback must mark changed sources, never silently accept them.
    write(f, std::string(70000, 'x') + "\n");
    bool mutated = false;
    auto race = analyze({f}, {}, {}, nullptr, [&](uint64_t, uint64_t) {
      if (!mutated) {
        mutated = true;
        std::ofstream append(f, std::ios::app);
        append << "extra\n";
      }
    });
    REQUIRE(race.partial);
    REQUIRE(race.sources[0].changed);
    REQUIRE(race.sources[0].scanned_bytes == 70001);
    std::filesystem::remove_all(temp);
    std::cout << "core evidence checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
