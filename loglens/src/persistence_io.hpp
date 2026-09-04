#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "loglens/persistence.hpp"

namespace loglens::detail {

bool readBoundedPersistenceFile(const std::string& path, std::string& bytes, bool& found,
                                PersistenceError& error);
bool atomicWritePersistenceFile(const std::string& path, std::string_view bytes,
                                PersistenceError& error);

} // namespace loglens::detail
