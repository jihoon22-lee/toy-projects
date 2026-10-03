#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <set>

#include "abilens/elf.hpp"
#include "evidence_internal.hpp"
namespace abilens::detail {
void resolve_loader(ElfReport& report, const InspectOptions& options) {
    if (options.sysroot.empty()) return;
    std::error_code ec;
    const auto root = std::filesystem::canonical(options.sysroot, ec);
    if (ec || !std::filesystem::is_directory(root))
        throw std::runtime_error("sysroot must be an existing directory");
    const int root_fd = ::open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) throw std::runtime_error("cannot open sysroot");
    struct RootGuard {
        int fd;
        ~RootGuard() { ::close(fd); }
    } guard{root_fd};
    std::vector<std::string> search = report.runpath.empty() ? report.rpath : report.runpath;
    search.insert(search.end(), options.library_paths.begin(), options.library_paths.end());
    const std::string triple = report.header.machine == "Advanced Micro Devices X86-64" ||
                                       report.header.machine == "x86-64"
                                   ? "x86_64-linux-gnu"
                               : report.header.machine == "AArch64" ? "aarch64-linux-gnu"
                                                                    : "";
    if (!triple.empty()) {
        search.push_back("/lib/" + triple);
        search.push_back("/usr/lib/" + triple);
    }
    search.insert(search.end(), {"/lib64", "/usr/lib64", "/lib", "/usr/lib"});
    for (const auto& needed : report.needed) {
        LoaderResolution resolution{needed, "unresolved", "", {}};
        for (auto directory : search) {
            if (resolution.searched.size() >= 128) {
                resolution.status = "limited";
                break;
            }
            for (const std::string token : {"${ORIGIN}", "$ORIGIN"}) {
                std::size_t position = 0;
                while (!options.origin.empty() &&
                       (position = directory.find(token, position)) != std::string::npos) {
                    directory.replace(position, token.size(), options.origin);
                    position += options.origin.size();
                }
            }
            if (directory.empty() || directory.front() != '/' ||
                directory.find('$') != std::string::npos || needed.find('/') != std::string::npos)
                continue;
            const auto logical = std::filesystem::path(directory) / needed;
            resolution.searched.push_back(logical.generic_string());
            struct open_how how{};
            how.flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
            how.resolve = RESOLVE_IN_ROOT | RESOLVE_NO_MAGICLINKS;
            const int fd = static_cast<int>(
                ::syscall(SYS_openat2, root_fd, logical.c_str(), &how, sizeof(how)));
            if (fd < 0) continue;
            const OpenInput input("/proc/self/fd/" + std::to_string(fd));
            const auto header = validate_elf_input(input);
            ::close(fd);
            if (header.status != InputStatus::Valid ||
                header.header.elf_class != report.header.elf_class ||
                header.header.endian != report.header.endian ||
                header.header.machine != report.header.machine)
                continue;
            resolution.path = logical.generic_string();
            resolution.status = "candidate";
            break;
        }
        report.resolutions.push_back(std::move(resolution));
    }
    report.diagnostics.push_back(
        "Offline loader candidates only: no ld.so.cache, environment, hwcaps, transitive RPATH or "
        "runtime loading");
}
}  // namespace abilens::detail
