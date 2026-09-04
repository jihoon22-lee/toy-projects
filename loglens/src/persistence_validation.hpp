#pragma once

#include "loglens/persistence.hpp"

namespace loglens::detail {

void setPersistenceError(PersistenceError& error, PersistenceErrorCode code,
                         const std::string& message, std::size_t offset = 0);

bool validSourceProfile(const SourceProfile& profile, PersistenceError& error);
bool validSavedQuery(const SavedQuery& query, PersistenceError& error);

bool validateAndSortProfiles(std::vector<SourceProfile>& profiles,
                             PersistenceError& error);
bool validateAndSortQueries(std::vector<SavedQuery>& queries,
                            PersistenceError& error);

} // namespace loglens::detail
