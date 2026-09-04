#pragma once

#include <string>
#include <vector>

#include "loglens/persistence.hpp"

namespace loglens::detail {

std::string serializeSourceProfiles(const std::vector<SourceProfile>& profiles);
std::string serializeSavedQueries(const std::vector<SavedQuery>& queries);

} // namespace loglens::detail
