#pragma once

#include <QString>

namespace buildscope::native {

// diff_glob.py: slash-aware glob matching for diff suppressions.
bool globMatches(const QString &path, const QString &pattern, bool windows);

}  // namespace buildscope::native
