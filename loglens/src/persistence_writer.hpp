#pragma once

#include <string>
#include <vector>

#include "loglens/persistence.hpp"

namespace loglens::detail {

std::string serializeSourceProfiles(const std::vector<SourceProfile>& profiles);
std::string serializeSavedQueries(const std::vector<SavedQuery>& queries);
std::string serializeSession(const SessionState& state);

} // namespace loglens::detail
