#pragma once

#include "persistence_io.hpp"

namespace loglens::detail {

bool readBoundedPersistenceFilePlatform(const std::string& path, std::string& bytes,
                                        bool& found, PersistenceError& error);
bool atomicWritePersistenceFilePlatform(const std::string& path, std::string_view bytes,
                                       PersistenceError& error);

} // namespace loglens::detail
