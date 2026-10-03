// The shipped CLI has to be able to state its identity without being handed a
// path to scan. That short-circuit only exists in main(), which no library test
// links, so this test runs the real `diskmap` binary and reads what it prints.
// The exact number lives in main.cpp; here we assert the shape and the
// exit.

#include "check.hpp"

#include <array>
#include <cstdio>
#include <regex>
#include <string>

#include <sys/wait.h>

namespace {

struct CommandResult {
    std::string output;
    int exitCode = -1;
};

CommandResult runCommand(const std::string& command) {
    CommandResult result;
    std::FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return result;
    }
    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = ::pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    }
    return result;
}

} // namespace

class TestCliVersion : public QObject
{
    Q_OBJECT

private slots:
    void run();
};

void TestCliVersion::run() {
    const std::string binary = DISKMAP_BINARY;

    // No path argument: --version must answer anyway.
    const CommandResult version = runCommand(binary + " --version 2>&1");
    CHECK_EQ(version.exitCode, 0);
    CHECK(std::regex_match(version.output, std::regex("diskmap [0-9]+\\.[0-9]+\\.[0-9]+\n")));

}

QTEST_GUILESS_MAIN(TestCliVersion)

#include "test_cli_version.moc"
