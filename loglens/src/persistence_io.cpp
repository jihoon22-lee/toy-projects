#include "persistence_io.hpp"

#include "persistence_io_platform.hpp"

namespace loglens::detail {

bool readBoundedPersistenceFile(const std::string& path, std::string& bytes, bool& found,
                                PersistenceError& error) {
    return readBoundedPersistenceFilePlatform(path, bytes, found, error);
}

bool atomicWritePersistenceFile(const std::string& path, std::string_view bytes,
                                PersistenceError& error) {
    return atomicWritePersistenceFilePlatform(path, bytes, error);
}

} // namespace loglens::detail
