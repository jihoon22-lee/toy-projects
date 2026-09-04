#include "abilens/readelf.hpp"

#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
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

struct ProcessEvidence {
    int return_code = -1;
    bool timed_out = false;
    bool truncated = false;
    std::string standard_output;
    std::string standard_error;
};

struct ProcessPipes {
    std::array<int, 2> standard_output{-1, -1};
    std::array<int, 2> standard_error{-1, -1};
};

struct ProcessState {
    bool output_eof = false;
    bool error_eof = false;
    bool reaped = false;
    int wait_status = -1;
};

void close_fd(int& fd) {
    if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
    }
}

void close_pipes(ProcessPipes& pipes) {
    close_fd(pipes.standard_output[0]);
    close_fd(pipes.standard_output[1]);
    close_fd(pipes.standard_error[0]);
    close_fd(pipes.standard_error[1]);
}

bool create_pipes(ProcessPipes& pipes) {
    if (::pipe(pipes.standard_output.data()) != 0) return false;
    if (::pipe(pipes.standard_error.data()) == 0) return true;
    close_pipes(pipes);
    return false;
}

bool make_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void kill_child(pid_t child) {
    if (child > 0) (void)::kill(child, SIGKILL);
}

bool append_pipe(int fd, std::string& destination, bool& truncated) {
    std::array<char, 16U * 1024U> buffer{};
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
                return true;
            }
            continue;
        }
        if (count == 0) return true;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return false;
        truncated = true;
        return true;
    }
}

void reap_child(pid_t child, ProcessState& state) {
    if (state.reaped) return;
    for (;;) {
        const pid_t result = ::waitpid(child, &state.wait_status, WNOHANG);
        if (result == child) {
            state.reaped = true;
            return;
        }
        if (result == 0) return;
        if (result < 0 && errno == EINTR) continue;
        state.reaped = true;
        state.wait_status = -1;
        return;
    }
}

[[noreturn]] void exec_child(const std::vector<std::string>& arguments,
                             ProcessPipes pipes) {
    (void)::dup2(pipes.standard_output[1], STDOUT_FILENO);
    (void)::dup2(pipes.standard_error[1], STDERR_FILENO);
    close_pipes(pipes);
    (void)::setenv("LC_ALL", "C", 1);
    (void)::setenv("LANG", "C", 1);
    (void)::setenv("LANGUAGE", "C", 1);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(127);
}

bool configure_parent_pipes(ProcessPipes& pipes, pid_t child,
                            ProcessEvidence& result) {
    close_fd(pipes.standard_output[1]);
    close_fd(pipes.standard_error[1]);
    if (make_nonblocking(pipes.standard_output[0]) &&
        make_nonblocking(pipes.standard_error[0])) {
        return true;
    }
    result.standard_error = "could not bound readelf output pipes";
    kill_child(child);
    (void)::waitpid(child, nullptr, 0);
    close_pipes(pipes);
    return false;
}

void wait_for_pipe_data(const ProcessPipes& pipes) {
    struct pollfd descriptors[2]{};
    nfds_t count = 0;
    if (pipes.standard_output[0] >= 0) {
        descriptors[count++] = pollfd{pipes.standard_output[0], POLLIN | POLLHUP, 0};
    }
    if (pipes.standard_error[0] >= 0) {
        descriptors[count++] = pollfd{pipes.standard_error[0], POLLIN | POLLHUP, 0};
    }
    if (count == 0U) {
        (void)::usleep(25000U);
    } else {
        (void)::poll(descriptors, count, 25);
    }
}

void drain_pipe(int& fd, std::string& destination, bool& eof, bool& truncated) {
    if (fd < 0) return;
    eof = append_pipe(fd, destination, truncated);
    if (eof) close_fd(fd);
}

void enforce_process_bounds(pid_t child, ProcessPipes& pipes,
                            ProcessState& state, ProcessEvidence& result,
                            Clock::time_point deadline) {
    if (!state.reaped && Clock::now() >= deadline) {
        result.timed_out = true;
    }
    if (!result.timed_out && !result.truncated) return;
    kill_child(child);
    close_fd(pipes.standard_output[0]);
    close_fd(pipes.standard_error[0]);
    state.output_eof = true;
    state.error_eof = true;
    reap_child(child, state);
}

void set_return_code(ProcessEvidence& result, int wait_status) {
    if (WIFEXITED(wait_status)) {
        result.return_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        result.return_code = -WTERMSIG(wait_status);
    }
}

void collect_process(pid_t child, ProcessPipes& pipes, ProcessEvidence& result) {
    ProcessState state;
    const auto deadline = Clock::now() + std::chrono::milliseconds(
        static_cast<long long>(kReadelfTimeoutSeconds * 1000.0));
    while (!state.output_eof || !state.error_eof || !state.reaped) {
        wait_for_pipe_data(pipes);
        drain_pipe(pipes.standard_output[0], result.standard_output,
                   state.output_eof, result.truncated);
        drain_pipe(pipes.standard_error[0], result.standard_error,
                   state.error_eof, result.truncated);
        reap_child(child, state);
        enforce_process_bounds(child, pipes, state, result, deadline);
    }
    close_pipes(pipes);
    if (!state.reaped) (void)::waitpid(child, &state.wait_status, 0);
    set_return_code(result, state.wait_status);
}

ProcessEvidence run_readelf_process(const std::vector<std::string>& arguments) {
    ProcessEvidence result;
    ProcessPipes pipes;
    if (!create_pipes(pipes)) {
        result.standard_error = "could not create readelf pipes";
        return result;
    }
    const pid_t child = ::fork();
    if (child == 0) exec_child(arguments, pipes);
    if (child < 0) {
        close_pipes(pipes);
        result.standard_error = "could not fork readelf";
        return result;
    }
    if (!configure_parent_pipes(pipes, child, result)) return result;
    collect_process(child, pipes, result);
    return result;
}

bool valid_version_token(const std::string& value) {
    if (value.empty()) return false;
    bool digit = false;
    for (const char character : value) {
        if (character == '.') {
            if (!digit) return false;
            digit = false;
        } else if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digit = true;
        } else {
            return false;
        }
    }
    return digit;
}

std::string first_version_token(const std::string& line) {
    std::istringstream words(line);
    std::string token;
    words >> token >> token;
    while (words >> token) {
        while (!token.empty() && (token.back() == ',' || token.back() == ';')) {
            token.pop_back();
        }
        if (valid_version_token(token)) return token;
    }
    return {};
}

std::string gnu_readelf_version(const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        line = detail::trim(line);
        if (line.empty()) continue;
        if (line.rfind("GNU readelf", 0U) != 0U) return {};
        return first_version_token(line);
    }
    return {};
}

ReadelfCapability query_readelf_capability() {
    const ProcessEvidence evidence = run_readelf_process({"readelf", "--version"});
    if (evidence.return_code != 0 || evidence.timed_out || evidence.truncated) return {};
    const std::string version = gnu_readelf_version(evidence.standard_output);
    if (version.empty()) return {};
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
    const ProcessEvidence evidence = run_readelf_process(
        {"readelf", "-h", "-S", "-d", "-V", "-W", "--", path.generic_string()});
    result.return_code = evidence.return_code;
    result.timed_out = evidence.timed_out;
    result.truncated = evidence.truncated;
    result.standard_output = evidence.standard_output;
    result.standard_error = evidence.standard_error;
    return result;
}

}  // namespace abilens
