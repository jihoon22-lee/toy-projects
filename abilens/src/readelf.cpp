#include "abilens/readelf.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>
#include <vector>

namespace abilens {
namespace {

using Clock = std::chrono::steady_clock;

void close_fd(int& fd) {
    if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
    }
}

bool make_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void kill_child(pid_t child) {
    if (child > 0) {
        (void)::kill(child, SIGKILL);
    }
}

bool append_pipe(int fd, std::string& destination, bool& truncated) {
    std::array<char, 16U * 1024U> buffer{};
    bool reached_eof = false;
    for (;;) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count > 0) {
            const std::size_t bytes = static_cast<std::size_t>(count);
            const std::size_t remaining = destination.size() < kReadelfOutputLimit
                                              ? kReadelfOutputLimit - destination.size()
                                              : 0U;
            destination.append(buffer.data(), std::min(bytes, remaining));
            if (bytes > remaining) {
                truncated = true;
                // The caller closes this descriptor after observing the bound.
                // This avoids waiting forever on an untrusted producer.
                return true;
            }
            continue;
        }
        if (count == 0) {
            reached_eof = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        truncated = true;
        return true;
    }
    return reached_eof;
}

void reap_child(pid_t child, bool& reaped, int& wait_status) {
    if (reaped) {
        return;
    }
    for (;;) {
        const pid_t result = ::waitpid(child, &wait_status, WNOHANG);
        if (result == child) {
            reaped = true;
            return;
        }
        if (result == 0) {
            return;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        reaped = true;
        wait_status = -1;
        return;
    }
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char character) { return std::isspace(character) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string bracket_value(const std::string& line) {
    const std::size_t begin = line.find('[');
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = line.find(']', begin + 1U);
    if (end == std::string::npos) {
        return {};
    }
    return line.substr(begin + 1U, end - begin - 1U);
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0U, prefix.size(), prefix) == 0;
}

bool valid_version(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    bool digit = false;
    for (const char character : value) {
        if (character == '.') {
            if (!digit) {
                return false;
            }
            digit = false;
        } else if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digit = true;
        } else {
            return false;
        }
    }
    return digit;
}

void append_version(std::vector<VersionRequirement>& values,
                    const std::string& token,
                    const std::string& library) {
    const std::array<std::string, 3U> namespaces{"GLIBCXX_", "GLIBC_", "CXXABI_"};
    for (const std::string& prefix : namespaces) {
        if (!starts_with(token, prefix)) {
            continue;
        }
        const std::string version = token.substr(prefix.size());
        if (!valid_version(version)) {
            return;
        }
        const std::string namespace_name = prefix.substr(0U, prefix.size() - 1U);
        const VersionRequirement candidate{namespace_name, version, library};
        const auto duplicate = std::find_if(
            values.begin(), values.end(), [&](const VersionRequirement& existing) {
                return existing.namespace_name == candidate.namespace_name &&
                       existing.version == candidate.version && existing.library == candidate.library;
            });
        if (duplicate == values.end()) {
            values.push_back(candidate);
        }
        return;
    }
}

std::string first_name_token(const std::string& line) {
    const std::size_t marker = line.find("Name:");
    if (marker == std::string::npos) {
        return {};
    }
    std::istringstream input(line.substr(marker + 5U));
    std::string token;
    input >> token;
    while (!token.empty() && (token.back() == ',' || token.back() == ';')) {
        token.pop_back();
    }
    return token;
}

bool has_symtab_section(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t marker = line.find(']');
        if (marker != std::string::npos && line.find(".symtab", marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

namespace {

struct ProcessEvidence {
    int return_code = -1;
    bool timed_out = false;
    bool truncated = false;
    std::string standard_output;
    std::string standard_error;
};

ProcessEvidence run_readelf_process(const std::vector<std::string>& arguments) {
    ProcessEvidence result;
    int standard_output[2] = {-1, -1};
    int standard_error[2] = {-1, -1};
    if (::pipe(standard_output) != 0 || ::pipe(standard_error) != 0) {
        close_fd(standard_output[0]);
        close_fd(standard_output[1]);
        close_fd(standard_error[0]);
        close_fd(standard_error[1]);
        result.standard_error = "could not create readelf pipes";
        return result;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        close_fd(standard_output[0]);
        close_fd(standard_output[1]);
        close_fd(standard_error[0]);
        close_fd(standard_error[1]);
        result.standard_error = "could not fork readelf";
        return result;
    }
    if (child == 0) {
        (void)::dup2(standard_output[1], STDOUT_FILENO);
        (void)::dup2(standard_error[1], STDERR_FILENO);
        close_fd(standard_output[0]);
        close_fd(standard_output[1]);
        close_fd(standard_error[0]);
        close_fd(standard_error[1]);
        (void)::setenv("LC_ALL", "C", 1);
        (void)::setenv("LANG", "C", 1);
        (void)::setenv("LANGUAGE", "C", 1);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 2U);
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        _exit(127);
    }

    close_fd(standard_output[1]);
    close_fd(standard_error[1]);
    if (!make_nonblocking(standard_output[0]) || !make_nonblocking(standard_error[0])) {
        result.standard_error = "could not bound readelf output pipes";
        kill_child(child);
        (void)::waitpid(child, nullptr, 0);
        close_fd(standard_output[0]);
        close_fd(standard_error[0]);
        return result;
    }

    bool output_eof = false;
    bool error_eof = false;
    bool reaped = false;
    int wait_status = -1;
    const auto deadline = Clock::now() + std::chrono::milliseconds(
                                        static_cast<long long>(kReadelfTimeoutSeconds * 1000.0));
    while ((!output_eof || !error_eof) || !reaped) {
        struct pollfd descriptors[2]{};
        nfds_t count = 0;
        if (standard_output[0] >= 0) {
            descriptors[count++] = pollfd{standard_output[0], POLLIN | POLLHUP, 0};
        }
        if (standard_error[0] >= 0) {
            descriptors[count++] = pollfd{standard_error[0], POLLIN | POLLHUP, 0};
        }
        if (count != 0U) {
            (void)::poll(descriptors, count, 25);
        } else {
            (void)::usleep(25000U);
        }
        if (standard_output[0] >= 0) {
            output_eof = append_pipe(standard_output[0], result.standard_output, result.truncated);
            if (output_eof) {
                close_fd(standard_output[0]);
            }
        }
        if (standard_error[0] >= 0) {
            error_eof = append_pipe(standard_error[0], result.standard_error, result.truncated);
            if (error_eof) {
                close_fd(standard_error[0]);
            }
        }
        reap_child(child, reaped, wait_status);
        if (!reaped && Clock::now() >= deadline) {
            result.timed_out = true;
            kill_child(child);
        }
        if (result.timed_out || result.truncated) {
            // No more evidence can be accepted after a timeout or bound
            // violation. Close both pipes immediately, then reap the child.
            kill_child(child);
            close_fd(standard_output[0]);
            close_fd(standard_error[0]);
            output_eof = true;
            error_eof = true;
        }
        if ((result.timed_out || result.truncated) && !reaped) {
            reap_child(child, reaped, wait_status);
        }
    }
    close_fd(standard_output[0]);
    close_fd(standard_error[0]);
    if (!reaped) {
        (void)::waitpid(child, &wait_status, 0);
    }
    if (WIFEXITED(wait_status)) {
        result.return_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        result.return_code = -WTERMSIG(wait_status);
    }
    return result;
}

std::string gnu_readelf_version(const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (!starts_with(line, "GNU readelf")) {
            return {};
        }
        std::istringstream words(line);
        std::string tool;
        std::string readelf;
        words >> tool >> readelf;
        if (tool != "GNU" || readelf != "readelf") {
            return {};
        }
        std::string token;
        while (words >> token) {
            while (!token.empty() && (token.back() == ',' || token.back() == ';')) {
                token.pop_back();
            }
            if (valid_version(token)) {
                return token;
            }
        }
        return {};
    }
    return {};
}

ReadelfCapability query_readelf_capability() {
    const ProcessEvidence evidence = run_readelf_process({"readelf", "--version"});
    if (evidence.return_code != 0 || evidence.timed_out || evidence.truncated) {
        return {};
    }
    const std::string version = gnu_readelf_version(evidence.standard_output);
    if (version.empty()) {
        return {};
    }
    return ReadelfCapability{true, "GNU readelf", version};
}

}  // namespace

ReadelfEvidence run_readelf(const std::filesystem::path& path) {
    ReadelfEvidence result;
    result.capability = query_readelf_capability();
    if (!result.capability.supported) {
        result.standard_error = "system readelf is not a supported GNU readelf";
        return result;
    }
    const ProcessEvidence evidence =
        run_readelf_process(
            {"readelf", "-h", "-S", "-d", "-V", "-W", "--", path.generic_string()});
    result.return_code = evidence.return_code;
    result.timed_out = evidence.timed_out;
    result.truncated = evidence.truncated;
    result.standard_output = evidence.standard_output;
    result.standard_error = evidence.standard_error;
    return result;
}

ElfReport parse_readelf_text(const std::string& input,
                             const ElfHeader& header,
                             const ReadelfEvidence& evidence) {
    ElfReport report;
    report.status = InputStatus::Valid;
    report.header = header;
    report.tool.name = evidence.capability.name;
    report.tool.version = evidence.capability.version;
    if (!evidence.capability.supported) {
        report.status = InputStatus::ToolError;
        report.message = "system readelf capability is not supported";
        if (!evidence.standard_error.empty()) {
            report.diagnostics.push_back(evidence.standard_error.substr(0U, 512U));
        }
        return report;
    }
    if (evidence.return_code != 0 || evidence.timed_out || evidence.truncated) {
        report.status = InputStatus::ToolError;
        report.message = evidence.timed_out
                             ? "readelf timed out"
                             : evidence.truncated ? "readelf output exceeded the safety bound"
                                                   : "readelf failed to produce complete evidence";
        if (!evidence.standard_error.empty()) {
            report.diagnostics.push_back(evidence.standard_error.substr(0U, 512U));
        }
        return report;
    }
    if (input.size() > kReadelfOutputLimit || input.find('\0') != std::string::npos) {
        report.status = InputStatus::ToolError;
        report.message = "readelf output is outside the bounded text contract";
        return report;
    }

    std::string current_library;
    std::istringstream lines(input);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("File:") != std::string::npos) {
            const std::size_t marker = line.find("File:");
            current_library = trim(line.substr(marker + 5U));
            const std::size_t whitespace = current_library.find_first_of(" \t");
            if (whitespace != std::string::npos) {
                current_library.resize(whitespace);
            }
        }
        if (line.find("(NEEDED)") != std::string::npos) {
            append_unique(report.needed, bracket_value(line));
        }
        if (line.find("(RPATH)") != std::string::npos) {
            const std::string value = bracket_value(line);
            std::istringstream paths(value);
            std::string item;
            while (std::getline(paths, item, ':')) {
                append_unique(report.rpath, item);
            }
        }
        if (line.find("(RUNPATH)") != std::string::npos) {
            const std::string value = bracket_value(line);
            std::istringstream paths(value);
            std::string item;
            while (std::getline(paths, item, ':')) {
                append_unique(report.runpath, item);
            }
        }
        if (line.find("Name:") != std::string::npos) {
            append_version(report.versions, first_name_token(line), current_library);
        }
    }
    std::sort(report.needed.begin(), report.needed.end());
    std::sort(report.rpath.begin(), report.rpath.end());
    std::sort(report.runpath.begin(), report.runpath.end());
    std::sort(report.versions.begin(), report.versions.end(), [](const VersionRequirement& left,
                                                                  const VersionRequirement& right) {
        if (left.namespace_name != right.namespace_name) {
            return left.namespace_name < right.namespace_name;
        }
        if (left.version != right.version) {
            return left.version < right.version;
        }
        return left.library < right.library;
    });
    report.stripped_known = input.find("Section Headers:") != std::string::npos;
    report.stripped = report.stripped_known && !has_symtab_section(input);
    if (!header.has_dynamic) {
        report.diagnostics.push_back("static-or-non-dynamic: no PT_DYNAMIC program header");
    }
    if (report.stripped_known) {
        report.diagnostics.push_back(report.stripped ? "stripped: no .symtab section" :
                                                         "not-stripped: .symtab section present");
    } else {
        report.diagnostics.push_back("stripped: unknown (section headers were unavailable)");
    }
    report.message = "ELF header and readelf evidence verified";
    return report;
}

}  // namespace abilens
